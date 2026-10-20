MAKE=make
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all clean:
	cd krsieve && ${MAKE} $@

install: FORCE
	@/bin/sh -c 'cd krsieve && ${MAKE} install BINDIR="${BINDIR}" DESTDIR="${DESTDIR}" INSTALL="${INSTALL}"'

FORCE:
