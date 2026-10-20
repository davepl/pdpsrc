# Native 2.11BSD/PDP-11 build; separate instruction and data spaces.
CC=cc
CFLAGS=-O
LDFLAGS=-i
LIBS=-ltermcap
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

# Keep symbols for debugging, but compare/deploy the stripped executable.
# Symbols inflate file length; they are not part of a running process.
all: top

top: top.o screen.o
	rm -f top
	$(CC) $(LDFLAGS) -o top top.o screen.o $(LIBS)
	cp top top.debug
	strip top

top.o: top.c screen.h format.h ../pdp11_unistd.h
	$(CC) $(CFLAGS) -c top.c

screen.o: screen.c screen.h
	$(CC) $(CFLAGS) -c screen.c

# Run these targets as root. Use only group read access to kernel memory.
install: FORCE
	@/bin/sh -c 'if test -f top; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		$(INSTALL) -c -o root -g kmem -m 2755 top "${DESTDIR}${BINDIR}/top" || exit 1; \
	else echo "top not built; skipped"; fi'

FORCE:

# For developers who run ./top in the source directory; repeat after rebuilds.
install-local: top
	chown root top
	chgrp kmem top
	chmod 2755 top

clean:
	rm -f top.o screen.o top top.debug
