# Makefile for compiling simple C programs

CC = cc
CFLAGS = 
# Modern warning flags (comment out on 2.11BSD)
MODERN_WARNINGS = -Wno-deprecated-non-prototype
# Uncomment the next line on macOS/Linux, comment out on 2.11BSD  
CFLAGS += $(MODERN_WARNINGS)
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
