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
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "usb.h"
#include "endians.h"

#if 0
#include "log.c"
#else
#define log_init(...)	/*nothing*/
#define log_info	printf
#define log_debug	printf
#define log_error	printf
#define log_hex		dump_hex
#endif

int error_count = 0;
int error_max   = 0;

#define RETURN(retval)	{ log_error ("\e[31mTest failed: %s(%d)\e[0m\n", __FUNCTION__, __LINE__); error_count++; if (error_count > error_max) exit (1); return retval; }
#define CONTINUE	{ log_error ("\e[31mTest failed: %s(%d)\e[0m\n", __FUNCTION__, __LINE__); error_count++; if (error_count > error_max) exit (1); continue; }

/**
 * enum fsm
 */
enum fsm {
	send,
	send_ack,
	recv,
	recv_ack,
	rcpt,
	rcpt_ack
};

/**
 * struct current_state
 */
struct current_state {
	enum fsm waiting_for;
	int      command;
	int      tag;
	u32      xfer_len;
	u32      urb_len;
	u32      data_len;
	u32      done;
};


/**
 * dump_hex
 */
static void dump_hex (void *buf, int start, int length)
{
	int off, i, s, e;
	u8 *mem = buf;

	unsigned char last[16];
	int same = 0;
	s =  start                & ~15;	// round down
	e = (start + length + 15) & ~15;	// round up

	for (off = s; off < e; off += 16) {

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

#if 0
/**
 * dump_string
 */
static void dump_string (u8 *data)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (!data[2*i])
			break;
		log_info ("%c", data[2*i]);
	}
	log_info ("\n");
}

#endif

/**
 * scsi_dump_sense
 */
static bool scsi_dump_sense (u8 *buffer, int size)
{
	const char *sense = NULL;

	/*
	printf ("\n");
	printf ("\tValid: %d\n", buffer[0]>>7);
	printf ("\tError code: 0x%02x\n", buffer[0]&0x7F);

	printf ("\tSense key: 0x%02x\n", buffer[2]&0x0F);
	if (buffer[7] == 0x0A) {
		printf ("\tAdditional sense: %d\n", buffer[12]);
		printf ("\tSense qualifier: %d\n", buffer[13]);
	}
	*/

	switch (((buffer[2]&0x0F)<<16) + (buffer[12] << 8) + buffer[13]) {
		case 0x000000: sense = "NO SENSE";                                          break;
		case 0x011701: sense = "RECOVERED DATA WITH RETRIES";                       break;
		case 0x011800: sense = "RECOVERED DATA WITH ECC";                           break;
		case 0x020401: sense = "LOGICAL DRIVE NOT READY - BECOMING READY";          break;
		case 0x020402: sense = "LOGICAL DRIVE NOT READY - INITIALIZATION REQUIRED"; break;
		case 0x020404: sense = "LOGICAL UNIT NOT READY - FORMAT IN PROGRESS";       break;
		case 0x0204FF: sense = "LOGICAL DRIVE NOT READY - DEVICE IS BUSY";          break;
		case 0x020600: sense = "NO REFERENCE POSITION FOUND";                       break;
		case 0x020800: sense = "LOGICAL UNIT COMMUNICATION FAILURE";                break;
		case 0x020801: sense = "LOGICAL UNIT COMMUNICATION TIME-OUT";               break;
		case 0x020880: sense = "LOGICAL UNIT COMMUNICATION OVERRUN";                break;
		case 0x023A00: sense = "MEDIUM NOT PRESENT";                                break;
		case 0x025400: sense = "USB TO HOST SYSTEM INTERFACE FAILURE";              break;
		case 0x028000: sense = "INSUFFICIENT RESOURCES";                            break;
		case 0x02FFFF: sense = "UNKNOWN ERROR";                                     break;
		case 0x030200: sense = "NO SEEK COMPLETE";                                  break;
		case 0x030300: sense = "WRITE FAULT";                                       break;
		case 0x031000: sense = "ID CRC ERROR";                                      break;
		case 0x031100: sense = "UNRECOVERED READ ERROR";                            break;
		case 0x031200: sense = "ADDRESS MARK NOT FOUND FOR ID FIELD";               break;
		case 0x031300: sense = "ADDRESS MARK NOT FOUND FOR DATA FIELD";             break;
		case 0x031400: sense = "RECORDED ENTITY NOT FOUND";                         break;
		case 0x033001: sense = "CANNOT READ MEDIUM - UNKNOWN FORMAT";               break;
		case 0x033101: sense = "FORMAT COMMAND FAILED";                             break;
		//case 0x0440NN: sense = "DIAGNOSTIC FAILURE ON COMPONENT NN (80H-FFH)";      break;
		case 0x051A00: sense = "PARAMETER LIST LENGTH ERROR";                       break;
		case 0x052000: sense = "INVALID COMMAND OPERATION CODE";                    break;
		case 0x052100: sense = "LOGICAL BLOCK ADDRESS OUT OF RANGE";                break;
		case 0x052400: sense = "INVALID FIELD IN COMMAND PACKET";                   break;
		case 0x052500: sense = "LOGICAL UNIT NOT SUPPORTED";                        break;
		case 0x052600: sense = "INVALID FIELD IN PARAMETER LIST";                   break;
		case 0x052601: sense = "PARAMETER NOT SUPPORTED";                           break;
		case 0x052602: sense = "PARAMETER VALUE INVALID";                           break;
		case 0x053900: sense = "SAVING PARAMETERS NOT SUPPORT";                     break;
		case 0x062800: sense = "NOT READY TO READY TRANSITION - MEDIA CHANGED";     break;
		case 0x062900: sense = "POWER ON RESET OR BUS DEVICE RESET OCCURRED";       break;
		case 0x062F00: sense = "COMMANDS CLEARED BY ANOTHER INITIATOR";             break;
		case 0x072700: sense = "WRITE PROTECTED MEDIA";                             break;
		case 0x0B4E00: sense = "OVERLAPPED COMMAND ATTEMPTED";                      break;
		default:       sense = "UNKNOWN";                                           break;
	}

	printf (" (%s)", sense);

	return true;
}

