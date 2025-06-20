# Makefile for sudo

CC = cc
# Base flags that work on all compilers
CFLAGS = -O
# Check if compiler supports K&R warning suppression (modern GCC/Clang)
WARN_FLAGS := $(shell $(CC) -Wno-implicit-int -E - < /dev/null > /dev/null 2>&1 && echo "-Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch")
CFLAGS += $(WARN_FLAGS)
PROGRAM = sudo
SRCS = sudo.c

all: $(PROGRAM)

$(PROGRAM): $(SRCS)
	$(CC) $(CFLAGS) -o $(PROGRAM) $(SRCS)

clean:
	rm -f $(PROGRAM) *.o

.PHONY: all clean
