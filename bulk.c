#include <stdio.h>
#include <libusb.h>

int main (int argc, char *argv[])
{
	libusb_device **devs;
	libusb_device_handle *dev_handle;
	libusb_context *ctx = NULL;
	int r; //for return values
	ssize_t count; //holding number of devices in list
	r = libusb_init(&ctx); //initialize the library for the session we just declared
	if(r < 0) {
		printf ("libusb_init failed\n");
		return 1;
	}
	libusb_set_debug(ctx, 3); //set verbosity level to 3, as suggested in the documentation

	count = libusb_get_device_list(ctx, &devs); //get the list of devices
	if(count < 0) {
		printf ("libusb_get_device_list failed\n");
		return 1;
	}
	printf ("%lu devices is list\n", count);

	dev_handle = libusb_open_device_with_vid_pid(ctx, 0x1BB4, 0x000A); //these are vendorID and productID I found for my usb device
	if(dev_handle == NULL) {
		printf ("Cannot open device\n");
		return 0;
	} else {
		printf ("Device Opened\n");
	}
	libusb_free_device_list(devs, 1); //free the list, unref the devices in it

	char *data = "abcd";

	int actual; //used to find out how many bytes were written
	if(libusb_kernel_driver_active(dev_handle, 0) == 1) { //find out if kernel driver is attached
		printf ("Kernel Driver Active\n");
		if(libusb_detach_kernel_driver(dev_handle, 0) == 0) //detach it
			printf ("Kernel Driver Detached!\n");
	}
	r = libusb_claim_interface(dev_handle, 0); //claim interface 0 (the first) of device (mine had jsut 1)
	if(r < 0) {
		printf ("Cannot Claim Interface\n");
		return 1;
	}
	printf ("Claimed Interface\n");

	printf ("Data->%s<<\n", data);
	printf ("Writing Data...\n");
	r = libusb_bulk_transfer(dev_handle, (2 | LIBUSB_ENDPOINT_OUT), (unsigned char*) data, 4, &actual, 0); //my device's out endpoint was 2, found with trial- the device had 2 endpoints: 2 and 129
	if(r == 0 && actual == 4) {
		printf ("Writing Successful!\n");
	} else {
		printf ("Write Error\n");
	}

	r = libusb_release_interface(dev_handle, 0); //release the claimed interface
	if(r!=0) {
		printf ("Cannot Release Interface\n");
		return 1;
	}
	printf ("Released Interface\n");

	libusb_close(dev_handle); //close the device we opened
	libusb_exit(ctx); //needs to be called to end the

	return 0;
}
