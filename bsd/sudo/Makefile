# Makefile for sudo

CC = cc
CFLAGS = -O
# Modern warning flags (comment out on 2.11BSD)
MODERN_WARNINGS = -Wno-implicit-int -Wno-deprecated-non-prototype -Wno-return-mismatch
# Uncomment the next line on macOS/Linux, comment out on 2.11BSD
CFLAGS += $(MODERN_WARNINGS)
PROGRAM = sudo
SRCS = sudo.c

all: $(PROGRAM)

$(PROGRAM): $(SRCS)
	$(CC) $(CFLAGS) -o $(PROGRAM) $(SRCS)

clean:
	rm -f $(PROGRAM) *.o

.PHONY: all clean
