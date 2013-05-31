#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "usb.h"

#if 0
#include "log.c"
#else
#define log_init(...)	/*nothing*/
#define log_info	printf
#define log_debug	printf
#define log_error	printf
#define log_hex		dump_hex
#endif

int error_count = 0;
int error_max   = 0;

#define RETURN(retval)	{ log_error ("\e[31mTest failed: %s(%d)\e[0m\n", __FUNCTION__, __LINE__); error_count++; if (error_count > error_max) exit (1); return retval; }

/**
 * dump_hex
 */
static void dump_hex (void *buf, int start, int length)
{
	int off, i, s, e;
	u8 *mem = buf;

	s =  start                & ~15;	// round down
	e = (start + length + 15) & ~15;	// round up

	for (off = s; off < e; off += 16) {
		if (off == s)
			printf("	%6.6x ", start);
		else
			printf("	%6.6x ", off);

		for (i = 0; i < 16; i++) {
			if (i == 8)
				printf(" -");
			if (((off+i) >= start) && ((off+i) < (start+length)))
				printf(" %02X", mem[off+i]);
			else
				printf("   ");
		}
		printf("  ");
		for (i = 0; i < 16; i++) {
			if (((off+i) < start) || ((off+i) >= (start+length)))
				printf(" ");
			else if (isprint(mem[off + i]))
				printf("%c", mem[off + i]);
			else
				printf(".");
		}
		printf("\n");
	}
}

/**
 * dump_string
 */
static void dump_string (u8 *data)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (!data[2*i])
			break;
		log_info ("%c", data[2*i]);
	}
	log_info ("\n");
}


/**
 * valid_cdb_6
 */
static bool valid_cdb_6 (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (false);

	if ((buffer[0] == 0x00) || (buffer[0] == 0x03) || (buffer[0] == 0x12)) {
		// Satmap sends 12 bytes; that doesn't match the SCSI spec.
		if (cbw->bCBWCBLength == 12) {
			// Satmap sends this excess data, which probably ought to be zero
			if (buffer[6] || buffer[7] || buffer[8] || buffer[9] || buffer[10] || buffer[11])
				RETURN (false);
		} else if (cbw->bCBWCBLength != 6) {
			RETURN (false);
		}
	} else {
		if (cbw->bCBWCBLength != 6) {
			printf ("XXX command = %#02x, length = %d\n", buffer[0], cbw->bCBWCBLength);
			RETURN (false);
		}
	}

	// check that the slack space is empty
	// XXX this clashes with the Satmap 12 byte check
	for (i = 6; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (false);
	}

	switch (buffer[0]) {
		case 0x00:		// TEST UNIT READY
			if ((buffer[1] & 0x1f) != 0)			// reserved
				RETURN (false);
			if (buffer[2] || buffer[3] || buffer[4])	// reserved
				RETURN (false);
			if (buffer[5])
				RETURN (false);
			return true;
		case 0x03:		// REQUEST SENSE
			if ((buffer[1] & 0x1f) != 0)			// reserved
				RETURN (false);
			if (buffer[2] || buffer[3])			// reserved
				RETURN (false);
			if (buffer[5])
				RETURN (false);
			// buffer[4] Allocation length can be any value
			return true;
		case 0x12:		// INQUIRY
			if (buffer[1] > 1)
				RETURN (false);
			if ((buffer[2] != 0) && (buffer[2] != 0x80))	// page code: none, vendor
				RETURN (false)
			if (buffer[5])
				RETURN (false);
			// buffer[3],buffer[4] Allocation length can be any value
			return true;
		case 0x1a:		// MODE SENSE (6)
			if (buffer[5])
				RETURN (false);
			// These fields can be any value:
			//	buffer[2] Page code
			//	buffer[3] Subpage code
			//	buffer[4] Allocation length
			return true;
		case 0x1e:		// PREVENT ALLOW MEDIUM REMOVAL
			if (buffer[1] || buffer[2] || buffer[3] || buffer[5])
				RETURN (false);
			if (buffer[4] > 1)
				RETURN (false);
			return true;
		default:
			printf ("XXX command %#02x\n", buffer[0]);
			RETURN (false);
	}
}

/**
 * valid_cdb_10
 */
