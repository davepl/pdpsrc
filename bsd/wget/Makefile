#
# Makefile for building 'wget' from wget.c
#

# Choose your compiler and options. For 2.11BSD, you might just do:
CC      = cc
# Base flags that work on all compilers
CFLAGS  = -O
# Check if compiler supports warning suppression (modern GCC/Clang)
WARN_FLAGS := $(shell $(CC) -Wno-deprecated-non-prototype -E - < /dev/null > /dev/null 2>&1 && echo "-Wno-deprecated-non-prototype")
CFLAGS += $(WARN_FLAGS)

# If you need special socket libs, uncomment or adjust the line below:
# LIBS   = -lsocket -lnsl

all: wget

wget: wget.c
	$(CC) $(CFLAGS) wget.c -o wget $(LIBS)

clean:
	rm -f wget

