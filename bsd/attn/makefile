AS=as
CC=cc
PROG=attn
OBJS=attn.o
LDFLAGS=

all:
	case "`uname -s 2>/dev/null`" in \
	Darwin) echo "PDP-11 assembly build is intended for 2.11BSD."; \
	        echo "Use 'make regen' on macOS to regenerate attn.s from ../sample sources." ;; \
	*) make ${PROG} ;; \
	esac

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o ${PROG} ${OBJS}

attn.o: attn.s
	${AS} -o attn.o attn.s

regen:
	python3 ./generate.py

clean:
	rm -f ${OBJS} ${PROG}
