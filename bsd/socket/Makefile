# Makefile for socket client/server programs
# Compatible with macOS, NetBSD, and 211BSD
# Clients are now organized by platform under arch/

CC = cc
CFLAGS = -O

# Default target
all: client server

# Detect platform and build appropriate client
client:
	@echo "Detecting platform..."
	@if [ "`uname -s`" = "Darwin" ]; then \
		echo "Building macOS client..."; \
		cd arch/macOS && $(MAKE) && cd ../..; \
		cp arch/macOS/client client; \
	elif [ "`uname -s`" = "NetBSD" ]; then \
		if [ "`uname -m`" = "amd64" ]; then \
			echo "Building NetBSD x64 client..."; \
			cd arch/NetBSDx64 && $(MAKE) && cd ../..; \
			cp arch/NetBSDx64/client client; \
		else \
			echo "Building NetBSD VAX client..."; \
			cd arch/NetBSDVAX && $(MAKE) && cd ../..; \
			cp arch/NetBSDVAX/client client; \
		fi; \
	elif [ "`uname -s`" = "Linux" ]; then \
		echo "Building Linux x64 client..."; \
		cd arch/LinuxX64 && $(MAKE) && cd ../..; \
		cp arch/LinuxX64/client client; \
	elif [ -f /usr/include/pdp11 ] || [ "`uname -s`" = "2.11BSD" ]; then \
		echo "Building 2.11BSD client..."; \
		cd arch/211BSD && $(MAKE) && cd ../..; \
		cp arch/211BSD/client client; \
	else \
		echo "Unknown platform, defaulting to macOS client..."; \
		cd arch/macOS && $(MAKE) && cd ../..; \
		cp arch/macOS/client client; \
	fi

server: server.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f client server
	@for dir in arch/*/; do \
		if [ -f "$$dir/Makefile" ]; then \
			echo "Cleaning $$dir..."; \
			cd "$$dir" && $(MAKE) clean && cd ../..; \
		fi; \
	done

.PHONY: all clean client server
