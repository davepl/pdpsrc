# rzsz

rzsz based on 3.48.

Run `make` to build on macOS or 2.11BSD/PDP-11, and `make clean` to remove
the generated programs. The default build includes `rz`, `sz`, `undos`,
`crc`, `minirb`, and their command aliases.

The Makefile recognizes both `pdp11` and `pdp-11` from `uname -m` and keeps
the native compiler's separate instruction/data space, small-memory, and
V7 terminal settings. macOS uses the POSIX terminal code and Clang's GNU89
mode for the historical K&R C sources. Some legacy compiler warnings remain.
