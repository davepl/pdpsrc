# MACRO11
MACRO11 is an PDP-11 cross assembler, written in C.
Projects files are included only for Win32 platform MS Visual Studio 2015, as it's been used as part of PDP11GUI.

## Building

From the repository root:

```
make
```

This builds `macro11` and `dumpobj` in `src`.

For a stricter portability check on macOS or another modern host compiler:

```
make portcheck
```

On 2.11BSD, build directly in `src` with the system `make` and `cc`:

```
cd src
make
```

This MACRO11 was forked from Richard Krebiehls version in 2008. Thanks!
[Richard's version is here.](http://www.dbit.com/pub/pdp11/macro11/).

Changes since then:
* restructured the sources
* bugfixes. For example "JMP Rn is illegal" on "jmp (rx)".
* added option "-listhex" to generate listings in hex instead of octal (helpful for work with modern logic analyzers).

Also see my [MACRO11 page](http://www.retrocmp.com/tools/macro-11-on-windows).

Apparently in 2009 my code was forked into github, without notifying me.
See github.com/shattered/macro11 !

The changes made there were also merged into this code in Nov 2016.
