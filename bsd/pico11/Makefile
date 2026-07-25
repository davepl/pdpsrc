CC=cc
CFLAGS=-O
LDFLAGS=
OBJS=pico11.o buffer.o terminal.o

all: pico11

test: test_buffer
	./test_buffer

test_buffer: test_buffer.o buffer.o
	$(CC) $(LDFLAGS) -o test_buffer test_buffer.o buffer.o

test_buffer.o: test_buffer.c buffer.h pdp11_compat.h
	$(CC) $(CFLAGS) -c test_buffer.c

pico11: $(OBJS)
	case "`uname -s`" in \
	2.11BSD) $(CC) -i $(LDFLAGS) -o pico11 $(OBJS) ;; \
	*) $(CC) $(LDFLAGS) -o pico11 $(OBJS) ;; \
	esac

pico11.o: pico11.c buffer.h terminal.h pdp11_compat.h
	$(CC) $(CFLAGS) -c pico11.c

buffer.o: buffer.c buffer.h pdp11_compat.h
	$(CC) $(CFLAGS) -c buffer.c

terminal.o: terminal.c terminal.h pdp11_compat.h
	$(CC) $(CFLAGS) -c terminal.c

clean:
	rm -f pico11 test_buffer test_buffer.o $(OBJS)
