#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

#include "usb.h"

typedef unsigned char      bool;
const bool true  = 1;
const bool false = 0;

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef   signed char      s8;
typedef   signed short     s16;
typedef   signed int       s32;
typedef   signed long long s64;

const char *dir = "out";

/**
 * struct usbmon_packet
 */
struct usbmon_packet
{
	u64 id;			/*  0: URB ID - from submission to callback */
	u8 type;		/*  8: Same as text; extensible. */
	u8 xfer_type;		/*  9: ISO (0), Intr, Control, Bulk (3) */
	u8 epnum;		/* 10: Endpoint number and transfer direction */
	u8 devnum;		/* 11: Device address */
	u16 busnum;		/* 12: Bus number */
	char flag_setup;	/* 14: Same as text */
	char flag_data;		/* 15: Same as text; Binary zero is OK. */
	s64 ts_sec;		/* 16: gettimeofday */
	s32 ts_usec;		/* 24: gettimeofday */
	int status;		/* 28: */
	u32 length;		/* 32: Length of data (submitted or actual) */
	u32 len_cap;		/* 36: Delivered length */
	u8 setup[8];		/* 40: Only for Control S-type */
	int interval;		/* 48: Only for Interrupt and ISO */
	int start_frame;	/* 52: For ISO */
	u32 xfer_flags;		/* 56: copy of URB's transfer_flags */
	u32 ndesc;		/* 60: Actual number of ISO descriptors */
};				/* 64 total length */


/**
 * dump_hex
 */
