# License and attribution

This directory is a port of tmux 1.1 to 2.11BSD. It combines four bodies of
code with different owners. No single license covers all of it; each part
keeps the terms it came with. All of them are permissive.

| Part | Where | Terms |
| --- | --- | --- |
| The 2.11BSD port | Everything not listed below | ISC (this file, section 1) |
| tmux 1.1 | `upstream/`, and files generated from it | ISC, per file (section 2) |
| Compatibility code shipped with tmux | `upstream/compat/`, and files generated from it | ISC, BSD 2-clause or BSD 3-clause, per file (section 3) |
| Build tools taken from 2.11BSD | `tools/cc211.c`, `tools/ld211.c`, `tools/archive.h` | Caldera license and the Berkeley license (section 4) |

Where a file carries its own copyright and license notice, that notice governs
the file. Keep every notice in place when copying or changing a file.

## 1. The 2.11BSD port

This covers the material written for the port: the native adapters and header
shims in `native/`, the generators and overlay planner in `tools/*.py`, the
makefiles, link scripts and compiler wrappers, `tools/prepare_libc.sh`, the
tests in `tests/`, the documentation, and `upstream/tmux.h` and
`upstream/config.h`. These files carry no notice of their own.

Changes made for the port inside a file that carries another notice (most of
`upstream/`, and the three files from 2.11BSD) are offered under the terms of
the file they change, so that no file ends up under two licenses.

    Copyright (c) 2026 David W Plummer

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

## 2. tmux 1.1

`upstream/` is the tmux 1.1 source, released November 5, 2009
(<https://github.com/tmux/tmux/tree/1.1>, commit
`09c558685a0e9a1ec6ae61ed04e8a0955ba97967`), with the port's changes.

tmux is the work of Nicholas Marriott and its contributors. The copyright
lines in this release name Nicholas Marriott (2004 to 2009), Tiago Cunha
(2008, 2009), Joshua Elsasser (2009) and Todd Carson (2009). Each source file
states its own holder and carries the ISC license, in this form:

    Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
    IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
    OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

`upstream/NOTES` states the terms for the files without a header: `NOTES`,
`CHANGES`, `FAQ` and `TODO` are under the ISC license, and the files under
`upstream/examples/` remain the copyright of their authors, who gave
permission for them to be distributed with tmux.

These files are generated from tmux sources and keep the notice of the file
they come from:

- `native-src/`: copies of `upstream/*.c` and `upstream/compat/*.c` with
  debug trace calls removed (`tools/generate_native_sources.py`).
- `upstream/tmux-native.h`: `upstream/tmux-full.h`, which is tmux's `tmux.h`,
  with declarations guarded per compilation unit
  (`tools/generate_native_header.py`).
- `native/utf8-width.inc`: the width table of `upstream/utf8.c` as code
  (`tools/generate_native_widths.py`).

## 3. Compatibility code shipped with tmux

tmux 1.1 ships these files in `compat/`. Each carries its complete notice.

| Files | Copyright holder named in the file | License |
| --- | --- | --- |
| `asprintf.c`, `forkpty-aix.c`, `forkpty-sunos.c` | Nicholas Marriott | ISC |
| `strlcpy.c`, `strlcat.c` | Todd C. Miller (1998) | ISC |
| `strtonum.c` | Ted Unangst and Todd Miller (2004) | ISC |
| `imsg.c`, `imsg-buffer.c` | Henning Brauer (2003, 2004) | ISC |
| `imsg.h` | Pierre-Yves Ritschard, Reyk Floeter, Henning Brauer | ISC |
| `bsd-poll.c` | Darren Tucker (2004, 2005, 2007) | ISC |
| `tree.h` | Niels Provos (2002) | BSD 2-clause |
| `bsd-poll.h` | Theo de Raadt (1996) | BSD 2-clause |
| `queue.h`, `getopt.c`, `vis.c`, `vis.h`, `unvis.c`, `daemon.c`, `strsep.c` | The Regents of the University of California | BSD 3-clause |
| `bitstring.h` | The Regents of the University of California; contributed by Paul Vixie | BSD 3-clause |
| `strcasestr.c` | The Regents of the University of California; contributed by Chris Torek | BSD 3-clause |
| `fgetln.c` | The NetBSD Foundation, Inc. (1998); contributed by Christos Zoulas | BSD 3-clause |

