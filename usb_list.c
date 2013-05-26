#include <stdio.h>
#include <assert.h>
#include <libusb.h>

int main (int argc, char *argv[])
{
	libusb_context *ctx = NULL;
	libusb_device **list = NULL;
	int rc;
	ssize_t count;
	ssize_t idx;

	rc = libusb_init(&ctx);
	assert(rc == 0);

	libusb_set_debug (ctx, 3);		// verbose

	count = libusb_get_device_list(ctx, &list);
	assert(count > 0);
	printf ("%lu devices\n", count);

	for (idx = 0; idx < count; idx++) {
		libusb_device *device = list[idx];
		struct libusb_device_descriptor desc = {0};

		rc = libusb_get_device_descriptor(device, &desc);
		assert(rc == 0);

		printf("Vendor:Device = %04x:%04x\n", desc.idVendor, desc.idProduct);
	}

	libusb_exit(ctx);
	return 0;
}
