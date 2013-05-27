#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <stdarg.h>

#define TAG "ioctl"

#define usb_typeint(type)	 (((type)&0x3) == PIPE_INTERRUPT)
#define usb_typeisoc(type)	 (((type)&0x3) == PIPE_ISOCHRONOUS)

/**
 * struct usbmon_packet
 */
struct usbmon_packet {
	uint64_t id;		/* URB ID - from submission to callback */
	unsigned char type;	/* Same as in text API; extensible. */
	unsigned char xfer_type; /* ISO, Intr, Control, Bulk */
	unsigned char epnum;	/* Endpoint number; 0x80 IN */
	unsigned char devnum;	/* Device address */
	unsigned short busnum;	/* Bus number */
	char flag_setup;
	char flag_data;
	int64_t ts_sec;		/* gettimeofday */
	int32_t ts_usec;	/* gettimeofday */
	int status;
	unsigned int length;	/* Length of data (submitted or actual) */
	unsigned int len_cap;	/* Delivered length */
	unsigned char setup[8];	/* Only for Control S-type */
};

/**
 * struct usbmon_packet_1
 */
struct usbmon_packet_1 {
	uint64_t id;		/* URB ID - from submission to callback */
	unsigned char type;	/* Same as in text API; extensible. */
	unsigned char xfer_type; /* ISO, Intr, Control, Bulk */
	unsigned char epnum;	/* Endpoint number; 0x80 IN */
	unsigned char devnum;	/* Device address */
	unsigned short busnum;	/* Bus number */
	char flag_setup;
	char flag_data;
	int64_t ts_sec;		/* gettimeofday */
	int32_t ts_usec;	/* gettimeofday */
	int status;
	unsigned int length;	/* Length of data (submitted or actual) */
	unsigned int len_cap;	/* Delivered length */
	union { 
		unsigned char setup[8];	/* Only for Control S-type */
		struct iso_rec {
			int error_count;
			int numdesc;	/* Number from the URB */
		} iso;
	} s;
	int interval;
	int start_frame;
	unsigned int xfer_flags;
	unsigned int ndesc;	/* Actual number of ISO descriptors */
};

/**
 * struct usbmon_isodesc
 */
struct usbmon_isodesc {
	int iso_stat;
	unsigned int iso_off;
	unsigned int iso_len;
	int iso_pad;
};

/*
 * Size this so that we see data even if many descriptors are used.
 * Notice that we reserve enough print buffer for all of them.
 */
#define ISODESC_MAX  8

#define PIPE_ISOCHRONOUS		0
#define PIPE_INTERRUPT			1
#define PIPE_CONTROL			2
#define PIPE_BULK			3

#define MON_IOC_MAGIC 0x92

#define MON_IOCG_STATS _IOR(MON_IOC_MAGIC, 3, struct usbmon_stats)

#define MON_IOCT_RING_SIZE _IO(MON_IOC_MAGIC, 4)

#define MON_IOCQ_RING_SIZE _IO(MON_IOC_MAGIC, 5)

/**
 * struct usbmon_get_arg
 */
struct usbmon_get_arg {
	struct usbmon_packet_1 *hdr;	/* Only 48 bytes, not 64. */
	void *data;
	size_t alloc;			/* Length of data (can be zero) */
};

#define MON_IOCX_GET   _IOW(MON_IOC_MAGIC, 6, struct usbmon_get_arg)

#define MON_IOCX_GETX  _IOW(MON_IOC_MAGIC, 10, struct usbmon_get_arg)

/**
 * struct usbmon_mfetch_arg
 */
struct usbmon_mfetch_arg {
	unsigned int *offvec;		/* Vector of events fetched */
	unsigned int nfetch;		/* Num. of events to fetch / fetched */
	unsigned int nflush;		/* Number of events to flush */
};

#define MON_IOCX_MFETCH _IOWR(MON_IOC_MAGIC, 7, struct usbmon_mfetch_arg)

/**
 * enum text_format
 */