/**
 * dump_scsi
 *
 * can we display the info succinctly?
 */
static bool dump_scsi (int command, u8 *buffer, int size)
{
	int i;

	switch (command) {
		case 0x00:		// TEST UNIT READY
			// no data
			return true;
		case 0x03:		// REQUEST SENSE
			return scsi_dump_sense (buffer, size);
		case 0x12:		// INQUIRY
			break;
		case 0x1a:		// MODE SENSE (6)
			for (i = 4; i < size; i++) {
				if (buffer[i])
					return false;
			}

			if ((buffer[0] != 0) || (buffer[1] != 6))
				return false;

			printf (" (Drive %s)", (buffer[3] & 0x80) ? "RO" : "RW");
			return true;
		case 0x1e:		// PREVENT ALLOW MEDIUM REMOVAL
			return true;
		case 0x23:		// READ FORMAT CAPACITIES
			break;
		case 0x25:		// READ CAPACITY(10)
			printf (" (%dx%d bytes)", be32_to_cpup (buffer+0), be32_to_cpup (buffer+4));
			return true;
		case 0x28:		// READ(10)
			for (i = 0; i < size; i++) {
				if (buffer[i])
					return false;
			}
			printf (" (%d zeros)", size);
			return true;
		default:
			printf ("\n");
			dump_hex (buffer, 0, size);
			break;
	}

	return false;
}

/**
 * scsi_get_command
 */
static const char *scsi_get_command (u8 id)
{
	switch (id) {
		case 0x00: return "TEST UNIT      ";
		case 0x03: return "REQ SENSE      ";
		case 0x12: return "INQUIRY        ";
		case 0x1a: return "MODE SENSE     ";
		case 0x1e: return "PREVENT REMOVAL";
		case 0x23: return "READ FORMAT    ";
		case 0x25: return "READ CAPACITY  ";
		case 0x28: return "READ           ";
		case 0xda: return "Vendor 1       ";
		case 0xdb: return "Vendor 2       ";
		default:   return "UNKNOWN        ";
	}
}


/**
 * valid_cdb_6
 */
static int valid_cdb_6 (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (-1);

	if ((buffer[0] == 0x00) || (buffer[0] == 0x03) || (buffer[0] == 0x12)) {
		// Satmap sometimes sends 12 bytes; that doesn't match the SCSI spec.
		if ((cbw->bCBWCBLength != 12) && (cbw->bCBWCBLength != 6)) {
			printf ("XXX another invalid length: %d\n", cbw->bCBWCBLength);
			RETURN (-1);
		}
	} else {
		if (cbw->bCBWCBLength != 6) {
			printf ("XXX command = 0x%02x, length = %d\n", buffer[0], cbw->bCBWCBLength);
			RETURN (-1);
		}
	}

	// check that the slack space is empty
	for (i = 6; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (-1);
	}

	switch (buffer[0]) {
		case 0x00:		// TEST UNIT READY
			if ((buffer[1] & 0x1f) != 0)			// reserved
				RETURN (-1);
			if (buffer[2] || buffer[3] || buffer[4])	// reserved
				RETURN (-1);
			if (buffer[5])
				RETURN (-1);
			return buffer[0];
		case 0x03:		// REQUEST SENSE
			if ((buffer[1] & 0x1f) != 0)			// reserved
				RETURN (-1);
			if (buffer[2] || buffer[3])			// reserved
				RETURN (-1);
			if (buffer[5])
				RETURN (-1);
			// buffer[4] Allocation length can be any value
			return buffer[0];
		case 0x12:		// INQUIRY
			if (buffer[1] > 1)
				RETURN (-1);
			if ((buffer[2] != 0) && (buffer[2] != 0x80))	// page code: none, vendor
				RETURN (-1)
			if (buffer[5])
				RETURN (-1);
			// buffer[3],buffer[4] Allocation length can be any value
			return buffer[0];
		case 0x1a:		// MODE SENSE (6)
			if (buffer[5])
				RETURN (-1);
			// These fields can be any value:
			//	buffer[2] Page code
			//	buffer[3] Subpage code
			//	buffer[4] Allocation length
			return buffer[0];
		case 0x1e:		// PREVENT ALLOW MEDIUM REMOVAL
			if (buffer[1] || buffer[2] || buffer[3] || buffer[5])
				RETURN (-1);
			if (buffer[4] > 1)
				RETURN (-1);
			return buffer[0];
		default:
			printf ("XXX command 0x%02x\n", buffer[0]);
			RETURN (-1);
	}
}

