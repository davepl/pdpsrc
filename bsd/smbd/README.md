# smbd for 2.11BSD

A small native SMB **2.0.2** server for transferring files between a PDP-11
and modern desktops. One configured SMB account accesses one disk share.
NTLMv2 through SPNEGO and HMAC-SHA256 signing are required by default.
All filesystem and protocol processing runs on the serving machine, using its
C library and BSD interfaces; there is no Samba, OpenSSL, or gateway dependency.

The service defaults to read-only. `-w` enables file creation, streamed writes,
truncation, flush, rename, deletion, and directory creation/removal. `IPC$` and
a bounded `srvsvc` RPC implementation provide share enumeration and share
information. A small on-disk registry coordinates open sharing and pending
deletions across worker processes.

Directory probes with literal names use direct case-insensitive comparison.
Each worker caches eight sharing-registry rows only while holding the registry
lock; releasing and reacquiring it forces fresh validation. Directory entries
and file attributes are not cached by the server. Small replies include their
TCP framing prefix in the first send.

## Build and configure

```
cd bsd/smbd
make
```

Keep `../pdp11_unistd.h` alongside the source. The same sources build on modern
Unix and the measured 2.11BSD patch-499 compiler. The native link uses separate
instruction/data spaces with two code overlays. Native compiler details, link layout, measurements,
and client acceptance evidence are in [TARGET.md](TARGET.md).

Create a protected directory outside the export and run `smbpwd` as the owner
of the credential file:

```
mkdir /etc/smbd
chmod 700 /etc/smbd
./smbpwd /etc/smbd/password.hash
```

`smbpwd` prompts twice without echo and atomically installs a mode-0600 NT-hash
file. Passwords are 1–256 printable ASCII characters; SMB usernames are 1–64
ASCII characters. The hash is an authentication secret: keep it protected.
Restart the daemon after changing it. Existing plaintext password files remain
supported with `-P file` instead of `-H file`; those files must contain exactly
one password line and have the same ownership and permission protection.

The SMB account is separate from the Unix account used for file access. For a
native launch, choose an existing non-root Unix account with `-U` (default
`nobody`). Give it read/search permissions on the export, and ownership/write
permissions when using `-w`. Root workers chroot to the export, clear extra
groups, and drop to that account before serving requests.

2.11BSD has no assumed cryptographic random device. Generate a fresh pool on
a trusted modern host, transfer it to `/etc/smbd/random`, and make it root-owned
mode 0600:

```
(umask 077; dd if=/dev/urandom of=smbd.random bs=8192 count=8)
```

Each admitted TCP connection consumes eight bytes. The listener exclusively
locks the pool, erases consumed bytes, and calls `fsync` before using them.
Restarts skip erased bytes; exhaustion rejects connections. Replenish with
fresh randomness. Never restore a consumed pool or use the same pool on two
servers. Modern hosts use `/dev/urandom` unless `-R` is supplied.

Native foreground launch (substitute actual paths/address):

```
./smbd -r /absolute/export -s pdp -u pdp -H /etc/smbd/password.hash \
    -R /etc/smbd/random -T /usr/tmp -U nobody -a SERVER_IPV4 -p 445 -v
```

Add `-w` for writable service. `-c N` sets the worker limit (default 4, maximum
16). `-C N` sets the credit limit (8–32, default 32). The physical Mentec M1
uses `-C 16` to limit queued work while leaving room for desktop metadata
requests. Lower limits need client testing; eight credits stalled macOS flushes.
`-T directory` places temporary spools and the sharing registry on a
chosen local filesystem (default `/tmp`); use a filesystem with adequate free
space, such as `/usr/tmp` on the measured PDP. Authenticated idle connections
retain their handles; unfinished logins and partial frames time out. Ctrl-C
stops the listener and its workers.
Installation, daemonization, and `/etc/rc` edits are not performed by `make`.

`-S optional` permits password-authenticated sessions without per-message
signing when the client allows it. Clients that require signing still get it,
and signed requests are always verified. The successful authentication reply
is still signed. `-S required` is the default. Optional signing keeps Unix
permissions and password authentication, but unsigned traffic has no
cryptographic protection against modification in transit; use it on a trusted
network. This stays on SMB 2.0.2 and does not enable SMB1 or guest access.

