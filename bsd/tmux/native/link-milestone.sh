#!/bin/sh
# Generated candidate only. Run from bsd/tmux/upstream.
# Validate final native sizes and runtime behavior before adoption.
../tools/ld211 -i -o tmux /lib/crt0.o clock.o tmux.o ../native/protocol-check.o -Z cmd-last-window.o \
    job.o status.o tty.o compat/asprintf.o -Z cmd-list-clients.o screen-write.o utf8.o window.o -Z \
    input.o server.o server-client.o session.o ../native/libc.o -Z cmd.o cmd-list-sessions.o \
    environ.o grid.o layout.o server-fn.o tty-keys.o osdep-unknown.o -Z client.o cmd-generic.o \
    cmd-list-keys.o cmd-new-session.o cmd-string.o key-bindings.o options.o screen-redraw.o \
    server-window.o tty-term.o window-more.o ../native/terminfo.o -Z buffer.o cmd-bind-key.o \
    cmd-command-prompt.o cmd-new-window.o cmd-send-keys.o cmd-unbind-key.o colour.o grid-view.o \
    key-string.o log.o mode-key.o names.o paste.o resize.o screen.o xmalloc.o compat/bsd-poll.o \
    compat/vis.o compat/unvis.o compat/imsg.o compat/imsg-buffer.o ../native/termios.o \
    ../native/forkpty.o ../native/line-reader.o -Z attributes.o buffer-poll.o cfg.o \
    cmd-attach-session.o cmd-detach-client.o cmd-display-message.o cmd-has-session.o \
    cmd-kill-server.o cmd-kill-session.o cmd-kill-window.o cmd-list.o cmd-list-commands.o \
    cmd-list-windows.o cmd-next-window.o cmd-previous-window.o cmd-refresh-client.o \
    cmd-rename-session.o cmd-rename-window.o cmd-select-window.o cmd-send-prefix.o input-keys.o \
    server-job.o xterm-keys.o compat/getopt.o compat/strcasestr.o compat/strlcat.o compat/strlcpy.o \
    compat/strtonum.o ../native/strings.o ../native/getcwd.o -Y -ltermlib -lc
result=$?
echo TMUX_LINK_STATUS=$result
exit "$result"
