# Makefile for Dhrystone 2.2 benchmark
# Simplified for 2.9BSD and modern systems

# Default target: build the simplified 2.9BSD version
all: dry29

# 2.9BSD version (integer-only, K&R C style)
dry29:
	cc -O -Wno-implicit-function-declaration -o dry29 dry29.c

# No optimization (safe for all PDP-11 models)  
dry29-noopt:
	cc -Wno-implicit-function-declaration -o dry29 dry29.c

# Debug version with extra output
dry29-debug:
	cc -Wno-implicit-function-declaration -o dry29 dry29.c

# Original cross-platform version (for comparison)
dry:
	cc -O -o dry dry.c

# Test targets
test: dry29
	./dry29 1000

test-long: dry29
	./dry29 100000

compare: dry29 dry
	@echo "Testing 2.9BSD version:"
	./dry29 100000
	@echo ""
	@echo "Testing original version:"
	./dry 100000

clean:
	rm -f dry dry29 *.o core

.PHONY: all test test-long compare clean
