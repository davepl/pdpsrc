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

clean:
	rm -f $(PROG) *.o

.PHONY: all clean
