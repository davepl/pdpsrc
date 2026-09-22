# Build with the native 2.11BSD compiler and running kernel's headers.
CC=cc
CFLAGS=-O

all: webtop webtop-cgi visit-proxy visit-total-proxy

webtop: webtop.c webtop-fields.h
	$(CC) $(CFLAGS) -i -o webtop webtop.c

webtop-cgi: webtop-cgi.c
	$(CC) $(CFLAGS) -o webtop-cgi webtop-cgi.c

visit-counter: visit-counter.c
	$(CC) $(CFLAGS) -o visit-counter visit-counter.c

visit-proxy: visit-proxy.c
	$(CC) $(CFLAGS) -o visit-proxy visit-proxy.c

visit-total-proxy: visit-proxy.c
	$(CC) $(CFLAGS) -DREAD_ONLY -o visit-total-proxy visit-proxy.c

install: all
	sh install.sh

backup:
	sh backup.sh

clean:
	rm -f webtop webtop-cgi visit-counter visit-proxy visit-total-proxy test-fields *.o core

test-fields: tests/webtop-fields.c webtop-fields.h
	$(CC) $(CFLAGS) -o test-fields tests/webtop-fields.c
	./test-fields
