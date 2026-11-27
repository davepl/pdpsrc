# Native client/server IPC

Native builds use protocol 7. The client sends its `ttyname` pathname in
`MSG_IDENTIFY`, and the server validates the named character device, opens it,
and checks that the opened terminal matches. Both client and server must use
the same protocol version. Ordinary host builds keep protocol 5 and their
original descriptor passing.

Because the server opens the terminal itself, it needs permission to: the
terminal must belong to the user the server runs as, or be readable and
writable by that user, or the server must run as root. With descriptor passing
the client's own open descriptor carried that access. A root client attaching
to another user's server therefore has to `chmod o+rw` its terminal first.
The native client compares the socket's owner with the terminal's owner and
mode before identifying, and when the server's open is certain to fail it
prints the server's user and that command and exits.

Protocol 6 introduced the pathname transport. Protocol 7 keeps it and makes a
connection cheap enough for the native data space:

| Field | Native bound | Host bound |
| --- | --- | --- |
| Packed command line (`COMMAND_LENGTH`) | 250 | 2048 |
| Environment variable (`ENVIRON_LENGTH`) | 256 | 1024 |
| Working directory (`CWD_LENGTH`) | 190 | `MAXPATHLEN` |
| `TERM` (`TERMINAL_LENGTH`) | 32 | 128 |
| Terminal pathname (`TTY_PATH_LENGTH`) | 32 | 128 |
| Printed line (`PRINT_LENGTH`) | 256 | 512 |
| Default shell (`SHELL_LENGTH`) | 128 | `MAXPATHLEN` |

Every native payload is at most 256 bytes, and `READ_BUF_SIZE` and
`MAX_IMSGSIZE` are 272. The server holds one read buffer per connected client
and copies each received payload once into its heap, so these bounds replaced
a 2.3 KiB buffer and a 2 KiB copy per command. `MSG_PRINT` and `MSG_ERROR` are
sent at their used length, including the terminating NUL, and the native
client accepts any length from 1 to `PRINT_LENGTH`: a command's whole output
is queued in the server's heap until the client reads it, and a fixed record
per line made `list-keys` cost more than the screens. A command line that does
not fit is refused with the existing "command too long" error, a working
directory that does not fit is sent empty, and an environment variable that
does not fit is not sent.

2.11BSD returns `EWOULDBLOCK` (35) when a non-blocking read, write or accept
cannot proceed; its `EAGAIN` (11) only reports a full process table. Native
units that include `<errno.h>` therefore get `EAGAIN` defined as `EWOULDBLOCK`
from `compat.h`, which is what every `EAGAIN` test in the IPC and terminal
paths means.

Native imsg uses `read` and `writev` over the existing local Unix stream sockets.
It does not call `sendmsg`/`recvmsg` or transmit access rights. Attempts to compose
or receive descriptor-bearing native messages fail with `EOPNOTSUPP` rather
than silently losing a descriptor. The write path retains its 15-vector and
32767-byte bounds and handles partial writes. This remains a native PDP-11 port
using the existing kernel; no external gateway or kernel modification is needed.

## Reproduction behind the change

An isolated SIMH clone of Minerva reproduced the kernel panic when launching the
exact previous milestone binary. The console reached `unp_internalize` and then
reported trap 0.

That pre-transport binary linked with resident text 28224 bytes, data 25046 bytes
and BSS 1452 bytes, plus seven overlay banks (six at 24576 bytes, one at 5376).
These are historical baseline measurements, not sizes or runtime results for
the protocol-6 executable. Overlays do not enlarge the flat data/heap/stack space.

The cause was found on November 26, 2026, and it is in `recvmsg`, not in the
rights code. `recvmsg` in 2.11BSD `uipc_syscalls.c` version 7.1.3 (1999/9/13)
copies the caller's iovec array into the kernel but never points `msg.msg_iov`
at the copy, so `recvit` reads the user's iovec address as a kernel address.
With the iovec on the user stack, that address is in the kernel's I/O page and
the read traps. The `unp_internalize` line is a debug `printf` from the sender,
whose `sendmsg` had already succeeded.

A short test program reproduces the panic on a stock patch level 482 system, and
a one-line fix makes descriptor passing work there.
[../kernel-recvmsg/](../kernel-recvmsg/README.md) has the patch, the test
programs and the evidence. Minerva's kernel was not changed, and the port does
not depend on the fix.

## Validation status

Host tests exercise the native imsg branches with deterministic I/O shims,
including framing, partial I/O, retries, errors, EOF, capacity bounds and explicit
FD rejection. An object-symbol check verifies no native `sendmsg`/`recvmsg`
references. A separate host test still transfers a real descriptor through the
ordinary host branch.

The protocol-7 executable passed live acceptance on Minerva on November 26,
2026: two populated windows with switching and detach/reattach, and two
sessions with two clients attached at once, with every shell PID preserved.
Resizing has not been exercised natively. The measurements are in the
[port README](../README.md#native-results-and-measured-memory).
