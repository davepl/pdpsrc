BINDIR= /usr/bin
DESTDIR=
INSTALL=install
CFLAGS= -O
LDFLAGS= -O
PROGRAM=	httpd
SRCS=		httpd.c
OBJS=		httpd.o
LIBS=

all:	${PROGRAM}

${OBJS}: ${SRCS} ../pdp11_unistd.h
	@if [ "`uname -m`" = "pdp11" ]; then \
		echo "Building for PDP-11..."; \
		${CC} ${CFLAGS} -c -o $@ ${SRCS}; \
	else \
		echo "Building for modern system..."; \
		${CC} ${CFLAGS} -Wno-deprecated-non-prototype -Wno-deprecated-declarations -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types -Wno-format -c -o $@ ${SRCS}; \
	fi

${PROGRAM}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

install: FORCE
	@/bin/sh -c 'if test -f ${PROGRAM}; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 ${PROGRAM} "${DESTDIR}${BINDIR}/${PROGRAM}" || exit 1; \
	else echo "${PROGRAM} not built; skipped"; fi'

FORCE:

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
