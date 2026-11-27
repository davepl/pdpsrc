#!/bin/sh
# Generated candidate only. Run from bsd/tmux/upstream.
# Validate final native sizes and runtime behavior before adoption.
../tools/ld211 -i -o tmux /lib/crt0.o clock.o tmux.o ../native/protocol-check.o ../native/xstr.o -Z \
    cmd-list-sessions.o environ.o tty.o window-copy.o -Z cmd-bind-key.o server.o status.o window.o \
    osdep-unknown.o -Z cmd-kill-server.o cmd-send-keys.o input.o screen-write.o utf8.o -Z cmd.o \
    cmd-generic.o grid.o server-client.o tty-keys.o -Z grid-view.o job.o layout.o options.o \
    screen-redraw.o server-fn.o session.o tty-term.o ../native/terminfo.o -Z client.o \
    cmd-command-prompt.o cmd-new-session.o cmd-new-window.o cmd-string.o colour.o grid-history.o \
    key-bindings.o log.o mode-key.o screen.o server-window.o window-more.o compat/vis.o \
    compat/imsg.o compat/imsg-buffer.o -Z attributes.o buffer.o buffer-poll.o cfg.o client-tty.o \
    cmd-attach-session.o cmd-copy-mode.o cmd-detach-client.o cmd-display-message.o cmd-has-session.o \
    cmd-kill-session.o cmd-kill-window.o cmd-last-window.o cmd-list.o cmd-list-clients.o \
    cmd-list-commands.o cmd-list-keys.o cmd-list-windows.o cmd-next-window.o cmd-paste-buffer.o \
    cmd-previous-window.o cmd-refresh-client.o cmd-rename-session.o cmd-rename-window.o \
    cmd-select-window.o cmd-send-prefix.o cmd-unbind-key.o input-keys.o key-string.o names.o paste.o \
    resize.o server-job.o xmalloc.o xterm-keys.o compat/asprintf.o compat/bsd-poll.o compat/getopt.o \
    compat/strcasestr.o compat/strlcat.o compat/strlcpy.o compat/strtonum.o compat/unvis.o \
    ../native/strings.o ../native/getcwd.o ../native/termios.o ../native/forkpty.o ../native/libc.o \
    ../native/line-reader.o -Y -lvmf -ltermlib -lc
result=$?
echo TMUX_LINK_STATUS=$result
exit "$result"
