# Makefile for sudo

CC = cc
CFLAGS = -O
PROGRAM = sudo
SRCS = sudo.c

all: $(PROGRAM)

$(PROGRAM): $(SRCS)
	@if [ "`uname -m`" = "pdp11" ]; then \
		$(CC) $(CFLAGS) -o $(PROGRAM) $(SRCS); \
	else \
		$(CC) $(CFLAGS) -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch -o $(PROGRAM) $(SRCS); \
	fi

clean:
	rm -f $(PROGRAM) *.o

.PHONY: all clean
