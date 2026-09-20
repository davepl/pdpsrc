# 2.11BSD httpd
# A simple web server for 2.11BSD on the PDP-11. Supports logging of HTTP
# requests, executing CGI binaries, and serving static documents.
#
# Original version available at:
# https://github.com/AaronJackson/2.11BSDhttpd

DESTDIR= /usr/libexec
CFLAGS= -O
PROGRAM=	httpd
SRCS=		httpd.c
OBJS=		httpd.o
LIBS=
LOGFILE=	/usr/adm/httpd.log

all:	${PROGRAM}

${PROGRAM}:	${OBJS}
	${CC} ${CFLAGS} -o $@ ${OBJS} ${LIBS}

install: ${PROGRAM}
	install -s -m 755 ${PROGRAM} ${DESTDIR}
	touch ${LOGFILE}
	chown www ${LOGFILE}
	chgrp staff ${LOGFILE}
	chmod 644 ${LOGFILE}

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
