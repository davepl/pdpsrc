#!/bin/sh
# Generated candidate only. Run from bsd/tmux/upstream.
# Validate final native sizes and runtime behavior before adoption.
../tools/ld211 -i -o tmux-full /lib/crt0.o tmux.o ../native/protocol-check.o -Z cmd-link-window.o \
    status.o tty.o tty-keys.o -Z cmd-send-keys.o cmd-start-server.o server.o window.o window-copy.o \
    osdep-unknown.o -Z cmd-find-window.o input.o screen-write.o utf8.o compat/strcasestr.o -Z cmd.o \
    grid.o job.o key-bindings.o server-client.o session.o -Z cmd-bind-key.o cmd-generic.o \
    cmd-kill-server.o cmd-lock-server.o environ.o layout.o layout-set.o screen-redraw.o server-fn.o \
    window-choose.o ../native/terminfo.o -Z client.o cmd-new-session.o cmd-server-info.o \
    cmd-split-window.o cmd-string.o grid-view.o mode-key.o options.o options-cmd.o server-window.o \
    tty-term.o window-more.o compat/imsg.o compat/imsg-buffer.o -Z attributes.o buffer.o \
    cmd-choose-session.o cmd-choose-window.o cmd-command-prompt.o cmd-copy-buffer.o cmd-list.o \
    cmd-list-keys.o cmd-new-window.o cmd-rotate-window.o cmd-set-option.o cmd-set-window-option.o \
    cmd-swap-pane.o cmd-unbind-key.o colour.o key-string.o log.o names.o paste.o resize.o screen.o \
    compat/bsd-poll.o compat/strlcat.o compat/vis.o compat/unvis.o ../native/termios.o \
    ../native/forkpty.o -Z buffer-poll.o cfg.o clock.o cmd-attach-session.o cmd-break-pane.o \
    cmd-choose-client.o cmd-clear-history.o cmd-clock-mode.o cmd-confirm-before.o cmd-copy-mode.o \
    cmd-delete-buffer.o cmd-detach-client.o cmd-display-message.o cmd-display-panes.o \
    cmd-down-pane.o cmd-has-session.o cmd-if-shell.o cmd-kill-pane.o cmd-kill-session.o \
    cmd-kill-window.o cmd-last-window.o cmd-list-buffers.o cmd-list-clients.o cmd-list-commands.o \
    cmd-list-panes.o cmd-list-sessions.o cmd-list-windows.o cmd-load-buffer.o cmd-lock-client.o \
    cmd-lock-session.o cmd-move-window.o cmd-next-layout.o cmd-next-window.o cmd-paste-buffer.o \
    cmd-pipe-pane.o cmd-previous-layout.o cmd-previous-window.o cmd-refresh-client.o \
    cmd-rename-session.o cmd-rename-window.o cmd-resize-pane.o cmd-respawn-window.o cmd-run-shell.o \
    cmd-save-buffer.o cmd-select-layout.o cmd-select-pane.o cmd-select-prompt.o cmd-select-window.o \
    cmd-send-prefix.o cmd-set-buffer.o cmd-set-environment.o cmd-show-buffer.o \
    cmd-show-environment.o cmd-show-options.o cmd-show-window-options.o cmd-source-file.o \
    cmd-suspend-client.o cmd-swap-window.o cmd-switch-client.o cmd-unlink-window.o cmd-up-pane.o \
    input-keys.o server-job.o window-clock.o xmalloc.o xterm-keys.o compat/asprintf.o \
    compat/getopt.o compat/strlcpy.o compat/strtonum.o ../native/strings.o ../native/getcwd.o \
    ../native/libc.o ../native/line-reader.o -Y -ltermlib -lc
result=$?
echo TMUX_LINK_STATUS=$result
exit "$result"
