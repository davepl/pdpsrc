# *                                                                            *
# *  ░███████  ░█████   ░███████        ░████████   ░████████     ░██████      *
# *  ░██   ░██ ░██ ░██  ░██   ░██       ░██    ░██  ░██    ░██   ░██   ░██     *
# *  ░██   ░██ ░██  ░██ ░██   ░██       ░██    ░██  ░██    ░██  ░██            *
# *  ░███████  ░██  ░██ ░███████  ░████ ░████████   ░████████    ░████████     *
# *  ░██       ░██  ░██ ░██             ░██     ░██ ░██     ░██         ░██    *
# *  ░██       ░██ ░██  ░██             ░██     ░██ ░██     ░██  ░██   ░██     *
# *  ░██       ░█████   ░██             ░█████████  ░█████████    ░██████      *
# *                                                                            *

# Dave's Garage PDP-11 BBS Menu System

The PDP-11 BBS Menu System is a curses-based UI designed to feel period-appropriate on VT220 terminals while still running on modern systems. `main.c` drives the UI flow, maintaining global navigation state, stack-based screen transitions, and the breadcrumb header. `data.c` abstracts all file I/O—groups, messages, address book, config, and global locking—to keep storage concerns away from the UI. `platform.c` hides the differences between legacy 2.11BSD curses and modern ncurses by providing helper APIs to draw borders, breadcrumbs, separators, and handle input consistently. Together these modules enable a tidy split: main handles user workflows, data owns persistence, and platform keeps VT220 quirks localized. Build via `make` (pattern rules detect PDP-11 vs modern) and run `./menu` to start the experience; the login screen showcases an ANSI art banner to set the tone.