static bool valid_cdb_10 (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (false);

	if (cbw->bCBWCBLength != 10)
		RETURN (false);

	// check that the slack space is empty
	for (i = 10; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (false);
	}

	switch (buffer[0]) {
		case 0x23:		// READ FORMAT CAPACITIES
			// Opcode, then all zeros (except buffer[8] == 0xFC)
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (i == 8) {
					if (buffer[i] != 0xFC)
						RETURN (false);
					continue;
				}
				if (buffer[i])
					RETURN (false);
			}
			return true;
		case 0x25:		// READ CAPACITY(10)
			// Opcode, then all zeros
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (buffer[i])
					RETURN (false);
			}
			return true;
		case 0x28:		// READ(10)
			// Opcode, then all zeros (except buffer[8] == 1)
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (i == 8) {
					if (buffer[i] != 1)
						RETURN (false);
					continue;
				}
				if (buffer[i])
					RETURN (false);
			}
			return true;
		default:
			printf ("XXX command = %#02x\n", buffer[0]);
			RETURN (false);
	}
}

/**
 * valid_cdb_vendor
 */
static bool valid_cdb_vendor (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (false);

	if (cbw->bCBWCBLength != 7)
		RETURN (false);

	// check that the slack space is empty
	for (i = 7; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (false);
	}

	switch (buffer[0]) {
		case 0xda:		// Vendor 1
			return true;
		case 0xdb:		// Vendor 2
			return true;
		default:
			RETURN (false);
	}
}


/**
 * valid_cdb
 */
static bool valid_cdb (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	if (!u || !cbw || !buffer)
		RETURN (false);

	if (buffer[0] < 0x20)
		return valid_cdb_6 (u, cbw, buffer);
	else if (buffer[0] < 0x60)
		return valid_cdb_10 (u, cbw, buffer);
	else if ((buffer[0] == 0xda) || (buffer[0] == 0xdb))
		return valid_cdb_vendor (u, cbw, buffer);

	// Other commands exist, but we don't use them
	RETURN (false);
}

/**
 * valid_cbw
 */
static bool valid_cbw (usbmon_packet *u, u8 *buffer)
{
	// These fields could contain any value:
	//	dCBWTag, dCBWDataTransferLength

	command_block_wrapper *cbw = NULL;

	if (!u || !buffer)
		RETURN (false);

	cbw = (command_block_wrapper *) buffer;

	if (u->len_cap != sizeof (command_block_wrapper))
		return (false);

	if (strncmp (cbw->dCBWSignature, "USBC", 4) != 0)
		RETURN (false);

	if ((cbw->bmCBWFlags != 0) && (cbw->bmCBWFlags != 0x80))
		RETURN (false);

	if (cbw->bCBWLUN > 15)
		RETURN (false);

	if ((cbw->bCBWCBLength < 6) || (cbw->bCBWCBLength > 16))
		RETURN (false);

	if (!valid_cdb (u, cbw, cbw->CBWCB))
		RETURN (false);

	return true;
}

/**
 * valid_csw
 */
static bool valid_csw (usbmon_packet *u, u8 *buffer)
{
	// This field could contain any value:
	//	dCSWTag

	command_status_wrapper *csw = NULL;

	if (!u || !buffer)
		RETURN (false);

	csw = (command_status_wrapper *) buffer;

	if (u->len_cap != sizeof (command_status_wrapper))
		return (false);

	if (strncmp (csw->dCSWSignature, "USBS", 4) != 0)
		RETURN (false);

	if (csw->bCSWStatus > 2)
		RETURN (false);

	//XXX how do I validate dCSWDataResidue?
	return true;
}

/**
 * valid_dd
 */
static bool valid_dd (usbmon_packet *usb, u8 *data)
{
	if (usb->epnum != 0x80)		// Inbound traffic
		return (false);

	if (usb->len_cap != 18)		// Data length
		return (false);

	usb_device_descriptor *dd = (usb_device_descriptor*) data;

	if (dd->bDescriptorType != 1)
		return (false);

	if (dd->bLength != 18)
		return (false);

	if (dd->bcdUSB != 0x200)	// USB 2.0
		return (false);

	if (dd->bDeviceClass || dd->bDeviceSubclass || dd->bDeviceProtocol)
		return false;

	if (dd->bMaxPacketSize0 != 8)
		return false;

	if (dd->idVendor != 0x1bb4)
		return false;

	if (dd->idProduct != 0x0010)
		return false;

	if (dd->bcdDevice)
		return false;

	if (dd->iManufacturer != 1)
		return false;

	if (dd->iProduct != 2)
		return false;

	if (dd->iSerialNumber)
		return false;

	if (dd->bNumConfigurations != 1)
		return false;

	return true;
}

