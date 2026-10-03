# Bootstrap object files are a preserved native compiler snapshot.
.SUFFIXES:

C0OBJS = from-host/c00.o from-host/c01.o from-host/c02.o from-host/c03.o from-host/c04.o from-host/c05.o from-host/ansi.o
C1OBJS = from-host/c10.o from-host/c11.o from-host/c12.o from-host/c13.o from-host/table.o
RUNTIME = crt0-stack.o csv-stack.o

all: c0 c1

c0: $(RUNTIME) $(C0OBJS) Makefile
	ld -i -o c0 $(RUNTIME) $(C0OBJS) -lc

c1: $(RUNTIME) $(C1OBJS) Makefile
	ld -n -o c1 $(RUNTIME) $(C1OBJS) -lc

crt0-stack.o: crt0-stack.s Makefile
	/lib/cpp -t crt0-stack.s > crt0-stack.cpp
	sed 's;^#;/;' crt0-stack.cpp > crt0-stack.as
	as -o crt0-stack.o crt0-stack.as
	rm -f crt0-stack.cpp crt0-stack.as

csv-stack.o: csv-stack.s DEFS.h Makefile
	/lib/cpp -t csv-stack.s > csv-stack.cpp
	sed 's;^#;/;' csv-stack.cpp > csv-stack.as
	as -o csv-stack.o csv-stack.as
	rm -f csv-stack.cpp csv-stack.as

clean:
	rm -f c0 c1 crt0-stack.o csv-stack.o *.cpp *.as