enum text_format {
	TFMT_OLD,	/* The v0 text API aka "1t" */
	TFMT_1U,	/* The "1u" text format */
	TFMT_HUMAN	/* Human-oriented format, changes over time. */
};

/**
 * enum usbmon_api
 */
enum usbmon_api {
	API_ANY,
	API_B0,		/* Old binary (48 bytes usbmon_packet) */
	API_B1,		/* New binary (64 bytes usbmon_packet_1) */
	API_B1M		/* New binary (64 bytes usbmon_packet_1) + mmap (2) */
};

/**
 * struct params
 */
struct params {
	int ifnum;	/* USB bus number */
	char *devname;	/* /dev/usbmonN */
	int data_size;	/* How many bytes to fetch, including ISO descriptors */
	int data_max;	/* How many bytes to print as data (<= data_size) */
	enum text_format format;
	enum usbmon_api api;

	int map_size;

	char *print_buf;
	int print_size;
};

enum { DATA_MAX = 32 };		/* Old limit used with 1t format (print_48) */

/**
 * struct print_cursor
 */
struct print_cursor {
	char *pbuf;
	int size;
	int count;		/* without the terminating nul */
};


struct params par;

/**
 * usage
 */
static void usage (void)
{
	fprintf (stderr, "usage: " TAG " [-i usbN] [-f0|-fu|-fh] [-a0|-a1|-am] [-s len]\n");
	exit (2);
}

/**
 * print_start
 */
static void print_start (struct print_cursor *t, char *buf, int size0)
{
	t->pbuf = buf;
	t->size = size0;
	t->count = 0;
}

/**
 * print_safe
 */
static void print_safe (struct print_cursor *t, const char *fmt, ...)
{
	va_list ap;
	int len;

	if (t->count+1 >= t->size)
		return;

	va_start (ap, fmt);
	len = vsnprintf (t->pbuf + t->count, t->size - t->count, fmt, ap);
	t->count += len;
	va_end (ap);
}

/**
 * print_done
 */
static int print_done (struct print_cursor *t)
{
	return t->count;
}

/**
 * print_48
 */
