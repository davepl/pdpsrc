#/******************************************************************************
# *                                                                            *
# *  ░███████  ░█████   ░███████        ░████████   ░████████     ░██████      *
# *  ░██   ░██ ░██ ░██  ░██   ░██       ░██    ░██  ░██    ░██   ░██   ░██     *
# *  ░██   ░██ ░██  ░██ ░██   ░██       ░██    ░██  ░██    ░██  ░██            *
# *  ░███████  ░██  ░██ ░███████  ░████ ░████████   ░████████    ░████████     *
# *  ░██       ░██  ░██ ░██             ░██     ░██ ░██     ░██         ░██    *
# *  ░██       ░██ ░██  ░██             ░██     ░██ ░██     ░██  ░██   ░██     *
# *  ░██       ░█████   ░██             ░█████████  ░█████████    ░██████      *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  PROGRAM:     DAVE'S GARAGE PDP-11 BBS MENU SYSTEM                         *
# *  MODULE:      PLATFORM.C                                                   *
# *  VERSION:     0.2                                                          *
# *  DATE:        NOVEMBER 2025                                                *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  DESCRIPTION:                                                              *
# *                                                                            *
# *    Encapsulates all platform-specific UI helpers: borders, headers,        *
# *    breadcrumbs, separators, input handling, reverse video, and legacy      *
# *    DEC special-graphics drawing. Abstracts VT220 vs. modern ncurses        *
# *    differences behind consistent APIs used by main.c.                      *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  AUTHOR:      DAVE PLUMMER                                                 *
# *  LICENSE:     GPL 2.0                                                      *
# *                                                                            *
# ******************************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include "platform.h"

static int platform_capture_input(int y, int x, char *buf, int maxlen);

#ifdef LEGACY_CURSES

// Toggles cursor visibility using VT220 escape codes or curses API.

void
platform_set_cursor(int visible)
{
    const char *seq = visible ? "\033[?25h" : "\033[?25l";
    fputs(seq, stdout);
    fflush(stdout);
}

// Paints the screen border using DEC graphics for legacy terminals.

void
platform_draw_border(void)
{
    int col;
    int row;

    fputs("\033(0", stdout);

    printf("\033[1;1H");
    putchar('l');
    for (col = 1; col < COLS - 1; ++col)
        putchar('q');
    putchar('k');

    for (row = 1; row < LINES - 1; ++row) {
        printf("\033[%d;1H", row + 1);
        putchar('x');
        printf("\033[%d;%dH", row + 1, COLS);
        putchar('x');
    }

    printf("\033[%d;1H", LINES);
    putchar('m');
    for (col = 1; col < COLS - 1; ++col)
        putchar('q');
    putchar('j');

    fputs("\033(B", stdout);
    fflush(stdout);
}

// Renders the top banner with title and user info in reverse video.

void
platform_draw_header_line(const char *left, const char *right)
{
    int gap;

    move(1, 1);
    platform_reverse_on();
    addch(' ');
    addstr(left);

    gap = COLS - (int)strlen(right) - (int)strlen(left) - 4;
    if (gap < 0)
        gap = 0;
    while (gap-- > 0)
        addch(' ');

    addstr(right);
    addch(' ');
    platform_reverse_off();
}

// Displays the breadcrumb line below the banner, padded edge to edge.

void
platform_draw_breadcrumb(const char *text)
{
    int col;

    platform_reverse_on();
    move(2, 1);
    for (col = 1; col < COLS - 1; ++col)
        addch(' ');
    mvprintw(2, 2, "%s", text ? text : "");
    platform_reverse_off();
    refresh();
}

// Draws the menu separator line using DEC graphics characters.

void
platform_draw_separator(int row)
{
    int col;

    row = LINES - PROMPT_ROW_OFFSET - 1;
    fputs("\033(0", stdout);
    printf("\033[%d;1H", row + 1);
    putchar('t');
    for (col = 1; col < COLS - 1; ++col)
        putchar('q');
    putchar('u');
    fputs("\033(B", stdout);
    fflush(stdout);
}

// Reads keyboard input on legacy terminals via custom mvgetnstr logic.

void
platform_read_input(int y, int x, char *buf, int maxlen)
{
    platform_capture_input(y, x, buf, maxlen);
}

// Draws a sample box using DEC special graphics; useful for demos.

