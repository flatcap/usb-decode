#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>

// HID Class-Specific Requests values. See section 7.2 of the HID specifications
#define HID_GET_REPORT			0x01
#define HID_GET_IDLE			0x02
#define HID_GET_PROTOCOL		0x03
#define HID_SET_REPORT			0x09
#define HID_SET_IDLE			0x0A
#define HID_SET_PROTOCOL		0x0B
#define HID_REPORT_TYPE_INPUT		0x01
#define HID_REPORT_TYPE_OUTPUT		0x02
#define HID_REPORT_TYPE_FEATURE		0x03

#define CTRL_IN			LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE
#define CTRL_OUT		LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE

const static int PACKET_CTRL_LEN = 2;

const static int PACKET_INT_LEN = 2;
const static int INTERFACE = 0;
const static int ENDPOINT_INT_IN = 0x81;	/* endpoint 0x81 address for IN */
const static int ENDPOINT_INT_OUT = 0x01;	/* endpoint 1 address for OUT */
const static int TIMEOUT = 5000;		/* timeout in ms */

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

	r = libusb_control_transfer (handle, CTRL_OUT, HID_SET_REPORT, (HID_REPORT_TYPE_FEATURE << 8) | 0x00, 0, question, PACKET_CTRL_LEN, TIMEOUT);
	if (r < 0) {
		printf ("Control Out error %d\n", r);
		return r;
	}

	r = libusb_control_transfer (handle, CTRL_IN, HID_GET_REPORT, (HID_REPORT_TYPE_FEATURE << 8) | 0x00, 0, answer, PACKET_CTRL_LEN, TIMEOUT);
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
 * test_interrupt_transfer
 */
static int test_interrupt_transfer (struct libusb_device_handle *handle)
{
	int r, i;
	int transferred;
	unsigned char answer[PACKET_INT_LEN];
	unsigned char question[PACKET_INT_LEN];

	for (i = 0; i < PACKET_INT_LEN; i++)
		question[i] = 0x40 + i;

	r = libusb_interrupt_transfer (handle, ENDPOINT_INT_OUT, question, PACKET_INT_LEN, &transferred, TIMEOUT);
	if (r < 0) {
		printf ("Interrupt write error %d\n", r);
		return r;
	}
	r = libusb_interrupt_transfer (handle, ENDPOINT_INT_IN, answer, PACKET_INT_LEN, &transferred, TIMEOUT);
	if (r < 0) {
		printf ("Interrupt read error %d\n", r);
		return r;
	}
	if (transferred < PACKET_INT_LEN) {
		printf ("Interrupt transfer short read (%d)\n", r);
		return -1;
	}

	for (i = 0; i < PACKET_INT_LEN; i++) {
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

	printf ("Testing control transfer using loop back test of feature report");
	test_control_transfer ();
	printf ("Testing interrupt transfer using loop back test of input/output report");
	test_interrupt_transfer ();

	if (kernel) {
		libusb_attach_kernel_driver (handle, iface);
	}

	libusb_release_interface (handle, iface);
	libusb_close (handle);
	libusb_exit (ctx);
	return 0;
}
