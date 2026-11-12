CC=cc
CFLAGS=-O
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: vmsieve

vmsieve: vmsieve.c ../sieve/sieve.c
	$(CC) $(CFLAGS) -DVMSIEVE -o vmsieve ../sieve/sieve.c vmsieve.c

clean:
	rm -f vmsieve *.o

install: FORCE
	@/bin/sh -c 'if test -f vmsieve; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 vmsieve "${DESTDIR}${BINDIR}/vmsieve" || exit 1; \
	else echo "vmsieve not built; skipped"; fi'

FORCE:
