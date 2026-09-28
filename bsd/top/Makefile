# Native 2.11BSD/PDP-11 build; separate instruction and data spaces.
CC=cc
CFLAGS=-O
LDFLAGS=-i
LIBS=-ltermcap

# Keep symbols for debugging, but compare/deploy the stripped executable.
# Symbols inflate file length; they are not part of a running process.
all: top

top: top.o screen.o
	$(CC) $(LDFLAGS) -o top top.o screen.o $(LIBS)
	cp top top.debug
	strip top

top.o: top.c screen.h ../pdp11_unistd.h
	$(CC) $(CFLAGS) -c top.c

screen.o: screen.c screen.h
	$(CC) $(CFLAGS) -c screen.c

clean:
	rm -f top.o screen.o top top.debug
