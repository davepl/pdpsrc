/*
 * A termcap screen with one bounded character image.  Changed spans are
 * emitted directly; there are no curses windows or second screen image.
 * Written for the native 2.11BSD compiler and sgtty terminal interface.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sgtty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

#define MAXCOLS 160
#define MAXROWS 50
#define MAXCELLS 5000

/* libtermcap uses these historical globals for cursor compensation and
 * padding. tputs honors terminal delays; plain fputs would print them. */
char PC, *BC, *UP;
short ospeed;
char *tgetstr(), *tgoto();
int tgetent(), tgetnum(), tputs();

int screen_rows, screen_cols;
char *screen_error;

/* Terminal output must be batched explicitly. On this libc, relying on
 * the default buffering can turn each putchar into a separate write.
 * setbuf requires exactly the library's BUFSIZ capacity, not a guessed
 * byte count. A frame ends with fflush, so batching adds no input delay. */
static char output_buffer[BUFSIZ];
static char *image, *cap_store;
static char *cm, *cl, *ce, *so, *se, *ti, *te, *ks, *ke;
static char valid[MAXROWS], seen[MAXROWS], reverse_row[MAXROWS];
static unsigned char row_length[MAXROWS];
static int term_rows, term_cols, tty_saved, active;
static int output_pending;
static struct sgttyb savedtty;

static int
outc(c)
int c;
{
    output_pending = 1;
    return putchar(c);
}

static void
emit(s, rows)
char *s;
int rows;
{
    if (s && *s) tputs(s, rows, outc);
}

static void
position(y, x)
int y, x;
{
    emit(tgoto(cm, x, y), 1);
}

static int
capabilities()
{
    char entry[1024], area[1024], *raw[12], *p, *term;
    char **dest[12];
    int i, bytes;

    term = getenv("TERM");
    if (!term || !*term || tgetent(entry, term) != 1) {
        screen_error = "unknown terminal; set TERM to a termcap name";
        return 0;
    }
    term_cols = tgetnum("co");
    term_rows = tgetnum("li");
    /* termcap describes terminal-specific escapes; do not assume ANSI.
     * Its input buffer and decoded strings are temporary stack storage.
     * Keep only the decoded bytes we need once initialization returns. */
    p = area;
    raw[0] = tgetstr("cm", &p); dest[0] = &cm;
    raw[1] = tgetstr("cl", &p); dest[1] = &cl;
    raw[2] = tgetstr("ce", &p); dest[2] = &ce;
    raw[3] = tgetstr("so", &p); dest[3] = &so;
    raw[4] = tgetstr("se", &p); dest[4] = &se;
    raw[5] = tgetstr("ti", &p); dest[5] = &ti;
    raw[6] = tgetstr("te", &p); dest[6] = &te;
    raw[7] = tgetstr("ks", &p); dest[7] = &ks;
    raw[8] = tgetstr("ke", &p); dest[8] = &ke;
    raw[9] = tgetstr("bc", &p); dest[9] = &BC;
    raw[10] = tgetstr("up", &p); dest[10] = &UP;
    raw[11] = tgetstr("pc", &p);
    if (!raw[0] || !*raw[0] || !raw[1] || !*raw[1] ||
        !raw[2] || !*raw[2]) {
        screen_error = "terminal needs cursor addressing and clear capabilities";
        return 0;
    }
    bytes = p - area;
    cap_store = (char *)malloc(bytes);
    if (!cap_store) {
        screen_error = "not enough data space for terminal capabilities";
        return 0;
    }
    memcpy(cap_store, area, bytes);
    for (i = 0; i < 11; i++)
        *dest[i] = raw[i] ? cap_store + (raw[i] - area) : NULL;
    PC = raw[11] ? *raw[11] : 0;
    /* A magic-cookie standout mode consumes screen columns. */
    if (!so || !se || tgetnum("sg") > 0) so = se = NULL;
    return 1;
}

