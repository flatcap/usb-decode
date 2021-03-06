#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>

#define CTRL_IN			LIBUSB_ENDPOINT_IN |LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_DEVICE
#define CTRL_OUT		LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_DEVICE

const int PACKET_CTRL_LEN = 2;

const int TIMEOUT = 5000;	/* timeout in ms */

/**
 * test_control_transfer
 */
static int test_control_transfer (struct libusb_device_handle *handle)
{
	int r, i;
	unsigned char answer[PACKET_CTRL_LEN];
	unsigned char question[PACKET_CTRL_LEN];

	for (i = 0; i < PACKET_CTRL_LEN; i++)
		question[i] = 0x20 + i;

	r = libusb_control_transfer (handle, CTRL_OUT, 0x02, 0, 0, question, PACKET_CTRL_LEN, TIMEOUT);
	if (r < 0) {
		printf ("Control Out error %d\n", r);
		return r;
	}

	r = libusb_control_transfer (handle, CTRL_IN, 0x02, 0, 0, answer, PACKET_CTRL_LEN, TIMEOUT);
	if (r < 0) {
		printf ("Control IN error %d\n", r);
		return r;
	}

	for (i = 0; i < PACKET_CTRL_LEN; i++) {
		if (i % 8 == 0)
			printf ("\n");
		printf ("%02x, %02x; ", question[i], answer[i]);
	}
	printf ("\n");

	return 0;
}

/**
 * main
 */
int main (void)
{
	const int vid   = 0x1bb4;	// Satmap
	const int pid   = 0x000a;	// Active 10
	const int iface = 0;		// GPS only has one interface

	libusb_device_handle *handle = NULL;
	libusb_context *ctx = NULL;
	int kernel;

	if (libusb_init (&ctx) < 0) {
		printf ("ERROR: libusb_init\n");
		return 1;
	}

	handle = libusb_open_device_with_vid_pid (ctx, vid, pid);
	if (handle == NULL) {
		printf ("ERROR: libusb_open_device_with_vid_pid\n");
		return 1;
	}

	kernel = libusb_kernel_driver_active (handle, iface);
	if (kernel) {
		libusb_detach_kernel_driver (handle, iface);
	}

	if (libusb_claim_interface (handle, 0) < 0) {
		printf ("ERROR: libusb_claim_interface\n");
		return 1;
	}

	test_control_transfer (handle);

	if (kernel) {
		libusb_attach_kernel_driver (handle, iface);
	}

	libusb_release_interface (handle, iface);
	libusb_close (handle);
	libusb_exit (ctx);
	return 0;
}