/**
 * valid_req_sense
 */
static bool valid_req_sense (usbmon_packet *u, u8 *buffer)
{
	if (!u || !buffer)
		RETURN (false);

	return false;
	return true;
}

/**
 * valid_usbmon
 */
static bool valid_usbmon (usbmon_packet *u)
{
	if (!u)
		RETURN (false);

	if ((u->id & 0xffffff0000000000) != 0xffff880000000000)
		RETURN (false);

	// type: submit, complete
	if ((u->type != 'S') && (u->type != 'C'))
		RETURN (false);

	// transfer type: iso, intr, control, bulk
	if (u->xfer_type > 3)
		RETURN (false);

	// endpoint number
	if ((u->epnum & 0x7f) > 2)
		RETURN (false);

	// device number -- incremented each time the device is un-/re-plugged
	if (u->devnum > 0x80)
		RETURN (false);

	// bus number, seems to be stable
	if (u->busnum != 3)
		RETURN (false);

	// setup section: present, not present
	if ((u->flag_setup != 0) && (u->flag_setup != '-'))
		RETURN (false);

	// data: present, not present, not present
	if ((u->flag_data != 0) && (u->flag_data != '<') && (u->flag_data != '>'))
		RETURN (false);

	// one year window
	if ((u->ts_sec < 1338380174) || (u->ts_sec > 1401452171))
		RETURN (false);

	// microseconds
	if (u->ts_usec > 1000000)
		RETURN (false);

	// XXX probably ought to quit on ENOENT
	// success, -2, -32, -115
	if ((u->status != 0) && (u->status != -ENOENT) && (u->status != -EPIPE) && (u->status != -EINPROGRESS)) {
		printf ("XXX status = %d\n", u->status);
		RETURN (false);
	}

	if (u->length > 64) {		// XXX validate this against the device descriptor
		printf ("XXX length = %d\n", u->length);
		//RETURN (false);
	}

	if (u->len_cap > 64)
		RETURN (false);

	// Transfer Type: Control (2)
	// Type: Submit ('S')
	// Setup: Relevant (0)
	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		usbmon_setup *setup = (usbmon_setup *) u->setup;

		// bitfield
		if (((setup->bmRequestType >> 5) & 0x03) == 3)
			RETURN (false);
		if ((setup->bmRequestType & 0x1f) > 3)
			RETURN (false);

		// request: GET_STATUS(0), CLEAR_FEATURE(1), SET_FEATURE(3), GET_DESCRIPTOR(6), SET_CONFIGURATION(9)
		if ((setup->bRequest != 0) && (setup->bRequest != 1) && (setup->bRequest != 3) && (setup->bRequest != 6) && (setup->bRequest != 9)) {
			printf ("XXX request = %d\n", setup->bRequest);
			//RETURN (false);
		}

	}
	return true;
}


/**
 * dump_cbw
 */
