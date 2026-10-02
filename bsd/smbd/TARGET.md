# Native target evidence

Verified on 2026-10-01 against the user-authorized `minerva` at
`192.168.1.26`, using Telnet port 23 and FTP port 21. Builds and exported
fixtures are confined to the project test tree, initially `/tmp/smbd-test` and now
`/usr/tmp/smbd-test`; no startup files or existing services were changed.
The server was tested in the foreground, then left as a temporary background
process using the installed `nohup`; no boot-time service was installed.
Native administrative credentials are not included in this repository.

The current native build supports signed read/write access, share enumeration,
and the persistent `-M` timestamp table. Its latest measurements and reconnect
checks are recorded under [Reconnect and timestamp completion](#reconnect-and-timestamp-completion),
following the Finder upload/Windows writable results. Earlier sections
retain measurements and observations from the stated implementation milestones.

## Actual environment

| Item | Verified result |
| --- | --- |
| `/VERSION` | 2.11BSD patch level **499**, dated January 2, 2026 |
| Kernel | `2.11 BSD UNIX #39: Tue Sep 29 17:02:20 PDT 2026`, `root@minerva:/usr/src/sys/MINERVA` |
| `sysctl hw.machine`, `hw.model` | `pdp11`, `83` |
| `sysctl hw.physmem` | 4,186,112 bytes |
| Compiler | Native `/bin/cc` with `/lib/cpp`, `/lib/c0`, `/lib/c1`, `/lib/c2` |
| Build tools | Native `make`, `cc -O`, separate I/D link with `cc -i`, `size`, `nm` |
| Syntax checked | K&R function definitions, ANSI parameter declarations/prototypes, `const`, `unsigned long` |
| Header inspection | `signed` and `volatile` compiler keywords present; `stdint.h` exists but its 64-bit typedefs are disabled under `#ifdef notyet` |

The repository's older `top` records refer to patch levels 481 and 498;
those are historical, not the running target's version. A separate source
checkout at `/Users/dave/source/repos/2.11BSD` identifies itself as patch
431, so it was not treated as the authority for installed headers. Its old
compiler lacks the later ANSI extensions. This server is verified with the
installed patch-499 compiler; older compiler support is unverified.

The live native [`target-probe.c`](tests/target-probe.c) printed:

```
sizeof: char=1 short=2 int=2 long=4 pointer=2
sizeof: off_t=4 time_t=4 size_t=2 ssize_t=2
unsigned long 0x12345678 memory bytes: 34 12 78 56
unsigned long shift: 1234
limits: MAXPATHLEN=256 MAXNAMLEN=63 NOFILE=30
sizeof: stat=52 direct=70 DIR=592 fd_set=4
char signed: 1
```

Native `long`, `off_t`, and `time_t` have 32 bits. Integers and pointers
have 16 bits. A native long stores its high 16-bit word first, with each
word little-endian; it must never be copied directly to an SMB wire field.
`off_t` and `time_t` are signed `long` typedefs.

## Interfaces actually inspected

Native headers and libc provide IPv4 sockets, `select`, `fork`, `wait3`,
`waitpid`, `sigaction`, `chroot`, `fchdir`, `setuid`, `setgid`, `lstat`,
`opendir`, `readdir`, `telldir`, and `seekdir`. Directory entries are
`struct direct` from `<sys/dir.h>`, with 63-byte names. `closedir` returns
`void`. `struct statfs` from `<sys/mount.h>` supplies `f_bsize`, `f_blocks`,
and `f_bavail`; `fstatfs` is used by the compiled server.
The installed `struct stat` has access, modification, and change times,
but no birth time or inode generation field (`st_birthtime`/`st_gen`).

The native `time.h` lacks an include guard and must not be included both
directly and through `sys/time.h`. `memmove` was absent from libc; the
server uses its own bounded overlapping-copy loop where required.
The installed `tmpfile` source (revision 8.2, 2025/12/25) calls `mkstemp`,
unlinks the new file immediately, and uses `fdopen`; it does not use an
unchecked `mktemp`/`fopen` sequence. The listener creates spool files before
workers enter the share's chroot.

## Initial read-only build and memory measurements

The read-only milestone build with SMB2 bootstrap negotiation, multiple
tree IDs, and the Windows directory-reopen flag correction produced:

```
text    data    bss     dec     hex
44288   2844    28476   75608   12758
```

The executable was 66,213 bytes including headers/symbols. Separate I/D
linking provides distinct text and data spaces; no overlays were needed.
Static data plus BSS is 31,320 bytes, leaving 34,216 bytes of the 65,536-byte
data space for heap and stack. This is a calculated allowance, not a
measured peak stack watermark. Heap use includes stdio and directory
objects. Server limits and fixed buffers still matter despite physical RAM.

The native cryptographic known-answer executable passed MD4, MD5, SHA-256,
streaming hashes including one million bytes, HMAC-MD5, HMAC-SHA256,
NT password hash, and NTLMv2 response-key vectors. It measured 11,456 bytes
text, 2,738 data, and 22 BSS. The type probe measured 4,096 text, 628 data,
and 20 BSS.

The native wire test also passed fixed byte vectors for 32/64-bit wire
encoding and FILETIME conversion at Unix seconds 0, 1, 1,700,000,000,
2,147,483,647, and the documented negative-time clamp. Its expected bytes
were calculated independently on the development host. It measured 5,376
text, 542 data, and 22 BSS.

## Reproduce

From the host, with Python 3, `expect`, and `telnet` installed:

```
python3 tests/target.py probe --host 192.168.1.26 --user root
python3 tests/target.py build --host 192.168.1.26 --user root
python3 tests/target.py build --incremental --host 192.168.1.26 --user root
python3 tests/target.py crypto --host 192.168.1.26 --user root
python3 tests/target.py wire --host 192.168.1.26 --user root
python3 tests/target.py password --host 192.168.1.26 --user root
python3 tests/target.py flags --host 192.168.1.26 --user root
```

The helper prompts for the password, or accepts `PDP11_PASSWORD` from the
environment. It stores no password and suppresses login output. FTP/Telnet
are the target's existing cleartext laboratory transports. The default
remote tree is `/usr/tmp/smbd-test/bsd/smbd`; `--remote` can select another
explicit tree below `/usr/tmp` or `/tmp`. `build` uploads source and builds; it does not
start or install a service. Stop a server running that same test-tree executable
before rebuilding it. Uploads are staged as immutable byte snapshots,
with a printed source SHA256; edits detected while snapshotting abort the
upload. The source glob includes every server module, including `session.c`.
`--incremental` compares the captured source bytes with the remote files,
uploads only changes, and runs `make` without `make clean`. `crypto` and `wire`
run against the previously uploaded source tree. `password` uploads and builds
the standalone password utility. Native manual commands are simply `make`, `size smbd`, and
`cc -O -i -o crypto-test tests/crypto_test.c crypto.c wire.c`.

## Other machines inspected

The development host is macOS 26.5.2, build 25F84, ARM64, with Apple clang
21.0.0, `smbutil`, and `mount_smbfs`. No client policy was changed by these
target probes. The Docker CLI exists but its daemon is not running; no
usable existing Linux client environment was identified here. The user
subsequently provided Windows `hpz2` access, verified below. The main README records the client tests actually run.

A later read-only inventory found an existing `Ubuntu` WSL distribution on
`hpz2` and Linux `6.8.0-142-generic` on `ubvmdell`. Neither had `smbclient`
or `mount.cifs` in PATH or their standard `/usr/bin`, `/usr/sbin`, and
`/sbin` locations. No packages or client settings were changed; a separate
Samba/Linux-kernel client test remains unperformed.

The old documented localhost Telnet 2323/FTP 2121 endpoints refused
connections. SIMH and disk images are available locally. A private cloned
root image booted successfully in SIMH, then was stopped after authorized
live-target access became available. The original images were not changed;
no simulator result is substituted for the native live-target build.

## Initial Windows native client

`hpz2` reports **Microsoft Windows 11 Pro, 10.0.26200, build 26200**.
The user's existing SSH key allowed `dave@hpz2` access. The client settings
were read, not changed:

```
EnableSecuritySignature   : True
RequireSecuritySignature  : True
EnableInsecureGuestLogons : False
Smb2DialectMin            : 0
Smb2DialectMax            : 65535
```

A temporary `net use \\192.168.1.26\pdp /user:pdp /persistent:no`
connection authenticated successfully with the disposable laboratory
account. PowerShell `Get-ChildItem` listed the root and nested directory,
confirmed the empty directory and zero-byte fixture, and `Copy-Item` copied
the 262,400-byte binary file. The local Windows `Get-FileHash -Algorithm
SHA256` result matched the original exactly:

```
85e1298a87a2077b5de87c6ea60e77be9ceba06f955c51b73676ee5a71f1187f
```

`Get-SmbConnection` reported `Dialect: 2.0.2`, `Signed: True`, and
`Encrypted: False`. Its `UserName` field identifies the owning Windows
logon (`HPZ2\dave`); the `net use` command supplied the SMB test account
`pdp`. The complete test returned success, and the connection was removed
after the check. Local copied files inherit the
share's read-only attribute, so the test removes only its own temporary
copy using `Remove-Item -Force`. The reusable
[`windows-client.ps1`](tests/windows-client.ps1) prompts for the SMB password
and leaves client signing and guest policies intact.

Windows initially requests directory information class 81. The server
returns `STATUS_NOT_SUPPORTED`, and Windows retries with class 37 plus
`SMB2_REOPEN` (`0x10`); this fallback is now verified. No implementation of
class 81 was needed for browsing and copying.

The FTP server's default upload mode was 0640 for files and 0751 for
directories. Test export files were explicitly made 0644 and directories
0755 so the configured `nobody` worker could read them. Password and random
pool files remained 0600 outside the export.

## Native password administration

The installed `/usr/src/lib/libc/gen/getpass.c` still uses a nine-byte
static buffer, silently limiting input to eight characters. `smbpwd` therefore
uses a bounded no-echo terminal reader with native `sgtty` CBREAK mode and
manual Backspace/Ctrl-U handling. This also bypasses the tty canonical-line
limit. Host builds use the corresponding noncanonical `termios` mode.

The native utility compiled without warnings after adapting signal-pointer
declarations to the installed headers. Its measured size was:

```
text    data    bss     dec     hex
12672   1590    94      14356   3814
```

An interactive native test entered the same 256-character disposable password
twice, then compared the resulting file against an independently calculated
Cryptodome MD4/UTF-16LE result: exact match. A 257-character input was rejected
and the previous file remained unchanged. The tool also created the existing
disposable SMB test account's hash file at `/tmp/smbd-test/password.hash`;
`ls -l` verified a root-owned, mode-0600, 33-byte file. Test comparison files
were removed. No password or generated hash was printed.

`python3 tests/password_test.py` reproduces the host pseudo-terminal checks:
known hashes, create/update, no echo, permissions, maximum length, mismatch,
and unsafe existing-file/parent-directory refusal. It needs access to
`/dev/tty` for the private pseudo-terminal. The native helper's `password`
action builds the utility; interactive execution in the current test tree is
`./smbpwd /usr/tmp/smbd-test/password.hash`. Restart the daemon with `-H` to load
the new credential.

The installed `/usr/src/bin/ld/ld.1` confirms that native automatic overlays
use `-i`, repeated `-Z` groups, and a final `-Y` before base libraries. Base
text and the largest overlay each round up to an 8192-byte segment; their
sum must fit 65536 bytes. The expanded server uses this overlay format;
the integrated measurements are recorded below.

## Native share enumeration

The integrated server's `IPC$` / `srvsvc` endpoint was tested on native
port 445 using the disposable test account and signed SMB 2.0.2 sessions.
`tests/rpc_test.py --host 192.168.1.26 --port 445` passed share enumeration
levels 0, 1, and 2; share information levels 0, 1, 2, and 1005; preferred
length and resume handling; bounded DCE fragmentation; requests split
across SMB writes; pipe transceive and peek; pipe-handle limits; invalid
fragment lengths; and RPC fault recovery.

The built-in macOS client command
`smbutil view //pdp@192.168.1.26` successfully listed `pdp` as a disk share
with comment `PDP-11 files`, and `IPC$` as a pipe share with comment
`Remote IPC`. An earlier loopback test using a custom TCP port failed in
macOS before creating the RPC pipe; the normal native port-445 test passed.

On Windows `hpz2`, a temporary authenticated
`net use \\192.168.1.26\IPC$ /user:pdp /persistent:no` connection followed
by `net view \\192.168.1.26` successfully listed the `pdp` disk share and
its comment. `Get-SmbConnection` reported dialect `2.0.2` with
`Signed: True` for this IPC session. Signing and guest-access policies
were unchanged. These are command-line share-browsing checks; desktop
Finder and Explorer tests are recorded separately.

The same RPC suite passed against the host build under AddressSanitizer
and UndefinedBehaviorSanitizer. `tests/rpc_fuzz.c` also passed every byte
split of valid bind and share-enumeration requests and 20,000 deterministic
bounded mutation/truncation cases under both sanitizers. This checks
parser bounds and state invariants; it is not a claim of exhaustive
protocol fuzzing.

## Integrated writable server and native overlays

The larger filesystem/RPC build compiled all six native C modules, but the
ordinary separate-I/D link exceeded the 64 KB text space. The native Makefile
now links base `smbd.o wire.o crypto.o`, first overlay `fs.o`, second overlay
`rpc.o auth.o`, then `-Y` before the C library. Native `size` reported:

```
text    data    bss     dec     hex
30976   3164    37922   72062   1197e   total text: 66560
        overlays: 20096,15488
```

This snapshot's executable was 95,818 bytes. Resident text capacity is
32768 bytes for the rounded base plus 24576 bytes for the largest rounded
overlay: 57344 bytes. Static data+BSS is 41086 bytes, leaving 24450 bytes
for heap and stack; this remains an allowance, not a measured peak. Native
`checkobj` was unavailable; `ld` accepted the layout and the executable ran.
The source upload snapshot was
`0248a5cb0259ddac2aabea019cf4e2fa801a35faefc3e3e6e48daddae80be809`;
the overlay command was first applied manually to that compiled snapshot,
then made the native Makefile default.

The root filesystem has only 7816 KB total. Compiling the larger module
while the test tree was under `/tmp` exhausted it. Only our stopped build's
processes and artifacts were touched. The test tree was relocated to
`/usr/tmp/smbd-test`, restoring 835 KB free on root; `/usr` had approximately
1.27 GB available. The entropy pool and credential files were moved, keeping
their consumed state and mode 0600. Sources/fixtures were verified in the new
tree before the old project tree was removed. Native `cc` still uses `/tmp`
for its own temporaries; the remaining space was enough after relocation.

The running test server uses `-H /usr/tmp/smbd-test/password.hash`,
`-R /usr/tmp/smbd-test/random`, `-T /usr/tmp`, `-w`, port 445, and eight workers.
The Unix account is `nobody`; the disposable export was changed to that
owner, retaining directory 0755/file 0644 modes. Credential/pool files remain
root-owned mode 0600 outside the chroot. `-T /usr/tmp` keeps streamed request/response
spools and the shared worker registry off the small root filesystem.

Against that native server, both independent host libraries passed signed
NTLMv2 listing and exact copying of zero-byte, small, and 262400-byte files.
The writable suite also passed native streamed upload/readback, FLUSH,
in-place update, truncation, rename, replacement, deletion, mkdir/rmdir,
cross-connection sharing conflicts in both directions, delete pending until
the last close, and open-handle identity across a cross-worker rename. Each
test removed its own temporary files. These are live-target results, not
host-only results.

A subsequent native rebuild included prompt transport closure after failed
NTLM authentication and preserved the same measured sizes. Its source
snapshot was
`4e332f5a6ec4b5e3b3e8bddbd8e91c5dfe09d0dad084b6fbc3dcf65c6d61f681`.
The focused live regression received the complete `STATUS_LOGON_FAILURE`
response, then EOF within two seconds while the rejected client socket
remained open, then completed a fresh authenticated login and verified its
signed ECHO response.

The built-in Windows 11 client then passed
`tests/windows-client.ps1 -Writable` against that server: share enumeration,
root/nested/empty-directory checks, zero-byte fixture, exact 262400-byte
download and upload/readback hashes, file rename/delete, directory
create/rename/remove, and cleanup of its own files and SMB connection.
`Get-SmbConnection` again reported dialect `2.0.2`, `Signed: True`, and
`Encrypted: False`. One post-upload `SET_INFO` returned
`STATUS_NOT_SUPPORTED`; Windows `Copy-Item` tolerated that metadata rejection
and the byte-hash and subsequent operations passed. This test establishes
byte-copy compatibility, not preservation of all Windows timestamps and
attributes. The temporary Windows script was removed afterward.

## Native persistent inode flags

The installed libc provides both `chflags` and `fchflags`. The live
`sys/stat.h` defines owner-changeable `UF_SETTABLE` as `0x00ff`; its named
user flags are NODUMP (`0x0001`), IMMUTABLE (`0x0002`), and APPEND (`0x0004`).
`SF_ARCHIVED` is `0x0100` and cannot be changed by an unprivileged worker.
The remaining owner bits are unnamed on this target. Their meanings on
modern BSD/macOS systems differ, so a native private bit cannot be copied
unchanged into a host implementation.

The installed `ufs_fio.c` owner path preserves the system bits and accepts
all `UF_SETTABLE` bits. `ufs_inode.c` copies the entire unsigned-short flag
word to and from the on-disk inode without filtering unnamed bits.
A native test then dropped to `nobody` (uid 32767), set bit `0x0080` and
the unrelated NODUMP bit on its own new file, called `fsync`, closed and
reopened it read-only, and confirmed both bits. It cleared `0x0080`, synced and
reopened again, and confirmed NODUMP was preserved. It restored the
original flags and removed its private file and directory.
The flag changes and `fsync` also passed on the read-only descriptor,
matching a handle opened only for SMB `WRITE_ATTRIBUTES` access.

`python3 tests/target.py flags --host 192.168.1.26 --user root` reproduces
this check using [target-flags-test.c](tests/target-flags-test.c). The probe
creates a unique private directory directly in `/usr/tmp`, removes it
after success, and does not modify exported files.

## Archive-storage milestone

The archive backend and authentication diagnostics compiled natively without
warnings. The immutable source snapshot was
`4fe655cdfcb97c40c81cd1ff817470e582654e91ea904d0b0a544014751f4443`.
The incremental helper uploaded only changed `fs.c`, then rebuilt and linked:

```
text    data    bss     dec     hex
31040   3490    37922   72452   11b04   total text: 67712
        overlays: 21120,15552
```

The executable is 97,762 bytes. Rounded base plus largest overlay still uses
57,344 bytes of instruction space. Static data+BSS is 41,412 bytes, leaving
a calculated 24,124 bytes for heap and stack. The standalone password utility
remains 12,672 text, 1,590 data, and 94 BSS.

The complete native protocol suite passed session-ID validation and failed
authentication recovery; related compounds with verified signatures; eight
65,536-byte signed reads in one 524,928-byte response; tree isolation;
fragmented TCP; invalid/unsigned signature handling; replay rejection; and
request/frame bounds. The optional 200-file fixture was skipped because its
helper expects a local export path; it remains covered by the host suite.

The archive-backed native writable suite passed both independent clients' byte-copy,
flush, update, truncate, rename, replacement, deletion, and directory checks.
It also passed cross-worker sharing conflicts and rename identity, plus
abrupt TCP-disconnect recovery: an exclusive handle was released and a
delete-on-close file survived until the final remaining reader closed.
Its unique test directory was removed.

`tests/archive_test.py` passed against four distinct signed native sessions:
NORMAL cleared persistent ARCHIVE, the cleared state survived close/reopen
and rename, file and directory queries agreed, and a later WRITE restored
ARCHIVE for another worker. Its unique files were removed.

Windows `windows-client.ps1 -Writable` was repeated against this
archive-backed native build and passed again, including exact 262400-byte
download/upload hashes, enumeration, file and directory rename/deletion,
and cleanup. The session remained signed SMB 2.0.2. The new diagnostic
identified the tolerated post-upload metadata rejection as unsupported
change time; archive updates no longer rely on accepting an ignored change.
The temporary Windows script and its SMB connection were removed.

## Windows Explorer GUI

On the archive-backed native build, Explorer connected through its credentials dialog
using the disposable SMB account. The desktop showed the `pdp` root and its
`empty`, `nested`, `hello.txt`, and `zero` entries, then opened `nested`.
The binary fixture was copied with Explorer's Copy/Paste commands into an
empty local Windows temporary directory. A separate read-only PowerShell
check found 262400 bytes and SHA256
`85e1298a87a2077b5de87c6ea60e77be9ceba06f955c51b73676ee5a71f1187f`.
The file was not copied by the command-line verifier. After the earlier CLI
test connection had been removed, `Get-SmbConnection` confirmed the active
Explorer connection used dialect `2.0.2`, `Signed: True`, and
`Encrypted: False`. Client policy was unchanged.

## macOS Finder GUI

Finder connected using the native Registered User credential dialog with
the disposable SMB account; remembering the password in Keychain was left
unchecked. Finder displayed the root and nested directory. The binary
fixture was copied with Finder's Copy/Paste commands into an owned local
temporary folder. Finder showed the completed 262 KB file, and a separate
local command-line check verified 262400 bytes and the same SHA256
`85e1298a87a2077b5de87c6ea60e77be9ceba06f955c51b73676ee5a71f1187f`.
The verifier did not perform the copy. This used the native target server,
not a host server substituted for the desktop test.
For that Finder mount, `smbutil statshares` reported `SMB_2.002`,
`SIGNING_REQUIRED: TRUE`, and `SIGNING_ON: TRUE`.

## Persistent timestamp build

A Finder GUI upload exposed a missing operation: setting creation time.
Without a metadata file, the server truthfully returned
`STATUS_NOT_SUPPORTED`, and Finder stopped with a zero-byte destination.
The new `-M` backend stores creation/change FILETIMEs in a protected table
outside the export. Native inode marker `0x0040` distinguishes files with
records; the installed `ufs_alloc.c` explicitly resets `i_flags` to zero
when allocating a free/reused inode, preventing an old row from attaching
to ordinary inode reuse. Archive bit `0x0080` is independent and preserved.

The immutable source snapshot
`3aac608a6c98c5b18b3cdd052e3ce58d57e512640d317c2b378686d3e0012007`
compiled natively without warnings and produced:

```
text    data    bss     dec     hex
31168   3588    38120   72876   11cac   total text: 73216
        overlays: 25600,16448
```

The executable is 105,692 bytes. Base plus largest overlay round to
32768 + 32768 = 65536 bytes of instruction space. Data+BSS is 41,708 bytes,
leaving a calculated 23,828 bytes for heap and stack. The first overlay now
contains `fs.o metadata.o`; the second contains `rpc.o auth.o metaio.o`.
Keeping protected metadata-file setup outside the base preserved this fit.

The native launch adds `-M /usr/tmp/smbd-test/times`. The created table is
root-owned mode 0600 and exactly 262208 bytes. An isolated native probe
verified that `ftruncate` to that size, a 64-byte header write, `fsync`, and
close/reopen leave every remaining byte zero, as initialization requires.
The probe file and one-off binaries were removed.

`tests/metadata_test.py` passed on the native server: exact 100 ns
creation/change values, native access/write times, fresh sessions, rename,
and directory enumeration. Its prepare/verify phases then passed across
an actual daemon restart using the same metadata table; the uniquely named
fixture was removed afterward. `tests/handle_limit.py` also passed with
`-M`: eight directory handles and all eight DIR cursors simultaneously
worked within the native 30-descriptor limit, a ninth handle was rejected,
and closing them permitted another normal file read.

Finder then authenticated afresh to this timestamp-enabled native build and
repeated its GUI upload. Choosing Keep Both preserved the earlier zero-byte
failed destination and created `finder-upload-20261001/binary file 2.bin`.
Finder completed the copy without an error alert and displayed 262 KB. A
separate read-only signed SMB connection verified 262400 bytes and SHA256
`85e1298a87a2077b5de87c6ea60e77be9ceba06f955c51b73676ee5a71f1187f`.
The verifier did not upload the file. Native diagnostics showed successful
WRITE, SET_INFO, FLUSH, and CLOSE operations for the completed transfer.

The Windows native `windows-client.ps1 -Writable` regression was also
repeated against this timestamp-enabled build and passed: share enumeration,
root/nested/empty/zero fixtures, exact 262400-byte download and upload hashes,
file and directory rename/deletion, and cleanup of its temporary files and
connection. Windows reported dialect `2.0.2`, `Signed: True`, and
`Encrypted: False`; its signing-required and guest-disabled policy remained
unchanged. SET_INFO operations during this run succeeded. This command-line
regression used separate temporary files and preserved the GUI test folders.

## Reconnect and timestamp completion

After a daemon restart, the earlier server rejected Explorer's reconnect
attempts at the first SESSION_SETUP with `STATUS_INVALID_PARAMETER`, which
Explorer displayed as “The parameter is incorrect.” The server had rejected
every nonzero PreviousSessionId. The completed
server accepts unknown previous IDs after fresh authentication, and uses
a bounded private 16-row registry to replace active sessions only after
the new client's credentials have been verified.

The combined reconnect and same-handle timestamp-suppression source snapshot
`8cf681536e2bd44378a590d5f2d54474a88a441bbab43f22352b77f52599a34c`
compiled natively without warnings:

```
text    data    bss     dec     hex
31808   3686    38720   74214   121e6   total text: 77056
        overlays: 27072,18176
```

The executable is 111478 bytes. Base plus largest overlay still round to
32768 + 32768 = 65536 bytes; the base has 960 bytes before its segment
boundary. Data+BSS is 42406 bytes, leaving a calculated 23130 bytes for
heap and stack. `session.o` belongs to the second overlay.

`tests/reconnect_test.py --writable` passed on the native server: failed
authentication preserves the old live session and handle; successful signed
replacement closes the old TCP transport and releases an exclusive handle;
a previous ID equal to the new ID is harmless; and delete-on-close cleanup
finishes before new authentication is acknowledged. This also verifies the
native SIGUSR1/shutdown path. A genuine session ID saved before stopping
the old daemon then authenticated successfully after this rebuild/restart
and read `hello.txt` through the new signed session.

The expanded native metadata test also passed exact creation/change values,
same-handle access/write-time suppression across reads, writes and truncation,
updates from another worker, fresh sessions, rename, and directory queries.

The full native protocol suite passed again, including unknown previous-ID
authentication, authentication-failure recovery, signed related compounds,
the 524928-byte maximum compound response, tree isolation, fragmented TCP,
signature/replay rejection, and request/frame bounds. The optional 200-file
local-export fixture remains covered by host tests. Eight simultaneous
directory handles and all eight DIR cursors also passed with the additional
registry descriptor; the ninth handle was rejected and normal reads worked
after closing the eight handles.

The Windows native writable script passed once more against this combined
build, including exact download/upload hashes, enumeration, rename and
deletion, and signed SMB 2.0.2. Its temporary script and UNC mapping were
removed before the final Explorer check.

For the final handoff, the same binary was restarted with the installed
`/usr/bin/nohup`, stdin redirected from `/dev/null`, and `umask 077`.
Its protected log is `/usr/tmp/smbd-test/server-20261001.log` and its PID file
is `/usr/tmp/smbd-test/server-20261001.pid` (PID 8966 when launched).
The installed wrapper selected `nohup.out`; that newly created mode-0600
file was renamed to the stated log path while the server kept it open.
The controlling root Telnet session was closed, after which an independent
signed reconnect and `hello.txt` read succeeded. The daemon keeps the same
export, credential hash, consumed entropy pool and persistent metadata table.

Explorer then reconnected to the final detached instance and opened the empty
`explorer-upload-20261001` folder. Its GUI Paste command uploaded the earlier
local GUI download. A separate read-only signed SMB verifier found
`binary file.bin`, read all 262400 bytes, and confirmed SHA256
`85e1298a87a2077b5de87c6ea60e77be9ceba06f955c51b73676ee5a71f1187f`.
The verifier did not perform the upload. Native diagnostics recorded all five
data WRITEs, both relevant SET_INFO operations, and the CLOSE operations as
successful. With the command-line regression connection already removed,
`Get-SmbConnection` confirmed the active Explorer connection used dialect
`2.0.2`, `Signed: True`, and `Encrypted: False`.

Finally, Explorer browsed `\\192.168.1.26` itself and displayed the single
`pdp` share icon, verifying server-level GUI share enumeration as well as
direct share access. The test Explorer window was then closed. Its owned
local temporary download was removed after checking the exact filename,
length, hash, and absence of unexpected files. Native share fixtures were
retained for review.
The local private target-credential file and temporary native-console helper
were deleted and verified absent; no root Telnet session was left open.

Finder subsequently authenticated to the final detached build through its
Registered User dialog, with Keychain storage unchecked. It opened the share
and displayed the Explorer-uploaded `binary file.bin` as 262 KB in
`explorer-upload-20261001`. That Finder tab and mount were left open for
review. The owned local Finder download/upload fixtures and their generated
view metadata were removed after checking their names and hashes.
