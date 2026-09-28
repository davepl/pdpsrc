/*
 * Compile the production renderer against a tiny fake tty and termcap.
 * The fake terminal interprets emitted operations into a character grid;
 * it knows nothing about the renderer's cache or dirty-span algorithm.
 * Thus randomized frames compare visible results, not a second copy of
 * the implementation.  A few trace assertions also check output economy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>

#define ORIGINAL_FLAGS (ECHO | CRMOD | 32)
static int ttyflags = ORIGINAL_FLAGS, columns = 80, lines = 24;
static int term_result = 1, missing_cap, fail_set, fail_alloc;
static int cursor_x, cursor_y, standout_on, in_control;
static char cells[50][160], attrs[50][160];
static char output[30000];
static unsigned used;
static unsigned flushes;
static size_t largest_allocation;

static int test_putc(int c)
{
    assert(used + 1 < sizeof(output));
    output[used++] = c;
    output[used] = 0;
    if (!in_control) {
        assert(cursor_y >= 0 && cursor_y < 50);
        assert(cursor_x >= 0 && cursor_x < 160);
        /* The rightmost terminal column is deliberately never written. */
        assert(columns == 0 || columns > 160 || cursor_x < columns - 1);
        cells[cursor_y][cursor_x] = c;
        attrs[cursor_y][cursor_x++] = standout_on;
    }
    return c;
}

static int test_flush(FILE *f) { flushes++; return 0; }

static void *test_malloc(size_t bytes)
{
    if (bytes > largest_allocation) largest_allocation = bytes;
    if (fail_alloc) { fail_alloc = 0; return NULL; }
    return malloc(bytes);
}

/* Replace external effects only; all rendering logic is production code. */
#undef putchar
#define putchar test_putc
#define fflush test_flush
#define malloc test_malloc
#include "../screen.c"
#undef putchar
#undef fflush
#undef malloc

int gtty(int fd, struct sgttyb *s)
{
    memset(s, 0, sizeof(*s));
    s->sg_flags = ttyflags;
    return 0;
}

int ioctl(int fd, unsigned long op, void *p)
{
    if (op == TIOCGWINSZ) {
        struct winsize *w = p;
        w->ws_col = columns;
        w->ws_row = lines;
    } else {
        if (fail_set) { fail_set = 0; return -1; }
        ttyflags = ((struct sgttyb *)p)->sg_flags;
    }
    return 0;
}

int tgetent(char *buf, char *term) { return term_result; }

int tgetnum(char *name)
{
    return !strcmp(name, "co") ? 80 : !strcmp(name, "li") ? 65 : -1;
}

char *tgetstr(char *name, char **p)
{
    char *s = *p;
    if ((missing_cap && !strcmp(name, "ce")) ||
        !strcmp(name, "pc") || !strcmp(name, "up") || !strcmp(name, "bc"))
        return NULL;
    *(*p)++ = '<'; *(*p)++ = name[0]; *(*p)++ = name[1];
    *(*p)++ = '>'; *(*p)++ = 0;
    return s;
}

char *tgoto(char *s, int x, int y)
{
    static char b[32];
    sprintf(b, "[%d,%d]", y, x);
    return b;
}

int tputs(char *s, int count, int (*fn)())
{
    if (*s == '[') {
        assert(sscanf(s, "[%d,%d]", &cursor_y, &cursor_x) == 2);
    } else if (!strcmp(s, "<cl>")) {
        memset(cells, ' ', sizeof(cells));
        memset(attrs, 0, sizeof(attrs));
        cursor_x = cursor_y = 0;
    } else if (!strcmp(s, "<ce>")) {
        memset(cells[cursor_y] + cursor_x, ' ', 160 - cursor_x);
        memset(attrs[cursor_y] + cursor_x, 0, 160 - cursor_x);
    } else if (!strcmp(s, "<so>")) standout_on = 1;
    else if (!strcmp(s, "<se>")) standout_on = 0;
    in_control = 1;
    while (*s) fn(*s++);
    in_control = 0;
    return 0;
}

static void reset_output(void) { used = flushes = 0; output[0] = 0; }

static void row_equals(int y, char *text, int reverse)
{
    unsigned i, n = strlen(text);
    for (i = 0; i < 160; i++) {
        assert(cells[y][i] == (i < n ? text[i] : ' '));
        assert(attrs[y][i] == (i < n ? reverse : 0));
    }
}