/**
 * valid_cdb_10
 */
static int valid_cdb_10 (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (-1);

	if (cbw->bCBWCBLength != 10)
		RETURN (-1);

	// check that the slack space is empty
	for (i = 10; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (-1);
	}

	switch (buffer[0]) {
		case 0x23:		// READ FORMAT CAPACITIES
			// Opcode, then all zeros (except buffer[8] == 0xFC)
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (i == 8) {
					if (buffer[i] != 0xFC)
						RETURN (-1);
					continue;
				}
				if (buffer[i])
					RETURN (-1);
			}
			return buffer[0];
		case 0x25:		// READ CAPACITY(10)
			// Opcode, then all zeros
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (buffer[i])
					RETURN (-1);
			}
			return buffer[0];
		case 0x28:		// READ(10)
			// Opcode, then all zeros (except buffer[8] == 1)
			for (i = 1; i < cbw->bCBWCBLength; i++) {
				if (i == 8) {
					if (buffer[i] != 1)
						RETURN (-1);
					continue;
				}
				if (buffer[i])
					RETURN (-1);
			}
			return buffer[0];
		default:
			printf ("XXX command = 0x%02x\n", buffer[0]);
			RETURN (-1);
	}
}

/**
 * valid_cdb_vendor
 */
static int valid_cdb_vendor (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	unsigned int i;

	if (!u || !cbw || !buffer)
		RETURN (-1);

	if (cbw->bCBWCBLength != 7)
		RETURN (-1);

	// check that the slack space is empty
	for (i = 7; i < sizeof (cbw->CBWCB); i++) {
		if (buffer[i])
			RETURN (-1);
	}

	switch (buffer[0]) {
		case 0xda:		// Vendor 1
			return buffer[0];
		case 0xdb:		// Vendor 2
			return buffer[0];
		default:
			RETURN (-1);
	}
}


/**
 * valid_cdb
 */
static int valid_cdb (usbmon_packet *u, command_block_wrapper *cbw, u8 *buffer)
{
	if (!u || !cbw || !buffer)
		RETURN (-1);

	if (buffer[0] < 0x20)
		return valid_cdb_6 (u, cbw, buffer);
	else if (buffer[0] < 0x60)
		return valid_cdb_10 (u, cbw, buffer);
	else if ((buffer[0] == 0xda) || (buffer[0] == 0xdb))
		return valid_cdb_vendor (u, cbw, buffer);

	// Other commands exist, but we don't use them
	RETURN (-1);
}

/**
 * valid_cbw
 */
static int valid_cbw (usbmon_packet *u, u8 *buffer)
{
	// These fields could contain any value:
	//	dCBWTag, dCBWDataTransferLength

	command_block_wrapper *cbw = NULL;

	if (!u || !buffer)
		RETURN (-1);

	cbw = (command_block_wrapper *) buffer;

	if (u->len_cap != sizeof (command_block_wrapper))
		return (-1);

	if (strncmp (cbw->dCBWSignature, "USBC", 4) != 0)
		RETURN (-1);

	if ((cbw->bmCBWFlags != 0) && (cbw->bmCBWFlags != 0x80))
		RETURN (-1);

	if (cbw->bCBWLUN > 15)
		RETURN (-1);

	if ((cbw->bCBWCBLength < 6) || (cbw->bCBWCBLength > 16))
		RETURN (-1);

	return (valid_cdb (u, cbw, cbw->CBWCB));
}

#if 0
/**
 * valid_csw
 */
static bool valid_csw (usbmon_packet *u, u8 *buffer)
{
	// This field could contain any value:
	//	dCSWTag

	command_status_wrapper *csw = NULL;

	if (!u || !buffer)
		RETURN (false);

	csw = (command_status_wrapper *) buffer;

	if (u->len_cap != sizeof (command_status_wrapper))
		return (false);

	if (strncmp (csw->dCSWSignature, "USBS", 4) != 0)
		RETURN (false);

	if (csw->bCSWStatus > 2)
		RETURN (false);

	//XXX how do I validate dCSWDataResidue?
	return true;
}

/**
 * valid_dd
 */
static bool valid_dd (usbmon_packet *usb, u8 *data)
{
	if (usb->epnum != 0x80)		// Inbound traffic
		return (false);

	if (usb->len_cap != 18)		// Data length
		return (false);

	usb_device_descriptor *dd = (usb_device_descriptor*) data;

	if (dd->bDescriptorType != 1)
		return (false);

	if (dd->bLength != 18)
		return (false);

	if (dd->bcdUSB != 0x200)	// USB 2.0
		return (false);

	if (dd->bDeviceClass || dd->bDeviceSubclass || dd->bDeviceProtocol)
		return false;

	if (dd->bMaxPacketSize0 != 8)
		return false;

	if (dd->idVendor != 0x1bb4)
		return false;

	if (dd->idProduct != 0x0010)
		return false;

	if (dd->bcdDevice)
		return false;

	if (dd->iManufacturer != 1)
		return false;

	if (dd->iProduct != 2)
		return false;

	if (dd->iSerialNumber)
		return false;

	if (dd->bNumConfigurations != 1)
		return false;

	return true;
}

