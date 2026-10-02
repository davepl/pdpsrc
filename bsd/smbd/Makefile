# Native make and K&R cc supported. Override CC/CFLAGS/LDFLAGS if needed.
CC=cc
CFLAGS=-O
LDFLAGS=
OBJS=smbd.o wire.o fs.o metadata.o metaio.o rpc.o auth.o session.o crypto.o revoke.o transport.o

all: smbd smbpwd

smbd: $(OBJS)
	@if test "`uname -m`" = pdp11; then \
		$(CC) -i $(LDFLAGS) -o smbd smbd.o wire.o crypto.o revoke.o \
		    -Z fs.o metadata.o -Z rpc.o auth.o metaio.o session.o transport.o -Y; \
	else \
		$(CC) $(LDFLAGS) -o smbd $(OBJS); \
	fi

smbd.o: smbd.c smbd.h
	$(CC) $(CFLAGS) -c smbd.c
wire.o: wire.c smbd.h
	$(CC) $(CFLAGS) -c wire.c
fs.o: fs.c smbd.h
	$(CC) $(CFLAGS) -c fs.c
metadata.o: metadata.c smbd.h
	$(CC) $(CFLAGS) -c metadata.c
metaio.o: metaio.c smbd.h
	$(CC) $(CFLAGS) -c metaio.c
rpc.o: rpc.c smbd.h
	$(CC) $(CFLAGS) -c rpc.c
auth.o: auth.c smbd.h
	$(CC) $(CFLAGS) -c auth.c
session.o: session.c smbd.h
	$(CC) $(CFLAGS) -c session.c
revoke.o: revoke.c smbd.h
	$(CC) $(CFLAGS) -c revoke.c
transport.o: transport.c smbd.h
	$(CC) $(CFLAGS) -c transport.c
crypto.o: crypto.c smbd.h
	$(CC) $(CFLAGS) -c crypto.c

smbpwd: smbpwd.c crypto.o wire.o smbd.h
	@if test "`uname -m`" = pdp11; then \
		$(CC) $(CFLAGS) -i $(LDFLAGS) -o smbpwd smbpwd.c crypto.o wire.o; \
	else \
		$(CC) $(CFLAGS) $(LDFLAGS) -o smbpwd smbpwd.c crypto.o wire.o; \
	fi

clean:
	rm -f smbd smbpwd *.o tests/crypto_test tests/wire_test

# Portable deterministic unit checks, also runnable on the native compiler.
unit: tests/crypto_test
	./tests/crypto_test

tests/crypto_test: tests/crypto_test.c crypto.c wire.c smbd.h
	@if test "`uname -m`" = pdp11; then \
		$(CC) $(CFLAGS) -i -o tests/crypto_test tests/crypto_test.c crypto.c wire.c; \
	else \
		$(CC) $(CFLAGS) -o tests/crypto_test tests/crypto_test.c crypto.c wire.c; \
	fi

# Host-only checks use modern Python clients and sanitizer-capable cc.
# First install tests/requirements.txt in a virtual environment.
test: all unit
	python3 tests/run_host.py
