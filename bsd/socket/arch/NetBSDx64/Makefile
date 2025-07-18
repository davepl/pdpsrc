# Makefile for NetBSD x64 client
# Uses modern C compiler with kvm library

CC = cc
CFLAGS = -O2 -Wall
LDFLAGS = 
LIBS = -lkvm

# Target
TARGET = client

# Source files
SRCS = client.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Build the client
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

# Compile source files
.c.o:
	$(CC) $(CFLAGS) -c $<

# Clean up
clean:
	rm -f $(OBJS) $(TARGET)

# Install
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install
