##
# Makefile for Linux x64 panel client and kernel module
##

CC = clang
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = 
TARGET = client
OBJECTS = client.o
KMODULE = kprobe.ko
KMODULE_NAME = kprobe

# Kernel module variables
KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Default target builds both userspace client and kernel module
all: $(TARGET) $(KMODULE)

# Userspace client
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

client.o: client.c panel_state.h ../../common.c ../../panel_packet.h
	$(CC) $(CFLAGS) -c client.c

# Kernel module
$(KMODULE): kprobe.c
	@echo "Building kernel module..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) CC=gcc modules

# Kernel module clean
kclean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) CC=gcc clean

clean: kclean
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/panel-client-linux

# Kernel module operations
load: $(KMODULE)
	@echo "Loading kernel module..."
	sudo insmod $(KMODULE)

unload:
	@echo "Unloading kernel module..."
	sudo rmmod $(KMODULE_NAME)

# Check kernel module output
dmesg:
	dmesg | tail -20

.PHONY: all clean kclean install load unload dmesg

# Kernel module Makefile section (required for kernel build system)
obj-m += $(KMODULE_NAME).o