static void dump_cbw (u8 *buffer)
{
	command_block_wrapper *cbw = NULL;
	char *op;
	char *direction;

	if (!buffer)
		return;

	cbw = (command_block_wrapper *) buffer;

	switch (cbw->CBWCB[0]) {
		case 0x00: op = "TEST UNIT READY";              break;
		case 0x03: op = "REQUEST SENSE";                break;
		case 0x12: op = "INQUIRY";                      break;
		case 0x1A: op = "MODE SENSE (6)";               break;
		case 0x1E: op = "PREVENT ALLOW MEDIUM REMOVAL"; break;
		case 0x23: op = "READ FORMAT CAPACITIES";       break;
		case 0x25: op = "READ CAPACITY(10)";            break;
		case 0x28: op = "READ(10)";                     break;
		case 0xDA: op = "VENDOR";                       break;
		case 0xDB: op = "VENDOR";                       break;
		default:   op = "UNKNOWN";
	}

	switch (cbw->bmCBWFlags) {
		case 0x00: direction = "host to device"; break;
		case 0x80: direction = "device to host"; break;
		default:   direction = "UNKNOWN";
	}

	printf ("Command Block Wrapper (CBW), 31 bytes\n");
	//printf ("	dCSWSignature: %.4s\n",                     buffer+0);
	printf ("	dCSWTag: 0x%04x\n",                *(u32 *)(buffer+4));
	printf ("	dCBWDataTransferLength: 0x%04x\n", *(u32 *)(buffer+8));
	printf ("	bmCBWFlags: 0x%02x %s\n",                   buffer[12], direction);
	printf ("	bCBWLUN: %d\n",                             buffer[13]);
	printf ("	bCBWCBLength: %d\n",                        buffer[14]);
	printf ("	CBWCB:\n");

	if (cbw->CBWCB[0] >= 0xD0) {
		log_debug ("Vendor: %02x\n", cbw->CBWCB[0]);
		//log_info ("Want %d bytes (0x%04x)\n", want, want);
	}
	printf ("		Operation code: 0x%02x %s\n", buffer[15], op);
	if (cbw->CBWCB[0] < 0xC0) {
		printf ("		LUN: %d\n",               cbw->CBWCB[1]>>5);
		printf ("		Reserved 1: %d\n",        cbw->CBWCB[1] & 0x1F);
		printf ("		Reserved 2: %d\n",        cbw->CBWCB[2]);
		printf ("		Reserved 3: %d\n",        cbw->CBWCB[3]);
		printf ("		Allocation length: %d\n", cbw->CBWCB[4]);
		printf ("		Control: %d\n",           cbw->CBWCB[5]);
	} else {
		dump_hex (cbw->CBWCB, 0, sizeof (cbw->CBWCB));
	}
}

/**
 * dump_csw
 */
static void dump_csw (u8 *buffer)
{
	command_status_wrapper *csw = NULL;

	if (!buffer)
		return;

	csw = (command_status_wrapper *) buffer;

	printf ("	Command Status Wrapper (CSW), 13 bytes\n");
	//printf ("		dCSWSignature: %.4s\n",     csw->dCSWSignature);
	printf ("		dCSWTag: 0x%04x\n",         csw->dCSWTag);
	printf ("		dCSWDataResidue: 0x%04x\n", csw->dCSWDataResidue);
	printf ("		dCSWStatus: %d\n",          csw->bCSWStatus);
	// status
	//	0	ok
	//	1	failed -> send "GetSense" immediately
	//	2	phase error
}

/**
 * dump_dd
 */
static bool dump_dd (usbmon_packet *usb, u8 *data)
{
	if (!usb || !data)
		return (false);

	usb_device_descriptor *dd = (usb_device_descriptor*) data;

	printf ("Device Descriptor\n");
	printf ("	bLength            : %d\n",     dd->bLength);
	printf ("	bDescriptorType    : %d\n",     dd->bDescriptorType);
	printf ("	bcdUSB             : 0x%04x\n", dd->bcdUSB);
	printf ("	bDeviceClass       : %d\n",     dd->bDeviceClass);
	printf ("	bDeviceSubClass    : %d\n",     dd->bDeviceSubclass);
	printf ("	bDeviceProtocol    : %d\n",     dd->bDeviceProtocol);
	printf ("	bMaxPacketSize0    : %d\n",     dd->bMaxPacketSize0);
	printf ("	idVendor           : 0x%04x\n", dd->idVendor);
	printf ("	idProduct          : 0x%04x\n", dd->idProduct);
	printf ("	bcdDevice          : %d\n",     dd->bcdDevice);
	printf ("	iManufacturer      : %d\n",     dd->iManufacturer);
	printf ("	iProduct           : %d\n",     dd->iProduct);
	printf ("	iSerialNumber      : %d\n",     dd->iSerialNumber);
	printf ("	bNumConfigurations : %d\n",     dd->bNumConfigurations);

	return true;
}

/**
 * dump_req_sense
 */
