# Makefile for Dhrystone benchmark
# Compatible with 2.11BSD, NetBSD, and macOS

CC = gcc
CFLAGS = -O2 -g
TARGET = dry
SOURCE = dry.c

# Default target
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

# For older systems without modern optimizations
basic:
	$(CC) -o $(TARGET) $(SOURCE)

# For systems with limited memory (like 2.11BSD)
small:
	$(CC) -Os -o $(TARGET) $(SOURCE)

# Clean target
clean:
	rm -f $(TARGET) dry_traditional *.o

# Test target
test: $(TARGET)
	./$(TARGET) 10000

.PHONY: clean test basic small
