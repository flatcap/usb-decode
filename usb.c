#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <string.h>

#define MICROCHIP_VENDOR_ID	0x1BB4
#define MY_PRODUCT_ID		0x000A

/**
 * is_usbdevblock
 *
 * Check For Our VID & PID
 */
static int is_usbdevblock (libusb_device *dev)
{
	struct libusb_device_descriptor desc;
	libusb_get_device_descriptor (dev, &desc);

	if (desc.idVendor == MICROCHIP_VENDOR_ID && desc.idProduct == MY_PRODUCT_ID) {
		return 1;
	}

	return 0;
}

/**
 * main
 */
int main (int argc, char *argv[])
{
	// discover devices
	libusb_device **list;
	libusb_device *found = NULL;
	libusb_context *ctx = NULL;
	int attached = 0;

	libusb_init (&ctx);
	libusb_set_debug (ctx,3);
	ssize_t cnt = libusb_get_device_list (ctx, &list);
	ssize_t i = 0;
	int err = 0;
	if (cnt < 0) {
		printf ("no usb devices found\n");
		exit (1);
	}

	// find our device
	for (i = 0; i < cnt; i++) {
		libusb_device *device = list[i];
		if (is_usbdevblock (device)) {
			found = device;
			break;
		}
	}

	if (found) {
		printf ("found usb-dev-block!\n");
		libusb_device_handle *handle;
		err = libusb_open (found, &handle);
		if (err) {
			printf ("Unable to open usb device\n");
			exit (1);
		}

		if (libusb_kernel_driver_active (handle,0)) {
			printf ("Device busy...detaching...\n");
			libusb_detach_kernel_driver (handle,0);
			attached = 1;
		}else printf ("Device free from kernel\n");

		err = libusb_claim_interface (handle, 0);
		if (err) {
			printf ("Failed to claim interface. ");
			switch (err) {
				case LIBUSB_ERROR_NOT_FOUND:	printf ("not found\n");	break;
				case LIBUSB_ERROR_BUSY:		printf ("busy\n");	break;
				case LIBUSB_ERROR_NO_DEVICE:	printf ("no device\n");	break;
				default:			printf ("other\n");	break;
			}
			exit (1);
		}

		int endpoint = 0x01;
		int timeout = 500;	//in milliseconds
		int written = 0;
		unsigned char buffer[65];

		memset (buffer, 0, sizeof (buffer));
		sprintf ((char*) buffer, "USBC");

		buffer[0] = 0x80;
		libusb_interrupt_transfer (handle, endpoint, buffer, 64, &written, timeout);
		printf ("wrote %d bytes to endpoint address 0x%X\n", written, endpoint);

		buffer[0] = 0x50;
		libusb_interrupt_transfer (handle, endpoint, buffer, 64, &written, timeout);
		printf ("wrote %d bytes to endpoint address 0x%X\n", written, endpoint);
		libusb_interrupt_transfer (handle, 0x81, buffer, 64, &written, timeout);
		printf ("read %d bytes from endpoint address 0x%X\n", written, endpoint);
		printf ("analog value is 0x%2.2X%2.2X\n", buffer[1], buffer[2]);

		//if we detached kernel driver, reattach.
		if (attached == 1) {
			libusb_attach_kernel_driver (handle, 0);
		}

		libusb_close (handle);
	}

	libusb_free_device_list (list, 1);
	libusb_exit (ctx);

	return 0;
}


