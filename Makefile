# Delegate the native BSD world build to its authoritative project list.
MAKE=make
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all clean:
	cd bsd && ${MAKE} $@

install:
	@/bin/sh -c 'if test -n "${INSTALLDIRS}"; then \
		cd bsd && ${MAKE} install BINDIR="${BINDIR}" DESTDIR="${DESTDIR}" \
		    INSTALL="${INSTALL}" INSTALLDIRS="${INSTALLDIRS}"; \
	else \
		cd bsd && ${MAKE} install BINDIR="${BINDIR}" DESTDIR="${DESTDIR}" INSTALL="${INSTALL}"; \
	fi'

help:
	@echo "make       - build all BSD subprojects on the PDP-11"
	@echo "make clean - remove all BSD build products"
	@echo "make install - copy compiled BSD programs to /usr/bin"
