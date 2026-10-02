MAKE=make
all:
	cd src && ${MAKE} all
	cd sample && ${MAKE} all

clean:
	cd sample && ${MAKE} clean
	cd src && ${MAKE} clean

smoke portcheck:
	cd src && ${MAKE} $@
