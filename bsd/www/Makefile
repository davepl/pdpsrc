MAKE=make
# Build with the native 2.11BSD compiler and running kernel's headers.
CC=cc
CFLAGS=-O
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: native-webtop webtop-cgi visit-counter visit-proxy visit-total-proxy httpd

native-webtop: FORCE
	@case "`uname -m`" in \
	pdp11|pdp-11) ${MAKE} webtop ;; \
	*) echo "webtop requires 2.11BSD/PDP-11 kernel interfaces; skipped" ;; \
	esac

httpd: FORCE
	cd server && ${MAKE} all

webtop: webtop.c webtop-fields.h ../pdp11_unistd.h
	$(CC) $(CFLAGS) -i -o webtop webtop.c

webtop-cgi: webtop-cgi.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -o webtop-cgi webtop-cgi.c

visit-counter: visit-counter.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -o visit-counter visit-counter.c

visit-proxy: visit-proxy.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -o visit-proxy visit-proxy.c

visit-total-proxy: visit-proxy.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -DREAD_ONLY -o visit-total-proxy visit-proxy.c

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1'
	@/bin/sh -c 'for prog in webtop webtop-cgi visit-counter visit-proxy visit-total-proxy; do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else echo "$$prog not built; skipped"; fi; \
	done'
	@/bin/sh -c 'cd server && ${MAKE} install BINDIR="${BINDIR}" DESTDIR="${DESTDIR}" INSTALL="${INSTALL}"'

install-site: all
	sh install.sh

backup:
	sh backup.sh

clean:
	rm -f webtop webtop-cgi visit-counter visit-proxy visit-total-proxy test-fields *.o core
	cd server && ${MAKE} clean

test-fields: tests/webtop-fields.c webtop-fields.h
	$(CC) $(CFLAGS) -o test-fields tests/webtop-fields.c
	./test-fields

FORCE:
