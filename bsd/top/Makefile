# Native 2.11BSD/PDP-11 build; separate instruction and data spaces.
CC=cc
CFLAGS=-O
LDFLAGS=-i
LIBS=-lcurses -ltermcap

all: top

top: top.o
	$(CC) $(LDFLAGS) -o top top.o $(LIBS)

top.o: top.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -c top.c

clean:
	rm -f top.o top
