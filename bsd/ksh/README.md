# pdksh for 2.11BSD on Mentec

This fork of [John D. Bruner's PDP-11 port](https://github.com/jdbruner/ksh)
adds the local compiler/runtime support used to build and run pdksh 5.2.14
on Dave Plummer's Mentec with **2.11BSD patch level 498**. Bruner did the
original PDP-11 port of the NetBSD shell; this fork adds the Mentec
stack-growth workaround, a reproducible build entry point, and regression tests.

## Reproduce the Mentec build

On a modern machine:

```sh
git clone https://github.com/davepl/ksh.git
cd ksh
# Verify the preserved compiler inputs (macOS; use sha256sum on Linux).
(cd native-tools/from-host && shasum -a 256 -c SHA256)
# Make a plain tar suitable for transfer to 2.11BSD.
git archive --format=tar HEAD > ../ksh-source.tar
```

Transfer `ksh-source.tar` in binary mode to your 2.11BSD system. As the
user who should own the source files:

```sh
mkdir ksh
cd ksh
tar xf ../ksh-source.tar
/bin/sh ./mentec-build.sh
./ksh
```

The build requires the native 2.11BSD C development tools and libc,
`make`, `mkdep`, `sed`, `awk`, `tbl`, and `nroff`. Patch level 498 is the
validated environment. Other patch levels, machines, and emulators have
not been validated with this workaround. The upstream build below has
its own requirements.

A fresh checkout contains the small **compiler bootstrap object snapshot**
and its source in `native-tools/from-host/`. Those `.o` files are intentional
build inputs: they allow private compiler passes to be linked before relying
on the compiler runtime affected by the stack problem. No Git LFS, downloads,
or modern build tools are required on the PDP-11. This is a native shell build
from source using preserved compiler objects, not a compiler bootstrap from
source alone. See [native-tools/README](native-tools/README) for provenance.

The script selects `Makefile.mentec`, rebuilds private compiler/runtime helpers,
cleans and compiles the shell, and runs **67 regression tests**. Expect
`67 passed, 0 failed`. Logs remain in `build.log`, `depend.log`, and `tests.log`.
If another `Makefile` is already selected, move it aside before running.
The stock `Makefile.2bsd` remains available for the upstream build path.
No system compiler, libc, or login shell is replaced by this build.

Two clean native builds of the validated shell produced identical binaries;
interactive editing, completion, history and job control also passed.
See [BUILD-RESULTS.txt](BUILD-RESULTS.txt) for the recorded binary hash and
[README.mentec](README.mentec) for the workaround and limitations.
The bootstrap snapshot is preserved unchanged; generated binaries and logs
are excluded from source control. Subsequent toolchain or source changes can
change the resulting executable hash.

## Attribution and contributions

Upstream baseline: `a7a376b650e1015afa044cbf3576ed78cfac5bd3` in
`jdbruner/ksh`. Original history and notices are retained. See
[LICENSE.txt](LICENSE.txt) and [native-tools/NOTICE](native-tools/NOTICE).

Contributions should describe the machine and 2.11BSD patch level tested,
include the regression result, and distinguish native from emulator tests.
The two small C compatibility edits are retained from the validated build;
testing whether they can be removed after the runtime fix is welcome.

---

The original upstream documentation follows.

## Overview

This is the version of ksh from NetBSD-5.0.2 (which is based upon the
public domain ksh pdksh), modified to build and run on 2.11BSD
(on a PDP-11). The NetBSD version of the source can be found
[here](https://archive.netbsd.org/pub/NetBSD-archive/NetBSD-5.0.2/source/sets/src.tgz).)

This version was chosen (vs newer versions of pdksh) because at that
point it had not evolved so much that back-porting to the PDP-11
would be more challenging. A more current version of pdksh is
[here](https://github.com/multishell/pdksh).

The primary changes for 2.11BSD from the NetBSD-5.0.2 version are
- use separated I/D overlay format
- change the definitions in config.h to reflect 2.11BSD on a PDP-11
  (config.h is now manually edited)
- fix BIT definitions for Tflag in table.h (when sizeof(int) == 2)
- work around some PDP-11 compiler "const" issues
- fix scripts for emacs.gen.out and siglist.out
  (to be compatible with 2.11BSD/cc and Linux/gcc)
- use signal list suitable for PDP11 (notably, includes SIGBUS, SIGIOT)
- don't declare functions accessed via pointers as static

There are eight overlays. The partitioning of .o files into overlays is
preliminary and is an area for further work.

Aside from code size issues, a notable concern with overlays is function
pointers that point to code in a different overlay. Cross-overlay
function calls are handled by a thunk that resides in the common
base area. Pointers to global functions point to those thunks
and work as expected. However, if a function is declared "static" then
there is no thunk and the pointer refers directly to the static
function address. Calling through that pointer from another overlay will
have unpredictable (but certainly undesirable) results. For that reason,
a number of previously-static functions (notably including functions
used as signal handlers) are now declared non-static. It is likely
that not all such cases have been found yet.

This generally depends upon a 2.11BSD system at patch level >= 490.
In particular, it depends upon ANSI (C89) compiler support for
function prototypes, const, and volatile. It is possible to configure
it to build without those, but this is untested. Doing so may require some
additional tweaking. The baseline pdksh code originally built in such an
environment, but the NetBSD-5.0.2 version appears to include some later
changes that are dependent upon C89.

## Build/Install

There are two sets of platform-dependent files (2BSD and Linux):
- Makefile.2bsd , siglist.2bsd.in , config.2bsd.h
- Makefile.linux , siglist.linux.in , config.linux.h

The Linux variant is intended as a comparable version for investigation
and/or debugging of issues with the PDP-11 version, specifically for
a Raspberry Pi for comparison to ksh on a PiDP-11/70. (If running ksh
on Linux is the primary objective, consider using one of the more
recent versions of ksh instead.)

### Build/Install on 2.11BSD
```
ln -s Makefile.2bsd Makefile
make depend
make
```
To install the executable (in /bin) and the manual page, and add
/bin/ksh to /etc/shells so that ksh can be used as a login shell:
```
make install
```

### Build on Linux
```
ln -s Makefile.linux Makefile
make depend
make
```