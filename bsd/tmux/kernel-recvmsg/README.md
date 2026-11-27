# 2.11BSD `recvmsg` panic: cause, fix and tests

The first tmux milestone binary passed a descriptor over a Unix socket and
panicked the kernel. The cause is a missing assignment in `recvmsg`. This
directory has the one-line fix and two native test programs. The port itself
does not need the fix: native tmux has not called `sendmsg` or `recvmsg` since
protocol 6 (see [../native/IPC.md](../native/IPC.md)).

| File | Purpose |
| --- | --- |
| `recvmsg.patch` | Context diff for `/usr/src/sys/sys/uipc_syscalls.c` 7.1.3 (2.11BSD) 1999/9/13. Apply with `patch -p0`. |
| `fdpass.c` | Smallest reproduction: passes one descriptor to a child, which reads from it. |
| `fdtest.c` | Repeated passes over stream and datagram sockets, several descriptors in one message, descriptors that are never received, and a three-iovec `recvmsg`. |

## Cause

`sendmsg` and `recvmsg` both copy the caller's iovec array into a kernel
array, `aiov`. `sendmsg` then sets `msg.msg_iov = aiov`. `recvmsg` does not,
so `recvit` walks `mp->msg_iov` while it still holds the user's address and
reads that address in kernel data space.

When the caller's iovec is on its stack, the address is at the top of the
64 KiB space, where kernel mode has the I/O page. The read finds no device
and traps. With an iovec at a low address `recvit` would instead read
unrelated kernel data as the iovec; that case was not run.

Nothing in the rights code is involved. In the panic log the line
`unp_internalize(0146734)` is a debug `printf` on the sending side, and the
`sendmsg` that printed it returned normally. The trap comes afterwards, in
the receiver.

## Evidence

Reproduced on November 26, 2026 on an unmodified 2.11BSD patch level 482
system (kernel configuration `PIDP11`) under SIMH as a PDP-11/70 with 4 MB.
The socket sources there have the same `sum` checksums as the public patch
level 479 tree. `fdpass` on the stock kernel:

    parent: sv=3,4 fd=5
    unp_internalize(0146734)
    parent: sendmsg=1 errno=0
    ka6 17545 aps 147324
    pc 175354 ps 30000
    ov 3
    cpuerr 20
    trap type 0
    panic: trap

The Minerva clone had printed the same thing with the tmux binary:

    unp_internalize(0146734)
    ka6 33154 aps 147324
    pc 175306 ps 30000
    ov 3
    trap type 0
    panic: trap

On the patch level 482 kernel `nm` puts `~recvit` at 0175216 in overlay 3,
so the saved PC is `~recvit+0136`. That is the instruction after the first
`iov->iov_len` test in the loop that adds up the iovec lengths:

    ~recvit+0120:   mov     04(r4),r2       / iov = mp->msg_iov
    ~recvit+0124:   clr     0177750(r5)     / i = 0
    ~recvit+0130:   br      ~recvit+0156
    ~recvit+0132:   tst     02(r2)          / iov->iov_len: traps here
    ~recvit+0136:   beq     ~recvit+0146

Minerva's PC, 175306, was not looked up in its own symbol table. Its kernel
is a different build, and its `/unix` was not reachable when this was
written.

## Fix

    	if (u.u_error)
    		return;
    +	msg.msg_iov = aiov;
    	recvit(uap->s, &msg, uap->flags,

## Result with the fix

The same system with the kernel rebuilt from the patched source
(`2.11 BSD UNIX #22`):

    # ./fdpass
    parent: sv=3,4 fd=5
    parent: sendmsg=1 errno=0
    child: recvmsg=1 errno=0 rightslen=2 newfd=3 c=X
    child: read 40 bytes: 2.11 BSD UNIX #21: Fri Mar 28 07:58:13 P
    parent: child status 0
    # ./fdtest iov
    recvmsg=10 errno=0 "ABC" "DEF" "GHIJ"
    # ./fdtest multi
    parent: sendmsg=1 errno=0
    child: got 3 rights: 3 5 6
    child: fd 3 starts "2.11 "
    child: fd 5 starts "root:"
    child: fd 6 starts "wheel"
    parent: child status 0
    # ./fdtest loop 100
    child: received and read 100 of 100, 4 fds open
    parent: sent 100, child status 0, 4 fds open
    # ./fdtest dgram 50
    child: received and read 50 of 50, 4 fds open
    parent: sent 50, child status 0, 4 fds open
    # ./fdtest drop 100
    dropped 100 messages with rights, 3 fds open
    # ./fdtest norights
    recv=1 c=n fds open before 6 after 7

`pstat -T` reported `6/160 files` before and after the run. The kernel's
`unp_internalize` and `unp_externalize` console lines are left out above;
the text read through the passed descriptor is `/etc/motd`, which still
names the previous kernel build.

## Not verified

- Patch levels 483 to 499 and 500. The patch was made and tested against
  482. Patches 483 to 499 were not checked for changes to this file.
- Minerva. Its kernel has not been rebuilt, and the clone was not booted
  with the fix.
- tmux with descriptor passing restored. The native port still uses the
  pathname transport.

## Also seen

- `unp_internalize` and `unp_externalize` in `sys/sys/uipc_usrreq.c` 7.1.2
  each print a line on the console for every message that carries rights.
- Sending a descriptor number that is not open fails, but with the wrong
  `errno`: 40 for descriptor 20, 14 for descriptor 7, and -28 for 99 and
  for -1, where `EBADF` (9) is expected. `unp_internalize` is an `int`
  function that uses the `GETF` macro, and `GETF` ends in a bare `return;`.
- `recv` on a message that carries rights still installs the descriptor in
  the receiver (`fdtest norights`), as in 4.3BSD.

The error paths around these were not reviewed further.

## Terms

`fdpass.c` and `fdtest.c` are part of the port and fall under section 1 of
[../LICENSE.md](../LICENSE.md). `recvmsg.patch` changes a 2.11BSD file and
quotes six of its lines; see the note in that section.
