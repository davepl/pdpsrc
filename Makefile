# Delegate the native BSD world build to its authoritative project list.
MAKE=make

all clean:
	cd bsd && ${MAKE} $@

help:
	@echo "make       - build all BSD subprojects on the PDP-11"
	@echo "make clean - remove all BSD build products"
