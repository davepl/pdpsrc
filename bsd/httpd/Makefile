DESTDIR= /usr/libexec
CFLAGS= -O
LDFLAGS= -O
PROGRAM=	httpd
SRCS=		httpd.c
OBJS=		httpd.o
LIBS=

all:	${PROGRAM}

${OBJS}: ${SRCS}
	@if [ "`uname -m`" = "pdp11" ]; then \
		echo "Building for PDP-11..."; \
		${CC} ${CFLAGS} -c -o $@ ${SRCS}; \
	else \
		echo "Building for modern system..."; \
		${CC} ${CFLAGS} -Wno-deprecated-non-prototype -Wno-deprecated-declarations -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types -Wno-format -c -o $@ ${SRCS}; \
	fi

${PROGRAM}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

install: ${PROGRAM}
	install -s -m 755 ${PROGRAM} ${DESTDIR}

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