void
legacy_draw_box(int top, int left, int height, int width)
{
    int row;
    int col;
    int bottom = top + height - 1;
    int right = left + width - 1;

    if (height < 2 || width < 2)
        return;

    fputs("\033(0", stdout);

    printf("\033[%d;%dH", top + 1, left + 1);
    putchar('l');
    for (col = left + 1; col < right; ++col)
        putchar('q');
    putchar('k');

    printf("\033[%d;%dH", bottom + 1, left + 1);
    putchar('m');
    for (col = left + 1; col < right; ++col)
        putchar('q');
    putchar('j');

    for (row = top + 1; row < bottom; ++row) {
        printf("\033[%d;%dH", row + 1, left + 1);
        putchar('x');
        printf("\033[%d;%dH", row + 1, right + 1);
        putchar('x');
    }

    fputs("\033(B", stdout);
    fflush(stdout);
}

// Enables reverse video under legacy curses using standout().

void
platform_reverse_on(void)
{
    standout();
}

// Disables reverse video and returns to normal text attributes.

void
platform_reverse_off(void)
{
    standend();
}

#else

// Modern curses cursor toggle using curses API.

void
platform_set_cursor(int visible)
{
    curs_set(visible);
}

// Draws the UI border using ncurses ACS line-drawing characters.

void
platform_draw_border(void)
{
    int col;
    for (col = 0; col < COLS; ++col) {
        mvaddch(0, col, ACS_HLINE);
        mvaddch(LINES - 1, col, ACS_HLINE);
    }
    for (col = 0; col < LINES; ++col) {
        mvaddch(col, 0, ACS_VLINE);
        mvaddch(col, COLS - 1, ACS_VLINE);
    }
    mvaddch(0, 0, ACS_ULCORNER);
    mvaddch(0, COLS - 1, ACS_URCORNER);
    mvaddch(LINES - 1, 0, ACS_LLCORNER);
    mvaddch(LINES - 1, COLS - 1, ACS_LRCORNER);
}

// Draws the header banner in reverse video on modern systems.
void
platform_draw_header_line(const char *left, const char *right)
{
    int col;
    int right_col;

    platform_reverse_on();
    move(1, 1);
    for (col = 1; col < COLS - 1; ++col)
        addch(' ');
    mvprintw(1, 2, "%s", left);
    right_col = COLS - (int)strlen(right) - 2;
    if (right_col < 2)
        right_col = 2;
    mvprintw(1, right_col, "%s", right);
    platform_reverse_off();
}

// Paints the breadcrumb line using standard ncurses calls.
void
platform_draw_breadcrumb(const char *text)
{
    int col;

    platform_reverse_on();
    move(2, 1);
    for (col = 1; col < COLS - 1; ++col)
        addch(' ');
    mvprintw(2, 2, "%s", text ? text : "");
    platform_reverse_off();
}

// Draws the bottom separator line in ncurses.
void
platform_draw_separator(int row)
{
    int col;
    row = LINES - PROMPT_ROW_OFFSET - 1;
    mvaddch(row, 0, ACS_LTEE);
    for (col = 1; col < COLS - 1; ++col)
        mvaddch(row, col, ACS_HLINE);
    mvaddch(row, COLS - 1, ACS_RTEE);
}

// Reads user input using custom handler for consistent behavior.
void
platform_read_input(int y, int x, char *buf, int maxlen)
{
    platform_capture_input(y, x, buf, maxlen);
}

// Enables reverse video (standout) in ncurses.
void
platform_reverse_on(void)
{
    attron(A_REVERSE);
}

// Disables reverse video (standout) in ncurses.
void
platform_reverse_off(void)
{
    attroff(A_REVERSE);
}

#endif

// Shared input handler for both legacy and modern builds.
static int
platform_capture_input(int y, int x, char *buf, int maxlen)
{
    int len;
    int limit;
    int ch;

    if (buf == NULL || maxlen <= 0)
        return 0;
    limit = maxlen;
    len = 0;
    buf[0] = '\0';
    move(y, x);
    refresh();
    while (1) {
        ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            break;
        } else if (ch == '\b' || ch == 127 || ch == CTRL_KEY('H') || ch == KEY_BACKSPACE) {
            if (len > 0) {
                --len;
                buf[len] = '\0';
                mvaddch(y, x + len, ' ');
                move(y, x + len);
                refresh();
            }
        } else if (ch == CTRL_KEY('U')) {
            while (len > 0) {
                --len;
                mvaddch(y, x + len, ' ');
            }
            move(y, x);
            buf[0] = '\0';
            refresh();
        } else if (isprint(ch)) {
            if (len < limit) {
                buf[len++] = (char)ch;
                buf[len] = '\0';
                mvaddch(y, x + len - 1, ch);
                move(y, x + len);
                refresh();
            }
        }
    }
    buf[len] = '\0';
    return len;
}
