DESTDIR= /usr/libexec
CFLAGS= -O
LDFLAGS= -O
PDP11_CFLAGS= -O -i
PDP11_LDFLAGS= -i
PROGRAM=	menu
SRCS=		main.c
OBJS=		main.o
LIBS=		-lcurses
LIBS_PDP11=	-lcurses -ltermcap

all:	${PROGRAM}

${OBJS}: ${SRCS}
	@if [ "`uname -m`" = "pdp11" ]; then \
		echo "Building for PDP-11..."; \
		${CC} ${PDP11_CFLAGS} -DLEGACY_CURSES -D__pdp11__ -c -o $@ ${SRCS}; \
	else \
		echo "Building for modern system..."; \
		${CC} ${CFLAGS} -Wno-deprecated-non-prototype -Wno-deprecated-declarations -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types -Wno-format -c -o $@ ${SRCS}; \
	fi

${PROGRAM}:	${OBJS}
	@if [ "`uname -m`" = "pdp11" ]; then \
		libs="${LIBS_PDP11}"; \
		extra="${PDP11_LDFLAGS}"; \
	else \
		libs="${LIBS}"; \
		extra=""; \
	fi; \
	${CC} ${LDFLAGS} $$extra -o $@ ${OBJS} $$libs

install: ${PROGRAM}
	install -s -m 755 ${PROGRAM} ${DESTDIR}

tags:
	ctags -tdw *.c

clean:
	rm -f a.out core *.o ${PROGRAM}
