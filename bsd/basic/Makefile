# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

# Portable makefile for 2.11BSD/pdp11 and modern Unix/macOS

TARGET=basic
SRCS=basic.c

# Defaults for modern systems; PDP11 settings are chosen in the rule below.
CC=cc
CFLAGS=-Wall -std=c89 -pedantic -O2 -DHAVE_USLEEP
LDFLAGS=-lm

all: $(TARGET)

$(TARGET): $(SRCS)
	@set -e; \
	M=`uname -m 2>/dev/null || echo unknown`; \
	U=`uname -s 2>/dev/null || echo unknown`; \
	case "$$M $$U" in \
	*pdp11*|*PDP*|*pdp*|*"2.11BSD"*) \
		CC=cc; CFLAGS="-i -O"; LDFLAGS="-lm"; PLATFORM="PDP11";; \
	*) \
		CC="$(CC)"; CFLAGS="$(CFLAGS)"; LDFLAGS="$(LDFLAGS)"; PLATFORM="UNIX";; \
	esac; \
	echo "Building for $$PLATFORM ($$M $$U)"; \
	$$CC $$CFLAGS -o $@ $(SRCS) $$LDFLAGS

clean:
	rm -f $(TARGET)

.PHONY: all clean install FORCE

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in $(TARGET); do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
