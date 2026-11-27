# Native Minerva build, run from this directory. No system tools are replaced.
CC=cc
CFLAGS=-O
PASSFLAGS=-t01p -B../native/compiler/
VM_PAGES=20

all: cc211 ld211

cc211: cc211.c ../native/compiler/c0 ../native/compiler/c1 ../native/compiler/cpp
	/bin/sh -c 'test -d ../native-tmp || mkdir ../native-tmp || exit 1'
	${CC} ${PASSFLAGS} ${CFLAGS} -o cc211 cc211.c

ld211.o: ld211.c archive.h ../../pdp11_unistd.h cc211
	./cc211 ${PASSFLAGS} ${CFLAGS} -DNUM_VM_PAGES=${VM_PAGES} -c ld211.c -o ld211.o

ld211: ld211.o cc211
	./cc211 -i -o ld211 ld211.o -lvmf

# Optional fallback only: prepare private libc archives for an overlaid link.
libc: prepare_libc.sh
	cd ../upstream && /bin/sh ../tools/prepare_libc.sh

clean:
	rm -f cc211 ld211 ld211.o
