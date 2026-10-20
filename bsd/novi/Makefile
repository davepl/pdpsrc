# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

CC=cc
CFLAGS=-O
LDFLAGS=
OBJS=novi.o buffer.o terminal.o

all: novi

test: test_buffer test_terminal
	./test_buffer
	./test_terminal

test_buffer: test_buffer.o buffer.o
	$(CC) $(LDFLAGS) -o test_buffer test_buffer.o buffer.o

test_buffer.o: test_buffer.c buffer.h pdp11_compat.h
	$(CC) $(CFLAGS) -c test_buffer.c

test_terminal: test_terminal.o terminal.o
	$(CC) $(LDFLAGS) -o test_terminal test_terminal.o terminal.o

test_terminal.o: test_terminal.c terminal.h pdp11_compat.h
	$(CC) $(CFLAGS) -c test_terminal.c

novi: $(OBJS)
	case "`uname -m`" in \
	pdp11) $(CC) -i $(LDFLAGS) -o novi $(OBJS) ;; \
	*) $(CC) $(LDFLAGS) -o novi $(OBJS) ;; \
	esac

novi.o: novi.c buffer.h terminal.h pdp11_compat.h
	$(CC) $(CFLAGS) -c novi.c

buffer.o: buffer.c buffer.h pdp11_compat.h
	$(CC) $(CFLAGS) -c buffer.c

terminal.o: terminal.c terminal.h pdp11_compat.h
	$(CC) $(CFLAGS) -c terminal.c

clean:
	rm -f novi test_buffer test_buffer.o test_terminal test_terminal.o $(OBJS)

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	for prog in novi; do \
		if test -f "$$prog"; then \
			${INSTALL} -c -m 755 "$$prog" "${DESTDIR}${BINDIR}/$$prog" || exit 1; \
		else \
			echo "$$prog not built; skipped"; \
		fi; \
	done'

FORCE:
