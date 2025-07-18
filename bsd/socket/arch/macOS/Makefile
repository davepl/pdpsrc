##
# Makefile for macOS panel client
##

CC = clang
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = -framework CoreFoundation
TARGET = client
OBJECTS = client.o

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

client.o: client.c panel_state.h ../../common.c ../../panel_packet.h
	$(CC) $(CFLAGS) -c client.c

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/panel-client-macos

.PHONY: all clean install