static void random_frames(void)
{
    unsigned seed = 79;
    char expected[12][40];
    int expected_reverse[12];
    int frame, y, x, n;
    columns = 40; lines = 12;
    assert(screen_start());
    for (frame = 0; frame < 200; frame++) {
        reset_output();
        screen_begin();
        for (y = 0; y < 12; y++) {
            seed = seed * 1103515245U + 12345U;
            n = (seed >> 8) % 31;
            expected_reverse[y] = (seed >> 18) & 1;
            if ((seed >> 20) & 1) {
                for (x = 0; x < n; x++)
                    expected[y][x] = 'a' + ((seed >> (x % 16)) % 26);
                expected[y][n] = 0;
                screen_row(y, expected[y], expected_reverse[y]);
            } else expected[y][0] = 0;
        }
        screen_finish();
        for (y = 0; y < 12; y++)
            row_equals(y, expected[y], expected_reverse[y]);
    }
}

int main(void)
{
    setenv("TERM", "test", 1);
    term_result = 0;
    assert(!screen_start() && screen_error);
    screen_stop(); assert(ttyflags == ORIGINAL_FLAGS);
    term_result = 1; missing_cap = 1;
    assert(!screen_start() && screen_error);
    screen_stop(); assert(ttyflags == ORIGINAL_FLAGS);
    missing_cap = 0;

    assert(screen_start()); assert(screen_cols == 80 && screen_rows == 24);
    assert(ttyflags == (CBREAK | 32));
    reset_output(); screen_begin();
    screen_row(0, "12:34:56", 0); screen_row(3, "abcdef", 0); screen_finish();
    assert(strstr(output, "[0,0]12:34:56<ce>") &&
        strstr(output, "[3,0]abcdef<ce>"));
    row_equals(0, "12:34:56", 0); row_equals(3, "abcdef", 0);
    reset_output(); screen_begin();
    screen_row(0, "12:34:56", 0); screen_row(3, "abcdef", 0); screen_finish();
    /* An identical frame must cost neither bytes nor a flush/write call. */
    assert(used == 0 && flushes == 0);
    reset_output(); screen_begin();
    screen_row(0, "12:34:57", 0); screen_row(3, "abc", 0); screen_finish();
    assert(!strcmp(output, "[0,7]7[3,3]<ce>[0,0]"));
    row_equals(3, "abc", 0);
    reset_output(); screen_begin(); screen_row(0, "12:34:57", 0); screen_finish();
    assert(!strcmp(output, "[3,0]<ce>[0,0]")); row_equals(3, "", 0);
    reset_output(); screen_begin(); screen_row(0, "12:34:57", 1); screen_finish();
    assert(!strcmp(output, "[0,0]<so>12:34:57<se>[0,0]"));
    row_equals(0, "12:34:57", 1);
    screen_row(1, "A\nB\177\200C", 0); screen_flush();
    row_equals(1, "A?B??C", 0); row_equals(0, "12:34:57", 1);
    reset_output(); screen_flush();
    assert(used == 0 && flushes == 0);
    /* Explicit clearing still reaches the terminal even without row text. */
    screen_clear(); screen_flush();
    assert(!strcmp(output, "<cl>[0,0]") && flushes == 1);

    random_frames();
    columns = lines = 65535;
    assert(screen_start()); assert(screen_cols == 160 && screen_rows == 31);
    columns = lines = 0;
    assert(screen_start()); assert(screen_cols == 80 && screen_rows == 50);
    columns = lines = 1;
    assert(screen_start()); assert(screen_cols == 1 && screen_rows == 1);
    reset_output(); screen_begin(); screen_row(0, "xx", 0); screen_finish();
    assert(!strstr(output, "x"));
    assert(largest_allocation <= 5000);

    /* Both failure paths leave stop able to restore the original tty. */
    columns = 80; lines = 24; fail_alloc = 1;
    assert(!screen_start() && screen_error);
    screen_stop(); assert(ttyflags == ORIGINAL_FLAGS);
    assert(screen_start()); fail_set = 1;
    assert(!screen_start() && screen_error);
    screen_stop(); assert(ttyflags == ORIGINAL_FLAGS);
    puts("screen tests passed: startup failures, unchanged frames, changed spans, attributes,");
    puts("200 randomized frames, resize bounds, clipping and tty restoration");
    return 0;
}
