MAKE=make
# Build the native 2.11BSD tree, or its portable programs on macOS.
# Keep this compatible with the system make and /bin/sh on the PDP-11.
# Recurse sequentially: the PDP has limited memory and swap space.
SUBDIRS=29bsd MACRO11 asmsieve attn basic bsdbasic bugs dry httpd \
	menu mqtt novi rzsz screensavers sieve socket sudo top wget www

all: FORCE
	@machine=`uname -m`; \
	for dir in ${SUBDIRS}; do \
		case "$$dir:$$machine" in \
		asmsieve:pdp11|asmsieve:pdp-11|top:pdp11|top:pdp-11) ;; \
		asmsieve:*|top:*) echo "===> $$dir (PDP-11 only; skipped)"; continue ;; \
		esac; \
		echo "===> $$dir (all)"; \
		(cd $$dir && ${MAKE} all) || exit 1; \
	done

clean: FORCE
	@for dir in ${SUBDIRS}; do \
		echo "===> $$dir (clean)"; \
		(cd $$dir && ${MAKE} clean) || exit 1; \
	done

FORCE:
