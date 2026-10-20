# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

#
# Makefile for building 'wget' from wget.c
#

# Choose your compiler and options. For 2.11BSD, you might just do:
CC      = cc
CFLAGS  = -O   # or -O2 if your system supports it; or just -g

# If you need special socket libs, uncomment or adjust the line below:
# LIBS   = -lsocket -lnsl

all: wget

wget: wget.c ../pdp11_unistd.h
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) wget.c -o wget $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype wget.c -o wget $(LIBS); \
	fi

clean:
	rm -f wget

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in wget; do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