static void dump_hex (void *buf, int start, int length)
{
	int off, i, s, e;
	u8 *mem = buf;

	s =  start                & ~15;	// round down
	e = (start + length + 15) & ~15;	// round up

	for (off = s; off < e; off += 16) {
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
 * dump_usb
 */
static void dump_usb (u8 *data)
{
	struct usbmon_packet *u = (struct usbmon_packet *) data;
	char *type;
	char *xfer;
	char *setup;
	char *present;
	char *status;

	if (0 && !u->len_cap)
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
		case 0:    status = "Success (0)";                                     break;
		case -115: status = "Operation now in progress (-EINPROGRESS) (-115)"; break;
		default:   status = "Unknown";                                         break;
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
	printf ("Device: %d\n", u->devnum);
	printf ("URB bus id: %d\n", u->busnum);
	printf ("Device setup request: %s\n", setup);
	printf ("Data: %s\n", present);
	printf ("URB sec: %lld\n", u->ts_sec);
	printf ("URB usec: %d\n", u->ts_usec);
	printf ("URB status: %s\n", status);
	printf ("URB length: %d\n", u->length);
	printf ("Data length: %d\n", u->len_cap);
	//printf ("	xfer_flags: %d\n", u->xfer_flags);

	// Transfer Type: Control (2)
	// Type: Submit ('S')
	// Setup: Relevant (0)
	if ((u->xfer_type == 2) && (u->type == 'S') && (u->flag_setup == 0)) {
		u16 *lang = (u16 *)(u->setup+4);
		u16 *len  = (u16 *)(u->setup+6);
		printf ("URB setup:\n");
		printf ("	bmRequestType: 0x%02x\n", u->setup[0]);
		printf ("	bRequest: %d\n",          u->setup[1]);
		printf ("	Descriptor index: %d\n",  u->setup[2]);
		printf ("	bDescriptor type: %d\n",  u->setup[3]);
		printf ("	Language Id: 0x%04x\n",  *lang);
		printf ("	wLength: %d\n",          *len);
	}
}


/**
 * display_usb_device_descriptor
 */
static bool display_usb_device_descriptor (struct usbmon_packet *usb, u8 *data)
{
	if (usb->epnum != 0x80)		// Inbound traffic
		return false;

	if (usb->len_cap != 18)		// Data length
		return false;

	if (data[0] != 18)		// Descriptor length
		return false;

	if (data[1] != 1)		// Descriptor type
		return false;

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


#if 0
/**
 * file_append
 */
static int file_append (char *name, u8 *data, int length)
{
	char filename[128];
	FILE *f = NULL;

	sprintf (filename, "%s/%s", dir, name);

	f = fopen (filename, "a+");
	if (!f) {
		perror ("fopen");
		exit (1);
	}

	return 0;
}

#endif

/**
 * main
 */
int main (int argc, char *argv[])
{
	u8 buffer[128];
	struct usbmon_packet usb;
	FILE *f = NULL;
	int count;
	int records = 0;

	//if (argc != 2) { exit (1); }
	f = fopen (argv[1], "r");
	//if (f == NULL) { exit (1); }

	while (!feof (f)) {
		memset (buffer, 'R', sizeof (buffer));
		count = fread (&usb, 1, 48, f);
		//printf ("header %d bytes\n", count);
		if (count < 48) {
			if (count == 0 && feof (f))
				break;
			exit (1);
		}
		dump_usb ((u8 *)&usb);
		//printf ("\n");

		//usb.length = 4 * ((usb.length + 3) / 4); // Round up
		//usb.len_cap = 4 * ((usb.len_cap + 3) / 4); // Round up
		//printf ("length = %d\n", usb.length);
		//printf ("len_cap = %d\n", usb.len_cap);

		if (usb.len_cap) {
			count = fread (buffer, 1, usb.len_cap, f);
			//printf ("read %d bytes\n", count);
			if ((usb.len_cap == 13) && (buffer[0] == 'U') && (buffer[1] == 'S') && (buffer[2] == 'B') && (buffer[3] == 'S')) {
				printf ("	Command Status Wrapper (CSW), 13 bytes\n");
				printf ("		dCSWSignature: %.4s\n",              buffer+0);
				printf ("		dCSWTag: 0x%04x\n",         *(u32 *)(buffer+4));
				printf ("		dCSWDataResidue: 0x%04x\n", *(u32 *)(buffer+8));
				printf ("		dCSWStatus: %d\n",                   buffer[12]);
				// status
				//	0	ok
				//	1	failed -> send "GetSense" immediately
				//	2	phase error
			} else if ((usb.len_cap == 31) && (buffer[0] == 'U') && (buffer[1] == 'S') && (buffer[2] == 'B') && (buffer[3] == 'C')) {
				printf ("Command Block Wrapper (CBW), 31 bytes\n");
				printf ("	dCSWSignature: %.4s\n",                     buffer+0);
				printf ("	dCSWTag: 0x%04x\n",                *(u32 *)(buffer+4));
				printf ("	dCBWDataTransferLength: 0x%04x\n", *(u32 *)(buffer+8));
				printf ("	bmCBWFlags: %d\n",                          buffer[12]);
				// flags
				//	0x00	direction 1
				//	0x80	direction 2
				// not sure which is inbound and which outbound
				printf ("	bCBWLUN: %d\n",                             buffer[13]);
				printf ("	bCBWCBLength: %d\n",                        buffer[14]);
				printf ("	CBWCB:\n");
				if (buffer[14] == 6) {
					printf ("		Operation code: %d\n", buffer[15]);
					printf ("		LUN: %d\n", buffer[16]>>5);
					printf ("		Reserved 1: %d\n", buffer[16] & 0x1F);
					printf ("		Reserved 2: %d\n", buffer[17]);
					printf ("		Reserved 3: %d\n", buffer[18]);
					printf ("		Allocation length: %d\n", buffer[19]);
					printf ("		Control: %d\n", buffer[20]);
				} else {
					dump_hex (buffer+15, 0, 16);
				}
			} else if (buffer[0] == 0x70) {
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
			} else if (display_usb_device_descriptor (&usb, buffer)) {
				// nothing
			} else {
				dump_hex (buffer, 0, usb.len_cap);
			}
			printf ("\n");
		}

		//printf ("\n");
		records++;
		//if (records == 2) break;
	}

	//count = fread (buffer, 1, 128, f);
	//printf ("\n");
	//dump_hex (buffer, 0, 128);
	//printf ("\n");

	fclose (f);
	//printf ("EOF\n");
	return 0;
}

