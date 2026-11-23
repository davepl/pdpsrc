# Host-side native build generators

Run these with Python 3 and Apple clang (or clang with working host headers)
from `bsd/links`. They generate C source for the native compiler; they do not
cross-compile PDP-11 binaries. Never infer native object size from host objects.

## Regenerating headers

`upstream/links-full.h` is the canonical header. `upstream/links.h` is the
host/native selector. Do not edit generated `links-native.h` directly.

```
python3 tools/generate_language.py upstream
python3 tools/prune_header.py upstream/links-full.h upstream/links-native.h --extern-inline
python3 tools/generate_native_sources.py upstream
```

Run again after changing canonical declarations, source references, inline
functions, or language IDs. Upload the regenerated files together with the
changed sources before rebuilding native objects. Host `config.h` must exist
for clang analysis (run the normal host configure/build first).

The pruning tool preserves original source declarations and conditional
branches. It guards common function/variable declarations by per-unit identifier
closure, including header macro dependencies; it retains structs and typedefs.
Native-only declarations not visible in the host AST remain untouched. This is
conservative, not a complete proof of native preprocessing equivalence. Compile
all native units after regeneration. Unknown dependencies in included `.inc`
files or platform-specific macros may need explicitly retaining declarations.

`--extern-inline` emits typed extern declarations in native units and one copy
of the formerly-inline bodies when `PDP11_UNIT_helpers` is defined. Compile
`helpers.c` once and include its object once in the link. Each ordinary native
unit needs `-DPDP11_UNIT_name`, as provided by `Makefile.211bsd`.

The language tool reads the full non-native define tail in `language.h` as the
stable ID registry. It retains numeric IDs, emits only direct per-source `T_*`
references for native units, and replaces identifiers outside strings/comments
in `language-small.inc` to generate `language-native.inc`. In particular,
`language.c` no longer requires every translation ID as a preprocessor macro.
Generation is deterministic and idempotent for unchanged inputs.

## Optional oversized-module splitter (not in the default build)

Prefer measuring all native objects and choosing overlay banks first. 2.11BSD
overlays are not universally limited to 8 KB each: the maximum overlay and base
are rounded to hardware segments and must fit the available instruction space.
Consult the native linker documentation and final linker results.

```
python3 tools/split_unit.py upstream/bfu.c /tmp/bfu-split
```

This emits one data-owner unit and one unit per function, a shared source
include, and a JSON manifest identifying each object. Compile `bfu_000.c` etc.
with native flags and an include path to upstream. Link **all** these objects
once in place of `bfu.pdp.o`, then pack their measured text sizes into overlays.
Do not also compile the original module or diagnostic `*-lifted.c` file.

`bfu` splitting passed complete host compile/link and renderer smoke tests.
The audited bfu/types function archives are now used by the native build; this
does not validate arbitrary modules or optional static-symbol lifting. The optional static-symbol
lifting resolves identifier references through clang AST nodes and gives each
file-scope private symbol one shared short external name, preserving function
pointer identity and shared state. It is experimental, not generally validated.
The tool rejects unsupported declarations instead of silently guessing:
conditional directives crossing signatures (`view.c`'s `srch_cmp`), anonymous
record variables (`html.c`'s `roman_tbl`), and problematic macro references need
manual normalization before splitting. Generate into a fresh temporary folder;
a failed run can leave diagnostic files, which must not be compiled blindly.

## Literal translation IDs for native compilation

`generate_native_sources.py` writes `<unit>.native.c` beside each canonical
`<unit>.c`, preserving original quoted-include paths. It substitutes translation
identifiers with stable numeric IDs outside comments and string/character
literals and defines `PDP11_LITERAL_IDS` before the source. Regenerate after
any source edit. Do not edit these generated copies.

The native branch of `language.h` must guard its per-unit `T_*` macro sections
with `#ifndef PDP11_LITERAL_IDS`. Keep nonnative definitions and N_LANGUAGES
available as before. Native make rules compile `menu.native.c`, for example,
with `-DPDP11_UNIT_menu`, not `-DPDP11_UNIT_menu.native`. This removes the
large translation macro population that exhausts the native preprocessor.
The English native translation table remains numerically generated separately.

All generators ignore `*.native.c` when enumerating canonical source units.
Regeneration order is language registry/table, pruned header, then native source
copies. The language generator emits the literal-ID guard on every regeneration.
The literal-ID copies passed full host compile/link and local/HTTP smoke tests;
native compilation is still the authoritative memory/ABI test.

## Reduced-profile native function libraries

```
python3 tools/generate_function_libraries.py .
cd upstream
make -f ../native-lib/Makefile
```

This produces `native-lib/libbfu.a` and `native-lib/libtypes.a`, with one member
per function and one shared global-data member for each module. It is restricted
to these audited modules; `default.c` contains macro-generated declarations and
is not supported automatically. Generated source has literal translation IDs
and retains the original unit macro for native declaration selection.

Replace the monolithic bfu/types objects in the native link with these archives,
placed after their callers. Never link both the originals and archive copies.
If the historical linker's archive search leaves cross-library dependencies
unresolved, repeat archive references in the intended overlay group or group
members in one archive; do not force-load everything, which defeats elimination.
Measure native members with `make -f ../native-lib/Makefile sizes` and inspect the
final native executable size/map to determine savings. Disabled UI functions can
only disappear when no live initializer, callback table or function refers to
them. Shared global initializers can themselves retain referenced callbacks.
No fixed native saving is guaranteed. The complete host browser linked using
both archives and passed the rendering/HTTP smoke suite; native archive linkage
and the reduced feature profile still require target verification.

## libc overlay safety

The historical overlay thunk skips a function's initial four-byte `jsr r5,csv`
and simulates that prologue. Assembly leaf routines cannot be placed behind
such thunks. `split_libc.py` parses native ar/a.out and verifies every exported
text entry's opcode and external `csv` relocation. It emits an audit report,
root members and a safe archive (run native ranlib before linking).

`prepare_libc.sh` is the checked-in native, no-Python manifest for the inspected
Minerva `/lib/libc.a`. From upstream run `sh ../tools/prepare_libc.sh`. It copies
the original archive, removes all audited unsafe members from that private
copy and extracts unsafe members into `native-tmp/libc-root`. It excludes
`csv.o` and `mon.o` from explicit root objects because the custom overlay csv
and crt0 already supply their root symbols. Link all paths in
`native-tmp/libc-root.list` before the first overlay and private `libc-safe.a`
after the browser overlays. The original libc is never changed.

The conservative root list is 3,470 text bytes, 18 initialized data bytes and
2 BSS bytes, before any referenced additional safe C dependencies. Relative
to the inspected browser executable it introduces getlogin, setjmperr, acvt
and ecvt dependencies. Re-audit/regenerate this manifest if the system libc
changes. Future use of profiling routines requires reconsidering mon.o.

The preparation script also builds `native-tmp/libc-root.a` for selective
extraction. Prefer placing safe libc in its overlay after browser objects,
then `-Y ../native-tmp/libc-root.a` to return to root text for assembly entries.
This avoids force-linking every syscall. The previously inspected browser's
70 used unsafe members totaled 1,728 text bytes and introduced no references
back into the safe archive. Actual selection can change with source changes.

Potential unsafe-to-safe references in the entire audited archive are limited
to `___acvt`, `__logname_valid`, `_ecvt`, `_fcvt`, and `_longjmperror`. If a new
profile leaves one unresolved after selective root extraction, add `-u symbol`
before the safe archive on the next link. Do not force all of them routinely:
that retains otherwise unused float-formatting, login, and error code.

## Private native linker and compiler

`ld211.c` is the target's 2.11BSD linker source with a narrowly scoped correction:
`finishout()` now tests the symbol type for equality with `N_EXT+N_TEXT`, not
bitwise overlap. BSS symbols with stale overlay fields formerly caused extra
uncounted thunks, shifting the output file by 64 bytes in the tested link.
The helper uses project-local temporary storage and the repository's compatible
unistd declarations; it links against the normal native libvmf. It is never
installed over the system linker.

`cc211.c` changes only compiler temporary-file location to `native-tmp/`.
`copy_mtime.c` is an optional reference-mtime helper because Minerva's old touch
has neither `-r` nor modern time arguments. These do not alter the system clock.

Native sources must be regenerated after canonical C changes:

    python3 tools/generate_compact_language.py
    python3 tools/generate_native_sources.py upstream
    python3 tools/prune_header.py upstream/links-full.h upstream/links-native.h --extern-inline
    python3 tools/generate_function_libraries.py . bfu types

These are developer-side Python/clang steps; generated outputs ship with the
port. Do not require these modern tools on the PDP itself.

## Native runtime memory verification

The delivered native build renders the live FrogFind homepage, submits a PDP-11
search, scrolls results, returns Back with the query preserved, and resubmits.
Repeated-navigation forced reload remains a documented heap limitation. See
BUILD-RESULTS.txt for clean-build evidence, native regression results and exact
artifact hashes. Host tests are separate from native runtime evidence.

The profile retains neither unreferenced raw responses nor formatted pages.
Before navigation, the outgoing presentation is detached while history retains
form values and scroll/link positions. Forced reload also collects unlocked raw
responses before starting the new fetch. History can therefore trigger another
fetch. Wrapped display rows shrink their
old buffers immediately, preserving the aligned capacity assumed by later row
growth. Bounded ANSI screen-output streaming in terminal.c avoids allocating
one large whole-screen redraw string. These
are peak-memory reductions, not promises that arbitrary pages fit; the complete
response still exists in memory while formatting.

Native deployment requires both `links-pdp` and its generated `language.dat`
sidecar. Preserve the sidecar when copying or installing an executable. Follow
the main README for executable-relative lookup and `LINKS_DATA_DIR`; modern
host builds do not require it. Regenerate the sidecar with
`python3 tools/generate_language_data.py` when changing the native English UI
labels, and deploy the matching generated language headers and sidecar together.
