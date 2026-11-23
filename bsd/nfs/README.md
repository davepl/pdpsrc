# NFS client for 2.11BSD

An NFS version 3 client for 2.11BSD on the PDP-11, in three parts:

| Part | What it is |
| --- | --- |
| `nfsc` | Stand-alone, ftp-like user-mode client. Needs no kernel changes. |
| `kernel/` | `nfsdisk`, a small kernel driver: a "virtual disk" served by a user process. |
| `mount_nfs` | Mounts an export on a directory, then serves that virtual disk from the NFS server. |
| `sbin/` | Small patches so `mount`, `umount`, `/etc/fstab` and `/etc/rc` handle NFS. |

The protocol code (`xdr.c`, `rpc.c`, `nfs3.c`) is shared by both programs. It
speaks ONC RPC over TCP with AUTH_UNIX, portmap v2, MOUNT v3 and NFS v3. That
matches what current servers offer: TrueNAS/Linux `knfsd` provides no NFSv2
and no UDP. 64-bit NFS quantities are carried as hi/lo `unsigned long` pairs,
and files over 2 GB are refused rather than truncated.

## How the mount works

2.11BSD has no vnode layer: the filesystem code reads and writes 1 KB blocks
of a block device through the buffer cache. `nfsdisk` adds a block device
`/dev/nfsN` (major 13) whose strategy routine queues each buffer. A daemon
holding the control device `/dev/nfsdN` (major 27) answers each request.
`mount_nfs` forks that daemon, and it **synthesizes a 2.11 UFS filesystem
image on demand** from the NFS export:

* block 1 is a superblock;
* blocks 2–4095 are inode blocks, built from NFS attributes;
* directory blocks are built from READDIRPLUS, as real `struct direct` records;
* file blocks are NFS READs (cached 4 KB at a time); symlink blocks are READLINK;
* block numbers encode the inode and logical block, `(ino << 15) | lbn`, with
  indirect blocks synthesized to match.

The kernel's buffer cache counts 512-byte sectors (`fsbtodb`), so the driver
converts back before passing a block number to the daemon.

So `mount(2)`, `umount(2)`, namei, `stat`, `ls -i`, `pwd`, `exec`, `df` and
`opendir` all run unmodified kernel and library code. The only change to
existing kernel code is a three-line early return in `free()`, and the
driver lives in its own overlay.

Kernel cost on MINERVA: base text +64 bytes (56,832 of 57,344), new overlay
2,240 bytes, data/bss +88 bytes.

### Read-write (experimental)

On a read-write mount, the daemon works out what the kernel meant from the
blocks it writes:

* **inode blocks** — new inodes, freed inodes, chmod/chown/utimes, truncation;
* **directory blocks** — names added, removed or redirected, which become
  CREATE, MKDIR, SYMLINK, REMOVE, RMDIR, RENAME and LINK;
* **data blocks** — WRITE, clamped to the size the kernel holds in core.

New data goes to "pool" blocks handed out through the superblock free list.
Their owner is found with an ioctl that searches the kernel's in-core inodes,
or from later inode and indirect blocks; until then the data is spooled
locally. 2.11 writes a new inode synchronously before entering its name, and
directory writes are always synchronous, so the order of events can be
relied on. A helper process runs `sync` every two seconds to push delayed
inode and partial-block writes. Pool blocks lie just below 2^30: `bmap()`
turns a new buffer back into a block number with a signed shift, so every
block the kernel allocates must stay below that. Rename is recognised as "name added for an
existing inode" followed by "same inode removed elsewhere". A removal is
acted on only if the old directory record is still physically present in the
written block, as `dirremove` leaves it. This guards against layout races
when a directory image was rebuilt.

Some kernel behaviour the daemon has to allow for:

* `direnter()` shrinks a directory and `itrunc()` zeroes the tail of its last
  block; those chunks are turned back into valid empty chunks.
* The kernel can reuse a freed inode number before it writes the freed
  inode, and delayed writes of a removed file's blocks still arrive later.
  After each remove the daemon asks the server whether the file still
  exists and forgets it if not. Each inode number carries a generation, so
  late writes meant for an earlier file with the same number are dropped.
