# attn

This directory contains a first native 2.11BSD `as` port of the ATTN11 project by
Damien Boureille at https://github.com/dbrll/ATTN-11

What changed:
- flattened the MACRO-11 include tree into one BSD-`as` source file: `attn.s`
- replaced the DL11 polled console routines with BSD user-mode `_write`/`_exit` based routines
- removed the bare-metal fixed origin and reset-vector startup model
- kept the model/math code in PDP-11 assembly

Important build assumption:
- the current port still contains self-patching parameter blocks inside the instruction stream
- because of that, the makefile intentionally uses the default impure link style on 2.11BSD
- do not switch this build to pure text or separate I/D yet

Build on 2.11BSD (ie: on a PDP-11 or simH)

```
cd attn
make
./attn
```

Build on macOS:
- macOS cannot assemble/link PDP-11 objects with its native toolchain
- `make regen` only refreshes `attn.s` from the `sample/` sources

Files:
- `attn.s`: translated BSD-`as` source
- `generate.py`: regeneration script used to derive `attn.s` from `../sample`
- `makefile`: 2.11BSD build

Known risks in this first cut:
- writable parameter blocks should eventually move into `.data`/`.bss`
