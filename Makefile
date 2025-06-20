# Master Makefile for pdpsrc project
# Builds all subprojects

# Subdirectories with existing Makefiles
MAKEFILE_DIRS = bsd/httpd \
				bsd/mqtt \
				bsd/rzsz \
				bsd/screensavers \
				bsd/sieve \
				bsd/sudo \
				bsd/wget

# Subdirectories with simple C programs (no existing Makefile)
SIMPLE_C_DIRS = 

# Assembly directories (special handling needed)
ASM_DIRS = bsd/asmsieve

# Default target
all: makefile-dirs simple-c-dirs asm-dirs

# Build directories that have existing Makefiles
makefile-dirs:
	@for dir in $(MAKEFILE_DIRS); do \
		echo "Building $$dir..."; \
		$(MAKE) -C $$dir || exit 1; \
	done

# Build simple C programs that don't have Makefiles
simple-c-dirs:
	@for dir in $(SIMPLE_C_DIRS); do \
		echo "Building $$dir..."; \
		cd $$dir && \
		for c_file in *.c; do \
			if [ -f "$$c_file" ]; then \
				prog_name=$${c_file%.c}; \
				echo "  Compiling $$c_file -> $$prog_name"; \
				cc -o $$prog_name $$c_file || exit 1; \
			fi; \
		done && \
		cd - > /dev/null; \
	done

# Build assembly programs (MACRO-11 only on PDP-11 architecture)
asm-dirs:
	@echo "Checking for MACRO-11 assembly programs..."
	@arch=$$(uname -m); \
	if [ "$$arch" = "pdp11" ] || [ "$$arch" = "PDP-11" ]; then \
		echo "PDP-11 architecture detected, building MACRO-11 assembly..."; \
		if command -v macro11 >/dev/null 2>&1 || command -v as >/dev/null 2>&1; then \
			for dir in $(ASM_DIRS); do \
				echo "Building $$dir..."; \
				cd $$dir && \
				for asm_file in *.asm; do \
					if [ -f "$$asm_file" ]; then \
						obj_name=$${asm_file%.asm}.o; \
						prog_name=$${asm_file%.asm}; \
						echo "  Assembling $$asm_file -> $$prog_name"; \
						if command -v macro11 >/dev/null 2>&1; then \
							macro11 -o $$obj_name $$asm_file && \
							ld -o $$prog_name $$obj_name || exit 1; \
						else \
							as -o $$obj_name $$asm_file && \
							ld -o $$prog_name $$obj_name || exit 1; \
						fi; \
					fi; \
				done && \
				cd - > /dev/null; \
			done; \
		else \
			echo "  Warning: MACRO-11 assembler not found, skipping assembly files"; \
		fi; \
	else \
		echo "  Skipping MACRO-11 assembly (not PDP-11 architecture: $$arch)"; \
		echo "  MACRO-11 assembly code is only compatible with PDP-11 systems"; \
	fi

# Clean all built files
clean: clean-makefile-dirs clean-simple-c-dirs clean-asm-dirs

clean-makefile-dirs:
	@for dir in $(MAKEFILE_DIRS); do \
		echo "Cleaning $$dir..."; \
		$(MAKE) -C $$dir clean 2>/dev/null || true; \
	done

clean-simple-c-dirs:
	@for dir in $(SIMPLE_C_DIRS); do \
		echo "Cleaning $$dir..."; \
		cd $$dir && \
		for c_file in *.c; do \
			if [ -f "$$c_file" ]; then \
				prog_name=$${c_file%.c}; \
				rm -f $$prog_name; \
			fi; \
		done && \
		cd - > /dev/null; \
	done

clean-asm-dirs:
	@for dir in $(ASM_DIRS); do \
		echo "Cleaning $$dir..."; \
		cd $$dir && \
		rm -f *.o && \
		for asm_file in *.asm; do \
			if [ -f "$$asm_file" ]; then \
				prog_name=$${asm_file%.asm}; \
				rm -f $$prog_name; \
			fi; \
		done && \
		cd - > /dev/null; \
	done

# Help target
help:
	@echo "Available targets:"
	@echo "  all              - Build all subprojects"
	@echo "  makefile-dirs    - Build directories with existing Makefiles"
	@echo "  simple-c-dirs    - Build simple C programs without Makefiles"
	@echo "  asm-dirs         - Build MACRO-11 assembly programs (PDP-11 only)"
	@echo "  clean            - Clean all built files"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Note: MACRO-11 assembly (.asm) files will only be built on PDP-11 architecture"

# Mark phony targets
.PHONY: all makefile-dirs simple-c-dirs asm-dirs clean clean-makefile-dirs clean-simple-c-dirs clean-asm-dirs help
