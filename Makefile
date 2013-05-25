##
## usbmon - simple front-end for in-kernel usbmon
##

#CFLAGS += -Wall -Wp,-D_FORTIFY_SOURCE=2 -O2
CFLAGS += -Wall -Wshadow -Wmissing-declarations -Wmissing-prototypes
CFLAGS += -Wnested-externs -Wpointer-arith -Wpointer-arith -Wsign-compare
CFLAGS += -Wchar-subscripts -Wstrict-prototypes -Wformat=2 -Wtype-limits
CFLAGS += -Wp,-D_FORTIFY_SOURCE=2
CFLAGS += -O2

all: jim usbmon mmap

jim.o: jim.c

mmap.o: mmap.c

usbmon.o: usbmon.c

jim: jim.o

mmap: mmap.o

usbmon: usbmon.o

clean:
	rm -f *.o usbmon jim mmap
