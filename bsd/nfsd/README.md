# nfsd: an NFS server for 2.11BSD

`nfsd` shares directories of a PDP-11 running 2.11BSD with other machines
over NFS version 3. It is the counterpart of the client in [`../nfs`](../nfs):
that one mounts someone else's files on the PDP-11, this one lets Linux,
macOS or another PDP-11 mount the PDP-11's files.

It is one ordinary program. There are no kernel changes, no new devices,
and nothing to configure in the kernel.

| File | What it is |
|---|---|
| `nfsd.c` | Sockets, RPC, the built-in portmapper, the MOUNT protocol, `/etc/exports`. |
| `nfs3.c` | The NFS version 3 procedures. |
| `fh.c` | File handles, the handle cache, open files, and whose identity the server has. |
| `dir.c` | Reading directories. |
| `xdr.c` | XDR encoding (the same code as the client). |
| `tests/nfstest.py` | Protocol test: 95 checks, run from any machine with Python 3. |
| `tests/nfsbench.py` | Write a file, read it back, compare, report the speed. |

## What it serves

* **NFS version 3 over TCP**, on port 2049. Every procedure is there except
  `MKNOD`, which answers "not supported".
* **MOUNT versions 1 and 3**, over TCP and UDP, on the same port.
  (`showmount -e` works; only version 3 hands out handles.)
* **A portmapper** (version 2, TCP and UDP, port 111), because 2.11BSD has
  none. It knows about these three programs only.

There is no NFS version 2 or 4, no NFS over UDP, and no file locking.

## Building and installing

On the PDP-11:

```sh
cd bsd/nfsd
make
make install        # /usr/sbin/nfsd
```

The Makefile deliberately compiles **without `-O`**. The 2.11BSD optimizer
(`c2`) miscompiles some chains of unsigned-long comparisons; with `-O` the
server rejects every TCP request as having a bad record mark.

The same sources build on a modern host (`cc -std=gnu89 -w *.c -o nfsd`),
which is only useful for testing: run it with `-p` and `-P` on unprivileged
ports and point `tests/nfstest.py` at it.

## /etc/exports

One directory per line:

```
/path  [-option[,option...]]  [host | network/bits] ...
```

```
# read-only for everyone
/usr/src
# read-write for one network, every client user acting as local uid 100
/usr/dave     -rw,mapall=100:10   10.1.2.0/24
# read-write for one host, whose root is root here too
/usr/work     -rw,root            10.1.2.50
```

| Option | Meaning |
|---|---|
| `ro` | Read-only. This is the default. |
| `rw` | Writable. |
| `root` | The client's root is root here. Otherwise root becomes uid 65534. |
| `mapall=uid[:gid]` | Every client user acts as this local user (a number, or a name from `/etc/passwd`). |
| `resvport` | Refuse requests that do not come from a port below 1024. |

Hosts are dotted addresses, optionally with `/bits` for a network. With no
hosts listed, anyone who can reach the machine may mount the directory.
Up to 8 exports, 6 hosts each. A client may also mount a subdirectory of an
exported directory.

Remember that NFS with `AUTH_SYS` trusts whatever uid the client claims.
Export read-only, or to named hosts, unless the network is yours.

**Users.** Without `mapall`, the uid and gid a client sends are used as
they are, so they have to mean the same thing on both machines. A Mac user
is uid 501 and a Linux user usually 1000; neither exists on the PDP-11.
`mapall` is the simple answer when one person uses the share.

## Running it

```sh
nfsd                      # reads /etc/exports, goes into the background
nfsd -f /some/other/file
kill -HUP  <pid>          # read the exports file again
kill -TERM <pid>          # stop
```

It logs mounts and problems through syslog (facility `daemon`).
`nfsd -d` stays in the foreground and prints each request; `-d -d` prints
more. `-p port` and `-P port` move the NFS and portmap ports, and
`-w bytes` sets the socket buffer size (see the next section).

### Socket buffers: read this before raising `-w`

`-w` is the size of each connection's socket buffers: the TCP window a
client is offered, and how much of a reply waits in the kernel. The
default is 1024 bytes, which is slow and is the only size that is safe
everywhere.

The reason is the 2.11BSD network's memory. The whole network has about
170 mbufs of 118 bytes and 12 pages of 1 KB, and 100 to 130 of the mbufs
are in use before any data moves (`netstat -m` shows the numbers). When
no mbuf is free, received frames are dropped and any process that tries
to send sleeps uninterruptibly waiting for one. If the mbufs are all
held by data that only those sleeping processes can read, that is
permanent: the machine answers nothing on the network, not even ping,
while its console keeps working, until it is rebooted.

How many mbufs a buffer of data takes depends on the Ethernet driver:

| Driver | A full-size received frame | 1 KB | 2 KB | 4 KB | 8 KB |
|---|---|---|---|---|---|
| `de` (DEUNA, DELUA) | 13 mbufs, no pages | 10 | 18 | 37 | 73 |
| `qe` (DEQNA, DELQA) | 6 mbufs and 1 page | 10 | 11 + 1 page | 18 + 3 pages | 36 + 6 pages |

Those are for one connection whose client is sending; every client that
is sending at the same moment costs that much again, replies cost about
one page per KB, and when the 12 pages run out everything falls back to
mbufs alone. An earlier version of this server asked for the largest
buffers the kernel would give, 8 KB or more, and a PDP-11/44 with a
DEUNA dropped off the network about 100 KB into the first large copy.

