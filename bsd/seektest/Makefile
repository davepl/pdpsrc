CC=cc
CFLAGS=-O
# Empty means automatic compiler selection; explicit PDPFLAGS overrides it.
PDPFLAGS=
BINDIR=/usr/local

all: seektest

seektest: seektest.c Makefile build-mentec.sh compile.sh
	/bin/sh ./compile.sh "${CC}" "${CFLAGS}" "${PDPFLAGS}"

install: seektest
	install -c -m 755 seektest ${BINDIR}/seektest

clean:
	rm -f seektest *.o
