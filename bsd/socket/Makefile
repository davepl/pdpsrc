MAKE=make
# Build only the client for the running system, plus the portable server.
CC=cc
CFLAGS=-O

all: client server

client: FORCE
	@case "`uname -m`:`uname -s`" in \
	pdp11:*) dir=211BSD ;; \
	*:Darwin) dir=macOS ;; \
	amd64:NetBSD) dir=NetBSDx64 ;; \
	*:NetBSD) dir=NetBSDVAX ;; \
	*:Linux) dir=LinuxX64 ;; \
	*) echo "Unsupported socket client platform"; exit 1 ;; \
	esac; \
	${MAKE} native-client CLIENTDIR=arch/$$dir

native-client: FORCE
	cd ${CLIENTDIR} && ${MAKE} all
	cp ${CLIENTDIR}/client client

server: server.c common.c panel_packet.h arch/211BSD/panel_state.h arch/NetBSDVAX/panel_state.h arch/NetBSDx64/panel_state.h arch/macOS/panel_state.h arch/LinuxX64/panel_state.h ../pdp11_unistd.h
	${CC} ${CFLAGS} -o server server.c

test: test-layout
	./test-layout

test-layout: test-layout.o
	${CC} -o test-layout test-layout.o

test-layout.o: test-layout.c panel_packet.h arch/211BSD/panel_state.h arch/NetBSDVAX/panel_state.h ../tools/cpp
	case "`uname -m`" in \
	pdp11) ${CC} -tp -B../tools/ ${CFLAGS} -c test-layout.c ;; \
	*) ${CC} ${CFLAGS} -c test-layout.c ;; \
	esac

# Avoid parsing foreign makefiles (Linux needs GNU make and kernel headers).
clean: FORCE
	rm -f client server test-layout *.o
	rm -f arch/*/client arch/*/*.o
	rm -f arch/211BSD/paneldump arch/211BSD/symboltest arch/211BSD/memtest
	rm -f arch/211BSD/endiantest arch/211BSD/clocktest arch/211BSD/allcounters
	rm -f arch/211BSD/memdiag arch/211BSD/udptest
	rm -f arch/LinuxX64/*.ko arch/LinuxX64/*.mod arch/LinuxX64/*.mod.c
	rm -f arch/LinuxX64/Module.symvers arch/LinuxX64/modules.order

FORCE:
