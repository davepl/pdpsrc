# Build with the native 2.11BSD compiler and running kernel's headers.
CC=cc
CFLAGS=-O

all: webtop webtop-cgi visit-counter

webtop: webtop.c
	$(CC) $(CFLAGS) -i -o webtop webtop.c

webtop-cgi: webtop-cgi.c
	$(CC) $(CFLAGS) -o webtop-cgi webtop-cgi.c

visit-counter: visit-counter.c
	$(CC) $(CFLAGS) -o visit-counter visit-counter.c

install: all
	sh install.sh

backup:
	sh backup.sh

clean:
	rm -f webtop webtop-cgi visit-counter *.o core
