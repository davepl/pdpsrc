# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

# Makefile for sieve programs

CC = cc
CFLAGS = -O
PROGRAMS = sieve krsieve
SRCS = sieve.c krsieve.c

all: $(PROGRAMS)

sieve: sieve.c
	$(CC) $(CFLAGS) -o sieve sieve.c

krsieve: krsieve.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o krsieve krsieve.c; \
	else \
		$(CC) $(CFLAGS) -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -Wno-return-type -o krsieve krsieve.c; \
	fi

clean:
	rm -f $(PROGRAMS) *.o

.PHONY: all clean install FORCE

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in $(PROGRAMS); do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