`native/trees/*.inc` are the red-black and splay tree macros of `tree.h`
expanded ahead of time for the native compiler
(`tools/generate_native_trees.py`). They carry Niels Provos's notice.

## 4. Build tools taken from 2.11BSD

Three files in `tools/` come from the 2.11BSD system sources. They build the
port; none of their code is part of tmux.

| File | Origin | Local changes |
| --- | --- | --- |
| `tools/cc211.c` | `/usr/src/bin/cc.c`, the C compiler driver, `cc.c 5.1 (2.11BSD)` | Temporary-file prefix |
| `tools/ld211.c` | The 2.11BSD linker, `ld.c 4.6 1998/01/19`. Its change history names Tim Shoppa and "sms" (Steven M. Schultz). | Overlay thunk accounting in `finishout()`; scratch-file path |
| `tools/archive.h` | `archive.h 5.8 (Berkeley) 4/12/91`; contributed to Berkeley by Hugh Smith | None recorded |

2.11BSD descends from Research UNIX Version 7 by way of the Berkeley 2BSD
releases, and is maintained by Steven M. Schultz. Two sets of terms apply to
these files.

**Caldera.** The code inherited from Research UNIX is distributed under the
license Caldera International granted on January 23, 2002 for the 16-bit UNIX
releases and 32V. The copy held by the Unix Heritage Society at
<https://www.tuhs.org/Archive/Caldera-license.pdf> is the authoritative one;
its terms are:

    Caldera International, Inc. hereby grants a fee free license that
    includes the rights use, modify and distribute this named source code,
    including creating derived binary products created from the source code.
    The source code for which Caldera International, Inc. grants rights are
    limited to the following UNIX Operating Systems that operate on the
    16-Bit PDP-11 CPU and early versions of the 32-Bit UNIX Operating System,
    with specific exclusion of UNIX System III and UNIX System V and
    successor operating systems:

        32-bit 32V UNIX
        16 bit UNIX Versions 1, 2, 3, 4, 5, 6, 7

    Caldera International, Inc. makes no guarantees or commitments that any
    source code is available from Caldera International, Inc.

    The following copyright notice applies to the source code files for
    which this license is granted.

    Copyright(C) Caldera International Inc. 2001-2002. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    Redistributions of source code and documentation must retain the above
    copyright notice, this list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

    All advertising materials mentioning features or use of this software
    must display the following acknowledgement:

        This product includes software developed or owned by Caldera
        International, Inc.

    Neither the name of Caldera International, Inc. nor the names of other
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

    USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
    INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
    NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR ANY DIRECT,
    INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

**Berkeley.** The Berkeley work in these files is under the license of The
Regents of the University of California. `tools/archive.h` carries it in
full. The University withdrew that license's third clause, the advertising
requirement, on July 22, 1999 for code it holds the copyright in. That
withdrawal does not change the Caldera terms above.

    Copyright (c) 1991 The Regents of the University of California.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. All advertising materials mentioning features or use of this software
       must display the following acknowledgement:
         This product includes software developed by the University of
         California, Berkeley and its contributors.
    4. Neither the name of the University nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.

## Binaries

This directory holds source only. A `tmux` built from it on 2.11BSD is linked
statically with that system's C library and termcap library, so the
executable also contains 2.11BSD code under the terms of section 4. Those
terms ask anyone distributing such an executable to reproduce the two notices
above in the accompanying documentation.

## What is not here

Tools used while developing the port, such as the SIMH simulator and the host
compilers, are not part of this directory and contribute no code to it.