Windows clients that require signing need an administrator to run
`Set-SmbClientConfiguration -RequireSecuritySignature $false` to use unsigned
sessions. This is a machine-wide client policy; restore `$true` to require
signing again. On macOS, check `smbutil statshares -m MOUNTPOINT` after a fresh
mount; `SIGNING_ON` shows whether the connection actually uses signing.

For desktop uploads, also use `-M /etc/smbd/times`. Writable startup creates
this mode-0600 metadata table if absent. It stores SMB creation/change times
that 2.11BSD cannot represent directly; Finder requires creation-time updates
when copying a file onto the server. The path must be absolute, outside the
export, and in a protected directory, with no symlink components. Use the
same table on subsequent launches, including read-only launches. The table
is bound to the export and file inode identities. Backups must preserve those
identities and inode flags, for example in a filesystem image; an ordinary
file-by-file restore does not preserve this association. Metadata migration
is not implemented. A marked file with missing or damaged metadata returns
an error.

For non-root host development, use a protected credential path, an owned
export, and `-a 127.0.0.1 -p 1445`; omit `-R` and `-U`. Explicit `-g` replaces
password authentication with unsigned laboratory guest mode. A client requiring
signing cannot use that mode. Server options do not change client policy.

## Connect

In Finder, use **Go → Connect to Server**, enter `smb://SERVER/pdp`, and select
Registered User. Server-level browsing uses `smb://SERVER`. Windows Explorer
uses `\\SERVER\pdp`, or `\\SERVER` to enumerate shares. Desktop clients should
use TCP port 445. Local-network discovery announcements are not implemented.

Windows commands prompt for the configured SMB password:

```
net use Z: \\SERVER\pdp /user:pdp *
net view \\SERVER
dir Z:\
copy /b Z:\nested\example.bin .\example.bin
net use Z: /delete
```

macOS commands:

```
smbutil view //pdp@SERVER
mkdir /tmp/pdp-mount
mount_smbfs //pdp@SERVER/pdp /tmp/pdp-mount
ls /tmp/pdp-mount
cp '/tmp/pdp-mount/nested/example.bin' ./example.bin
umount /tmp/pdp-mount
```

These commands are usage examples. [TARGET.md](TARGET.md) distinguishes actual
PDP-11, Windows, macOS, GUI, and independent-library test results.

## Limits and filesystem behavior

- Eight disk handles, two RPC pipes, and eight tree connections per worker;
  eight commands per compound and at most 32 credits. One authenticated session
  per TCP connection; reconnect after LOGOFF or failed authentication.
  Reconnect establishes a fresh session. After successful authentication,
  `PreviousSessionId` can replace a live session and release its handles;
  unknown IDs from an earlier daemon run are accepted. A private 16-entry
  registry coordinates replacement across workers. Existing open handles
  cannot be recovered after reconnecting.
- Two 8,192-byte protocol buffers and two 4,096-byte RPC buffers. Transfers up
  to 65,536 bytes stream through small workspaces and private temporary files.
  While hashing, the worker drains incoming TCP data into a bounded 2 MiB
  disk ring under `-T`, preventing client send timeouts on slower CPUs.
  Prefetched bytes are authenticated and executed in their original order.
  Incoming frames are capped at 524,288 bytes and compound replies at 589,824.
  Signed READ data is snapshotted once, then hashed and sent from that snapshot.
  Temporary disk storage must be available outside the chroot.
- Regular files and directories only, with signed 32-bit file offsets/sizes.
  Names are printable ASCII, including spaces; other encodings are rejected or
  omitted. Paths are bounded to 511 bytes; native system limits are lower
  (256-byte paths, 63-byte components on the measured target).
  ASCII case-insensitive lookup preserves case and prefers exact matches.
  Symlinks, absolute paths, `..`, and alternate data streams are refused.
