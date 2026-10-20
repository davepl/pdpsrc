MAKE=make
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all:
	cd src && ${MAKE} all
	cd sample && ${MAKE} all

install: FORCE
	@/bin/sh -c 'cd src && ${MAKE} install BINDIR="${BINDIR}" DESTDIR="${DESTDIR}" INSTALL="${INSTALL}"'

clean:
	cd sample && ${MAKE} clean
	cd src && ${MAKE} clean

smoke portcheck:
	cd src && ${MAKE} $@

FORCE:
