# Makefile for sieve programs

CC = cc
CFLAGS = -O
# Modern warning flags (comment out on 2.11BSD)
KR_MODERN_WARNINGS = -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -Wno-return-type
# Uncomment the next line on macOS/Linux, comment out on 2.11BSD
KR_CFLAGS = $(CFLAGS) $(KR_MODERN_WARNINGS)
PROGRAMS = sieve krsieve
SRCS = sieve.c krsieve.c

all: $(PROGRAMS)

sieve: sieve.c
	$(CC) $(CFLAGS) -o sieve sieve.c

krsieve: krsieve.c
	$(CC) $(KR_CFLAGS) -o krsieve krsieve.c

clean:
	rm -f $(PROGRAMS) *.o

.PHONY: all clean
