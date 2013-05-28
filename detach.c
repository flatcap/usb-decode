#include <stdio.h>
#include <unistd.h>
#include <libusb.h>

int main (int argc, char *argv[])
{
	const int vid   = 0x1bb4;	// Satmap
	const int pid   = 0x000a;	// Active 10
	const int iface = 0;		// GPS only has one interface

	libusb_device_handle *handle = NULL;
	libusb_context *ctx = NULL;

	if (libusb_init (&ctx) < 0) {
		printf ("ERROR: libusb_init\n");
		return 1;
	}

	handle = libusb_open_device_with_vid_pid (ctx, vid, pid);
	if (handle == NULL) {
		printf ("ERROR: libusb_open_device_with_vid_pid\n");
		return 1;
	}

	if (libusb_kernel_driver_active (handle, iface) == 1) {
		if (libusb_detach_kernel_driver (handle, iface) == 0) {
			printf ("kernel driver detached\n");
		} else {
			printf ("ERROR: libusb_detach_kernel_driver\n");
		}
	} else {
		printf ("kernel driver was already detached\n");
	}

	libusb_release_interface (handle, iface);
	libusb_close (handle);
	libusb_exit (ctx);

	return 0;
}

