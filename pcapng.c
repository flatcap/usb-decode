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

#define SETUP_LEN          8

u8 buffer[512];

/**
 * struct ntarHeader
 */
struct ntarHeader
{
	u32 type;
	u32 length;
};

/**
 * struct usbmon_packet
 */
struct usbmon_packet
{
	u64 id;			/*  0: URB ID - from submission to callback */
	u8 type;		/*  8: Same as text; extensible. */
	u8 xfer_type;		/*    ISO (0), Intr, Control, Bulk (3) */
	u8 epnum;		/*     Endpoint number and transfer direction */
	u8 devnum;		/*     Device address */
	u16 busnum;		/* 12: Bus number */
	char flag_setup;	/* 14: Same as text */
	char flag_data;		/* 15: Same as text; Binary zero is OK. */
	s64 ts_sec;		/* 16: gettimeofday */
	s32 ts_usec;		/* 24: gettimeofday */
	int status;		/* 28: */
	u32 length;		/* 32: Length of data (submitted or actual) */
	u32 len_cap;		/* 36: Delivered length */
	union {			/* 40: */
		u8 setup[SETUP_LEN];	/* Only for Control S-type */
		struct iso_rec {		/* Only for ISO */
			int error_count;
			int numdesc;
		} iso;
	} s;
	int interval;		/* 48: Only for Interrupt and ISO */
	int start_frame;	/* 52: For ISO */
	u32 xfer_flags;		/* 56: copy of URB's transfer_flags */
	u32 ndesc;		/* 60: Actual number of ISO descriptors */
};				/* 64 total length */


/**
 * dump_hex - Display a block of memory in hex and ascii
 */
void dump_hex (void *buf, int start, int length)
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
void dump_usb (u8 *data)
{
	struct usbmon_packet *u = (struct usbmon_packet *) data;

	if (0 && !u->len_cap)
		return;

	if (0 && u->type == 'S')
		return;

	if (1) {
		printf ("\e[32mUSB Block\e[0m\n");

		printf ("\tid: 0x%llx\n", u->id);
		printf ("\ttype: %c\n", u->type);
		printf ("\txfer_type: %d\n", u->xfer_type);
		printf ("\tepnum: %d\n", u->epnum);
		printf ("\tdev/bus: %d/%d\n", u->devnum, u->busnum);
		printf ("\tflag_setup: %d\n", u->flag_setup);
		printf ("\tflag_data: %d\n", u->flag_data);
		printf ("\tstatus: %d\n", u->status);
		printf ("\tlength: %d\n", u->length);
		printf ("\tlen_cap: %d\n", u->len_cap);
		//printf ("\txfer_flags: %d\n", u->xfer_flags);
	}

	if (u->len_cap) {
		dump_hex (data + 64, 0, u->len_cap);
	}
}


/**
 * dump_idb - Interface Description Block
 */
void dump_idb (u8 *block)
{
	return;
	u16 length = *(u16 *)(block + 18);

	length = 4 * ((length + 3) / 4); // Round up

	// Link type 220 = LINKTYPE_USB_LINUX_MMAPPED
	printf ("\e[32mInterface Description Block\e[0m\n");
	printf ("\tLink type: %u\n", *(u16*)(block + 8));
	printf ("\tReserved: %u\n", *(u16*)(block + 10));
	printf ("\tSnap len: %u\n", *(u32*)(block + 12));

	printf ("\tOptions:\n");
	printf ("\t\tdev: %s\n", block + 20);
	printf ("\t\tresolution: 10^-%d sec\n", block[24+length]);
	printf ("\t\tOS: %s\n", block + 32 + length);

	printf ("\n");
}

/**
 * dump_pb - Packet Block
 */
void dump_pb (u8 *block)
{
	printf ("\e[32mPacket Block\e[0m\n");
}

/**
 * dump_spb - Simple Packet Block
 */
void dump_spb (u8 *block)
{
	printf ("\e[32mSimple Packet Block\e[0m\n");
}

/**
 * dump_nrb - Name Resolution Block
 */
void dump_nrb (u8 *block)
{
	printf ("\e[32mName Resolution Block\e[0m\n");
}

/**
 * dump_isb - Interface Statistics Block
 */
void dump_isb (u8 *block)
{
	printf ("\e[32mInterface Statistics Block\e[0m\n");
}

/**
 * dump_epb - Enhanced Packet Block
 */
void dump_epb (u8 *block)
{
	//u32 length = *(u32 *)(block + 20);

	//printf ("\e[32mEnhanced Packet Block\e[0m\n");

	//printf ("\tinterface: %d\n", *(u32 *)(block + 8));
	//printf ("\ttime low/high: %u/%u\n", *(u32 *)(block + 12), *(u32 *)(block + 16));
	//printf ("\tcapture len: %u\n", *(u32 *)(block + 20));
	//printf ("\tpacket len: %u\n", *(u32 *)(block + 24));

	//printf ("\n");

	//dump_hex (block + 28, 0, length);

	dump_usb (block + 28);

	//printf ("\n");
}

/**
 * dump_shb - Section Header Block
 */
void dump_shb (u8 *block)
{
	return;
	u16 length = *(u16 *)(block + 26);

	length = 4 * ((length + 3) / 4); // Round up

	printf ("\e[32mSection Header Block\e[0m\n");
	printf ("\tMagic: 0x%08x\n", *(u32 *)(block + 8));
	printf ("\tMajor: %d\n", *(u16 *)(block + 12));
	printf ("\tMinor: %d\n", *(u16 *)(block + 14));
	printf ("\tSection length: %d\n", *(u32 *)(block + 16));

	printf ("\tOptions:\n");
	printf ("\t\tOS: %s\n", block + 28);
	printf ("\t\tApp: %s\n", block + 32 + length);

	printf ("\n");
}


/**
 * main
 */
int main (int argc, char *argv[])
{
	const int HEADER_SIZE = sizeof (struct ntarHeader);
	FILE *f = NULL;
	int count;
	struct ntarHeader *header = (struct ntarHeader*) &buffer;

	//if (argc != 2) { exit (1); }
	f = fopen (argv[1], "r");
	//if (f == NULL) { exit (1); }

	while (!feof (f)) {
		count = fread (buffer, 1, HEADER_SIZE, f);
		if (count < HEADER_SIZE) {
			if (count == 0 && feof (f))
				break;
			exit (1);
		}

		count = fread (buffer + HEADER_SIZE, 1, (header->length - HEADER_SIZE), f);
		//printf ("read %d bytes\n", count);

		switch (header->type)
		{
			case 0x00000001: dump_idb (buffer); break;
			case 0x00000002: dump_pb  (buffer); break;
			case 0x00000003: dump_spb (buffer); break;
			case 0x00000004: dump_nrb (buffer); break;
			case 0x00000005: dump_isb (buffer); break;
			case 0x00000006: dump_epb (buffer); break;
			case 0x0a0d0d0a: dump_shb (buffer); break;
			default:
				printf ("\e[31mUNKNOWN\e[0m %d\n", header->type);
				exit (1);
		}
	}

	fclose (f);
	return 0;
}

