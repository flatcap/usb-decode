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
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>

#include "usbtypes.h"
#include "usb.h"

/**
 * dump_hex
 */
static void dump_hex (void *buf, int start, int length)
{
	int off, i, s, e;
	u8 *mem = buf;

#if 0
	unsigned char last[16];
	int same = 0;
#endif
	s =  start                & ~15;	// round down
	e = (start + length + 15) & ~15;	// round up

	for (off = s; off < e; off += 16) {
#if 0
		if (memcmp ((char*)buf+off, last, sizeof (last)) == 0) {
			if (!same) {
				printf ("	        ...\n");
				same = 1;
			}
			if ((off + 16) < e)
				continue;
		} else {
			same = 0;
			memcpy (last, (char*)buf+off, sizeof (last));
		}
#endif

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
 * vendor_command_str
 */
static const char *vendor_command_str (u8 command, u8 sub_command)
{
	switch ((command<<8)+sub_command) {
		case 0xdb00: return "List Directory";
		case 0xda01: return "File/Dir list";
		case 0xdb02: return "Select transfer file";
		case 0xda03: return "Download file";
		case 0xdb04: return "Upload file";
		case 0xdb05: return "Select file/dir";
		case 0xdb06: return "Delete file";
		case 0xdb07: return "Rename file/dir";
		case 0xdb08: return "Make directory";
		case 0xdb09: return "Remove directory";
		default:     return "Unknown";
	}
}


/**
 * vendor_status
 */
static bool vendor_status (libusb_device_handle *handle)
{
	int r;
	int xfer = 0;
	command_block_wrapper cbw;
	command_status_wrapper csw;
	u8 data[0x20C];
	int tag = rand();

	memset (&cbw, 0, sizeof (cbw));
	memset (&csw, 0, sizeof (csw));
	memset (data, 0, sizeof (data));

	// Command block
	cbw.dCBWSignature[0]       = 'U';
	cbw.dCBWSignature[1]       = 'S';
	cbw.dCBWSignature[2]       = 'B';
	cbw.dCBWSignature[3]       = 'C';
	cbw.dCBWTag                = tag;
	cbw.dCBWDataTransferLength = 0x20C;		// Length of reply
	cbw.bmCBWFlags             = LIBUSB_ENDPOINT_OUT;
	cbw.bCBWLUN                = 0;
	cbw.bCBWCBLength           = 7;

	// Vendor command
	cbw.CBWCB[0] = 0xDB;	// vendor send
	cbw.CBWCB[1] = 0x02;	// select file
	cbw.CBWCB[2] = 0;
	cbw.CBWCB[3] = 0;
	cbw.CBWCB[4] = 0x02;	// length
	cbw.CBWCB[5] = 0x0C;
	cbw.CBWCB[6] = 0;

	data[0]  = 0x84;	// Status
	data[4]  = 'S';
	data[6]  = 'T';
	data[8]  = 'A';
	data[10] = 'T';
	data[12] = 'U';
	data[14] = 'S';

	if (0)
		dump_hex (data, 0, 32);

	printf ("Command:\n");
	printf ("\tSignature:   %c%c%c%c\n",    cbw.dCBWSignature[0], cbw.dCBWSignature[1], cbw.dCBWSignature[2], cbw.dCBWSignature[3]);
	printf ("\tTag:         0x%08X\n",      cbw.dCBWTag);
	printf ("\tXfer length: 0x%04X (%d)\n", cbw.dCBWDataTransferLength, cbw.dCBWDataTransferLength);
	printf ("\tFlags:       0x%02X\n",      cbw.bmCBWFlags);
	printf ("\tLUN:         %d\n",          cbw.bCBWLUN);
	printf ("\tCDB length:  %d\n",          cbw.bCBWCBLength);
	printf ("\tCDB:         ");
	for (r = 0; r < cbw.bCBWCBLength; r++) {
		printf ("%02X ", cbw.CBWCB[r]);
	}
	printf ("\n");
	printf ("\t\tCommand: %s\n", vendor_command_str (cbw.CBWCB[0], cbw.CBWCB[1]));
	printf ("\t\tLength:  0x%04X (%d) bytes\n", (cbw.CBWCB[4]<<8) + cbw.CBWCB[5], (cbw.CBWCB[4]<<8) + cbw.CBWCB[5]);
	printf ("\n");

	// Send command
	xfer = 0;
	r = libusb_bulk_transfer (handle, (2 | LIBUSB_ENDPOINT_OUT), (unsigned char*) &cbw, sizeof (cbw), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (cbw))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(send)");
		//return false;
	}
	printf ("send command  ok: 0x%04X (%d) bytes\n", xfer, xfer);

	// Send data
	xfer = 0;
	r = libusb_bulk_transfer (handle, (2 | LIBUSB_ENDPOINT_OUT), data, sizeof (data), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (data))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(send)");
		//return false;
	}
	printf ("send data     ok: 0x%04X (%d) bytes\n", xfer, xfer);

	// Receive response
	xfer = 0;
	r = libusb_bulk_transfer (handle, (1 | LIBUSB_ENDPOINT_IN), (unsigned char*) &csw, sizeof (csw), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (csw))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(recv)");
		//return false;
	}
	printf ("recv response ok: 0x%04X (%d) bytes\n", xfer, xfer);

	printf ("\n");
	printf ("Response:\n");
	printf ("\tSignature:   %c%c%c%c\n",    csw.dCSWSignature[0], csw.dCSWSignature[1], csw.dCSWSignature[2], csw.dCSWSignature[3]);
	printf ("\tTag:         0x%08X\n",      csw.dCSWTag);
	printf ("\tResidue:     0x%04X (%u)\n", csw.dCSWDataResidue, csw.dCSWDataResidue);
	printf ("\tStatus:      %d\n",          csw.bCSWStatus);
	printf ("\n");

	printf ("----------------------------------------------------------------------\n");

	// Command block
	cbw.dCBWSignature[0]       = 'U';
	cbw.dCBWSignature[1]       = 'S';
	cbw.dCBWSignature[2]       = 'B';
	cbw.dCBWSignature[3]       = 'C';
	cbw.dCBWTag                = tag;
	cbw.dCBWDataTransferLength = 0x2800;		// Length of reply
	cbw.bmCBWFlags             = LIBUSB_ENDPOINT_IN;
	cbw.bCBWLUN                = 0;
	cbw.bCBWCBLength           = 7;

	// Vendor command
	cbw.CBWCB[0] = 0xDA;	// vendor recv
	cbw.CBWCB[1] = 0x03;	// get file
	cbw.CBWCB[2] = 0;
	cbw.CBWCB[3] = 0;
	cbw.CBWCB[4] = 0x28;	// length (10KiB)
	cbw.CBWCB[5] = 0x00;
	cbw.CBWCB[6] = 0;

	printf ("\n");
	printf ("Command:\n");
	printf ("\tSignature:   %c%c%c%c\n",    cbw.dCBWSignature[0], cbw.dCBWSignature[1], cbw.dCBWSignature[2], cbw.dCBWSignature[3]);
	printf ("\tTag:         0x%08X\n",      cbw.dCBWTag);
	printf ("\tXfer length: 0x%04X (%d)\n", cbw.dCBWDataTransferLength, cbw.dCBWDataTransferLength);
	printf ("\tFlags:       0x%02X\n",      cbw.bmCBWFlags);
	printf ("\tLUN:         %d\n",          cbw.bCBWLUN);
	printf ("\tCDB length:  %d\n",          cbw.bCBWCBLength);
	printf ("\tCDB:         ");
	for (r = 0; r < cbw.bCBWCBLength; r++) {
		printf ("%02X ", cbw.CBWCB[r]);
	}
	printf ("\n");
	printf ("\t\tCommand: %s\n", vendor_command_str (cbw.CBWCB[0], cbw.CBWCB[1]));
	printf ("\t\tLength:  0x%04X (%d) bytes\n", (cbw.CBWCB[4]<<8) + cbw.CBWCB[5], (cbw.CBWCB[4]<<8) + cbw.CBWCB[5]);
	printf ("\n");

	// Send command
	xfer = 0;
	r = libusb_bulk_transfer (handle, (2 | LIBUSB_ENDPOINT_OUT), (unsigned char*) &cbw, sizeof (cbw), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (cbw))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(send)");
		return false;
	}
	printf ("send command  ok: 0x%04X (%d) bytes\n", xfer, xfer);

	// Receive response
	xfer = 0;
	r = libusb_bulk_transfer (handle, (1 | LIBUSB_ENDPOINT_IN), (unsigned char*) &csw, sizeof (csw), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (csw))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(recv)");
		return false;
	}
	printf ("recv response ok: 0x%04X (%d) bytes\n", xfer, xfer);

	printf ("\n");
	printf ("Response:\n");
	printf ("\tSignature:   %c%c%c%c\n",    csw.dCSWSignature[0], csw.dCSWSignature[1], csw.dCSWSignature[2], csw.dCSWSignature[3]);
	printf ("\tTag:         0x%08X\n",      csw.dCSWTag);
	printf ("\tResidue:     0x%04X (%u)\n", csw.dCSWDataResidue, csw.dCSWDataResidue);
	printf ("\tStatus:      %d\n",          csw.bCSWStatus);
	printf ("\n");

	// Receive data
	xfer = 0;
	r = libusb_bulk_transfer (handle, (1 | LIBUSB_ENDPOINT_IN), data, sizeof (data), &xfer, 1000);
	if ((r != 0) || (xfer < (int) sizeof (data))) {
		printf ("r = %d, xfer = %d\n", r, xfer);
		perror ("libusb_bulk_transfer(recv)");
		//return false;
	}
	printf ("recv response ok: 0x%04X (%d) bytes\n", xfer, xfer);

	if (0)
		dump_hex (data, 0, sizeof (data));

	return true;
}

