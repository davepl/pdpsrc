AS=as
CC=cc
LDFLAGS=

all: sieve

sieve: sieve.o
	${CC} ${LDFLAGS} -o sieve sieve.o

sieve.o: sieve.asm
	${AS} -o sieve.o sieve.asm

clean:
	rm -f sieve sieve.o
