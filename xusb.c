#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>

#define bool int
#define true (1 == 1)
#define false (!true)

// Future versions of libusbx will use usb_interface instead of interface
// in libusb_config_descriptor => cater for that
#define usb_interface interface

/**
 * perr
 */
static int perr (char const *format, ...)
{
	va_list args;
	int r;

	va_start (args, format);
	r = vfprintf (stderr, format, args);
	va_end (args);

	return r;
}

#define ERR_EXIT(errcode) do { perr ("	%s\n", libusb_error_name ((enum libusb_error)errcode)); return -1; } while (0)
#define CALL_CHECK(fcall) do { r=fcall; if (r < 0) ERR_EXIT (r); } while (0);
#define be_to_int32(buf) (((buf)[0]<<24)| ((buf)[1]<<16)| ((buf)[2]<<8)| (buf)[3])

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24
#define READ_CAPACITY_LENGTH          0x08

// Mass Storage Requests values. See section 3 of the Bulk-Only Mass Storage Class specifications
#define BOMS_RESET                    0xFF
#define BOMS_GET_MAX_LUN              0xFE

/**
 * Section 5.1: Command Block Wrapper (CBW)
 */
struct command_block_wrapper {
	uint8_t dCBWSignature[4];	// 0x00
	uint32_t dCBWTag;		// 0x04
	uint32_t dCBWDataTransferLength;// 0x08
	uint8_t bmCBWFlags;		// 0x0C
	uint8_t bCBWLUN;		// 0x0D
	uint8_t bCBWCBLength;		// 0x0E
	uint8_t CBWCB[16];		// 0x0F
}  __attribute__((__packed__));

/**
 * Section 5.2: Command Status Wrapper (CSW)
 */
struct command_status_wrapper {
	uint8_t dCSWSignature[4];	// 0x00
	uint32_t dCSWTag;		// 0x04
	uint32_t dCSWDataResidue;	// 0x08
	uint8_t bCSWStatus;		// 0x0C
}  __attribute__((__packed__));


/**
 * cdb_length
 */
static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};


/**
 * display_buffer_hex
 */
static void display_buffer_hex (unsigned char *buffer, unsigned size)
{
	unsigned i, j, k;

	for (i=0; i<size; i+=16) {
		printf ("\n  %08x  ", i);
		for (j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				printf ("%02x", buffer[i+j]);
			} else {
				printf ("  ");
			}
			printf (" ");
		}
		printf (" ");
		for (j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				if ((buffer[i+j] < 32) || (buffer[i+j] > 126)) {
					printf (".");
				} else {
					printf ("%c", buffer[i+j]);
				}
			}
		}
	}
	printf ("\n" );
}

/**
 * send_mass_storage_command
 */
