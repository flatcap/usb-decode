#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

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
			printf("\t%6.6x ", start);
		else
			printf("\t%6.6x ", off);

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

	printf ("\e[32mUSB Block\e[0m\n");

	dump_hex (data, 0, 40);
	printf ("\n");

	printf ("\tURB ID: 0x%llx\n", u->id);
	printf ("\tURB Type: %s\n", type);
	printf ("\tURB transfer type: %s\n", xfer);
	printf ("\tEndpoint: 0x%02x\n", u->epnum);
	printf ("\t\tDirection: %s\n", (u->epnum & 0x80) ? "IN" : "OUT");
	printf ("\t\tEndpoint: %d\n", (u->epnum & 0x7f));
	printf ("\tDevice: %d\n", u->devnum);
	printf ("\tURB bus id: %d\n", u->busnum);
	printf ("\tDevice setup request: %s\n", setup);
	printf ("\tData: %s\n", present);
	printf ("\tURB sec: %lld\n", u->ts_sec);
	printf ("\tURB usec: %d\n", u->ts_usec);
	printf ("\tURB status: %s\n", status);
	printf ("\tURB length: %d\n", u->length);
	printf ("\tData length: %d\n", u->len_cap);
	//printf ("\txfer_flags: %d\n", u->xfer_flags);

	if ((u->xfer_type == 2) && (u->flag_data == 0)) {
		u16 *lang = (u16 *)(u->setup+4);
		u16 *len  = (u16 *)(u->setup+6);
		printf ("\tURB setup:\n");
		printf ("\t\tbmRequestType: 0x%02x\n", u->setup[0]);
		printf ("\t\tbRequest: %d\n",          u->setup[1]);
		printf ("\t\tDescriptor index: %d\n",  u->setup[2]);
		printf ("\t\tbDescriptor type: %d\n",  u->setup[3]);
		printf ("\t\tLanguage Id: 0x%04x\n",  *lang);
		printf ("\t\twLength: %d\n",          *len);
	}
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
			dump_hex (buffer, 0, usb.len_cap);
			printf ("\n");
		}

		if ((usb.epnum == 0x80) && (buffer[0] == 18) && 0) {
			printf ("\tDEVICE DESCRIPTOR\n");
			printf ("\t\tbLength: %d\n",             buffer[0]);
			printf ("\t\tbDescriptorType: %d\n",     buffer[1]);
			printf ("\t\tbcdUSB: %d\n",    *(u16 *) (buffer+2));
			printf ("\t\tbDeviceClass: %d\n",        buffer[4]);
			printf ("\t\tbDeviceSubClass: %d\n",     buffer[5]);
			printf ("\t\tbDeviceProtocol: %d\n",     buffer[6]);
			printf ("\t\tbMaxPacketSize0: %d\n",     buffer[7]);
			printf ("\t\tidVendor: %d\n",  *(u16 *) (buffer+8));
			printf ("\t\tidProduct: %d\n", *(u16 *) (buffer+10));
			printf ("\t\tbcdDevice: %d\n", *(u16 *) (buffer+12));
			printf ("\t\tiManufacturer: %d\n",       buffer[14]);
			printf ("\t\tiProduct: %d\n",            buffer[15]);
			printf ("\t\tiSerialNumber: %d\n",       buffer[16]);
			printf ("\t\tbNumConfigurations: %d\n",  buffer[17]);
		}

		printf ("\n");
		records++;
		if (records == 2)
			break;
	}

	count = fread (buffer, 1, 128, f);
	printf ("\n");
	dump_hex (buffer, 0, 128);
	printf ("\n");

	fclose (f);
	printf ("EOF\n");
	return 0;
}

