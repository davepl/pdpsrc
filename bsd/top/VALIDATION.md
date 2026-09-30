# Live verification

Built and tested on the user's PDP over Telnet at 127.0.0.1:2323, using
FTP at 127.0.0.1:2121 to transfer source. No simulated process statistics
or fixture values are used by the monitor.

- Guest: mentec, 2.11BSD patch level 498, PDP-11/73.
- Directory: /home/dave/source/repos/pdpsrc/bsd/top.
- Build: cc -O -c top.c; cc -i -o top top.o -lcurses -ltermcap.
- Native executable: 53,772 bytes, with 33,856 bytes text, 4,366 data,
  and 7,696 BSS before runtime allocations.

51 live display/interaction assertions and 16 lifecycle/size-limit
assertions passed. They covered:

- The reference's five summary rows, blank separator, reverse-video
  column heading, column spacing, and normal-video process rows.
- Real names and CPU times, continuously advancing clock, task-state
  totals, CPU percentages totaling 100%, and memory/swap arithmetic.
- PID and CPU-time sorting, idle filtering, help, interval changes,
  invalid options, combined options, and process-row limits.
- 120x30, 80x24 and 132x36 terminals and resizing while running.
- Clean q exit, Ctrl-C return to the shell, and restoration of tty flags.
- A temporary workload with more than 32 processes, including a stopped
  child and an unreaped zombie. The monitor grew its display array,
  reported both states, and kept zombie memory fields separate from the
  overlaid live-process fields. The workload and children were cleaned up.
- A 200x80 terminal capped within the PDP data-space budget, a 30x8
  terminal showing a resize notice, and recovery to the normal display.

The final capture uses a 120x30 VT100 Telnet session and ./top -n 17 to
match the reference's 17 visible process rows. The PNG is a rendering of
that captured live terminal stream, using a matching monospace font.
Actual times, processes, and resource use naturally differ from the image.

The old telnetd requires the client to offer NAWS for automatic window
size updates. Instructions are included in README. The system-installed
/usr/ucb/top was not replaced.
