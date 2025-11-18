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
#include "data.h"
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
    int top_index;
    int bottom_row;
    int available_rows;
    int visible_rows = 0;
    int first_row = start_row;
    int indicator_top_row = -1;
    int indicator_bottom_row = -1;
    int show_top = 0;
    int show_bottom = 0;
    int menu_width;
    int indicator_col;
    int indicator_width;
    int prev_indicator_top = -1;
    int prev_indicator_bottom = -1;

    if (items == NULL || count <= 0)
        return 0;

    highlight = initial_highlight;
    if (highlight < 0)
        highlight = 0;
    if (highlight >= count)
        highlight = count - 1;

    menu_width = COLS - 8;
    if (menu_width < 1)
        menu_width = (COLS > 1) ? COLS - 1 : 1;
    indicator_col = 6;
    indicator_width = COLS - indicator_col - 2;
    if (indicator_width < 1)
        indicator_width = 1;

    bottom_row = LINES - PROMPT_ROW_OFFSET - 2;
    if (bottom_row < start_row)
        bottom_row = start_row;
    available_rows = bottom_row - start_row + 1;
    if (available_rows < 1)
        available_rows = 1;

    while (1) {
        int adjust = 1;

        while (adjust) {
            int new_show_top;
            int new_show_bottom;

            visible_rows = available_rows - (show_top ? 1 : 0) - (show_bottom ? 1 : 0);
            if (visible_rows < 1)
                visible_rows = 1;
            if (visible_rows > count)
                visible_rows = count;

            top_index = highlight - visible_rows + 1;
            if (top_index < 0)
                top_index = 0;
            if (top_index + visible_rows > count)
                top_index = count - visible_rows;
            if (top_index < 0)
                top_index = 0;

            new_show_top = top_index > 0;
            new_show_bottom = (top_index + visible_rows) < count;

            if (new_show_top == show_top && new_show_bottom == show_bottom) {
                adjust = 0;
            }
            show_top = new_show_top;
            show_bottom = new_show_bottom;
        }

        first_row = start_row + (show_top ? 1 : 0);
        indicator_top_row = show_top ? start_row : -1;
        indicator_bottom_row = show_bottom ? (start_row + available_rows - 1) : -1;

        if (prev_indicator_top >= 0 && prev_indicator_top != indicator_top_row)
            mvprintw(prev_indicator_top, indicator_col, "%-*s", indicator_width, "");
        if (prev_indicator_bottom >= 0 && prev_indicator_bottom != indicator_bottom_row)
            mvprintw(prev_indicator_bottom, indicator_col, "%-*s", indicator_width, "");

        for (i = 0; i < visible_rows; ++i) {
            int item_index = top_index + i;
            int row = first_row + i;

            if (item_index < count) {
                if (items[item_index].key != 0)
                    menu_draw_highlighted(row, 6, menu_width, item_index == highlight,
                        "%c) %s", items[item_index].key, items[item_index].label);
                else
                    menu_draw_highlighted(row, 6, menu_width, item_index == highlight,
                        "    %s", items[item_index].label);
            } else {
                menu_draw_highlighted(row, 6, menu_width, 0, "%s", "");
            }
        }

        if (indicator_top_row >= 0) {
            mvprintw(indicator_top_row, indicator_col, "%-*s", indicator_width, "");
            mvprintw(indicator_top_row, indicator_col, "<<<----");
        }
        if (indicator_bottom_row >= 0) {
            mvprintw(indicator_bottom_row, indicator_col, "%-*s", indicator_width, "");
            mvprintw(indicator_bottom_row, indicator_col, "---->>>");
        }
        prev_indicator_top = indicator_top_row;
        prev_indicator_bottom = indicator_bottom_row;
        refresh();

        ch = menu_read_key();
        if (ch == KEY_UP) {
            if (highlight > 0) {
                --highlight;
                if (highlight < top_index) {
                    top_index = highlight;
                    if (top_index < 0)
                        top_index = 0;
                }
            }
            continue;
        }
        if (ch == KEY_DOWN) {
            if (highlight < count - 1) {
                ++highlight;
                if (highlight >= top_index + visible_rows) {
                    top_index = highlight - visible_rows + 1;
                    if (top_index + visible_rows > count)
                        top_index = count - visible_rows;
                    if (top_index < 0)
                        top_index = 0;
                }
            }
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == CTRL_KEY('H') || ch == 127 || ch == '\b' || ch == 27) {
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
