DESTDIR= /usr/libexec
CFLAGS= -O
# Modern warning flags (comment out on 2.11BSD)
MODERN_WARNINGS= -Wno-deprecated-non-prototype -Wno-deprecated-declarations -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types -Wno-format
# Uncomment the next line on macOS/Linux, comment out on 2.11BSD
CFLAGS += $(MODERN_WARNINGS)
PROGRAM=	httpd
SRCS=		httpd.c
OBJS=		httpd.o
LIBS=

all:	${PROGRAM}

${PROGRAM}:	${OBJS}
	${CC} ${CFLAGS} -o $@ ${OBJS} ${LIBS}

install: ${PROGRAM}
	install -s -m 755 ${PROGRAM} ${DESTDIR}

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
