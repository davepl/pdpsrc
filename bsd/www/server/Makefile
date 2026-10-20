# 2.11BSD httpd
# A simple web server for 2.11BSD on the PDP-11. Supports logging of HTTP
# requests, executing CGI binaries, and serving static documents.
#
# Original version available at:
# https://github.com/AaronJackson/2.11BSDhttpd

BINDIR= /usr/bin
DESTDIR=
INSTALL=install
INSTNAME=www-httpd
CFLAGS= -O
PROGRAM=	httpd
SRCS=		httpd.c
OBJS=		httpd.o
LIBS=
LOGFILE=	/usr/adm/httpd.log

all:	${PROGRAM}

${OBJS}: ${SRCS} ../../pdp11_unistd.h

${PROGRAM}:	${OBJS}
	${CC} ${CFLAGS} -o $@ ${OBJS} ${LIBS}

install: FORCE
	@/bin/sh -c 'if test -f ${PROGRAM}; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 ${PROGRAM} "${DESTDIR}${BINDIR}/${INSTNAME}" || exit 1; \
	else echo "${PROGRAM} not built; skipped"; fi'

FORCE:

install-service: ${PROGRAM}
	install -c -s -m 755 ${PROGRAM} /usr/libexec/httpd
	touch ${LOGFILE}
	chown www ${LOGFILE}
	chgrp staff ${LOGFILE}
	chmod 644 ${LOGFILE}

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
