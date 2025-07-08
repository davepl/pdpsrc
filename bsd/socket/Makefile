# Makefile for socket client/server programs
# Compatible with both macOS and 211BSD

CC = cc
CFLAGS = -O
LDFLAGS = 

# Default target
all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

clean:
	rm -f client server

.PHONY: all clean
