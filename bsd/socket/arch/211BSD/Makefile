# Makefile for PDP-11 2.11BSD client
# Uses K&R C compiler

CC = cc
CFLAGS = -O -D__pdp11__
LDFLAGS = 
LIBS = 
BINDIR=/usr/bin
DESTDIR=
INSTALL=install
DIAGNOSTICS=paneldump symboltest memtest endiantest clocktest allcounters memdiag udptest

# Targets
all: client paneldump symboltest memtest endiantest clocktest allcounters memdiag udptest

install install-diagnostics: FORCE
	@/bin/sh -c 'case "$@" in \
	install) progs="client ${DIAGNOSTICS}" ;; \
	*) progs="${DIAGNOSTICS}" ;; \
	esac; \
	for prog in $$progs; do \
		if test -f "$$prog"; then \
			test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:

client: client.o wait_for_panel.o
	$(CC) $(CFLAGS) -o client client.o wait_for_panel.o $(LDFLAGS) $(LIBS)

paneldump: paneldump.o
	$(CC) $(CFLAGS) -o paneldump paneldump.o $(LDFLAGS) $(LIBS)

symboltest: symboltest.o
	$(CC) $(CFLAGS) -o symboltest symboltest.o $(LDFLAGS) $(LIBS)

memtest: memtest.o
	$(CC) $(CFLAGS) -o memtest memtest.o $(LDFLAGS) $(LIBS)

endiantest: endiantest.o
	$(CC) $(CFLAGS) -o endiantest endiantest.o $(LDFLAGS) $(LIBS)

clocktest: clocktest.o
	$(CC) $(CFLAGS) -o clocktest clocktest.o $(LDFLAGS) $(LIBS)

allcounters: allcounters.o
	$(CC) $(CFLAGS) -o allcounters allcounters.o $(LDFLAGS) $(LIBS)

memdiag: memdiag.o
	$(CC) $(CFLAGS) -o memdiag memdiag.o $(LDFLAGS) $(LIBS)

udptest: udptest.c
	$(CC) $(CFLAGS) -o udptest udptest.c $(LDFLAGS) $(LIBS)

# Compile source files
client.o: client.c panel_state.h ../../common.c ../../panel_packet.h
	$(CC) $(CFLAGS) -c client.c

wait_for_panel.o: wait_for_panel.s
	as -o wait_for_panel.o wait_for_panel.s

paneldump.o: paneldump.c panel_state.h
	$(CC) $(CFLAGS) -c paneldump.c

symboltest.o: symboltest.c
	$(CC) $(CFLAGS) -c symboltest.c

memtest.o: memtest.c
	$(CC) $(CFLAGS) -c memtest.c

endiantest.o: endiantest.c
	$(CC) $(CFLAGS) -c endiantest.c

clocktest.o: clocktest.c
	$(CC) $(CFLAGS) -c clocktest.c

allcounters.o: allcounters.c
	$(CC) $(CFLAGS) -c allcounters.c

memdiag.o: memdiag.c
	$(CC) $(CFLAGS) -c memdiag.c

# Clean up
clean:
	rm -f client.o wait_for_panel.o paneldump.o symboltest.o memtest.o endiantest.o clocktest.o allcounters.o memdiag.o client paneldump symboltest memtest endiantest clocktest allcounters memdiag udptest

.PHONY: all clean