So: leave the default on a `de` interface. On a `qe` interface 2048 is
a reasonable choice. 4096 is the system's own default for TCP and is
faster again, but two clients copying at once can use up the pool.
Measured on simh, 1 MB written and read back:

| `-w` | Write | Read |
|---|---|---|
| 1024 (default) | 54 KB/s | 54 KB/s |
| 2048 | 94 KB/s | 96 KB/s |
| 4096 | 159 KB/s | 159 KB/s |

On real hardware the processor sets the pace: a PDP-11/73 with a DEQNA at
2048 writes 46 KB/s and reads 42 KB/s, and a PDP-11/44 with a DEUNA at the
default writes 6 KB/s and reads 12 KB/s. On that 11/44, with two clients
writing at once, mbuf use peaked at 118 of 170 and nothing was dropped;
with the old buffers a single client took it to 170 of 170 in under a
minute.

To start it at boot, add to `/etc/rc.local`:

```sh
if [ "$INET" = YES -a -f /usr/sbin/nfsd -a -f /etc/exports ]; then
	/usr/sbin/nfsd && echo "nfsd started"		>/dev/console 2>&1
fi
```

Add `-w 2048` there on a machine with a DEQNA or DELQA.

## Mounting from a client

```sh
# Linux
mount -t nfs -o vers=3,tcp,nolock pdp11:/usr/dave /mnt/pdp

# macOS (no sudo needed if you own the mount point)
mkdir -p ~/mnt/pdp
mount -t nfs -o vers=3,tcp,nolocks pdp11:/usr/dave ~/mnt/pdp

# another 2.11BSD machine with ../nfs installed
mount -t nfs -o rw pdp11:/usr/dave /mnt/pdp
```

`nolock` matters: there is no lock daemon here, and without the option a
client may wait for one.

## How it works

**File handles.** An NFS client holds a handle for every file it knows and
expects it to work indefinitely. 2.11BSD has no way to open a file by inode
number, so a handle has to be turned back into a path. Here a handle is the
list of inode numbers on the way down from the exported directory: two
bytes each (inode numbers are 16 bits), after an 8-byte header naming the
export. To resolve one, the server reads each directory on the way for the
entry with the next number. Nothing is kept on disk, so handles survive a
restart of `nfsd` and a reboot.

A cache of 40 handles and their paths makes this cheap: `LOOKUP` enters the
path it just built, so in normal use directories are only searched after a
restart or when a handle has fallen out of the cache. Each use of a cached
path is checked against the inode number and file type in the handle.

**Open files.** Up to 8 files stay open between requests, so a run of
`READ`s or `WRITE`s is one system call each plus the reply. They are closed
after five idle seconds, when their file is removed, and whenever the
server changes identity.

**Identity.** The server keeps its real uid as root and takes the caller's
effective uid and groups for each request, so the kernel does the
permission checks and new files get the right owner. As other NFS servers
do, it lets a file's owner read and write it whatever its mode, because a
client checks the mode at `open` and expects I/O on a file it created
read-only to keep working.

**Memory.** Requests and replies use two static buffers of about 9 KB, for
8 KB reads and writes. The whole program is about 45 KB of code and 31 KB
of data.

## Limits and known gaps

* **Depth.** A handle has room for 28 inode numbers, so files more than 28
  directories below the exported one cannot be reached; creating or looking
  up something deeper gives "name too long".
* **Moving a file to another directory** changes the path its old handle
  describes. The server keeps the old handle working while it is in the
  cache; once it has been evicted, or after a restart, a client still
  holding it gets "stale file handle". Renaming within a directory never
  has this problem.
* **Hard links** in different directories have different handles for the
  same file.
* **No generation numbers.** 2.11BSD inodes have none, so if a file is
  deleted and a new one of the same type gets its inode number in the same
  directory, an old handle will reach the new file.
* **Mount points.** An export that contains another mounted filesystem
  shows it, with a different filesystem id. Linux follows that; other
  clients may not.
* **Size.** 2 GB files at most, user and group ids below 65536, names up
  to 63 characters, paths up to 255.
* **One request at a time**, at most 10 connections, 8 exports. Each
  connection also costs about 5 mbufs just to exist.
* **Network memory.** See *Socket buffers* above: the kernel's mbuf pool
  is small enough that a server can exhaust it, and the kernel does not
  recover from that. The default buffer size is chosen to stay clear of
  it, at a cost in speed.
* **No duplicate-request cache.** A client that reconnects and repeats a
  `REMOVE` or `RENAME` that had already been done gets an error for it.
* **A 2.11BSD oddity.** `rmdir(2)` of a directory that is not empty
  occasionally returns success, having done nothing: roughly once in 200
  to 1000 calls, on simh and on a real 11/73 alike. The cause has not been
  found. The server therefore believes a successful `rmdir` only once the
  directory is really gone, and logs the first few times it catches this.

## Testing

```sh
# all 95 protocol checks, as local uid 501
python3 tests/nfstest.py -u 501 -g 20 pdp11 /some/exported/dir

# speed
python3 tests/nfsbench.py -u 501 -g 20 -n 2048 pdp11 /some/exported/dir
```

Both need the export to be writable by that uid. Neither needs root or a
mount: they speak the protocols directly.

`nfsbench.py` writes a file one request at a time, reads it back and
compares. The speeds for each buffer size are in *Socket buffers* above.

Also tested: the macOS client (copying a tree in, `diff -r`, large files,
rename, symlinks, removal) against both machines, and the 2.11BSD client
in `../nfs` on one PDP-11 mounting the other, including a restart of
`nfsd` while mounted. The Linux kernel client has not been tried.
