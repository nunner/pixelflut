CC = gcc
DEFS = -D_BSD_SOURCE -D_SVID_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS)
LDFLAGS = -lpng -lpthread

OBJECTS = pixelflut.o

.PHONY: all clean
all: pixelflut 

pixelflut: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

pixelflut.o: pixelflut.c pixelflut.h

clean:
	rm -rf *.o pixelflut
