# Makefile for 2.11BSD

# List all .c files and their corresponding executable targets
SRCS = bug1.c bug2.c bug3.c bug4.c bug5.c bug6.c
PROGS = bug1 bug2 bug3 bug4 bug5 bug6

all: ${PROGS}

# Rule to compile each source file into an executable
bug1: bug1.c
	cc -o bug1 bug1.c

bug2: bug2.c
	cc -o bug2 bug2.c

bug3: bug3.c
	cc -o bug3 bug3.c

bug4: bug4.c
	cc -o bug4 bug4.c

bug5: bug5.c
	cc -o bug5 bug5.c

bug6: bug6.c
	cc -o bug6 bug6.c

clean:
	rm -f ${PROGS} *.o
