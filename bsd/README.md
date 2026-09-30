# Building the BSD tree

Build on the PDP-11 running 2.11BSD, using its native `make`, `cc`, `as`,
system headers, curses, termcap and math libraries, or on macOS with the
Xcode command-line tools:

```
cd ~/source/repos/pdpsrc/bsd
make
```

`make` (or `make all`) builds the programs supported on the current host in
sequence and stops with a nonzero status if any build fails. `make clean`
visits every subproject and
removes their generated programs, objects and assembly output. Running either
command from the repository root delegates to this directory.

The build includes the 2.9BSD sieve, MACRO11 tools and assembly sample,
native assembly sieve and attention program, both BASIC interpreters,
bug demonstrations, both Dhrystone versions, HTTP servers, menu, MQTT,
novi, rz/sz and transfer utilities, screen demonstrations, sieves, the native
socket client and diagnostics, sudo, top, wget, and the web helpers. The
MACRO11 sample produces a bare-metal image; building it does not run it.
Foreign socket clients, kernel source examples, archived websites and the
historical simulator/Fortran prototype images are not native BSD programs.
The socket server on the PDP displays PDP-11 and VAX packets; the displays
that require 64-bit register types remain available on modern hosts.
`cd socket && make test` checks the packet layouts on either host.

Two targeted compiler workarounds live in `tools/`. The socket layout test
uses the preprocessor's standard-output mode because the installed
preprocessor corrupts its intermediate source when given an output filename.
MACRO11's command parser also invokes the code-generation pass through a
shell wrapper to avoid a crash in direct driver invocation. Other sources
use the normal compiler command. No system compiler files are replaced.
MACRO11 keeps deeply nested parsers in smaller functions for this compiler.
The shared `pdp11_unistd.h` supplies native system-call declarations on PDP
installations whose system `unistd.h` refers to a missing `stdint.h`; modern
hosts continue to use their system header.

Focused checks are available with `make smoke` in `MACRO11`, `make test` in
`bsdbasic`, and `make test` in `socket`.

To rebuild one project, use its own directory, or select it explicitly:

```
make SUBDIRS=novi
make SUBDIRS=novi clean
```

On macOS, the same command builds all portable programs and the MACRO11
assembly sample. It reports skips for the native assembly sieve, attention
program, `top`, and `webtop`, which require PDP-11 assembly or 2.11BSD kernel
interfaces. The other web helpers and HTTP servers still build, and the socket
client is selected for the running system. The complete set builds on the PDP.
`make clean` cleans every subproject on either host, including native-only
outputs left from a previous build.

Generated executables are ignored by Git so a checkout
cannot accidentally use Mac binaries on the PDP. Keep source files synchronized
between machines; compile the executables on their destination machine.

These targets build and clean only. Installation, running services, kernel
changes and publishing website content remain separate operations. The bug
examples intentionally demonstrate faulty C code; building them does not run
them or remove their teaching examples.
