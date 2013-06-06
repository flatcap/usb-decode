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

	if (libusb_kernel_driver_active (handle, iface) == 0) {
		if (libusb_attach_kernel_driver (handle, iface) == 0) {
			printf ("kernel driver attached\n");
		} else {
			printf ("ERROR: libusb_attach_kernel_driver\n");
		}
	} else {
		printf ("kernel driver was already attached\n");
	}

	libusb_release_interface (handle, iface);
	libusb_close (handle);
	libusb_exit (ctx);

	return 0;
}

