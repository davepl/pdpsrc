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
# *  MODULE:      MENU.C                                                       *
# *  VERSION:     0.2                                                          *
# *  DATE:        NOVEMBER 2025                                                *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  DESCRIPTION:                                                              *
# *                                                                            *
# *    Centralizes all menu related code                                       *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  AUTHOR:      DAVE PLUMMER                                                 *
# *  LICENSE:     GPL 2.0                                                      *
# *                                                                            *
# ******************************************************************************/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <curses.h>
#include "platform.h"
#include "menu.h"

static void
menu_draw_highlighted(int row, int col, int width, int highlighted, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

#ifdef LEGACY_CURSES
    {
        int fill = width > 0 ? width : (int)strlen(buf);
        printf("\033[%d;%dH", row + 1, col + 1);
        if (highlighted)
            printf("\033[7m%s\033[0m", buf);
        else
            printf("%s", buf);
        if (width > 0 && (int)strlen(buf) < fill) {
            int pad = fill - (int)strlen(buf);
            while (pad-- > 0)
                printf(" ");
        }
        fflush(stdout);
    }
#else
    if (highlighted)
        attron(A_REVERSE);
    if (width > 0)
        mvprintw(row, col, "%-*s", width, buf);
    else
        mvprintw(row, col, "%s", buf);
    if (highlighted)
        attroff(A_REVERSE);
#endif
}


static int
menu_read_key(void)
{
    int ch;
    static int pending = -1;
    int next;
    int dir;

    if (pending != -1) {
        ch = pending;
        pending = -1;
        return ch;
    }

    ch = getch();
    if (ch != 27)
        return ch;

    next = getch();
    if (next == ERR)
        return ch;

    if (next == '[' || next == 'O') {
        dir = getch();
        if (dir == ERR) {
            pending = next;
            return ch;
        }
        if (dir == 'A')
            return KEY_UP;
        if (dir == 'B')
            return KEY_DOWN;
        if (dir == 'C')
            return KEY_RIGHT;
        if (dir == 'D')
            return KEY_LEFT;
        pending = dir;
        return ch;
    }
    pending = next;
    return ch;
}

int
run_menu(int start_row,
         const struct menu_item *items,
         int count,
         int initial_highlight,
         int *selected_index,
         int *focus_index)
{
    int highlight;
    int ch;
    int i;

    if (items == NULL || count <= 0)
        return 0;

    highlight = initial_highlight;
    if (highlight < 0 || highlight >= count)
        highlight = 0;
    while (1) {
        for (i = 0; i < count; ++i) {
            if (items[i].key != 0)
                menu_draw_highlighted(start_row + i, 6, COLS - 8, i == highlight,
                    "%c) %s", items[i].key, items[i].label);
            else
                menu_draw_highlighted(start_row + i, 6, COLS - 8, i == highlight,
                    "    %s", items[i].label);
        }
        refresh();

        ch = menu_read_key();
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
            continue;
        }
        if (ch == KEY_DOWN) {
            if (highlight < count - 1)
                ++highlight;
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == 27) {
            if (selected_index)
                *selected_index = -1;
            if (focus_index)
                *focus_index = highlight;
            return 0;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (selected_index)
                *selected_index = highlight;
            if (focus_index)
                *focus_index = highlight;
            return items[highlight].key;
        }

        for (i = 0; i < count; ++i) {
            if (items[i].key == 0)
                continue;
            if (toupper((unsigned char)items[i].key) ==
                toupper((unsigned char)ch)) {
                if (selected_index)
                    *selected_index = i;
                if (focus_index)
                    *focus_index = highlight;
                return items[i].key;
            }
        }
    }
}
