# Makefile for compiling mqtt.c

CC = cc
CFLAGS = -O

all: mqtt

mqtt: mqtt.c ../pdp11_unistd.h
	$(CC) $(CFLAGS) -o mqtt mqtt.c

clean:
	rm -f mqtt
