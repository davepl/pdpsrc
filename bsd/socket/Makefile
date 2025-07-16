# Makefile for socket client/server programs
# Compatible with both macOS and 211BSD

CC = cc
CFLAGS = -O

# Platform-specific libraries
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),NetBSD)
    CLIENT_LIBS = -lkvm
    SERVER_LIBS = 
else
    CLIENT_LIBS = 
    SERVER_LIBS = 
endif

# Default target
all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(CLIENT_LIBS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(SERVER_LIBS)

clean:
	rm -f client server

.PHONY: all clean