static int send_mass_storage_command (libusb_device_handle *handle, uint8_t endpoint, uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	struct command_block_wrapper cbw;

	if (cdb == NULL) {
		return -1;
	}

	if (endpoint & LIBUSB_ENDPOINT_IN) {
		perr ("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof (cbw.CBWCB))) {
		perr ("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}

	memset (&cbw, 0, sizeof (cbw));
	cbw.dCBWSignature[0] = 'U';
	cbw.dCBWSignature[1] = 'S';
	cbw.dCBWSignature[2] = 'B';
	cbw.dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw.dCBWTag = tag++;
	cbw.dCBWDataTransferLength = data_length;
	cbw.bmCBWFlags = direction;
	cbw.bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw.bCBWCBLength = cdb_len;
	memcpy (cbw.CBWCB, cdb, cdb_len);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer (handle, endpoint, (unsigned char*)&cbw, sizeof (cbw), &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt (handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr ("	send_mass_storage_command: %s\n", libusb_error_name (r));
		return -1;
	}

	printf ("	sent %d CDB bytes\n", cdb_len);
	return 0;
}

/**
 * get_mass_storage_status
 */
static int get_mass_storage_status (libusb_device_handle *handle, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = libusb_bulk_transfer (handle, endpoint, (unsigned char*)&csw, sizeof (csw), &size, 1000);
		//printf ("\e[33mcsw size = %lu, size read = %d\e[0m\n", sizeof (csw), size);
		//display_buffer_hex ((unsigned char*) &csw, sizeof (csw));
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt (handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr ("	get_mass_storage_status: %s\n", libusb_error_name (r));
		return -1;
	}
	if (size != sizeof (csw)) {
		perr ("	get_mass_storage_status: received %d bytes (expected %d)\n", size, sizeof (csw));
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		perr ("	get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printf ("	Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}

/**
 * get_sense
 */
static void get_sense (libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out)
{
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t sense[18];
	uint32_t expected_tag;
	int size;

	// Request Sense
	printf ("Request Sense:\n");
	memset (sense, 0, sizeof (sense));
	memset (cdb, 0, sizeof (cdb));
	cdb[0] = 0x03;	// Request Sense
	cdb[4] = REQUEST_SENSE_LENGTH;

	send_mass_storage_command (handle, endpoint_out, 0, cdb, LIBUSB_ENDPOINT_IN, REQUEST_SENSE_LENGTH, &expected_tag);
	libusb_bulk_transfer (handle, endpoint_in, (unsigned char*)&sense, REQUEST_SENSE_LENGTH, &size, 1000);
	printf ("	received %d bytes\n", size);

	if ((sense[0] != 0x70) && (sense[0] != 0x71)) {
		perr ("	ERROR No sense data\n");
	} else {
		perr ("	ERROR Sense: %02X %02X %02X\n", sense[2]&0x0F, sense[12], sense[13]);
	}
	// Strictly speaking, the get_mass_storage_status() call should come
	// before these perr() lines.  If the status is nonzero then we must
	// assume there's no data in the buffer.  For xusb it doesn't matter.
	get_mass_storage_status (handle, endpoint_in, expected_tag);
}

/**
 * test_mass_storage
 *
 * Mass Storage device to test bulk transfers (non destructive test)
 */
static int test_mass_storage (libusb_device_handle *handle, uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r, size;
	uint8_t lun;
	uint32_t expected_tag;
	uint32_t i, max_lba, block_size;
	double device_size;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t buffer[64];
	char vid[9], pid[9], rev[5];
	unsigned char *data;

	printf ("Reading Max LUN:\n");
	r = libusb_control_transfer (handle, LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE,
		BOMS_GET_MAX_LUN, 0, 0, &lun, 1, 1000);
	// Some devices send a STALL instead of the actual value.
	// In such cases we should set lun to 0.
	if (r == 0) {
		lun = 0;
	} else if (r < 0) {
		perr ("	Failed: %s", libusb_error_name ((enum libusb_error)r));
	}
	printf ("	Max LUN = %d\n", lun);

	// Send Inquiry
	printf ("Sending Inquiry:\n");
	memset (buffer, 0, sizeof (buffer));
	memset (cdb, 0, sizeof (cdb));
	cdb[0] = 0x12;	// Inquiry
	cdb[4] = INQUIRY_LENGTH;

	send_mass_storage_command (handle, endpoint_out, lun, cdb, LIBUSB_ENDPOINT_IN, INQUIRY_LENGTH, &expected_tag);
	CALL_CHECK (libusb_bulk_transfer (handle, endpoint_in, (unsigned char*)&buffer, INQUIRY_LENGTH, &size, 1000));
	printf ("	received %d bytes\n", size);
	display_buffer_hex (buffer, size);
	// The following strings are not zero terminated
	for (i=0; i<8; i++) {
		vid[i] = buffer[8+i];
		pid[i] = buffer[16+i];
		rev[i/2] = buffer[32+i/2];	// instead of another loop
	}
	vid[8] = 0;
	pid[8] = 0;
	rev[4] = 0;
	printf ("	VID:PID:REV \"%8s\":\"%8s\":\"%4s\"\n", vid, pid, rev);
	if (get_mass_storage_status (handle, endpoint_in, expected_tag) == -2) {
		get_sense (handle, endpoint_in, endpoint_out);
	}

	// Read capacity
	printf ("Reading Capacity:\n");
	memset (buffer, 0, sizeof (buffer));
	memset (cdb, 0, sizeof (cdb));
	cdb[0] = 0x25;	// Read Capacity

	send_mass_storage_command (handle, endpoint_out, lun, cdb, LIBUSB_ENDPOINT_IN, READ_CAPACITY_LENGTH, &expected_tag);
	CALL_CHECK (libusb_bulk_transfer (handle, endpoint_in, (unsigned char*)&buffer, READ_CAPACITY_LENGTH, &size, 1000));
	printf ("	received %d bytes\n", size);
	display_buffer_hex (buffer, size);
	max_lba = be_to_int32 (&buffer[0]);
	block_size = be_to_int32 (&buffer[4]);
	device_size = ((double) (max_lba+1))*block_size/ (1024*1024*1024);
	printf ("	Max LBA: %08X, Block Size: %08X (%.2f GB)\n", max_lba, block_size, device_size);
	if (get_mass_storage_status (handle, endpoint_in, expected_tag) == -2) {
		get_sense (handle, endpoint_in, endpoint_out);
	}

	data = (unsigned char*) calloc (1, block_size);
	if (data == NULL) {
		perr ("	unable to allocate data buffer\n");
		return -1;
	}
	//memset (data, 'R', block_size);

	// Send Read
	printf ("Attempting to read %d bytes:\n", block_size);
	memset (cdb, 0, sizeof (cdb));

	cdb[0] = 0x28;	// Read (10)
	cdb[8] = 0x01;	// 1 block

	send_mass_storage_command (handle, endpoint_out, lun, cdb, LIBUSB_ENDPOINT_IN, block_size, &expected_tag);
	libusb_bulk_transfer (handle, endpoint_in, data, block_size, &size, 5000);
	printf ("	READ: received %d bytes\n", size);
	if (get_mass_storage_status (handle, endpoint_in, expected_tag) == -2) {
		get_sense (handle, endpoint_in, endpoint_out);
	} else {
		if (0)
			display_buffer_hex (data, size);
	}
	free (data);

	return 0;
}

/**
 * test_device
 */
static int test_device (libusb_context *ctx, uint16_t vid, uint16_t pid)
{
	libusb_device_handle *handle;
	libusb_device *dev;
	uint8_t bus, port_path[8];
	struct libusb_config_descriptor *conf_desc;
	const struct libusb_endpoint_descriptor *endpoint;
	int i, j, k, r;
	int iface, num_ifaces;
	//int first_iface = -1;

	// Attaching/detaching the kernel driver is only relevant for Linux
	int iface_detached = -1;

	struct libusb_device_descriptor dev_desc;
	const char *speed_name[5] = { "Unknown", "1.5 Mbit/s (USB LowSpeed)", "12 Mbit/s (USB FullSpeed)", "480 Mbit/s (USB HighSpeed)", "5000 Mbit/s (USB SuperSpeed)"};
	char string[128];
	uint8_t string_index[3];	// indexes of the string descriptors
	uint8_t endpoint_in = 0, endpoint_out = 0;	// default IN and OUT endpoints

	printf ("Opening device %04X:%04X...\n", vid, pid);
	handle = libusb_open_device_with_vid_pid (ctx, vid, pid);
	if (handle == NULL) {
		perr ("  Failed.\n");
		return -1;
	}

	dev = libusb_get_device (handle);
	bus = libusb_get_bus_number (dev);

	r = libusb_get_port_path (ctx, dev, port_path, sizeof (port_path));
	if (r > 0) {
		printf ("\nDevice properties:\n");
		printf ("        bus number: %d\n", bus);
		printf ("         port path: %d", port_path[0]);
		for (i=1; i<r; i++) {
			printf ("->%d", port_path[i]);
		}
		printf (" (from root hub)\n");
	}
	r = libusb_get_device_speed (dev);
	if ((r<0) || (r>4)) r=0;
	printf ("             speed: %s\n", speed_name[r]);

	printf ("\nReading device descriptor:\n");
	CALL_CHECK (libusb_get_device_descriptor (dev, &dev_desc));
	printf ("            length: %d\n",           dev_desc.bLength);
	printf ("      device class: %d\n",           dev_desc.bDeviceClass);
	printf ("               S/N: %d\n",           dev_desc.iSerialNumber);
	printf ("           VID:PID: %04X:%04X\n",    dev_desc.idVendor,
						      dev_desc.idProduct);
	printf ("         bcdDevice: %04X\n",         dev_desc.bcdDevice);
	printf ("	iMan:iProd:iSer: %d:%d:%d\n", dev_desc.iManufacturer,
						      dev_desc.iProduct,
						      dev_desc.iSerialNumber);
	printf ("         num confs: %d\n",           dev_desc.bNumConfigurations);
	// Copy the string descriptors for easier parsing
	string_index[0] = dev_desc.iManufacturer;
	string_index[1] = dev_desc.iProduct;
	string_index[2] = dev_desc.iSerialNumber;

	printf ("\nReading configuration descriptors:\n");
	CALL_CHECK (libusb_get_config_descriptor (dev, 0, &conf_desc));
	num_ifaces = conf_desc->bNumInterfaces;
	printf ("            num interfaces: %d\n", num_ifaces);
	//if (num_ifaces > 0)
	//	first_iface = conf_desc->usb_interface[0].altsetting[0].bInterfaceNumber;
	for (i=0; i<num_ifaces; i++) {
		printf ("              interface[%d]: id = %d\n", i, conf_desc->usb_interface[i].altsetting[0].bInterfaceNumber);
		for (j=0; j<conf_desc->usb_interface[i].num_altsetting; j++) {
			printf ("interface[%d].altsetting[%d]: num endpoints = %d\n", i, j, conf_desc->usb_interface[i].altsetting[j].bNumEndpoints);
			printf ("	Class.SubClass.Protocol: %02X.%02X.%02X\n",
					conf_desc->usb_interface[i].altsetting[j].bInterfaceClass,
					conf_desc->usb_interface[i].altsetting[j].bInterfaceSubClass,
					conf_desc->usb_interface[i].altsetting[j].bInterfaceProtocol);

			if ((conf_desc->usb_interface[i].altsetting[j].bInterfaceClass == LIBUSB_CLASS_MASS_STORAGE)
			  && ((conf_desc->usb_interface[i].altsetting[j].bInterfaceSubClass == 0x01)
			  || (conf_desc->usb_interface[i].altsetting[j].bInterfaceSubClass == 0x06) )
			  && (conf_desc->usb_interface[i].altsetting[j].bInterfaceProtocol == 0x50) ) {
				// Mass storage devices that can use basic SCSI commands
				//test_mode = USE_SCSI;
			}

			for (k=0; k<conf_desc->usb_interface[i].altsetting[j].bNumEndpoints; k++) {
				endpoint = &conf_desc->usb_interface[i].altsetting[j].endpoint[k];
				printf ("       endpoint[%d].address: %02X\n", k, endpoint->bEndpointAddress);
				// Use the first interrupt or bulk IN/OUT endpoints as default for testing
				if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) & (LIBUSB_TRANSFER_TYPE_BULK | LIBUSB_TRANSFER_TYPE_INTERRUPT)) {
					if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
						if (!endpoint_in)
							endpoint_in = endpoint->bEndpointAddress;
					} else {
						if (!endpoint_out)
							endpoint_out = endpoint->bEndpointAddress;
					}
				}
				printf ("           max packet size: %04X\n", endpoint->wMaxPacketSize);
				printf ("          polling interval: %02X\n", endpoint->bInterval);
			}
		}
	}
	libusb_free_config_descriptor (conf_desc);

	for (iface = 0; iface < num_ifaces; iface++)
	{
		printf ("\nClaiming interface %d...\n", iface);
		r = libusb_claim_interface (handle, iface);

		if ((r != LIBUSB_SUCCESS) && (iface == 0)) {
			// Maybe we need to detach the driver
			perr ("	Failed. Trying to detach driver...\n");
			libusb_detach_kernel_driver (handle, iface);
			iface_detached = iface;
			printf ("	Claiming interface again...\n");
			r = libusb_claim_interface (handle, iface);
		}

		if (r != LIBUSB_SUCCESS) {
			perr ("	Failed.\n");
		}
	}

	printf ("\nReading string descriptors:\n");
	for (i=0; i<3; i++) {
		if (string_index[i] == 0) {
			continue;
		}
		if (libusb_get_string_descriptor_ascii (handle, string_index[i], (unsigned char*)string, 128) >= 0) {
			printf ("	String (0x%02X): \"%s\"\n", string_index[i], string);
		}
	}

	if (1)
		CALL_CHECK (test_mass_storage (handle, endpoint_in, endpoint_out));

	printf ("\n");
	for (iface = 0; iface<num_ifaces; iface++) {
		printf ("Releasing interface %d...\n", iface);
		libusb_release_interface (handle, iface);
	}

	if (iface_detached >= 0) {
		printf ("Re-attaching kernel driver...\n");
		libusb_attach_kernel_driver (handle, iface_detached);
	}

	printf ("Closing device...\n");
	libusb_close (handle);

	return 0;
}

/**
 * main
 */
int main (int argc, char *argv[])
{
	const struct libusb_version *version;
	libusb_context *ctx = NULL;

	version = libusb_get_version();
	printf ("Using libusbx v%d.%d.%d.%d\n\n", version->major, version->minor, version->micro, version->nano);

	if (libusb_init (&ctx) < 0)
		return 1;

	if ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == 'd'))
		libusb_set_debug (ctx, LIBUSB_LOG_LEVEL_DEBUG);

	test_device (ctx, 0x1bb4, 0x000a);

	libusb_exit (ctx);

	return 0;
}

