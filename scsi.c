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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>

/**
 * main
 */
int main (int argc, char *argv[])
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

