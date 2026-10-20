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
novi, rz/sz and transfer utilities, the read-only RA seek exerciser, the SMB2 file server, screen demonstrations, sieves, the native
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
program, `seektest`, `top`, and `webtop`, which require PDP-11 assembly or 2.11BSD kernel
interfaces. The other web helpers and HTTP servers still build, and the socket
client is selected for the running system. The complete set builds on the PDP.
`make clean` cleans every subproject on either host, including native-only
outputs left from a previous build.

Generated executables are ignored by Git so a checkout
cannot accidentally use Mac binaries on the PDP. Keep source files synchronized
between machines; compile the executables on their destination machine.

After building, install the finished programs as root:

```
make install
```

From `bsd`, this visits every subproject, including separately built
`pdpfetch` and `ksh`, and copies existing binaries to `/usr/bin`. Each
subproject also accepts `make install` (for `ksh`, select its platform
makefile as for building). Missing binaries are reported and skipped;
installation never rebuilds or removes the source-directory executables.
An installation error stops the recursive command. Install recipes run
in a fresh native shell and check failures explicitly, because make's
implicit error-exit option otherwise aborts on conditions used to skip files. The repository-root
`make install` delegates here as well.

Override `BINDIR` to choose another directory, or use an absolute `DESTDIR`
to stage the same layout elsewhere. For example:

```
make install BINDIR=/usr/local/bin
make install DESTDIR=/tmp/pdp-install
make install INSTALLDIRS=novi BINDIR=/usr/bin
```

Runtime tools, transfer aliases and the native socket diagnostics are included;
test executables, compiler helpers and the MACRO11 bare-metal sample are not
installed. To keep distinct programs from overwriting one another, the
assembly sieve installs as `asmsieve`, the 2.9BSD sieve as `krsieve29`, and
the website's HTTP server as `www-httpd`. The other programs keep their
binary names; `graphics/graphics` installs as `graphics`.

`sudo` is installed root-owned with mode 4755, and `top` retains its
root:kmem ownership and mode 2755. Other binaries use mode 755. These
privileged ownership settings also require root when staging those programs.
For the existing SMB service's directory, use
`cd smbd && make install BINDIR=/usr/local/sbin`.

Publishing website content remains an explicit `cd www && make install-site`
operation. The server's separate `install-service` target installs
`/usr/libexec/httpd` and prepares its log. The ksh `install-system` target
retains installation into `/bin` with its manual and login-shell registration.
Ordinary `make install` copies binaries only; it does not start services,
publish website content or change kernel configuration. The bug examples
intentionally demonstrate faulty C code; building or installing them does
not run them or remove their teaching examples.