/**
 * valid_req_sense
 */
static bool valid_req_sense (usbmon_packet *u, u8 *buffer)
{
	if (!u || !buffer)
		RETURN (false);

	return false;
	return true;
}

#endif
/**
 * valid_usbmon
 */
static bool valid_usbmon (usbmon_packet *u)
{
	if (!u)
		RETURN (false);

	if ((u->id & 0xffffff0000000000) != 0xffff880000000000)
		RETURN (false);

	// type: submit, complete
	if ((u->type != 'S') && (u->type != 'C'))
		RETURN (false);

	// transfer type: iso, intr, control, bulk
	if (u->xfer_type > 3)
		RETURN (false);

	// endpoint number
	if ((u->epnum & 0x7f) > 2)
		RETURN (false);

	// device number -- incremented each time the device is un-/re-plugged
	if (u->devnum > 0x80)
		RETURN (false);

	// bus number, seems to be stable
	if (u->busnum != 3)
		RETURN (false);

	// setup section: present, not present
	if ((u->flag_setup != 0) && (u->flag_setup != '-'))
		RETURN (false);

	// data: present, not present, not present
	if ((u->flag_data != 0) && (u->flag_data != '<') && (u->flag_data != '>'))
		RETURN (false);

	// one year window
	if ((u->ts_sec < 1338380174) || (u->ts_sec > 1401452171))
		RETURN (false);

	// microseconds
	if (u->ts_usec > 1000000)
		RETURN (false);

	// XXX probably ought to quit on ENOENT
	// success, -2, -32, -115
	if ((u->status != 0) && (u->status != -ENOENT) && (u->status != -EPIPE) && (u->status != -EINPROGRESS)) {
		printf ("XXX status = %d\n", u->status);
		RETURN (false);
	}

	if (u->length > 64) {		// XXX validate this against the device descriptor
		printf ("XXX length = %d\n", u->length);
		//RETURN (false);
	}

	if (u->len_cap > 64)
		RETURN (false);

	// Transfer Type: Control (2)
	// Type: Submit ('S')
	// Setup: Relevant (0)
	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		usbmon_setup *setup = (usbmon_setup *) u->setup;

		// bitfield
		if (((setup->bmRequestType >> 5) & 0x03) == 3)
			RETURN (false);
		if ((setup->bmRequestType & 0x1f) > 3)
			RETURN (false);

		// request: GET_STATUS(0), CLEAR_FEATURE(1), SET_FEATURE(3), GET_DESCRIPTOR(6), SET_CONFIGURATION(9)
		if ((setup->bRequest != 0) && (setup->bRequest != 1) && (setup->bRequest != 3) && (setup->bRequest != 6) && (setup->bRequest != 9)) {
			printf ("XXX request = %d\n", setup->bRequest);
			//RETURN (false);
		}

	}
	return true;
}


#if 0
/**
 * dump_cbw
 */
static void dump_cbw (u8 *buffer)
{
	command_block_wrapper *cbw = NULL;
	char *op;
	char *direction;

	if (!buffer)
		return;

	cbw = (command_block_wrapper *) buffer;

	switch (cbw->CBWCB[0]) {
		case 0x00: op = "TEST UNIT READY";              break;
		case 0x03: op = "REQUEST SENSE";                break;
		case 0x12: op = "INQUIRY";                      break;
		case 0x1A: op = "MODE SENSE (6)";               break;
		case 0x1E: op = "PREVENT ALLOW MEDIUM REMOVAL"; break;
		case 0x23: op = "READ FORMAT CAPACITIES";       break;
		case 0x25: op = "READ CAPACITY(10)";            break;
		case 0x28: op = "READ(10)";                     break;
		case 0xDA: op = "VENDOR 0xDA";                  break;
		case 0xDB: op = "VENDOR 0xDB";                  break;
		default:   op = "UNKNOWN";
	}

	switch (cbw->bmCBWFlags) {
		case 0x00: direction = "host to device"; break;
		case 0x80: direction = "device to host"; break;
		default:   direction = "UNKNOWN";
	}

	printf ("Command Block Wrapper (CBW): 31 bytes\n");
	//printf ("	dCSWSignature: %.4s\n",                     buffer+0);
	printf ("	dCSWTag: 0x%04x\n",                *(u32 *)(buffer+4));
	printf ("	dCBWDataTransferLength: 0x%04x\n", *(u32 *)(buffer+8));
	printf ("	bmCBWFlags: 0x%02x %s\n",                   buffer[12], direction);
	printf ("	bCBWLUN: %d\n",                             buffer[13]);
	printf ("	bCBWCBLength: %d\n",                        buffer[14]);
	printf ("	CBWCB:\n");

	if (cbw->CBWCB[0] >= 0xD0) {
		log_debug ("Vendor: %02x\n", cbw->CBWCB[0]);
		//log_info ("Want %d bytes (0x%04x)\n", want, want);
	}
	printf ("		Operation code: 0x%02x %s\n", buffer[15], op);
	if (cbw->CBWCB[0] < 0xC0) {
		printf ("		LUN: %d\n",               cbw->CBWCB[1]>>5);
		printf ("		Reserved 1: %d\n",        cbw->CBWCB[1] & 0x1F);
		printf ("		Reserved 2: %d\n",        cbw->CBWCB[2]);
		printf ("		Reserved 3: %d\n",        cbw->CBWCB[3]);
		printf ("		Allocation length: %d\n", cbw->CBWCB[4]);
		printf ("		Control: %d\n",           cbw->CBWCB[5]);
	} else {
		dump_hex (cbw->CBWCB, 0, sizeof (cbw->CBWCB));
	}
}

