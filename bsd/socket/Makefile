# Makefile for socket client/server programs
# Compatible with both macOS and 211BSD

CC = cc
CFLAGS = -O

# Default target
all: client server

# Use different targets for different platforms
client: client.c
	@if [ "`uname -s`" = "NetBSD" ]; then \
		$(CC) $(CFLAGS) -o client client.c -lkvm; \
	else \
		$(CC) $(CFLAGS) -o client client.c; \
	fi

server: server.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f client server

.PHONY: all clean
