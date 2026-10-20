# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

# Makefile for compiling mqtt.c

CC = cc
CFLAGS = -O

all: mqtt

mqtt: mqtt.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -o mqtt mqtt.c

clean:
	rm -f mqtt

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in mqtt; do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