/**
 * dump_csw
 */
static void dump_csw (u8 *buffer)
{
	command_status_wrapper *csw = NULL;

	if (!buffer)
		return;

	csw = (command_status_wrapper *) buffer;

	printf ("	Command Status Wrapper (CSW), 13 bytes\n");
	//printf ("		dCSWSignature: %.4s\n",     csw->dCSWSignature);
	printf ("		dCSWTag: 0x%04x\n",         csw->dCSWTag);
	printf ("		dCSWDataResidue: 0x%04x\n", csw->dCSWDataResidue);
	printf ("		dCSWStatus: %d\n",          csw->bCSWStatus);
	// status
	//	0	ok
	//	1	failed -> send "GetSense" immediately
	//	2	phase error
}

/**
 * dump_dd
 */
static bool dump_dd (usbmon_packet *usb, u8 *data)
{
	if (!usb || !data)
		return (false);

	usb_device_descriptor *dd = (usb_device_descriptor*) data;

	printf ("Device Descriptor\n");
	printf ("	bLength            : %d\n",     dd->bLength);
	printf ("	bDescriptorType    : %d\n",     dd->bDescriptorType);
	printf ("	bcdUSB             : 0x%04x\n", dd->bcdUSB);
	printf ("	bDeviceClass       : %d\n",     dd->bDeviceClass);
	printf ("	bDeviceSubClass    : %d\n",     dd->bDeviceSubclass);
	printf ("	bDeviceProtocol    : %d\n",     dd->bDeviceProtocol);
	printf ("	bMaxPacketSize0    : %d\n",     dd->bMaxPacketSize0);
	printf ("	idVendor           : 0x%04x\n", dd->idVendor);
	printf ("	idProduct          : 0x%04x\n", dd->idProduct);
	printf ("	bcdDevice          : %d\n",     dd->bcdDevice);
	printf ("	iManufacturer      : %d\n",     dd->iManufacturer);
	printf ("	iProduct           : %d\n",     dd->iProduct);
	printf ("	iSerialNumber      : %d\n",     dd->iSerialNumber);
	printf ("	bNumConfigurations : %d\n",     dd->bNumConfigurations);

	return true;
}

/**
 * dump_req_sense
 */
static void dump_req_sense (u8 *buffer)
{
	if (!buffer)
		return;

	printf ("Request Sense Response\n");
	printf ("	Valid: %d\n", buffer[0] >> 7);
	printf ("	Response Code: %d\n", buffer[0] & 0x7f);
	printf ("	Obsolete: %d\n", buffer[1]);
	printf ("	Filemark: %d\n", (buffer[2] & 0x80) >> 7);
	printf ("	EOM: %d\n", (buffer[2] & 0x40) >> 6);
	printf ("	ILI: %d\n", (buffer[2] & 0x20) >> 5);
	printf ("	Reserved: %d\n", (buffer[2] & 0x10) >> 4);
	printf ("	Sense Key: %d\n", buffer[2] & 0x0F);
	// Information dependent on (buffer[0] >> 7)
	printf ("	Information: %02x %02x %02x %02x\n", buffer[3], buffer[4], buffer[5], buffer[6]);
	printf ("	Additional sense length: %d\n", buffer[7]);
	printf ("	Command-specific information: %02x %02x %02x %02x\n", buffer[8], buffer[9], buffer[10], buffer[11]);
	printf ("	Additional sense code: 0x%02x\n", buffer[12]);
	printf ("	Additional sense code qualifier: %d\n", buffer[13]);
	printf ("	Field replaceable unit code qualifier: %d\n", buffer[14]);
	printf ("	SKSV: %d\n", buffer[15] >> 7);
	printf ("	Sense key specfic 1: %d\n", buffer[15] & 0x7F);
	printf ("	Sense key specfic 2: %d\n", buffer[16]);
	printf ("	Sense key specfic 3: %d\n", buffer[17]);
}

/**
 * dump_usbmon_one
 */
