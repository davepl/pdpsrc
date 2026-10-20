# Native 2.11BSD make; no GNU make or ANSI compiler required.
CC=cc
CFLAGS=-O
LDFLAGS=-i
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: pdpfetch

pdpfetch: pdpfetch.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o pdpfetch pdpfetch.c

install: FORCE
	@/bin/sh -c 'if test -f pdpfetch; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 pdpfetch "${DESTDIR}${BINDIR}/pdpfetch" || exit 1; \
	else echo "pdpfetch not built; skipped"; fi'

FORCE:

clean:
	rm -f pdpfetch pdpfetch.o

check: pdpfetch
	/bin/sh check.sh
