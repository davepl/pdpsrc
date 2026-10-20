# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

PROG = bsdbasic
SRC = main.c

CFLAGS_GENERIC = -O2 -std=c89 -Wall -Wextra -pedantic
LDFLAGS_GENERIC = -lm

CFLAGS_PDP11 = -O -i
LDFLAGS_PDP11 = -lm

all: $(PROG)

$(PROG): $(SRC)
	@UNAME=`uname -m 2>/dev/null || uname -p 2>/dev/null || echo unknown`; \
	case "$$UNAME" in \
	*pdp*|*PDP* ) CF="$(CFLAGS_PDP11)"; LF="$(LDFLAGS_PDP11)";; \
	* ) CF="$(CFLAGS_GENERIC)"; LF="$(LDFLAGS_GENERIC)";; \
	esac; \
	echo "$(CC) $$CF -o $@ $(SRC) $$LF"; \
	$(CC) $$CF -o $@ $(SRC) $$LF

test: $(PROG)
	./$(PROG) test.bas > test.out
	cmp test.expected test.out
	rm -f test.out

clean:
	rm -f $(PROG) *.o test.out

.PHONY: all clean test install FORCE

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in $(PROG); do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
