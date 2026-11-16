@echo off
set CFLAGS=-Wimplicit -Wreturn-type -Wuninitialized -g -O2
rm -f config.h Makefile config.cache
bash ./configure
make clean
make
