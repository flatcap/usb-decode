CC	= gcc

PROGS	= bulk dump ioctl mmap usb usb_list usbmon xusb detach attach scsi status control

PACKAGES = libusb-1.0

CFLAGS += -Wall -Wshadow -Wmissing-declarations -Wmissing-prototypes
CFLAGS += -Wnested-externs -Wpointer-arith -Wpointer-arith -Wsign-compare
CFLAGS += -Wchar-subscripts -Wstrict-prototypes -Wformat=2 -Wtype-limits
#CFLAGS += -Wp,-D_FORTIFY_SOURCE=2
#CFLAGS += -O2
CFLAGS += -g

CFLAGS	+= $(shell pkg-config --cflags $(PACKAGES))
LDFLAGS += $(shell pkg-config --libs   $(PACKAGES))

all: tags $(PROGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(PROGS) tags dump_old

tags:	phony
	ctags dump.c usb.h usbtypes.h

phony:

