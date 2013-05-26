#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#define handle_error(msg) do { perror (msg); exit (EXIT_FAILURE); } while (0)

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#if 0
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
			printf ("\t%6.6x ", start);
		else
			printf ("\t%6.6x ", off);

		for (i = 0; i < 16; i++) {
			if (i == 8)
				printf (" -");
			if (((off+i) >= start) && ((off+i) < (start+length)))
				printf (" %02X", mem[off+i]);
			else
				printf ("   ");
		}
		printf ("  ");
		for (i = 0; i < 16; i++) {
			if (((off+i) < start) || ((off+i) >= (start+length)))
				printf (" ");
			else if (isprint (mem[off + i]))
				printf ("%c", mem[off + i]);
			else
				printf (".");
		}
		printf ("\n");
	}
}

#endif

/**
 * main
 */
int main (int argc, char *argv[])
{
	u8 *addr;
	int fd;
	struct stat sb;
	int length;
	int place;
	int next;
	int i;
	char filename[128];
	int fd_out;
	int count = 0;
	int bytes;

	if (argc != 2) {
		fprintf (stderr, "%s file\n", argv[0]);
		exit (EXIT_FAILURE);
	}

	fd = open (argv[1], O_RDONLY);
	if (fd == -1)
		handle_error ("open");

	if (fstat (fd, &sb) == -1)
		handle_error ("fstat");

	length = sb.st_size;

	addr = mmap (NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED)
		handle_error ("mmap");

	//dump_hex (addr, 0, length);

	place = 0;
	next = 0;
	length -= 128;
	count = 1;
	while (place < length) {
		for (i = place+8; i < (place+128); i++) {
			if ((addr[i] == 0x88) && (addr[i+1] == 0xff) && (addr[i+2] == 0xff) && ((addr[i+3] == 0x43) || (addr[i+3] == 0x53))) {
				next = i - 5;
				memset (filename, 0, sizeof (filename));
				//sprintf (filename, "out/%llx", *(u64 *)(addr + place));
				sprintf (filename, "out/%06d", count);
				count++;
				fd_out = open (filename, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
				if (fd_out < 0)
					handle_error ("open");
				bytes = write (fd_out, addr + place, next - place);
				if (bytes < 0)
					handle_error ("write");
				close (fd_out);
				printf ("%s\n", filename);
				//dump_hex (addr + place, 0, next - place);
				//printf ("\n");
				place = next;
				break;
			}
		}
		//fprintf (stderr, "place = %d, next = %d, length = %d\n", place, next, length);
	}

	exit (EXIT_SUCCESS);
}