/**
 * scsi_sense
 */
static int scsi_sense (void)
{
	unsigned char send[6];
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

	send[0] = 3;		// Request Sense(6)
	send[4] = 18;		// Expected length of reply

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
		printf ("%02X ", send[i]);
	}
	printf ("\n");

	ioctl (fd, SG_IO, &hdr);

	printf ("recv:\t");
	for (i = 0; i < send[4]; i++) {
		printf ("%02X ", recv[i]);
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
	const int vid   = 0x1bb4;	// Satmap
	const int pid   = 0x000a;	// Active 10
	const int iface = 0;		// GPS only has one interface

	srand (time (NULL));

	libusb_device_handle *handle = NULL;
	libusb_context *ctx = NULL;
	int kernel;

	if (libusb_init (&ctx) < 0) {
		printf ("ERROR: libusb_init\n");
		return 1;
	}

	libusb_set_debug (ctx, LIBUSB_LOG_LEVEL_DEBUG);

	handle = libusb_open_device_with_vid_pid (ctx, vid, pid);
	if (handle == NULL) {
		printf ("ERROR: libusb_open_device_with_vid_pid\n");
		return 1;
	}

	kernel = libusb_kernel_driver_active (handle, iface);
	if (kernel) {
		libusb_detach_kernel_driver (handle, iface);
	}

	if (libusb_claim_interface (handle, iface) < 0) {
		printf ("ERROR: libusb_claim_interface\n");
		return 1;
	}

	if (!vendor_status (handle)) {
		//printf ("vendor_status\n");
	}

	if (0) {
		scsi_sense();
	}

	libusb_release_interface (handle, iface);

	if (kernel) {
		libusb_attach_kernel_driver (handle, iface);
	}

	libusb_close (handle);
	libusb_exit (ctx);

	return 0;
}

