# Makefile for sieve programs

CC = cc
# Base flags that work on all compilers
CFLAGS = -O
# Check if compiler supports K&R warning suppression (modern GCC/Clang)
KR_WARN_FLAGS := $(shell $(CC) -Wno-implicit-int -E - < /dev/null > /dev/null 2>&1 && echo "-Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -Wno-return-type")
KR_CFLAGS = $(CFLAGS) $(KR_WARN_FLAGS)
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
