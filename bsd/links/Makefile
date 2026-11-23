MAKE=make
# Native builds do not require autoconf, Python, or a modern make.
CC=cc
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all:
	@case `uname -m` in 	pdp11|pdp-11) cd upstream && make -f ../Makefile.211bsd ;; 	*) ${MAKE} host ;; 	esac

host:
	cd upstream && CC="${CC} -std=gnu89 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types" ./configure --without-ssl
	cd upstream && ${MAKE} CFLAGS="-Os -DPDP11_SMALL -Wno-pointer-sign"

test: host
	python3 tests/smoke.py

native:
	@test -f upstream/language.dat || { echo "Missing language.dat; generate/copy native English sidecar first"; exit 1; }
	cd upstream && ${MAKE} -f ../Makefile.211bsd

native-objects:
	cd upstream && ${MAKE} -f ../Makefile.211bsd objects

clean:
	cd upstream && rm -f *.o links links-pdp terminal-decls.h terminal-decls.tmp
	rm -f native-lib/*.o native-lib/*.a tools/cc211 tools/ld211 tools/ld211.o
	rm -rf native-tmp

install:
	@/bin/sh -c 'case `uname -m` in pdp11|pdp-11) exe=upstream/links-pdp ;; *) exe=upstream/links ;; esac; \
		if test -f "$$exe"; then \
			test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
			if test "$$exe" = upstream/links-pdp; then \
				${INSTALL} -c -m 644 upstream/language.dat "${DESTDIR}${BINDIR}/language.dat" || exit 1; \
			fi; \
			${INSTALL} -c -m 755 "$$exe" "${DESTDIR}${BINDIR}/links" || exit 1; \
		else echo "links not built; skipped"; fi'

native-test: native
	sh tests/native-smoke.sh ${TEST_HTTP_BASE}