static void print_48 (const struct params *prm, const struct usbmon_packet *ep, const unsigned char *data)
{
	struct print_cursor pcur;
	char udir, utype;
	int data_len, i;
	int cnt;
	ssize_t rc;

	print_start (&pcur, prm->print_buf, prm->print_size);

	udir = ((ep->epnum & 0x80) != 0) ? 'i' : 'o';
	switch (ep->xfer_type & 0x3) {
	case PIPE_ISOCHRONOUS:	  utype = 'Z'; break;
	case PIPE_INTERRUPT:	  utype = 'I'; break;
	case PIPE_CONTROL:	  utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	print_safe (&pcur, "%llx %u %c %c%c:%03u:%02u",
	    (long long) ep->id,
	    (unsigned int) (ep->ts_sec & 0xFFF) * 1000000 + ep->ts_usec,
	    ep->type,
	    utype, udir, ep->devnum, ep->epnum & 0x7f);

	if (ep->flag_setup == 0) {   /* Setup packet is present and captured */
		print_safe (&pcur, " s %02x %02x %04x %04x %04x",
		    ep->setup[0],
		    ep->setup[1],
		    (ep->setup[3] << 8) | ep->setup[2],
		    (ep->setup[5] << 8) | ep->setup[4],
		    (ep->setup[7] << 8) | ep->setup[6]);
	} else if (ep->flag_setup != '-') { /* Unable to capture setup packet */
		print_safe (&pcur, " %c __ __ ____ ____ ____", ep->flag_setup);
	} else {                     /* No setup for this kind of URB */
		print_safe (&pcur, " %d", ep->status);
	}
	print_safe (&pcur, " %d", ep->length);

	if (ep->length > 0) {
		if (ep->flag_data == 0) {
			print_safe (&pcur, " =");
			if ((data_len = ep->len_cap) >= DATA_MAX)
				data_len = DATA_MAX;
			for (i = 0; i < data_len; i++) {
				if (i % 4 == 0) {
					print_safe (&pcur, " ");
				}
				print_safe (&pcur, "%02x", data[i]);
			}
			print_safe (&pcur, "\n");
		} else {
			print_safe (&pcur, " %c\n", ep->flag_data);
		}
	} else {
		print_safe (&pcur, "\n");
	}

	cnt = print_done (&pcur);
	if ((rc = write (1, prm->print_buf, cnt)) < cnt) {
		if (rc < 0) {
			fprintf (stderr, TAG ": Write error: %s\n", strerror (errno));
		} else {
			fprintf (stderr, TAG ": Short write\n");
		}
		exit (1);
	}
}

/**
 * print_1u
 */
static void print_1u (const struct params *prm, const struct usbmon_packet_1 *ep, const unsigned char *data)
{
	struct print_cursor pcur;
	char udir, utype;
	int data_len, i;
	int ndesc;				/* Display this many */
	const struct usbmon_isodesc *dp;
	int cnt;
	ssize_t rc;

	print_start (&pcur, prm->print_buf, prm->print_size);

	if ((data_len = ep->len_cap) < 0) {	/* Overflow */
		data_len = 0;
	}

	udir = ((ep->epnum & 0x80) != 0) ? 'i' : 'o';
	switch (ep->xfer_type & 0x3) {
	case PIPE_ISOCHRONOUS:	utype = 'Z'; break;
	case PIPE_INTERRUPT:	utype = 'I'; break;
	case PIPE_CONTROL:	utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	print_safe (&pcur, "%llx %u %c %c%c:%u:%03u:%u",
	    (long long) ep->id,
	    (unsigned int) (ep->ts_sec & 0xFFF) * 1000000 + ep->ts_usec,
	    ep->type,
	    utype, udir, ep->busnum, ep->devnum, ep->epnum & 0x7f);

	if (ep->type == 'E') {
		print_safe (&pcur, " %d", ep->status);
	} else {
		if (ep->flag_setup == 0) {
			/* Setup packet is present and captured */
			print_safe (&pcur, " s %02x %02x %04x %04x %04x",
			    ep->s.setup[0],
			    ep->s.setup[1],
			    (ep->s.setup[3] << 8) | ep->s.setup[2],
			    (ep->s.setup[5] << 8) | ep->s.setup[4],
			    (ep->s.setup[7] << 8) | ep->s.setup[6]);
		} else if (ep->flag_setup != '-') {
			/* Unable to capture setup packet */
			print_safe (&pcur, " %c __ __ ____ ____ ____", ep->flag_setup);
		} else {
			/* No setup for this kind of URB */
			print_safe (&pcur, " %d", ep->status);
			if (usb_typeisoc (ep->xfer_type) ||
			    usb_typeint (ep->xfer_type)) {
				print_safe (&pcur, ":%d", ep->interval);
			}
			if (usb_typeisoc (ep->xfer_type)) {
				print_safe (&pcur, ":%d", ep->start_frame);
				if (ep->type == 'C') {
					print_safe (&pcur, ":%d", ep->s.iso.error_count);
				}
			}
		}
		if (usb_typeisoc (ep->xfer_type)) {
			/*
			 * This is the number of descriptors used by HC.
			 */
			print_safe (&pcur, " %d", ep->s.iso.numdesc);

			/*
			 * This is the number of descriptors which we print.
			 */
			ndesc = ep->ndesc;
			if (ndesc > ISODESC_MAX)
				ndesc = ISODESC_MAX;
			if (ndesc * (int) sizeof (struct usbmon_isodesc) > data_len) {
				ndesc = data_len / sizeof (struct usbmon_isodesc);
			}
			/* This is aligned by malloc */
			dp = (struct usbmon_isodesc *) data;
			for (i = 0; i < ndesc; i++) {
				print_safe (&pcur, " %d:%u:%u", dp->iso_stat, dp->iso_off, dp->iso_len);
				dp++;
			}

			/*
			 * The number of descriptors captured is used to
			 * find where the data starts.
			 */
			ndesc = ep->ndesc;
			if (ndesc * (int) sizeof (struct usbmon_isodesc) > data_len) {
				data_len = 0;
			} else {
				data += ndesc * sizeof (struct usbmon_isodesc);
				data_len -= ndesc * sizeof (struct usbmon_isodesc);
			}
		}
	}

	print_safe (&pcur, " %d", ep->length);

	if (ep->length > 0) {
		if (ep->flag_data == 0) {
			print_safe (&pcur, " =");
			if (data_len >= prm->data_max)
				data_len = prm->data_max;
			for (i = 0; i < data_len; i++) {
				if (i % 4 == 0) {
					print_safe (&pcur, " ");
				}
				print_safe (&pcur, "%02x", data[i]);
			}
			print_safe (&pcur, "\n");
		} else {
			print_safe (&pcur, " %c\n", ep->flag_data);
		}
	} else {
		print_safe (&pcur, "\n");
	}

	cnt = print_done (&pcur);
	if ((rc = write (1, prm->print_buf, cnt)) < cnt) {
		if (rc < 0) {
			fprintf (stderr, TAG ": Write error: %s\n", strerror (errno));
		} else {
			fprintf (stderr, TAG ": Short write\n");
		}
		exit (1);
	}
}

/**
 * print_human_data
 */
static void print_human_data (struct print_cursor *curs, const struct usbmon_packet_1 *ep, const unsigned char *data, int data_len)
{
	int any_printable;
	int i;

	print_safe (curs, "   ");
	for (i = 0; i < data_len; i++) {
		if (i % 4 == 0)
			print_safe (curs, " ");
		print_safe (curs, "%02x", data[i]);
	}
	print_safe (curs, "\n");

	any_printable = 0;
	for (i = 0; i < data_len; i++) {
		if (isprint (data[i])) {
			any_printable = 1;
			break;
		}
	}
	if (any_printable) {
		print_safe (curs, "   ");
		for (i = 0; i < data_len; i++) {
			if (i % 4 == 0)
				print_safe (curs, " ");
			print_safe (curs, " %c", isprint (data[i]) ? data[i] : '.');
		}
		print_safe (curs, "\n");
	}
}

/**
 * print_human
 */
static void print_human (const struct params *prm, const struct usbmon_packet_1 *ep, const unsigned char *data, uint64_t start_sec)
{
	struct print_cursor pcur;
	char udir, utype;
	int data_len, i;
	int ndesc;				/* Display this many */
	const struct usbmon_isodesc *dp;
	int cnt;
	ssize_t rc;

	print_start (&pcur, prm->print_buf, prm->print_size);

	if ((data_len = ep->len_cap) < 0) {	/* Overflow */
		data_len = 0;
	}

#if 0
	enum { TAG_BUF_SIZE = 17 };
	char tag_buf[TAG_BUF_SIZE];
	print_human_tag (tag_buf, TAG_BUF_SIZE, prm->tagp, ep);
#endif
	/*
	 * We cast into a truncated type for readability.
	 * The danger of collisions is negligible.
	 */
	print_safe (&pcur, "%08x", (unsigned int) ep->id);

	udir = ((ep->epnum & 0x80) != 0) ? 'i' : 'o';
	switch (ep->xfer_type & 0x3) {
	case PIPE_ISOCHRONOUS:	utype = 'Z'; break;
	case PIPE_INTERRUPT:	utype = 'I'; break;
	case PIPE_CONTROL:	utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	print_safe (&pcur, " %u.%06u %c %c%c:%u:%03u:%u",
	    (unsigned int) (ep->ts_sec - start_sec), ep->ts_usec,
	    ep->type,
	    utype, udir, ep->busnum, ep->devnum, ep->epnum & 0x7f);

	if (ep->type == 'E') {
		print_safe (&pcur, " %d", ep->status);
	} else {
		if (ep->flag_setup == 0) {
			/* Setup packet is present and captured */
			print_safe (&pcur, " s %02x %02x %04x %04x %04x",
			    ep->s.setup[0],
			    ep->s.setup[1],
			    (ep->s.setup[3] << 8) | ep->s.setup[2],
			    (ep->s.setup[5] << 8) | ep->s.setup[4],
			    (ep->s.setup[7] << 8) | ep->s.setup[6]);
		} else if (ep->flag_setup != '-') {
			/* Unable to capture setup packet */
			print_safe (&pcur, " %c __ __ ____ ____ ____", ep->flag_setup);
		} else {
			/* No setup for this kind of URB */
			if (ep->type == 'S' && ep->status == -EINPROGRESS) {
				print_safe (&pcur, " -");
			} else {
				print_safe (&pcur, " %d", ep->status);
			}
			if (usb_typeisoc (ep->xfer_type) ||
			    usb_typeint (ep->xfer_type)) {
				print_safe (&pcur, ":%d", ep->interval);
			}
			if (usb_typeisoc (ep->xfer_type)) {
				print_safe (&pcur, ":%d", ep->start_frame);
				if (ep->type == 'C') {
					print_safe (&pcur, ":%d", ep->s.iso.error_count);
				}
			}
		}
		if (usb_typeisoc (ep->xfer_type)) {
			/*
			 * This is the number of descriptors used by HC.
			 */
			print_safe (&pcur, " %d", ep->s.iso.numdesc);

			/*
			 * This is the number of descriptors which we print.
			 */
			ndesc = ep->ndesc;
			if (ndesc > ISODESC_MAX)
				ndesc = ISODESC_MAX;
			if (ndesc * (int) sizeof (struct usbmon_isodesc) > data_len) {
				ndesc = data_len / sizeof (struct usbmon_isodesc);
			}
			/* This is aligned by malloc */
			dp = (struct usbmon_isodesc *) data;
			for (i = 0; i < ndesc; i++) {
				print_safe (&pcur, " %d:%u:%u", dp->iso_stat, dp->iso_off, dp->iso_len);
				dp++;
			}

			/*
			 * The number of descriptors captured is used to
			 * find where the data starts.
			 */
			ndesc = ep->ndesc;
			if (ndesc * (int) sizeof (struct usbmon_isodesc) > data_len) {
				data_len = 0;
			} else {
				data += ndesc * sizeof (struct usbmon_isodesc);
				data_len -= ndesc * sizeof (struct usbmon_isodesc);
			}
		}
	}

	print_safe (&pcur, " %d", ep->length);

	if (ep->length > 0) {
		if (ep->flag_data == 0) {
			print_safe (&pcur, " =\n");
			if (data_len >= prm->data_max)
				data_len = prm->data_max;
			print_human_data (&pcur, ep, data, data_len);
		} else {
			print_safe (&pcur, " %c\n", ep->flag_data);
		}
	} else {
		print_safe (&pcur, "\n");
	}

	cnt = print_done (&pcur);
	if ((rc = write (1, prm->print_buf, cnt)) < cnt) {
		if (rc < 0) {
			fprintf (stderr, TAG ": Write error: %s\n", strerror (errno));
		} else {
			fprintf (stderr, TAG ": Short write\n");
		}
		exit (1);
	}
}

/**
 * print
 */
static void print (const struct params *prm, const struct usbmon_packet_1 *ep, const unsigned char *data)
{
	static uint64_t start_sec = 0;

	switch (par.format) {
	case TFMT_OLD:
		/*
		 * Old and new APIs are made compatible just so we
		 * can cast like this.
		 */
		print_48 (&par, (struct usbmon_packet *) ep, data);
		break;
	case TFMT_1U:
		print_1u (&par, ep, data);
		break;
	default: /* TFMT_HUMAN */
		if (start_sec == 0)
			start_sec = ep->ts_sec;
		print_human (&par, ep, data, start_sec);
	}
}

/**
 * parse_params
 */
static void parse_params (struct params *p, char **argv)
{
	char *arg;
	long num;

	memset (p, 0, sizeof (struct params));
	p->data_max = DATA_MAX;	/* Same as 1t text API. */
	p->format = TFMT_HUMAN;
	p->api = API_ANY;

	while ((arg = *argv++) != NULL) {
		if (arg[0] == '-') {
			if (arg[1] == 0)
				usage ();
			switch (arg[1]) {
			case 'i':
				if (arg[2] != 0)
					usage ();
				if ((arg = *argv++) == NULL)
					usage ();
				if (strncmp (arg, "usb", 3) == 0)
					arg += 3;
				if (!isdigit (arg[0]))
					usage ();
				errno = 0;
				num = strtol (arg, NULL, 10);
				if (errno != 0)
					usage ();
				if (num < 0 || num >= 128) {
					fprintf (stderr, TAG ": Bus number %ld is out of bounds\n", num);
					exit (2);
				}
				p->ifnum = num;
				break;
			case 'f':
				switch (arg[2]) {
				case '0':
					p->format = TFMT_OLD;
					break;
				case 'u':
					p->format = TFMT_1U;
					break;
				case 'h':
					p->format = TFMT_HUMAN;
					break;
				default:
					usage ();
				}
				break;
			case 'a':
				switch (arg[2]) {
				case '0':
					p->api = API_B0;
					break;
				case '1':
					p->api = API_B1;
					break;
				case 'm':
					p->api = API_B1M;
					break;
				default:
					usage ();
				}
				break;
			case 's':
				if (arg[2] != 0)
					usage ();
				if ((arg = *argv++) == NULL)
					usage ();
				if (!isdigit (arg[0]))
					usage ();
				errno = 0;
				num = strtol (arg, NULL, 10);
				if (errno != 0)
					usage ();
				if (num < 0) {
					fprintf (stderr, TAG ": negative size %ld\n", num);
					exit (1);
				}
				p->data_max = num;
				break;
			default:
				usage ();
			}
		} else {
			usage ();
		}
	}

	if (p->data_size == 0) {
		p->data_size = p->data_max + 96;
	}

	if (p->devname == NULL) {
		if ((p->devname = malloc (100)) == NULL) {
			fprintf (stderr, TAG ": No core\n");
			exit (1);
		}
		snprintf (p->devname, 100, "/dev/usbmon%d", p->ifnum);
	}

	if (p->format == TFMT_1U)
		p->api = API_B1;

	/*
	 * This is somewhat approximate, but seems like not overflowing.
	 * We cannot rely on print_safe, because when it triggers it violates
	 * the documented output format. It only exists to prevent crashes.
	 */
	if (p->format == TFMT_OLD) {
		if (p->data_max != DATA_MAX) {
			fprintf (stderr, TAG ": -f0 requires -s 32\n");
			exit (1);
		}
		p->print_size = 160;
	} else {
		p->print_size = 100;
		p->print_size += (((p->data_max+3)/4 * 9) + 5) * 2;
		p->print_size += 10 + ISODESC_MAX*26;	/* " %d:%u:%u" */
	}
	if ((p->print_buf = malloc (p->print_size)) == NULL) {
		fprintf (stderr, TAG ": No core\n");
		exit (1);
	}
}

/**
 * main
 */
int main (int argc, char **argv)
{
	int fd;
	struct usbmon_packet_1 hdrb;
	struct usbmon_packet_1 *hdr;
	struct usbmon_get_arg getb;
	enum { MFETCH_NM = 3 };
	unsigned int offs[MFETCH_NM];
	int off;
	struct usbmon_mfetch_arg mfb;
	unsigned char *data_buff;
	unsigned int toflush;
	unsigned int i;
	int rc;

	parse_params (&par, argv+1);

	/*
	 * Two reasons to do this:
	 * 1. Reduce weird error messages.
	 * 2. If we create device nodes, we want them owned by root.
	 */
	if (geteuid () != 0) {
		//fprintf (stderr, TAG ": Must run as root\n");
		//exit (1);
	}

	if ((fd = open (par.devname, O_RDWR)) == -1) {
		if (errno == ENOENT) {
			fprintf (stderr, TAG ": Can't open %s: %s\n", par.devname, strerror (errno));
			exit (1);
		}
		if (fd == -1) {
			if (errno == ENODEV && par.ifnum == 0) {
				fprintf (stderr, TAG ": Can't open pseudo-bus zero at %s (probably not supported by kernel)\n", par.devname);
			} else {
				fprintf (stderr, TAG ": Can't open %s: %s\n", par.devname, strerror (errno));
			}
			exit (1);
		}
	}


	if (par.api == API_B1M) {
		rc = ioctl (fd, MON_IOCQ_RING_SIZE, 0);
		if (rc == -1) {
			fprintf (stderr, TAG ": Cannot get ring size: %s\n", strerror (errno));
			exit (1);
		}
		printf ("Ring size: %d\n", rc); /* P3 */
		par.map_size = rc;
		data_buff = mmap (0, par.map_size, PROT_READ, MAP_SHARED, fd, 0);
		if (data_buff == MAP_FAILED) {
			fprintf (stderr, TAG ": Cannot mmap: %s\n", strerror (errno));
			exit (1);
		}
	} else {
		if ((data_buff = malloc (par.data_size)) == NULL) {
			fprintf (stderr, TAG ": No core\n");
			exit (1);
		}
	}

	if (par.format == TFMT_HUMAN && par.api == API_B0) {
		/*
		 * Zero fields which are not present in old (zero) API
		 */
		memset (&hdrb, 0, sizeof (struct usbmon_packet_1));
	} else {
		/*
		 * Make uninitialized fields visible.
		 */
		memset (&hdrb, 0xdb, sizeof (struct usbmon_packet_1));
	}

	toflush = 0;
	for (;;) {
		if (par.api == API_B0) {
			getb.hdr = &hdrb;
			getb.data = data_buff;
			getb.alloc = par.data_size;
			if ((rc = ioctl (fd, MON_IOCX_GET, &getb)) != 0) {
				fprintf (stderr, TAG ": MON_IOCX_GET: %s\n", strerror (errno));
				exit (1);
			}
			print (&par, &hdrb, data_buff);
		} else if (par.api == API_B1) {
			getb.hdr = &hdrb;
			getb.data = data_buff;
			getb.alloc = par.data_size;
			if ((rc = ioctl (fd, MON_IOCX_GETX, &getb)) != 0) {
				fprintf (stderr, TAG ": MON_IOCX_GETX: %s\n", strerror (errno));
				exit (1);
			}
			print (&par, &hdrb, data_buff);
		} else if (par.api == API_B1M) {
			mfb.offvec = offs;
			mfb.nfetch = MFETCH_NM;
			mfb.nflush = toflush;
			if ((rc = ioctl (fd, MON_IOCX_MFETCH, &mfb)) != 0) {
				fprintf (stderr, TAG ": MON_IOCX_MFETCH: %s\n", strerror (errno));
				exit (1);
			}
			for (i = 0; i < mfb.nfetch; i++) {
				off = offs[i];
				if (off >= par.map_size) {
					fprintf (stderr, TAG ": offset\n");
					continue;
				}
				hdr = (struct usbmon_packet_1 *) (data_buff + off);
				if (hdr->type == '@')
					continue;
				print (&par, hdr, (const unsigned char *) (hdr + 1));
			}
			toflush = mfb.nfetch;
		} else {
			getb.hdr = &hdrb;
			getb.data = data_buff;
			getb.alloc = par.data_size;
			if ((rc = ioctl (fd, MON_IOCX_GETX, &getb)) != 0) {
				if (errno == ENOTTY) {
					par.api = API_B0;
					rc = ioctl (fd, MON_IOCX_GET, &getb);
					if (rc != 0) {
						fprintf (stderr, TAG ": MON_IOCX_GET: %s\n", strerror (errno));
						exit (1);
					}
				} else {
					fprintf (stderr, TAG ": MON_IOCX_GETX: %s\n", strerror (errno));
					exit (1);
				}
			}
			print (&par, &hdrb, data_buff);
		}
	}

	return 0;
}

