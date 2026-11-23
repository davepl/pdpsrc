# NFS version 3 client for 2.11BSD.  Native make and cc; also builds on
# modern hosts for compile checks.
CC=cc
CFLAGS=-O
LDFLAGS=
BINDIR=/usr/bin
SBINDIR=/usr/sbin
DESTDIR=
INSTALL=install
LIBOBJS=xdr.o rpc.o nfs3.o

DOBJS=mount_nfs.o vdisk.o vdwrite.o itab.o

all: nfsc mount_nfs

nfsc: nfsc.o $(LIBOBJS)
	@if test "`uname -m`" = pdp11; then \
		$(CC) -i $(LDFLAGS) -o nfsc nfsc.o $(LIBOBJS); \
	else \
		$(CC) $(LDFLAGS) -o nfsc nfsc.o $(LIBOBJS); \
	fi

mount_nfs: $(DOBJS) $(LIBOBJS)
	@if test "`uname -m`" = pdp11; then \
		$(CC) -i $(LDFLAGS) -o mount_nfs $(DOBJS) $(LIBOBJS); \
	else \
		$(CC) $(LDFLAGS) -o mount_nfs $(DOBJS) $(LIBOBJS); \
	fi

VTOBJS=vdtest.o vdisk.o vdwrite.o itab.o

# vdtest: user-mode check of the virtual-disk read path (not installed)
vdtest: $(VTOBJS) $(LIBOBJS)
	@if test "`uname -m`" = pdp11; then \
		$(CC) -i $(LDFLAGS) -o vdtest $(VTOBJS) $(LIBOBJS); \
	else \
		$(CC) $(LDFLAGS) -o vdtest $(VTOBJS) $(LIBOBJS); \
	fi

vdtest.o: vdtest.c vd.h nfs.h
	$(CC) $(CFLAGS) -c vdtest.c

mount_nfs.o: mount_nfs.c vd.h nfs.h
	$(CC) $(CFLAGS) -c mount_nfs.c
vdisk.o: vdisk.c vd.h nfs.h
	$(CC) $(CFLAGS) -c vdisk.c
vdwrite.o: vdwrite.c vd.h nfs.h
	$(CC) $(CFLAGS) -c vdwrite.c
itab.o: itab.c vd.h nfs.h
	$(CC) $(CFLAGS) -c itab.c
xdr.o: xdr.c nfs.h
	$(CC) $(CFLAGS) -c xdr.c
rpc.o: rpc.c nfs.h
	$(CC) $(CFLAGS) -c rpc.c
nfs3.o: nfs3.c nfs.h
	$(CC) $(CFLAGS) -c nfs3.c
nfsc.o: nfsc.c nfs.h
	$(CC) $(CFLAGS) -c nfsc.c

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1'
	@/bin/sh -c 'if test -f nfsc; then ${INSTALL} -c -m 755 nfsc "${DESTDIR}${BINDIR}/nfsc" || exit 1; \
		else echo "nfsc not built; skipped"; fi'
	@/bin/sh -c 'test -d "${DESTDIR}${SBINDIR}" || mkdir -p "${DESTDIR}${SBINDIR}" || exit 1'
	@/bin/sh -c 'if test -f mount_nfs; then ${INSTALL} -c -m 755 mount_nfs "${DESTDIR}${SBINDIR}/mount_nfs" || exit 1; \
		else echo "mount_nfs not built; skipped"; fi'

clean:
	rm -f nfsc mount_nfs vdtest *.o

FORCE:
