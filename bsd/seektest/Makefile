CC=cc
CFLAGS=-O
# Empty means automatic compiler selection; explicit PDPFLAGS overrides it.
PDPFLAGS=
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: seektest

seektest: seektest.c Makefile build-mentec.sh compile.sh
	/bin/sh ./compile.sh "${CC}" "${CFLAGS}" "${PDPFLAGS}"

install: FORCE
	@/bin/sh -c 'if test -f seektest; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 seektest "${DESTDIR}${BINDIR}/seektest" || exit 1; \
	else echo "seektest not built; skipped"; fi'

FORCE:

clean:
	rm -f seektest *.o