static void dump_usbmon_one (usbmon_packet *u, char *output)
{
	int index = 0;
	char *xfer;
	char *setup;
	char *present;
	char *status;

	if (!u)
		return;

	memset (output, 0, sizeof (output));

	switch (u->xfer_type) {
		case 0:  xfer = "ISO "; break;
		case 1:  xfer = "Intr"; break;
		case 2:  xfer = "Ctrl"; break;
		case 3:  xfer = "Bulk"; break;
		default: xfer = "XXXX"; break;
	}

	switch (u->flag_setup) {
		case 0:   setup = "relevant (0)";       break;
		case '-': setup = "not relevant ('-')"; break;
		default:  setup = "Unknown";            break;
	}

	switch (u->flag_data) {
		case 0:   present = "present (0)";       break;
		case '<': present = "not present ('<')"; break;
		case '>': present = "not present ('>')"; break;
		default:  present = "Unknown";           break;
	}

	switch (u->status) {
		case    0: status = "Success";     break;
		case  -32: status = "Broken pipe"; break;
		case -115: status = "In progress"; break;
		default:   status = "Unknown";     break;
	}

	index += sprintf (output+index, "%08x ", (u32) u->id);
	index += sprintf (output+index, "%d:%-2d ", u->busnum, u->devnum);
	index += sprintf (output+index, "%s ", xfer);
	index += sprintf (output+index, "%c ", u->type);
	index += sprintf (output+index, "%-3s ", (u->epnum & 0x80) ? "IN" : "OUT");
	index += sprintf (output+index, "%d ", (u->epnum & 0x7f));
	index += sprintf (output+index, "%-11s ", status);
	index += sprintf (output+index, "U%-2d D%-2d ", u->length, u->len_cap);

	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		u16 *lang = (u16 *)(u->setup+4);
		u16 *len  = (u16 *)(u->setup+6);
		char *rt_direction;
		char *rt_type;
		char *rt_recipient;

		switch (u->setup[0] >> 7) {
			case 0:    rt_direction = "Host to Device"; break;
			case 1:    rt_direction = "Device to Host"; break;
			default:   rt_direction = "Unknown";        break;
		}

		switch ((u->setup[0] >> 5) & 0x03) {
			case 0:    rt_type = "Standard"; break;
			case 1:    rt_type = "Class";    break;
			case 2:    rt_type = "Vendor";   break;
			default:   rt_type = "Reserved"; break;
		}

		switch (u->setup[0] & 0x1f) {
			case 0:    rt_recipient = "Device";    break;
			case 1:    rt_recipient = "Interface"; break;
			case 2:    rt_recipient = "Endpoint";  break;
			case 3:    rt_recipient = "Other";     break;
			default:   rt_recipient = "Reserved";  break;
		}

		printf ("	bmRequestType: 0x%02x\n", u->setup[0]);
		printf ("		Transfer direction: %s\n", rt_direction);
		printf ("		Type: %s\n", rt_type);
		printf ("		Recipient: %s\n", rt_recipient);
		printf ("	bRequest: %d\n",          u->setup[1]);
		printf ("	Descriptor index: %d\n",  u->setup[2]);
		printf ("	bDescriptor type: %d\n",  u->setup[3]);
		printf ("	Language Id: 0x%04x\n",  *lang);
		printf ("	wLength: %d\n",          *len);
	}
}

/**
 * dump_usbmon
 */
static void dump_usbmon (usbmon_packet *u)
{
	char time_buf[64];
	struct tm *tm = NULL;
	char *type;
	char *xfer;
	char *setup;
	char *present;
	char *status;

	if (!u)
		return;

	switch (u->type) {
		case 'S': type = "URB_SUBMIT ('S')";   break;
		case 'C': type = "URB_COMPLETE ('C')"; break;
		default:  type = "Unknown";            break;
	}

	switch (u->xfer_type) {
		case 0:  xfer = "ISO (0)";     break;
		case 1:  xfer = "Intr (1)";    break;
		case 2:  xfer = "Control (2)"; break;
		case 3:  xfer = "Bulk (3)";    break;
		default: xfer = "Unknown";     break;
	}

	switch (u->flag_setup) {
		case 0:   setup = "relevant (0)";       break;
		case '-': setup = "not relevant ('-')"; break;
		default:  setup = "Unknown";            break;
	}

	switch (u->flag_data) {
		case 0:   present = "present (0)";       break;
		case '<': present = "not present ('<')"; break;
		case '>': present = "not present ('>')"; break;
		default:  present = "Unknown";           break;
	}

	switch (u->status) {
		case    0: status = "Success";                                  break;
		case  -32: status = "Broken pipe (-EPIPE)";                     break;
		case -115: status = "Operation now in progress (-EINPROGRESS)"; break;
		default:   status = "Unknown";                                  break;
	}

	//printf ("\e[32mUSB Block\e[0m\n");

	//dump_hex (data, 0, 40);
	//printf ("\n");

	printf ("URB ID: 0x%llx\n", u->id);
	printf ("URB Type: %s\n", type);
	printf ("URB transfer type: %s\n", xfer);
	printf ("Endpoint: 0x%02x\n", u->epnum);
	printf ("	Direction: %s\n", (u->epnum & 0x80) ? "IN" : "OUT");
	printf ("	Endpoint: %d\n", (u->epnum & 0x7f));
	//printf ("Device: %d\n", u->devnum);
	//printf ("URB bus id: %d\n", u->busnum);
	printf ("Device setup request: %s\n", setup);
	printf ("Data: %s\n", present);
#if 0
	printf ("URB sec: %lld\n", u->ts_sec);
	printf ("URB usec: %d\n", u->ts_usec);
#else
	tm = localtime ((time_t*)&u->ts_sec);
	strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S", tm);
	//printf ("URB Time: %s.%06d\n", time_buf, u->ts_usec);
#endif
	printf ("URB status: %s (%d)\n", status, u->status);
	printf ("URB length: %d\n", u->length);
	printf ("Data length: %d\n", u->len_cap);
	//printf ("	xfer_flags: %d\n", u->xfer_flags);

	// Transfer Type: Control (2)
	// Type: Submit ('S')
	// Setup: Relevant (0)
	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		u16 *lang = (u16 *)(u->setup+4);
		u16 *len  = (u16 *)(u->setup+6);
		char *rt_direction;
		char *rt_type;
		char *rt_recipient;

		switch (u->setup[0] >> 7) {
			case 0:    rt_direction = "Host to Device"; break;
			case 1:    rt_direction = "Device to Host"; break;
			default:   rt_direction = "Unknown";        break;
		}

		switch ((u->setup[0] >> 5) & 0x03) {
			case 0:    rt_type = "Standard"; break;
			case 1:    rt_type = "Class";    break;
			case 2:    rt_type = "Vendor";   break;
			default:   rt_type = "Reserved"; break;
		}

		switch (u->setup[0] & 0x1f) {
			case 0:    rt_recipient = "Device";    break;
			case 1:    rt_recipient = "Interface"; break;
			case 2:    rt_recipient = "Endpoint";  break;
			case 3:    rt_recipient = "Other";     break;
			default:   rt_recipient = "Reserved";  break;
		}

		printf ("URB setup:\n");
		printf ("	bmRequestType: 0x%02x\n", u->setup[0]);
		printf ("		Transfer direction: %s\n", rt_direction);
		printf ("		Type: %s\n", rt_type);
		printf ("		Recipient: %s\n", rt_recipient);
		printf ("	bRequest: %d\n",          u->setup[1]);
		printf ("	Descriptor index: %d\n",  u->setup[2]);
		printf ("	bDescriptor type: %d\n",  u->setup[3]);
		printf ("	Language Id: 0x%04x\n",  *lang);
		printf ("	wLength: %d\n",          *len);
	}
}

