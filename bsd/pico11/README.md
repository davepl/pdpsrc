# pico11

`pico11` is a small nano-like full-screen editor intended for a split-I/D
PDP-11 running 2.11BSD.  It also builds on macOS and Linux.

The document is not loaded into memory.  Two unlinked scratch files form a
disk-backed gap at the cursor: text before the cursor is in one file and text
after it is stored in reverse in the other.  Editing and cursor motion need
bounded memory, and saving streams both halves into a replacement file.

Build with:

    make

Run the disk-buffer regression test with:

    make test

To upload the source to the configured PDP-11 over FTP and build it there
over telnet, run on the development machine:

    ./deploy-pdp11.sh

The script prompts for the remote password.  Set `PDP11_RUN_TESTS=1` to run
the comparatively slow disk-buffer regression test on the PDP-11 as well.
The host, user, and destination can be overridden with `PDP11_HOST`,
`PDP11_USER`, and `PDP11_REMOTE_DIR`.

On 2.11BSD the Makefile detects `uname -s` and passes `-i` at link time to
produce a split-I/D executable.  The Makefile intentionally uses only basic
make and Version 7 shell syntax.

Run with:

    ./pico11 [file]

Supported commands include arrow keys, Home/End, Page Up/Page Down, `^A`,
`^E`, `^B`, `^F`, `^P`, `^N`, `^D`, `^G` (help), `^O` (write), `^R`
(insert file), `^W` (search), `^K` (cut), `^U` (uncut), `^C` (position),
and `^X` (exit).

The display expects an ANSI/VT100-compatible terminal.  Window size is read
with `TIOCGWINSZ`, with an 80x24 fallback.