static void dump_req_sense (u8 *buffer)
{
	if (!buffer)
		return;

	printf ("Request Sense Response\n");
	printf ("	Valid: %d\n", buffer[0] >> 7);
	printf ("	Response Code: %d\n", buffer[0] & 0x7f);
	printf ("	Obsolete: %d\n", buffer[1]);
	printf ("	Filemark: %d\n", (buffer[2] & 0x80) >> 7);
	printf ("	EOM: %d\n", (buffer[2] & 0x40) >> 6);
	printf ("	ILI: %d\n", (buffer[2] & 0x20) >> 5);
	printf ("	Reserved: %d\n", (buffer[2] & 0x10) >> 4);
	printf ("	Sense Key: %d\n", buffer[2] & 0x0F);
	// Information dependent on (buffer[0] >> 7)
	printf ("	Information: %02x %02x %02x %02x\n", buffer[3], buffer[4], buffer[5], buffer[6]);
	printf ("	Additional sense length: %d\n", buffer[7]);
	printf ("	Command-specific information: %02x %02x %02x %02x\n", buffer[8], buffer[9], buffer[10], buffer[11]);
	printf ("	Additional sense code: 0x%02x\n", buffer[12]);
	printf ("	Additional sense code qualifier: %d\n", buffer[13]);
	printf ("	Field replaceable unit code qualifier: %d\n", buffer[14]);
	printf ("	SKSV: %d\n", buffer[15] >> 7);
	printf ("	Sense key specfic 1: %d\n", buffer[15] & 0x7F);
	printf ("	Sense key specfic 2: %d\n", buffer[16]);
	printf ("	Sense key specfic 3: %d\n", buffer[17]);
}

/**
 * dump_usbmon
 */
static void dump_usbmon (usbmon_packet *u)
{
	char time_buf[64];
	struct tm *tm = NULL;
	char *type;
	char *xfer;
	char *setup;
	char *present;
	char *status;

	if (!u)
		return;

	switch (u->type) {
		case 'S': type = "URB_SUBMIT ('S')";   break;
		case 'C': type = "URB_COMPLETE ('C')"; break;
		default:  type = "Unknown";            break;
	}

	switch (u->xfer_type) {
		case 0:  xfer = "ISO (0)";     break;
		case 1:  xfer = "Intr (1)";    break;
		case 2:  xfer = "Control (2)"; break;
		case 3:  xfer = "Bulk (3)";    break;
		default: xfer = "Unknown";     break;
	}

	switch (u->flag_setup) {
		case 0:   setup = "relevant (0)";       break;
		case '-': setup = "not relevant ('-')"; break;
		default:  setup = "Unknown";            break;
	}

	switch (u->flag_data) {
		case 0:   present = "present (0)";       break;
		case '<': present = "not present ('<')"; break;
		case '>': present = "not present ('>')"; break;
		default:  present = "Unknown";           break;
	}

	switch (u->status) {
		case    0: status = "Success";                                  break;
		case  -32: status = "Broken pipe (-EPIPE)";                     break;
		case -115: status = "Operation now in progress (-EINPROGRESS)"; break;
		default:   status = "Unknown";                                  break;
	}

	//printf ("\e[32mUSB Block\e[0m\n");

	//dump_hex (data, 0, 40);
	//printf ("\n");

	printf ("URB ID: 0x%llx\n", u->id);
	printf ("URB Type: %s\n", type);
	printf ("URB transfer type: %s\n", xfer);
	printf ("Endpoint: 0x%02x\n", u->epnum);
	printf ("	Direction: %s\n", (u->epnum & 0x80) ? "IN" : "OUT");
	printf ("	Endpoint: %d\n", (u->epnum & 0x7f));
	//printf ("Device: %d\n", u->devnum);
	//printf ("URB bus id: %d\n", u->busnum);
	printf ("Device setup request: %s\n", setup);
	printf ("Data: %s\n", present);
#if 0
	printf ("URB sec: %lld\n", u->ts_sec);
	printf ("URB usec: %d\n", u->ts_usec);
#else
	tm = localtime ((time_t*)&u->ts_sec);
	strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S", tm);
	//printf ("URB Time: %s.%d\n", time_buf, u->ts_usec);
#endif
	printf ("URB status: %s (%d)\n", status, u->status);
	printf ("URB length: %d\n", u->length);
	printf ("Data length: %d\n", u->len_cap);
	//printf ("	xfer_flags: %d\n", u->xfer_flags);

	// Transfer Type: Control (2)
	// Type: Submit ('S')
	// Setup: Relevant (0)
	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		u16 *lang = (u16 *)(u->setup+4);
		u16 *len  = (u16 *)(u->setup+6);
		char *rt_direction;
		char *rt_type;
		char *rt_recipient;

		switch (u->setup[0] >> 7) {
			case 0:    rt_direction = "Host to Device"; break;
			case 1:    rt_direction = "Device to Host"; break;
			default:   rt_direction = "Unknown";        break;
		}

		switch ((u->setup[0] >> 5) & 0x03) {
			case 0:    rt_type = "Standard"; break;
			case 1:    rt_type = "Class";    break;
			case 2:    rt_type = "Vendor";   break;
			default:   rt_type = "Reserved"; break;
		}

		switch (u->setup[0] & 0x1f) {
			case 0:    rt_recipient = "Device";    break;
			case 1:    rt_recipient = "Interface"; break;
			case 2:    rt_recipient = "Endpoint";  break;
			case 3:    rt_recipient = "Other";     break;
			default:   rt_recipient = "Reserved";  break;
		}

		printf ("URB setup:\n");
		printf ("	bmRequestType: 0x%02x\n", u->setup[0]);
		printf ("		Transfer direction: %s\n", rt_direction);
		printf ("		Type: %s\n", rt_type);
		printf ("		Recipient: %s\n", rt_recipient);
		printf ("	bRequest: %d\n",          u->setup[1]);
		printf ("	Descriptor index: %d\n",  u->setup[2]);
		printf ("	bDescriptor type: %d\n",  u->setup[3]);
		printf ("	Language Id: 0x%04x\n",  *lang);
		printf ("	wLength: %d\n",          *len);
	}
}