#endif

/**
 * listen
 */
static void listen (FILE *f)
{
	u8 buffer[128];
	usbmon_packet usb;
	int count;
	u8 *data_sent = NULL;
	u8 *data_recv = NULL;
	int command = -1;
	int record = 0;
	int total_bytes = 0;
	//char output_usb[128];
	struct current_state current = { send, 0, 0, 0, 0, 0, 0 };

	while (!feof (f)) {
		memset (buffer, 0xdd, sizeof (buffer));

		count = fread (&usb, 1, 48, f);
		if (count < 48) {
			if (count == 0 && feof (f))
				break;
			exit (1);
		}

		record++;
		total_bytes += count;

		if (!valid_usbmon (&usb)) {
			printf ("XXX invalid usbmon packet\n");
			break;
		}

		if (usb.len_cap) {
			count = fread (buffer, 1, usb.len_cap, f);
			total_bytes += count;
		} else {
			count = 0;
		}

		//dump_usbmon (&usb);
		//dump_usbmon_one (&usb, output_usb);
		//printf ("%s\n", output_usb);

		switch (current.waiting_for) {
			case send:
				if (usb.type != 'S') {
					CONTINUE;
				}

				command = valid_cbw (&usb, buffer);
				if (command < 0) {
					printf ("XXX FSM(%d)\n", __LINE__);
					dump_hex (&usb, 0, 48);
					CONTINUE;
				}

				if (usb.length != usb.len_cap)
					CONTINUE;

				current.command  = command;
				current.urb_len  = usb.length;
				current.data_len = usb.len_cap;
				current.tag      = *(u32 *)(buffer+4);
				current.xfer_len = *(u32 *)(buffer+8);
				current.done = 0;

#if 0
				if (current.command == 0xdb) {
					dump_usbmon (&usb);
					if (usb.len_cap > 0) {
						//dump_csw (&usb, buffer);
						dump_cbw (buffer);
					}
				}
#endif

				if (current.xfer_len) {
					data_recv = calloc (1, current.xfer_len);
					if (!data_recv)
						CONTINUE;
					memset (data_recv, 0xdd, current.xfer_len);	//XXX temporary
				}

				//printf ("SCSI 0x%02x %s SEND", current.command, scsi_get_command(current.command));
				printf ("0x%05x %4d 0x%02x %s S", total_bytes-48-usb.len_cap, record, current.command, scsi_get_command(current.command));

				if (command == 0xdb) {
					int size = usb.len_cap-15;
					data_sent = malloc (size);
					if (!data_sent)
						CONTINUE;
					memcpy (data_sent, buffer+usb.len_cap-size, size);
#if 0
					printf ("\n");
					dump_hex (data_sent, 0, size);
					printf ("\n");
#endif
				}

				current.waiting_for = send_ack;
				continue;
			case send_ack:
				if (usb.type != 'C')
					CONTINUE;
				if (current.urb_len != usb.length)
					CONTINUE;
				if (usb.len_cap != 0)
					CONTINUE;
				current.urb_len = -1;
				current.data_len = -1;
				//printf (" ACK");
				printf ("✓");
				if (current.xfer_len) {
					current.waiting_for = recv;
				} else {
					current.waiting_for = rcpt;
				}
				continue;
			case recv:
				if (usb.type != 'S')
					CONTINUE;
				if (usb.length == 0)
					CONTINUE;
				/*
				if (usb.len_cap != 0)
					CONTINUE;
				*/
#if 0
				if (current.command == 0xdb) {
					dump_usbmon (&usb);
				}
#endif
#if 0
				if (usb.len_cap != 0) {
					printf ("recv\n");
					dump_hex (buffer, 0, usb.len_cap);
					printf ("\n");
				}
#endif
				current.urb_len  = usb.length;
				current.data_len = usb.len_cap;
				//printf (" RECV");
				printf ("R");
				current.waiting_for = recv_ack;
				continue;
			case recv_ack:
				if (usb.type != 'C')
					CONTINUE;
				/*
				if (usb.length != usb.len_cap)
					CONTINUE;
				*/
				if (current.urb_len != usb.length)
					CONTINUE;

				//printf (" ACK ");
				printf ("✓");
#if 0
				if (current.command == 0xdb) {
					printf ("\ndone = %d, size = %d, xfer = %d, len_cap = %d\n", current.done, usb.len_cap, current.xfer_len, usb.len_cap);
					dump_hex (buffer, current.done, usb.len_cap);
					printf ("\n");
				}
#endif
				memcpy (data_recv+current.done, buffer, usb.len_cap);
				current.done += usb.len_cap;
				if (current.done >= current.xfer_len) {
					// We've got all the data, now
					current.waiting_for = rcpt;
				} else {
					// We want more data
					current.waiting_for = recv;
				}
				continue;
			case rcpt:
				if (usb.type != 'S')
					CONTINUE;
				//printf (" RCPT");
				printf ("X");
				current.urb_len  = usb.length;
				current.data_len = usb.len_cap;
				current.waiting_for = rcpt_ack;
				continue;
			case rcpt_ack:
				if (usb.type != 'C')
					CONTINUE;
				if (current.urb_len != usb.length)
					CONTINUE;
				//printf (" ACK");
				printf ("✓");
				printf (" (%s)", (buffer[12] == 0) ? "SUCCESS" : "FAILURE");

				if (current.command > 0xd0) {
					printf ("\n");
					dump_hex (data_sent, 0, 16);
					printf ("\n");
					//printf ("%d 0x%x\n", current.xfer_len-13, current.xfer_len-13);
					dump_hex (data_recv+13, 0, current.xfer_len-13);
				} else {
					if (dump_scsi (current.command, data_recv, current.xfer_len)) {
						//data is processed
						printf ("\n");
					}
				}

				current.command  = 0;
				current.urb_len  = 0;
				current.data_len = 0;
				current.done     = 0;
				current.tag      = 0;
				current.waiting_for = send;

				free (data_sent);
				free (data_recv);
				data_recv = NULL;
				//printf ("0x%05x\n", total_bytes);
				continue;
		}

		continue;
#if 0
		if (valid_dd (&usb, buffer)) {
			dump_dd (&usb, buffer);
			continue;
		}

		if (valid_csw (&usb, buffer)) {
			dump_csw (buffer);
			continue;
		}

		if (valid_req_sense (&usb, buffer)) {
			dump_req_sense (buffer);
			continue;
		}

		dump_hex (buffer, 0, usb.len_cap);
		if (want > 0) {
			memcpy (data_recv + done, buffer, usb.len_cap);
			want -= usb.len_cap;
			done += usb.len_cap;
			//log_info ("done = %d, want = %d\n", done, want);

			if (want <= 0) {
				long size = 0;
				char *type = NULL;
				int disk = 0;

				if (done == 0x238) {	// VENDOR 0xDA
					switch (data_recv[0x230]) {
						case 0x10: type = "Dir";     break;
						case 0x20: type = "File";    break;
						default:   type = "Unknown"; break;
					}

					disk = data_recv[0] & 0x0F;
					log_info ("Disk: %d\n", disk);

					log_info ("%s: ", type);
					dump_string (data_recv + 4);

					size = (data_recv[0x210]) + (data_recv[0x211]<<8) + (data_recv[0x212]<<16) + (data_recv[0x213]<<24);
					printf ("Size: %ld\n", size);
				} else if (done == 0x20C) {	// VENDOR 0xDB
					disk = data_recv[0] & 0x0F;
					log_info ("Disk: %d\n", disk);

					log_info ("Listing: ");
					dump_string (data_recv + 4);
				} else if (done == 0x2800) {	// VENDOR 0xDA status
					log_info ("Status:\n");
					dump_string (data_recv + 4);
				} else {
					log_info ("Unknown: ");
					dump_string (data_recv + 4);
				}
				//log_hex (data_recv + 0x210, 0, done - 0x210);
				log_info ("%02x %02x %02x %02x\n", data_recv[0], data_recv[1], data_recv[2], data_recv[3]);
				log_hex (data_recv, 0, done);
			}
		} else {
			//dump_hex (buffer, 0, usb.len_cap);
		}
#endif
	}

}


/**
 * main
 */
int main (int argc, char *argv[])
{
	FILE *f = NULL;

	log_init ("/dev/pts/3");

	if (argc == 2) {
		f = fopen (argv[1], "r");
	} else {
		f = fopen ("/dev/usbmon3", "r");
	}

	if (f == NULL) {
		printf ("fopen\n");
		exit (1);
	}

	listen (f);
	fclose (f);
	return 0;
}