- Local Unix permissions apply. Read-only mode denies mutation regardless of
  those permissions. Writable mode implements cross-worker share-access checks
  and deletion after the last open handle closes. It does not coordinate SMB
  share modes with unrelated local Unix programs. Renaming directories with
  open descendants is deliberately restricted.
- The DOS archive flag is stored persistently in each inode: this server
  reserves user flag `0x0080` on 2.11BSD and `0x0800` on macOS, uses `UF_ARCHIVE`
  on BSD systems that define it, and `user.smbd.archive` on Linux. Other inode
  flags are preserved. NORMAL clears the stored flag; creation, writes, and
  truncation set it. Writable exports should be owned by the worker's Unix
  account because changing inode flags requires ownership. Native flag storage
  adds no per-file memory table or sidecar files.
  HIDDEN is also persistent: user flag `0x0020` on 2.11BSD, `UF_HIDDEN`
  where available (otherwise private flag `0x0400`), and `user.smbd.hidden`
  on Linux. This permits macOS to create dot directories such as Git's `.git`.
  HIDDEN survives writes and renames; an explicit attribute update can clear it
  independently of ARCHIVE. SYSTEM and other unimplemented flags are rejected.
- Access/write timestamps have native one-second resolution. With `-M`, SMB
  creation/change times persist as exact FILETIME values in a 262,208-byte
  table with 2,048 entries and a 128-byte workspace. Rename and hard links
  preserve their association. A separate inode marker (`0x0040` on 2.11BSD,
  `0x1000` on macOS, an xattr on Linux) prevents stale entries from attaching
  to reused inodes. SMB deletion reclaims entries; files deleted by unrelated
  local programs can leave stale entries that consume table capacity.
  Files without stored metadata initially derive creation/change time from
  Unix `ctime`. Changes to logical timestamps track SMB operations; local Unix
  programs do not maintain this table. Unsupported metadata requests receive
  an error instead of a successful no-op.
- No byte-range locks, leases, durable handles, domain membership, Kerberos,
  encryption, compression, multichannel, Unicode filenames, or SMB1 sessions.
  The legacy multiprotocol NEGOTIATE envelope can select SMB2 for desktop
  clients. Applications requiring unsupported locking/metadata are outside the
  tested file-transfer scope.

## Reproduce tests

On a modern host:

```
python3 -m venv /tmp/smbd-client-venv
/tmp/smbd-client-venv/bin/pip install -r tests/requirements.txt
make all unit
/tmp/smbd-client-venv/bin/python tests/run_host.py
```

The host suite uses disposable exports, independent Impacket/smbprotocol
clients, known-answer crypto tests, sanitizer checks, signed compound requests,
malformed requests, credential-file bounds, and entropy-pool consumption tests.
Additional client entry points use the test account `pdp`/`testpass` by default;
use only disposable fixtures or override the account parameters:

```
/tmp/smbd-client-venv/bin/python tests/clients.py --host SERVER --port 445
/tmp/smbd-client-venv/bin/python tests/rpc_test.py --host SERVER --port 445
/tmp/smbd-client-venv/bin/python tests/write_test.py --host SERVER --port 445
```

`write_test.py` requires `-w` and creates a unique test directory. Its
`--read-only` mode verifies that the default denies creation. Windows has a
native [PowerShell acceptance script](tests/windows-client.ps1), including an
optional `-Writable` round trip. Target upload/build helpers prompt for the
login password and keep builds under `/usr/tmp/smbd-test`; see [TARGET.md](TARGET.md).

The next milestone is an independent Linux SMB-client acceptance run, followed
by bounded byte-range locking and editor save tests. These remain outside the
verified desktop file-transfer support described here.

Protocol references: Microsoft's [SMB2 specification](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/),
[NTLMv2 calculations](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-nlmp/5e550938-91d4-459f-b67d-75d70009e3f3),
[filesystem information](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/4718fc40-e539-4014-8e33-b675af74e3e1),
[basic timestamps and attributes](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/16023025-8a78-492f-8b96-c873b042ac50),
and [share enumeration RPC](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-srvs/c4a98e7b-d416-439c-97bd-4d9f52f8ba52).
