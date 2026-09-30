# Native 2.11BSD make; no GNU make or ANSI compiler required.
CC=cc
CFLAGS=-O
LDFLAGS=-i
BINDIR=/usr/local

all: pdpfetch

pdpfetch: pdpfetch.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o pdpfetch pdpfetch.c

install: pdpfetch
	install -c -s -m 755 pdpfetch $(BINDIR)/pdpfetch

clean:
	rm -f pdpfetch pdpfetch.o

check: pdpfetch
	/bin/sh check.sh
