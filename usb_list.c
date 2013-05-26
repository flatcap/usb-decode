#include <stdio.h>
#include <assert.h>
#include <libusb.h>

int main (int argc, char *argv[])
{
	libusb_context *context = NULL;
	libusb_device **list = NULL;
	int rc = 0;
	ssize_t count = 0;
	size_t idx;

	rc = libusb_init(&context);
	assert(rc == 0);

	count = libusb_get_device_list(context, &list);
	assert(count > 0);

	for (idx = 0; idx < count; idx++) {
		libusb_device *device = list[idx];
		struct libusb_device_descriptor desc = {0};

		rc = libusb_get_device_descriptor(device, &desc);
		assert(rc == 0);

		printf("Vendor:Device = %04x:%04x\n", desc.idVendor, desc.idProduct);
	}
}
