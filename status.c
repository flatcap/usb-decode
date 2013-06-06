/* Copyright (c) 2013 Richard Russon (FlatCap)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <libusb.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>

/**
 * vendor
 */
static int vendor (void)
{
	unsigned char send[7];
	unsigned char recv[64];
	sg_io_hdr_t hdr;
	int fd;
	unsigned int i;

	fd = open ("/dev/sg3", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf ("open failed\n");
		return 1;
	}

	memset (&hdr, 0, sizeof (hdr));
	memset (send, 0, sizeof (send));
	memset (recv, 0, sizeof (recv));

	send[0] = 0xdb;
	send[1] = 0x02;
	send[2] = 0x00;
	send[3] = 0x00;
	send[4] = 0x02;
	send[5] = 0x0c;
	send[6] = 0x00;

	hdr.interface_id    = 'S';
	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	hdr.cmd_len         = sizeof (send);
	hdr.mx_sb_len       = sizeof (recv);
	hdr.dxfer_len       = sizeof (recv);
	hdr.dxferp          = recv;
	hdr.cmdp            = send;
	hdr.timeout         = 5000;

	printf ("send:\t");
	for (i = 0; i < sizeof (send); i++) {
		printf ("%02x ", send[i]);
	}
	printf ("\n");

	ioctl (fd, SG_IO, &hdr);

	printf ("recv:\t");
	for (i = 0; i < send[4]; i++) {
		printf ("%02x ", recv[i]);
	}
	printf ("\n");

	close (fd);
	return 0;
}

/**
 * main
 */
int main (int argc, char *argv[])
{
#if 1
	const int vid   = 0x1bb4;	// Satmap
	const int pid   = 0x000a;	// Active 10
	const int iface = 0;		// GPS only has one interface

	libusb_device_handle *handle = NULL;
	libusb_context *ctx = NULL;
	int written;
	int kernel;
	int r;
	char *data = "abcd";

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

	//---------------------------------------------------------------------------------

	printf ("Data->%s<<\n", data);
	printf ("Writing Data...\n");
	r = libusb_bulk_transfer (handle, (2 | LIBUSB_ENDPOINT_OUT), (unsigned char*) data, 4, &written, 0);
	if (r == 0 && written == 4) {
		printf ("Writing Successful!\n");
	} else {
		printf ("Write Error\n");
	}

	//---------------------------------------------------------------------------------

	if (kernel) {
		libusb_attach_kernel_driver (handle, iface);
	}

	libusb_release_interface (handle, iface);
	libusb_close (handle);
	libusb_exit (ctx);
#endif

	vendor();
	return 0;
}

