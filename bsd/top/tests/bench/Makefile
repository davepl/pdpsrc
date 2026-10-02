# Build this observer natively on the 2.11BSD host being measured.
CC=cc
CFLAGS=-O
LDFLAGS=-i

all: topbench

topbench: topbench.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o topbench topbench.c

clean:
	rm -f topbench
