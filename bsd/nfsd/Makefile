# NFS version 3 server for 2.11BSD.  Native make and cc; also builds on
# modern hosts for testing (make HOSTCFLAGS=...).
CC=cc
# No -O: the 2.11BSD optimizer (c2) miscompiles some chains of unsigned
# long comparisons, such as the record-mark check in serve_tcp.
CFLAGS=
LDFLAGS=
SBINDIR=/usr/sbin
DESTDIR=
INSTALL=install
OBJS=nfsd.o nfs3.o fh.o dir.o xdr.o

all: nfsd

nfsd: $(OBJS)
	@if test "`uname -m`" = pdp11; then \
		$(CC) -i $(LDFLAGS) -o nfsd $(OBJS); \
	else \
		$(CC) $(LDFLAGS) -o nfsd $(OBJS); \
	fi

nfsd.o: nfsd.c nfsd.h
	$(CC) $(CFLAGS) -c nfsd.c
nfs3.o: nfs3.c nfsd.h
	$(CC) $(CFLAGS) -c nfs3.c
fh.o: fh.c nfsd.h
	$(CC) $(CFLAGS) -c fh.c
dir.o: dir.c nfsd.h
	$(CC) $(CFLAGS) -c dir.c
xdr.o: xdr.c nfsd.h
	$(CC) $(CFLAGS) -c xdr.c

install: nfsd
	$(INSTALL) -c -m 755 nfsd $(DESTDIR)$(SBINDIR)/nfsd

clean:
	rm -f nfsd *.o
