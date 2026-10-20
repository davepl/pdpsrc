CC=cc
CFLAGS=-O
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: krsieve

krsieve: krsieve.c
	${CC} ${CFLAGS} -o krsieve krsieve.c

install: FORCE
	@/bin/sh -c 'if test -f krsieve; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 krsieve "${DESTDIR}${BINDIR}/krsieve29" || exit 1; \
	else \
		echo "krsieve not built; skipped"; \
	fi'

clean:
	rm -f krsieve *.o

FORCE:
