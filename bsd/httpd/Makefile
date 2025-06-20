DESTDIR= /usr/libexec
# Base flags that work on all compilers
CFLAGS= -O
# Check if compiler supports warning suppression (modern GCC/Clang)
WARN_FLAGS := $(shell $(CC) -Wno-deprecated-non-prototype -E - < /dev/null > /dev/null 2>&1 && echo "-Wno-deprecated-non-prototype -Wno-deprecated-declarations -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types -Wno-format")
CFLAGS += $(WARN_FLAGS)
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