* `namei()` scans a directory block for free space, can sleep while the new
  inode is allocated, then `direnter()` reads the block again and panics
  ("wdir: compact") if its layout changed. So on a read-write mount a
  directory's image is never rebuilt or evicted while the kernel has that
  directory in core. The daemon keeps 64 images and evicts the least
  recently used one the kernel is not using. Changes made on the server are
  noticed when the daemon is idle, for directories the kernel is not using.
* `direnter()` can shrink a directory: `itrunc()` zeroes the freed tail of
  its last block with a *delayed* write, which the periodic cache flush
  cannot drop. An image the kernel wrote in the last 8 seconds is therefore
  never evicted or rebuilt; the sync helper pushes delayed writes every two
  seconds.
* After `close()` the kernel's updated inode can sit in a delayed write
  while its data blocks arrive first, and it may already have dropped the
  inode from core. Data is clamped to the size the kernel has, so a block
  reaching past the last size the daemon saw is held until the inode
  block arrives, and the inode is recorded before its blocks are replayed.
* A new directory's first block is cached by the kernel with zeroed chunks
  past its size, so a directory the kernel has written is always reported
  with exactly the size the kernel last had. (When the kernel reads past
  its own zeroed chunk it logs "bad dir ... mangled entry".)
* An inode block is re-read for a neighbour while the kernel holds other
  inodes from it, so a held inode's shadow copy is not replaced, and a
  change is acted on only when it differs from both the shadow and the
  server.

## Installing on a new machine

Everything below is done as root on the PDP-11. Nothing is built or
installed for you in one go: each step is short, and the kernel is replaced
only when you copy it into place yourself.

### What you need

* 2.11BSD with networking (a kernel config that builds `unix` and `netnix`)
  and the PDP able to reach the NFS server over TCP.
* The kernel sources in `/usr/src/sys`, a working kernel config directory
  (for example `/usr/src/sys/MINERVA`), and the `mount`/`umount` sources in
  `/usr/src/sbin`. `patch` must be installed.
* Free disk space for one kernel build and a backup copy of `/unix` and
  `/netnix`.
* An NFSv3 export the PDP is allowed to mount (see *Server setup* below).

### 1. Copy this directory to the PDP

Put the whole `bsd/nfs` directory on the PDP, for example by FTP or tar,
at any path. The steps below call it `$NFS`:

```
NFS=/usr/dave/source/repos/pdpsrc/bsd/nfs
```

### 2. Patch the kernel sources

```
cd $NFS/kernel
sh install.sh MINERVA          # use your kernel config's name
```

This changes `/usr/src/sys` and does nothing else:

* applies `kernel.diff`: device entries in `pdp/conf.c` (block major 13,
  character major 27), a three-line change to `free()` in
  `sys/ufs_alloc.c`, `-DNFSDISK` in the config's `Makefile`, and the driver
  in overlay OV10 in its `Make.sys`;
* copies the driver to `sys/nfsdisk.c` and its header to `h/nfsdisk.h`
  (which is also `/usr/include/sys/nfsdisk.h`);
* creates `/dev/nfs0`–`nfs3` and `/dev/nfsd0`–`nfsd3`.

The four patched files are first saved as `*.pre-nfs`. Running the script
again is safe: it re-copies the driver and header but patches only once.
`kernel.diff` was made against MINERVA's config; on another config the
script stops with "kernel.diff did not apply" if that config's `Makefile` or
`Make.sys` differs, and you have to add the two changes by hand. Majors 13
and 27 are free on a stock 2.11BSD tree.

### 3. Build the kernel

```
cd /usr/src/sys/MINERVA
make
```

If the link step says an overlay is too big, move `nfsdisk.o` to another
overlay with room in `Make.sys`.

### 4. Install the kernel and reboot

Keep the old pair, then install the new pair. Always install `unix` and
`netnix` together.

```
cp /unix /unix.pre-nfs; cp /netnix /netnix.pre-nfs
cp unix /unix; cp netnix /netnix
reboot
```

If the new kernel does not boot, type `unix.pre-nfs` at the `:` boot
prompt, then copy the `.pre-nfs` files back.