int
screen_start()
{
    struct sgttyb tty;
    struct winsize ws;
    int width, height;

    screen_error = NULL;
    if (!cap_store) {
        setbuf(stdout, output_buffer);
        if (!capabilities()) return 0;
    }
    /* Save once, before our first mode change.  A resize must not replace
     * the saved login-shell settings with our own no-echo settings. */
    if (!tty_saved) {
        if (gtty(0, &savedtty) < 0) {
            screen_error = "cannot read terminal settings";
            return 0;
        }
        tty_saved = 1;
    }
    ospeed = savedtty.sg_ospeed;
    width = term_cols > 0 ? term_cols : 80;
    height = term_rows > 0 ? term_rows : 24;
    if (ioctl(0, TIOCGWINSZ, &ws) >= 0) {
        /* Check unsigned tty values before assigning to a 16-bit int. */
        if (ws.ws_col)
            width = ws.ws_col > MAXCOLS ? MAXCOLS : ws.ws_col;
        if (ws.ws_row)
            height = ws.ws_row > MAXROWS ? MAXROWS : ws.ws_row;
    }
    if (width > MAXCOLS) width = MAXCOLS;
    if (height > MAXROWS) height = MAXROWS;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    /* Clamp each factor first: even their product then fits in a PDP-11
     * signed int (160 * 50).  The single cache can never exceed 5000
     * bytes, independently of a huge xterm entry or reported window. */
    if (width * height > MAXCELLS) height = MAXCELLS / width;
    if (!image || width != screen_cols || height != screen_rows) {
        if (image) free(image);
        image = (char *)malloc(width * height);
        if (!image) {
            screen_error = "not enough data space for the screen";
            return 0;
        }
    }
    screen_cols = width;
    screen_rows = height;
    tty = savedtty;
    /* CBREAK delivers keys immediately while retaining interrupt signals.
     * TIOCSETN changes flags without discarding an already typed key. */
    tty.sg_flags &= ~(ECHO | CRMOD | RAW);
    tty.sg_flags |= CBREAK;
    if (ioctl(0, TIOCSETN, &tty) < 0) {
        screen_error = "cannot set terminal input mode";
        return 0;
    }
    if (!active) {
        active = 1;
        emit(ti, 1);
        emit(ks, 1);
    }
    screen_clear();
    return 1;
}

void
screen_clear()
{
    memset(valid, 0, sizeof(valid));
    if (active) emit(cl, screen_rows);
}

void
screen_begin()
{
    memset(seen, 0, sizeof(seen));
}

void
screen_row(y, text, reverse)
int y, reverse;
char *text;
{
    char line[MAXCOLS];
    /* Explicit register hints matter to this native compiler. These three
     * values are reused while clipping and comparing characters, so keep
     * them in word registers instead of repeatedly fetching stack locals. */
    register char *old;
    register int n, start;
    int end, oldlen, same, i;

    if (!active || y < 0 || y >= screen_rows) return;
    seen[y] = 1;
    reverse = reverse && so != NULL;
    old = image + y * screen_cols;
    same = valid[y] && reverse_row[y] == reverse;
    /* Cached text is already sanitized. Exact equality therefore proves
     * that another copy/filter/length pass cannot change the visible row.
     * Truncated or unsanitized strings take the ordinary bounded path. */
    if (same && !strcmp(old, text)) return;
    for (n = 0; text[n] && n < screen_cols - 1; n++)
        line[n] = text[n] >= ' ' && text[n] <= '~' ? text[n] : '?';
    line[n] = 0;
    oldlen = valid[y] ? row_length[y] : 0;
    start = 0;
    /* Unchanged cells keep both their characters and their attributes.
     * Skip an equal prefix/suffix so a clock tick need not retransmit a
     * complete row across a slow serial line.  An attribute change must
     * rewrite the whole row even when its characters are identical. */
    if (same) {
        while (start < n && old[start] == line[start]) start++;
        if (start == n && oldlen == n) return;
    }
    end = n;
    if (same && oldlen == n)
        while (end > start && old[end - 1] == line[end - 1]) end--;
    position(y, start);
    if (reverse) emit(so, 1);
    for (i = start; i < end; i++) putchar(line[i]);
    if (reverse) emit(se, 1);
    /* Clear only when shortening, or when this row has no known image. */
    if (!valid[y] || oldlen > n) emit(ce, 1);
    strcpy(old, line);
    row_length[y] = n;
    reverse_row[y] = reverse;
    valid[y] = 1;
}

void
screen_flush()
{
    /* Sampling can find an unchanged screen. Do not turn that no-op into
     * a cursor-address sequence and a terminal write on every refresh.
     * One flag is enough: no second screen image or output queue is needed.
     * Row text always follows position(), whose output marks this flag. */
    if (!output_pending) return;
    if (active) position(0, 0);
    fflush(stdout);
    output_pending = 0;
}

void
screen_finish()
{
    int y;
    /* Rows may disappear after a process exits, filtering, or closing
     * help.  Marking visited rows clears those gaps as well as the tail. */
    for (y = 0; y < screen_rows; y++)
        if (!seen[y] && valid[y] && image[y * screen_cols])
            screen_row(y, "", 0);
    screen_flush();
}

void
screen_stop()
{
    if (active) {
        position(screen_rows - 1, 0);
        emit(ce, 1);
        emit(ke, 1);
        emit(te, 1);
        fflush(stdout);
        output_pending = 0;
        active = 0;
    }
    /* Also restore after a partially failed startup.  Terminal settings
     * belong to the caller and must survive every normal error path. */
    if (tty_saved) ioctl(0, TIOCSETN, &savedtty);
}
