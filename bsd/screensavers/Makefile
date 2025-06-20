# Makefile for compiling simple C programs

CC = cc
# Base flags that work on all compilers
CFLAGS = 
# Check if compiler supports warning suppression (modern GCC/Clang)
WARN_FLAGS := $(shell $(CC) -Wno-deprecated-non-prototype -E - < /dev/null > /dev/null 2>&1 && echo "-Wno-deprecated-non-prototype")
CFLAGS += $(WARN_FLAGS)
LDFLAGS =
LIBS = -lm

# Program names
PROGRAMS = stars matrix sine matrix2 sine2

# Default target
all: $(PROGRAMS)

# Explicit rules for each program
stars: stars.c
	$(CC) $(CFLAGS) -o stars stars.c $(LDFLAGS) $(LIBS)

matrix: matrix.c
	$(CC) $(CFLAGS) -o matrix matrix.c $(LDFLAGS) $(LIBS)

matrix2: matrix2.c
	$(CC) $(CFLAGS) -o matrix2 matrix2.c $(LDFLAGS) $(LIBS)

sine: sine.c
	$(CC) $(CFLAGS) -o sine sine.c $(LDFLAGS) $(LIBS)

sine2: sine2.c
	$(CC) $(CFLAGS) -o sine2 sine2.c $(LDFLAGS) $(LIBS)

# Clean target
clean:
	rm -f $(PROGRAMS) *.o
