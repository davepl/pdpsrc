#ifndef PLATFORM_H
#define PLATFORM_H

#include <curses.h>

#define CTRL_KEY(x) ((x) & 0x1f)
#define PROMPT_ROW_OFFSET 2
#ifndef A_REVERSE
#ifdef A_STANDOUT
#define A_REVERSE A_STANDOUT
#else
#define A_REVERSE 0x01
#endif
#endif
#ifndef KEY_ENTER
#define KEY_ENTER '\n'
#endif
#ifndef KEY_UP
#define KEY_UP 0403
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 0402
#endif
#ifndef KEY_LEFT
#define KEY_LEFT 0404
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT 0405
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 0407
#endif
#ifndef ACS_HLINE
#define ACS_HLINE '-'
#endif
#ifndef ACS_VLINE
#define ACS_VLINE '|'
#endif
#ifndef ACS_ULCORNER
#define ACS_ULCORNER '+'
#endif
#ifndef ACS_URCORNER
#define ACS_URCORNER '+'
#endif
#ifndef ACS_LLCORNER
#define ACS_LLCORNER '+'
#endif
#ifndef ACS_LRCORNER
#define ACS_LRCORNER '+'
#endif
#ifndef ACS_LTEE
#define ACS_LTEE '+'
#endif
#ifndef ACS_RTEE
#define ACS_RTEE '+'
#endif

void platform_set_cursor(int visible);
void platform_draw_border(void);
void platform_draw_header_line(const char *left, const char *right);
void platform_draw_separator(int row);
void platform_read_input(int y, int x, char *buf, int maxlen);
void platform_draw_breadcrumb(const char *text);
void platform_reverse_on(void);
void platform_reverse_off(void);
#ifdef LEGACY_CURSES
void legacy_draw_box(int top, int left, int height, int width);
#endif
#endif
