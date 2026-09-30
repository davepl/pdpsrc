# Makefile for compiling simple C programs

CC = cc
CFLAGS = 
LDFLAGS =
LIBS = -lm

# Program names
PROGRAMS = stars matrix sine matrix2 sine2 life

# Default target
all: $(PROGRAMS)

# Explicit rules for each program with platform detection
stars: stars.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o stars stars.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o stars stars.c $(LIBS); \
	fi

matrix: matrix.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o matrix matrix.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o matrix matrix.c $(LIBS); \
	fi

matrix2: matrix2.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o matrix2 matrix2.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o matrix2 matrix2.c $(LIBS); \
	fi

sine: sine.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o sine sine.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o sine sine.c $(LIBS); \
	fi

sine2: sine2.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o sine2 sine2.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o sine2 sine2.c $(LIBS); \
	fi

life: life.c
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o life life.c $(LIBS); \
	else \
		$(CC) $(CFLAGS) -Wno-deprecated-non-prototype -o life life.c $(LIBS); \
	fi

# Clean target
clean:
	rm -f $(PROGRAMS) *.o
