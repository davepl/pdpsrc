# Makefile for sudo

CC = cc
CFLAGS = -O
PROGRAM = sudo
SRCS = sudo.c
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: $(PROGRAM)

$(PROGRAM): $(SRCS) ../pdp11_unistd.h
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o $(PROGRAM) $(SRCS); \
	else \
		$(CC) $(CFLAGS) -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -o $(PROGRAM) $(SRCS); \
	fi

clean:
	rm -f $(PROGRAM) *.o

install: FORCE
	@/bin/sh -c 'if test -f $(PROGRAM); then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -o root -m 4755 $(PROGRAM) "${DESTDIR}${BINDIR}/$(PROGRAM)" || exit 1; \
	else echo "$(PROGRAM) not built; skipped"; fi'

FORCE:

.PHONY: all clean install