/**
 * listen
 */
static void listen (FILE *f)
{
	u8 buffer[128];
	usbmon_packet usb;
	int count;
	int done = 0;
	int want = 0;
	u8 collected[1024];

	while (!feof (f)) {
		memset (buffer, 0xdd, sizeof (buffer));

		count = fread (&usb, 1, 48, f);
		if (count < 48) {
			if (count == 0 && feof (f))
				break;
			exit (1);
		}

		if (!valid_usbmon (&usb)) {
			printf ("XXX invalid usbmon packet\n");
			break;
		}

		dump_usbmon (&usb);

		if (!usb.len_cap)
			continue;

		count = fread (buffer, 1, usb.len_cap, f);
		dump_hex (buffer, 0, usb.len_cap);

		if (valid_dd (&usb, buffer)) {
			dump_dd (&usb, buffer);
			continue;
		}

		if (valid_csw (&usb, buffer)) {
			dump_csw (buffer);
			continue;
		}

		if (valid_cbw (&usb, buffer)) {
			dump_cbw (buffer);

			want = (buffer[19]<<8) + buffer[20];	// XXX Big-endian
			done = 0;
			continue;
		}

		if (valid_req_sense (&usb, buffer)) {
			dump_req_sense (buffer);
			continue;
		}

		if (want > 0) {
			memcpy (collected + done, buffer, usb.len_cap);
			want -= usb.len_cap;
			done += usb.len_cap;
			//log_info ("done = %d, want = %d\n", done, want);

			if (want <= 0) {
				long size = 0;
				char *type = NULL;
				int disk = 0;

				if (done == 0x238) {	// VENDOR 0xDA
					switch (collected[0x230]) {
						case 0x10: type = "Dir";     break;
						case 0x20: type = "File";    break;
						default:   type = "Unknown"; break;
					}

					disk = collected[0] & 0x0F;
					log_info ("Disk: %d\n", disk);

					log_info ("%s: ", type);
					dump_string (collected + 4);

					size = (collected[0x210]) + (collected[0x211]<<8) + (collected[0x212]<<16) + (collected[0x213]<<24);
					printf ("Size: %ld\n", size);
				} else if (done == 0x20C) {	// VENDOR 0xDB
					disk = collected[0] & 0x0F;
					log_info ("Disk: %d\n", disk);

					log_info ("Listing: ");
					dump_string (collected + 4);
				} else if (done == 0x2800) {	// VENDOR 0xDA status
					log_info ("Status:\n");
					dump_string (collected + 4);
				} else {
					log_info ("Unknown: ");
					dump_string (collected + 4);
				}
				//log_hex (collected + 0x210, 0, done - 0x210);
				log_info ("%02x %02x %02x %02x\n", collected[0], collected[1], collected[2], collected[3]);
				log_hex (collected, 0, done);
			}
		} else {
			dump_hex (buffer, 0, usb.len_cap);
		}
	}

}


/**
 * main
 */
int main (int argc, char *argv[])
{
	FILE *f = NULL;

	log_init ("/dev/pts/3");

	if (argc == 2) {
		f = fopen (argv[1], "r");
	} else {
		f = fopen ("/dev/usbmon3", "r");
	}

	if (f == NULL) {
		printf ("fopen\n");
		exit (1);
	}

	listen (f);
	fclose (f);
	return 0;
}

