# Makefile for sieve programs

CC = cc
CFLAGS = -O
PROGRAMS = sieve krsieve
SRCS = sieve.c krsieve.c

all: $(PROGRAMS)

sieve: sieve.c
	$(CC) $(CFLAGS) -o sieve sieve.c

krsieve: krsieve.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o krsieve krsieve.c; \
	else \
		$(CC) $(CFLAGS) -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -Wno-return-type -o krsieve krsieve.c; \
	fi

clean:
	rm -f $(PROGRAMS) *.o

.PHONY: all clean
