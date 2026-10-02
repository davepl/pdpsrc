/* Host-only stand-ins for the small part of sgtty used by screen.c.
 * These exercise behavior; they are not a substitute for PDP-11 ABI tests. */
#ifndef TOP_TEST_IOCTL_H
#define TOP_TEST_IOCTL_H
struct sgttyb {
    char sg_ispeed, sg_ospeed, sg_erase, sg_kill;
    int sg_flags;
};
struct winsize {
    unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define ECHO 1
#define CRMOD 2
#define RAW 4
#define CBREAK 8
#define TIOCGWINSZ 1
#define TIOCSETN 2
int gtty(int, struct sgttyb *);
int ioctl(int, unsigned long, void *);
#endif