### 5. Build and install the programs

```
cd $NFS
make
make install                   # nfsc to /usr/bin, mount_nfs to /usr/sbin
```

This needs `/usr/include/sys/nfsdisk.h` from step 2.

### 6. Teach mount, umount and /etc/rc about NFS

```
cd $NFS/sbin
sh install.sh
```

This patches and rebuilds `/usr/src/sbin/mount` and `/usr/src/sbin/umount`,
installs them in `/sbin`, and patches `/etc/rc`. It keeps `*.pre-nfs`
copies of the sources, the old binaries and `/etc/rc`. The changes:

* `mount host:/path dir` runs `mount_nfs` (2.11's `mount` already runs
  `mount_TYPE` for fstab types other than ufs; it only refused `host:path`);
* `umount` maps an NFS mount (recorded as `host:/path`) back to its
  `/dev/nfsN`, accepts the type name `nfs`, and no longer loses its `-t`
  list after the first file system;
* `/etc/rc` runs `mount -a -t nonfs` early and `mount -a -t nfs` once the
  network is up. fsck already skips non-ufs entries.

### 7. Mount a share

```
mkdir /mnt/nfs
mount -o ro 192.168.1.191:/mnt/main_pool/nfs /mnt/nfs
ls -l /mnt/nfs
```

To mount it at boot, add a line to `/etc/fstab` (`ro` or `rw`):

```
192.168.1.191:/mnt/main_pool/nfs  /mnt/nfs  nfs  ro  0  0
```

### Server setup

The server must offer NFS version 3 over TCP and let the PDP's address
mount the export (TrueNAS: add the PDP's address or network to the NFS
share). Servers usually map root to `nobody`, so the PDP can read
world-readable files and write only where `nobody` may: make the shared
directory writable by `nobody` (for example mode 777), or set *Maproot
User* to root for the PDP. See *Permissions* below.

### Removing it

Copy each `*.pre-nfs` file back over the original: `/unix`, `/netnix`,
`/sbin/mount`, `/sbin/umount`, `/etc/rc`, and in `/usr/src` the files
`sys/pdp/conf.c`, `sys/sys/ufs_alloc.c`, `sys/CONFIG/Makefile`,
`sys/CONFIG/Make.sys`, `sbin/mount/mount.c`, `sbin/umount/umount.c`. Then
delete `/usr/sbin/mount_nfs`, `/usr/bin/nfsc`, `/sys/sys/nfsdisk.c`,
`/sys/h/nfsdisk.h` and `/dev/nfs?` `/dev/nfsd?`, remove NFS lines from
`/etc/fstab`, and reboot.

### Building on another host

`make` also works on macOS or Linux, as a compile check only; the programs
run only on 2.11BSD.

## Using it

Stand-alone client (no kernel changes):

```
nfsc truenas:/mnt/main_pool/nfs ls -l
nfsc truenas:/mnt/main_pool/nfs get random.bin /tmp/random.bin
nfsc truenas:/mnt/main_pool/nfs          # interactive: ls cd get put mkdir ...
```

Mounting:

```
mount -o ro 192.168.1.191:/mnt/main_pool/nfs /mnt/nfs
ls -l /mnt/nfs
umount /mnt/nfs
```

or in `/etc/fstab`, mounted at boot and by `mount /mnt/nfs`:

```
192.168.1.191:/mnt/main_pool/nfs  /mnt/nfs  nfs  ro  0  0
```

Use a host name only if the PDP can resolve it (`/etc/hosts`).
`mount_nfs` can also be run directly with the same arguments.

