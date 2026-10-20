# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

MAKE=make
AS=as
CC=cc
PROG=attn
OBJS=attn.o
LDFLAGS=

all:
	case "`uname -s 2>/dev/null`" in \
	Darwin) echo "PDP-11 assembly build is intended for 2.11BSD."; \
	        echo "Use 'make regen' on macOS to regenerate attn.s from ../sample sources." ;; \
	*) ${MAKE} ${PROG} ;; \
	esac

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o ${PROG} ${OBJS}

attn.o: attn.s
	${AS} -o attn.o attn.s

regen:
	python3 ./generate.py

clean:
	rm -f ${OBJS} ${PROG}

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in ${PROG}; do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