`mount_nfs` options: `ro`, `rw` (experimental), `nosuid`, `nodev`, `noexec`,
`actimeo=N` (attribute and directory cache seconds, default 5), `inval=N`
(seconds between flushes of the kernel's cached copies, default 10),
`uid=N,gid=N` (identity used on the server, default root), `tmp=DIR`
(private work files, default `/usr/tmp`), `debug` (log every block and every
create, remove, rename and setattr to syslog).

## Permissions

Requests reach the daemon as *blocks*, not as system calls, so the daemon
does not know which local user caused them. It talks to the server with one
identity (`uid=`/`gid=`, default root), and the PDP kernel checks
permissions locally against the owners and modes the server reports, as it
would for a local disk. With the usual root squashing the daemon acts as
`nobody` on the server: you can read world-readable files and write only
where `nobody` may. For full local semantics, export the share to the PDP
with root mapped to root (TrueNAS: *Maproot User* = root).

## Limits

* Up to 4 mounts at once (`NNFSD`).
* Files up to 31.75 MB (32,512 KB blocks); larger files are shown truncated.
* About 32,500 server files per mount, and 32,700 files created by the PDP.
  Inode numbers 32,512–32,767 are reserved: their block range is the pool.
* About 8 GB of new data per mount. Freed blocks are not reused; remount to
  start a fresh pool.
* Names longer than 63 characters (2.11's `MAXNAMLEN`) are not shown.
* Device files, FIFOs and sockets on the server appear as empty regular files.
* Coherence is close-to-open-ish. Cached blocks and unreferenced inodes are
  dropped every `inval` seconds, and attributes are refreshed after
  `actimeo`. A file or directory held open on the PDP keeps the size it had
  when opened. Directories are reported 512 bytes larger than their contents,
  so a few names added remotely stay visible. On a read-write mount, names
  added on the server to a directory that is in use on the PDP (a shell's
  current directory, say) appear only once it is no longer in use.
* Directories up to 128 KB (about 6,000 names); larger ones return EFBIG.
* Read-write: at most 64 directories can be in use on the PDP, or written by
  it in the last 8 seconds, at once. Past that, opening another one fails
  and the daemon logs "all 64 directory images in use".
* If the daemon is killed, the mount returns errors (ENXIO) instead of hanging;
  `umount` still works.
* `df` shows the server's free space, but the size column is meaningless:
  block numbers encode inodes, so the "disk" is 2^31 blocks.
* Unmount NFS file systems before `reboot` or `halt`: the daemon cannot be
  asked to finish once user processes are killed.

## Testing

`make vdtest` builds a harness that runs the virtual-disk read path in user
mode, without the kernel: `vdtest host:/path [ino[d] ...]` prints the
superblock, inodes and directory blocks it would serve.

Verified on MINERVA (simh) against TrueNAS SCALE: `nfsc` get/put/mkdir/mv/rm;
read-only mount, `ls -l`, `ls -i`, symlinks, nested directories, `pwd`,
`cat`, `cp` of 300 KB (about 2 s) with `sum` matching the server, `exec` of a
binary stored on the share, a file added on the server appearing at once,
`mount`/`umount` by hand, by fstab entry, and `mount -a -t nfs` /
`umount -a -t nfs`, and the fstab entry mounted at boot.

Read-write, checked on the PDP and on the server, and again after the
10-second flush of cached inodes: create and write (142 KB and 300 KB files
with matching `sum`, about 50 KB/s), append, overwrite in the middle with
`dd conv=notrunc`, truncate, `mkdir`, `rmdir`, `mv` within and between
directories (files and directories, and onto an existing file), symlinks,
hard links, `chmod`, `rm`; overwriting a read-only file as root; removing
files and at once creating new ones that reuse the same inode numbers
(server and pool), including late delayed writes of the removed files; and
data written just before `umount`. Two back-to-back `cp -r` copies of
`/usr/src/lib/libc` (1.1 MB, 40 directories, 408 files each) compared clean
with `diff -r` on the PDP, again after a fresh remount, and by MD5 on the
server. `cp -r` of 26 source trees (2.3 MB, 50 directories,
over 300 files) compared clean after a remount, with no "mangled entry"
messages, and `rm -r` of the copy removed everything on the server.

### Development helpers

`tools/up file...` uploads files (paths relative to this directory) to the
same place on the PDP-11 by FTP, and `tools/pdp "commands"` runs a shell
command line there as root over telnet and prints its output and exit code.
Both need `PDP_PASS` set to the PDP's root password; `PDP_HOST` defaults to
192.168.1.26 and `PDPT` sets the `pdp` timeout in seconds. They use `curl`
and `expect`, which ship with macOS.
