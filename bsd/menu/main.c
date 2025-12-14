/******************************************************************************
 *                                                                            *
 *  ░███████  ░█████   ░███████        ░████████   ░████████     ░██████      *
 *  ░██   ░██ ░██ ░██  ░██   ░██       ░██    ░██  ░██    ░██   ░██   ░██     *
 *  ░██   ░██ ░██  ░██ ░██   ░██       ░██    ░██  ░██    ░██  ░██            *
 *  ░███████  ░██  ░██ ░███████  ░████ ░████████   ░████████    ░████████     *
 *  ░██       ░██  ░██ ░██             ░██     ░██ ░██     ░██         ░██    *
 *  ░██       ░██ ░██  ░██             ░██     ░██ ░██     ░██  ░██   ░██     *
 *  ░██       ░█████   ░██             ░█████████  ░█████████    ░██████      *
 *                                                                            * 
 *  ════════════════════════════════════════════════════════════════════════  *
 *                                                                            *
 *  PROGRAM:     DAVE'S GARAGE PDP-11 BBS MENU SYSTEM                         *
 *  MODULE:      MAIN.C                                                       *
 *  VERSION:     0.2                                                          *
 *  DATE:        NOVEMBER 15, 2025                                            *
 *                                                                            *
 *  ════════════════════════════════════════════════════════════════════════  *
 *                                                                            *
 *  DESCRIPTION:                                                              *
 *                                                                            *
 *    Main menu and user interface for a bulletin board system designed       *
 *    for the PDP-11 running 2.11BSD. Features VT220 terminal support         *
 *    with DEC Special Graphics Character Set for elegant box drawing.        *
 *                                                                            *
 *    Provides hierarchical message group browsing, user management,          *
 *    address book functionality, and message composition with a full         *
 *    screen curses-based interface optimized for low-bandwidth serial        *
 *    terminals at 9600 baud.                                                 *
 *                                                                            *
 *  ════════════════════════════════════════════════════════════════════════  *
 *                                                                            *
 *  COMPATIBILITY:                                                            *
 *                                                                            *
 *    - PDP-11 with 2.11BSD (LEGACY_CURSES)                                   *
 *    - Modern Unix systems with ncurses                                      *
 *    - VT220 terminal or compatible emulator                                 *
 *                                                                            *
 *  ════════════════════════════════════════════════════════════════════════  *
 *                                                                            *
 *  AUTHOR:      DAVE PLUMMER                                                 *
 *  LICENSE:     GPL 2.0                                                      *
 *                                                                            *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <curses.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#if !defined(__pdp11__)
#include <locale.h>
#endif
#if !defined(__pdp11__)
#include <time.h>
#else
extern time_t time();
extern struct tm *localtime();
#endif
#include "data.h"
#include "platform.h"
#include "menu.h"
#include "session.h"
#include "auth.h"
#include "screens.h"
#include "msgs.h"
#ifdef TEST
#include "tests.h"
#endif

#if defined(__APPLE__) || defined(__MACH__) || defined(__linux__)
#include <unistd.h>
#else
extern int unlink();
extern int link();
extern int getpid();
extern unsigned sleep();
#endif

#ifdef LEGACY_CURSES
static int legacy_mvgetnstr(int y, int x, char *buf, int maxlen);
#endif

#ifndef L_tmpnam
#define L_tmpnam 64
#endif

#if defined(__pdp11__)
int
compat_snprintf(char *buf, size_t len, const char *fmt, ...)
{
    va_list ap;
    int ret;

    if (buf == NULL || len == 0)
        return 0;
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    if (ret < 0) {
        buf[0] = '\0';
        return ret;
    }
    if ((size_t)ret >= len) {
        buf[len - 1] = '\0';
        ret = (int)strlen(buf);
    }
    return ret;
}
#endif

#define ESC_KEY 27

static void
debug_log(const char *fmt, ...)
{
    FILE *fp = fopen("debug.log", "a");
    if (fp) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        fprintf(fp, "\n");
        va_end(ap);
        fclose(fp);
    }
}

static const char *g_login_banner[] = {
"   _____  _____  _____        ____  ____   _____  ",
"   |  __ \\|  __ \\|  __ \\      |  _ \\|  _ \\ / ____| ",
"   | |__) | |  | | |__) |_____| |_) | |_) | (___   ",
"   |  ___/| |  | |  ___/______|  _ <|  _ < \\___ \\  ",
"   | |    | |__| | |          | |_) | |_) |____) | ",
"   |_|    |_____/|_|          |____/|____/|_____/  "
};

#define LOGIN_BANNER_LINES (int)(sizeof(g_login_banner) / sizeof(g_login_banner[0]))

enum main_menu_action {
    MAIN_MENU_GROUP,
    MAIN_MENU_GROUP_MGMT,
    MAIN_MENU_BACK,
    MAIN_MENU_SETUP
};

enum group_menu_entry_type {
    GROUP_MENU_ENTRY_GROUP = 0,
    GROUP_MENU_ENTRY_CREATE,
    GROUP_MENU_ENTRY_BACK
};

struct main_menu_entry {
    char key;
    enum main_menu_action action;
    int group_index;
    char label[64];
};

#define MAX_MAIN_MENU_ENTRIES 16
#define GROUP_MENU_LABEL_LEN (MAX_GROUP_NAME + MAX_GROUP_DESC + 16)

struct session g_session;
static enum screen_id g_screen = SCREEN_LOGIN;
static int g_running = 1;
static int g_last_highlight = 0;
static int g_ui_ready = 0;

static void
draw_highlighted_text(int row, int col, int width, int highlighted, const char *fmt, ...)
{
    char buf[256];
    char render[256];
    va_list ap;
    int fill;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    fill = width > 0 ? width : (int)strlen(buf);
    if (fill >= (int)sizeof(render))
        fill = (int)sizeof(render) - 1;
    safe_copy(render, sizeof render, buf);
    if ((int)strlen(render) > fill)
        render[fill] = '\0';

#ifdef LEGACY_CURSES
    {
        printf("\033[%d;%dH", row + 1, col + 1);
        if (highlighted)
            printf("\033[7m%s\033[0m", render);
        else
            printf("%s", render);
        if (width > 0 && (int)strlen(render) < fill) {
            int pad = fill - (int)strlen(render);
            while (pad-- > 0)
                printf(" ");
        }
        fflush(stdout);
    }
#else
    if (highlighted)
        attron(A_REVERSE);
    if (width > 0)
        mvprintw(row, col, "%-*.*s", width, width, render);
    else
        mvprintw(row, col, "%s", render);
    if (highlighted)
        attroff(A_REVERSE);
#endif
}

static void start_ui(void);
static void stop_ui(void);
static int screen_too_small(void);
static void require_screen_size(void);
static void logout_session(void);
void draw_layout(const char *title, const char *status);
void draw_menu_lines(const char *line1, const char *line2, const char *line3);
static void show_status(const char *msg);
void wait_for_ack(const char *msg);
void prompt_string(const char *label, char *buffer, int maxlen);
int prompt_yesno(const char *question);
int is_valid_username(const char *text);
void show_help(const char *title, const char *body);
static void login_screen(void);
static void main_menu_screen(void);
static void group_list_screen(void);
static int group_action_menu(int group_index);
static char group_menu_key_for_index(int idx);
static void setup_screen(void);
static void draw_group_rows(int highlight);
static void handle_group_action(char action_key, int group_index);
static int build_main_menu_entries(struct main_menu_entry *entries, int max_entries);
static void update_status_line(void);
static void draw_group_list_commands(void);
static const char *current_group_name(void);
static void add_group(void);
static void edit_group_details(int idx);
static void mark_delete_group(int idx);
static void expunge_groups(void);
static void select_group(int idx);
static void goto_group_prompt(void);
static void whereis_group(void);
static void edit_signature(void);
static void change_password(void);
int read_key(void);
static int input_has_data(void);
static int input_has_data_timeout(int usec);
int is_back_key(int ch);
int normalize_key(int ch);
static int confirm_exit_prompt(void);
void draw_back_hint(void);
int require_admin(const char *action);
static void set_current_label(const char *label, enum screen_id id);
static void reset_navigation(enum screen_id screen, const char *label);
void push_screen(enum screen_id next, const char *label);
int pop_screen(void);
static void update_breadcrumb(void);
static const char *default_label_for_screen(enum screen_id id);
int handle_back_navigation(void);
static void log_denied_action(const char *action, const char *reason);

static int g_pending_key = -1;
static char g_current_label[64];
static char g_breadcrumb[256];

struct nav_entry {
    enum screen_id id;
    char label[64];
};

#define NAV_STACK_MAX 16
static struct nav_entry g_nav_stack[NAV_STACK_MAX];
static int g_nav_size = 0;

// Returns default label text for each screen type used in breadcrumb navigation.
// Called when screen doesn't provide custom label. 

static const char *
default_label_for_screen(enum screen_id id)
{
    switch (id) {
    case SCREEN_LOGIN:
        return "Login";
    case SCREEN_GROUP_LIST:
        return "Groups";
    case SCREEN_POST_INDEX:
        return "Group";
    case SCREEN_POST_VIEW:
        return "Post";
    case SCREEN_COMPOSE:
        return "Compose";
    case SCREEN_SETUP:
        return "Setup";
    case SCREEN_HELP:
        return "Help";
    case SCREEN_MAIN:
    default:
        return "";
    }
}

// Rebuilds breadcrumb trail from navigation stack showing user's current location.
// Format: "Home > Groups > General > Post" for hierarchical navigation.

static void
update_breadcrumb(void)
{
    int i;

    safe_copy(g_breadcrumb, sizeof g_breadcrumb, "Home");
    for (i = 0; i < g_nav_size; ++i) {
        if (g_nav_stack[i].label[0]) {
            safe_append(g_breadcrumb, sizeof g_breadcrumb, " > ");
            safe_append(g_breadcrumb, sizeof g_breadcrumb, g_nav_stack[i].label);
        }
    }
    if (g_current_label[0]) {
        safe_append(g_breadcrumb, sizeof g_breadcrumb, " > ");
        safe_append(g_breadcrumb, sizeof g_breadcrumb, g_current_label);
    }
}

/* Sets label for current screen and updates breadcrumb trail.
 * Uses provided label or defaults to screen type name if NULL. */
static void
set_current_label(const char *label, enum screen_id id)
{
    if (label != NULL)
        safe_copy(g_current_label, sizeof g_current_label, label);
    else
        safe_copy(g_current_label, sizeof g_current_label, default_label_for_screen(id));
    update_breadcrumb();
}

/* Resets navigation stack to initial state with specified screen.
 * Used when logging in or returning to main menu. */
static void
reset_navigation(enum screen_id screen, const char *label)
{
    g_nav_size = 0;
    g_screen = screen;
    set_current_label(label, screen);
}

/* Pushes current screen onto navigation stack and switches to new screen.
 * Enables back navigation by preserving screen history. */
void
push_screen(enum screen_id next, const char *label)
{
    if (g_nav_size < NAV_STACK_MAX) {
        g_nav_stack[g_nav_size].id = g_screen;
        safe_copy(g_nav_stack[g_nav_size].label, sizeof g_nav_stack[g_nav_size].label, g_current_label);
        ++g_nav_size;
    }
    g_screen = next;
    set_current_label(label, next);
}

/* Pops previous screen from navigation stack and returns to it.
 * Returns 1 if successful, 0 if already at root level. */
int
pop_screen(void)
{
    if (g_nav_size == 0)
        return 0;
    --g_nav_size;
    g_screen = g_nav_stack[g_nav_size].id;
    safe_copy(g_current_label, sizeof g_current_label, g_nav_stack[g_nav_size].label);
    update_breadcrumb();
    return 1;
}

/* Handles back/quit key by popping navigation stack or prompting logout.
 * Returns 1 if navigation occurred or logout confirmed. */
int
handle_back_navigation(void)
{
    if (pop_screen())
        return 1;
    if (confirm_exit_prompt())
        logout_session();
    return 0;
}

/* Opens compose screen with optional source message for reply/forward.
 * Sets global compose state and pushes compose screen onto navigation stack. */
#if 0
static void
open_compose(struct message *src, int forward_mode)
{
    if (src != NULL) {
        g_compose_reply_source = *src;
        g_compose_source = &g_compose_reply_source;
    } else {
        g_compose_source = NULL;
    }
    g_compose_forward = forward_mode;
    push_screen(SCREEN_COMPOSE, "Compose");
}

/* Opens post viewer for message at specified index.
 * Saves highlight position and pushes post view screen onto stack. */
static void
open_post_view(int index)
{
    g_last_highlight = index;
    push_screen(SCREEN_POST_VIEW, "Post");
}
#endif

/* Draws text at specified position with optional reverse video highlighting.
 * Handles platform differences between legacy BSD curses and modern ncurses. */
int
main(int argc, char **argv)
{
    argc = argc;
    argv = argv;

#if !defined(__pdp11__)
    setlocale(LC_ALL, "");
#endif
    debug_log("Main started");

    ensure_data_dir();
    init_config();
    load_config();
    load_groups();

    start_ui();
    require_screen_size();

    g_session.username[0] = '\0';
    g_session.is_admin = 0;
    g_session.current_group = -1;

    reset_navigation(SCREEN_LOGIN, "Login");

    while (g_running) {
        switch (g_screen) {
        case SCREEN_LOGIN:
            login_screen();
            break;
        case SCREEN_MAIN:
            main_menu_screen();
            break;
        case SCREEN_GROUP_LIST:
            group_list_screen();
            break;
        case SCREEN_POST_INDEX:
            msgs_post_index_screen(&g_last_highlight);
            break;
        case SCREEN_POST_VIEW:
            msgs_post_view_screen(g_last_highlight);
            break;
        case SCREEN_COMPOSE:
            /* Compose handled inside msgs_post_index_screen/view via push_screen; no-op here */
            handle_back_navigation();
            break;
        case SCREEN_SETUP:
            setup_screen();
            break;
        case SCREEN_HELP:
        default:
            show_help("Help", "Context help is not yet implemented.");
            reset_navigation(SCREEN_MAIN, "");
            break;
        }
    }

    free_cached_messages();
    stop_ui();
    return EXIT_SUCCESS;
}

static void
start_ui(void)
{
    initscr();
    cbreak();
    noecho();
#ifdef NCURSES_VERSION
    keypad(stdscr, TRUE);
#endif
    platform_set_cursor(0);
    platform_refresh();
    g_ui_ready = 1;
}

static void
stop_ui(void)
{
    platform_set_cursor(1);
    endwin();
    g_ui_ready = 0;
}

static int
screen_too_small(void)
{
    if (LINES < MIN_ROWS || COLS < MIN_COLS)
        return 1;
    return 0;
}

static void
require_screen_size(void)
{
    if (screen_too_small()) {
        stop_ui();
        fprintf(stderr, "Terminal must be at least %dx%d.\n", MIN_COLS, MIN_ROWS);
        exit(EXIT_FAILURE);
    }
}

static void
logout_session(void)
{
    g_session.username[0] = '\0';
    g_session.is_admin = 0;
    g_session.current_group = -1;
    free_cached_messages();
    g_last_highlight = 0;
    reset_navigation(SCREEN_LOGIN, "Login");
}

void
draw_layout(const char *title, const char *status)
{
    int menu_bar;
    char header_left[256];
    char header_right[256];

    erase();

    safe_copy(header_left, sizeof header_left, PROGRAM_TITLE);
    safe_append(header_left, sizeof header_left, " ");
    safe_append(header_left, sizeof header_left, PROGRAM_VERSION);
    safe_copy(header_right, sizeof header_right, "User: ");
    safe_append(header_right, sizeof header_right,
        g_session.username[0] ? g_session.username : "(not logged)");
    if (g_session.is_admin)
        safe_append(header_right, sizeof header_right, " (admin)");
    {
        int target_col = COLS - 40;
        if (target_col < 2)
            target_col = 2;
        if (target_col > (int)sizeof(header_right) - 1)
            target_col = (int)sizeof(header_right) - 1;
        while ((int)strlen(header_right) < target_col)
            safe_append_char(header_right, sizeof header_right, ' ');
    }
    platform_draw_header_line(header_left, header_right);
    platform_draw_breadcrumb(g_breadcrumb);

    mvprintw(4, 2, "%-*s", COLS - 4, "");
    mvprintw(4, 2, "%-*.*s", COLS - 4, COLS - 4, title ? title : "");
    if (status && *status) {
        int status_col = COLS - (int)strlen(status) - 3;
        int status_width;
        if (status_col < 2)
            status_col = 2;
        status_width = COLS - status_col - 2;
        if (status_width < 1)
            status_width = 1;
        mvprintw(4, status_col, "%-*.*s", status_width, status_width, status);
    }
    
    menu_bar = LINES - MENU_ROWS - 1;
    (void)menu_bar;
    platform_refresh();
}

void
draw_menu_lines(const char *line1, const char *line2, const char *line3)
{
    const char *lines[3];
    int start_row;
    int stop_row;
    int i;

    lines[0] = line1 ? line1 : "";
    lines[1] = line2 ? line2 : "";
    lines[2] = line3 ? line3 : "";

    start_row = LINES - MENU_ROWS + 1; /* leave status line intact */
    stop_row = LINES - PROMPT_ROW_OFFSET; /* avoid clobbering prompt row */
    if (start_row >= stop_row)
        start_row = stop_row - 1;
    for (i = 0; i < 3; ++i) {
        int row = start_row + i;
        if (row >= stop_row)
            break; /* avoid clobbering prompt line */
        mvprintw(row, 4, "%-*.*s", COLS - 6, COLS - 6, lines[i]);
    }
}


static void
show_status(const char *msg)
{
    int row;

    row = LINES - MENU_ROWS;
    mvprintw(row, 2, "%-*.*s", COLS - 4, COLS - 4, msg ? msg : "");
    platform_refresh();
}

void
wait_for_ack(const char *msg)
{
    show_status(msg);
    getch();
}

static void
log_denied_action(const char *action, const char *reason)
{
    FILE *fp;
    time_t now;
    struct tm *tmv;
    char when[32];
    const char *user;

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv != NULL) {
        snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d",
            tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
            tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        safe_copy(when, sizeof when, "unknown-time");
    }
    user = g_session.username[0] ? g_session.username : "(not logged)";

    fp = fopen("admin.log", "a");
    if (fp != NULL) {
        fprintf(fp, "[%s] user=%s ip=unknown action=%s denied: %s\n",
            when, user, action ? action : "(unspecified)",
            reason ? reason : "(no reason)");
        fclose(fp);
    }
}

void
prompt_string(const char *label, char *buffer, int maxlen)
{
    int row;
    int col;
    int inside_width;
    const int start_col = 2;

    debug_log("prompt_string: %s", label);
    row = LINES - PROMPT_ROW_OFFSET;
    inside_width = COLS - 2 * start_col;
    if (inside_width < 1)
        inside_width = 1;
    move(row, start_col);
    /* Clear the prompt line without touching the border columns. */
    mvprintw(row, start_col, "%-*s", inside_width, "");
    mvprintw(row, start_col, "%s ", label);
    col = start_col + (int)strlen(label) + 1;
    if (col >= COLS - 2)
        col = COLS - 3;
    move(row, col);
    platform_set_cursor(1);
    debug_log("Calling platform_read_input");
    platform_read_input(row, col, buffer, maxlen - 1);
    debug_log("Returned from platform_read_input");
    platform_set_cursor(0);
    trim_newline(buffer);
}

int
prompt_yesno(const char *question)
{
    char buf[8];
    const char *p;
    int ch = 0;

    prompt_string(question, buf, sizeof buf);
    for (p = buf; *p && isspace((unsigned char)*p); ++p)
        ;
    if (*p != '\0')
        ch = toupper((unsigned char)*p);
    if (ch == 'Y')
        return 1;
    if (ch == 'N')
        return 0;
    return 0;
}

/* Prefixes each line of src with "> " for reply quoting. */
static void
quote_body(const char *src, char *dst, int dstlen)
{
    const char *p;

    if (dst == NULL || dstlen <= 0)
        return;
    dst[0] = '\0';
    if (src == NULL)
        return;

    p = src;
    while (*p && (int)strlen(dst) < dstlen - 2) {
        safe_append(dst, dstlen, "> ");
        while (*p && *p != '\n' && (int)strlen(dst) < dstlen - 1) {
            safe_append_char(dst, dstlen, *p++);
        }
        safe_append_char(dst, dstlen, '\n');
        if (*p == '\n')
            ++p;
    }
}

int
is_valid_username(const char *text)
{
    const unsigned char *p;

    if (text == NULL)
        return 0;
    for (p = (const unsigned char *)text; *p; ++p) {
        if (!isalnum(*p))
            return 0;
    }
    return 1;
}

int
read_key(void)
{
    int ch;
    int next;
    int dir;

    if (g_pending_key != -1) {
        ch = g_pending_key;
        g_pending_key = -1;
        return ch;
    }

    ch = getch();
    if (ch != ESC_KEY)
        return ch;
    if (!g_ui_ready)
        return ch;

    /* Got ESC - always try to read escape sequence for arrow keys */
    next = getch();
    if (next == ERR)
        return ch;
    
    if (next == '[' || next == 'O') {
        /* For VT220/ANSI sequences: ESC [ A or ESC O A */
        dir = getch();
        
        if (dir == ERR) {
            g_pending_key = next;
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
        /* Unknown escape sequence */
        g_pending_key = dir;
        return ch;
    }
    if (next == 'A')
        return KEY_UP;
    if (next == 'B')
        return KEY_DOWN;
    if (next == 'C')
        return KEY_RIGHT;
    if (next == 'D')
        return KEY_LEFT;
    /* Not an arrow key sequence */
    g_pending_key = next;
    return ch;
}

int
is_back_key(int ch)
{
    if (ch == KEY_BACKSPACE || ch == CTRL_KEY('H'))
        return 1;
    if (ch == 127 || ch == '\b')
        return 1;
    return 0;
}

int
normalize_key(int ch)
{
    if (ch >= 0 && ch <= 255)
        return toupper((unsigned char)ch);
    return ch;
}

static int
confirm_exit_prompt(void)
{
    char buf[8];
    const char *p;
    int ch = 0;
    int row = LINES - PROMPT_ROW_OFFSET;

    prompt_string("Logout (Y/n):", buf, sizeof buf);
    mvprintw(row, 2, "%-*s", COLS - 4, "");
    platform_refresh();
    /* Accept Y/y as yes (default), ignore leading spaces */
    for (p = buf; *p && isspace((unsigned char)*p); ++p)
        ;
    if (*p != '\0')
        ch = toupper((unsigned char)*p);
    if (ch == 'N')
        return 0;
    return (ch == 0 || ch == 'Y');
}

void
draw_back_hint(void)
{
    int row = LINES - MENU_ROWS - 1;
    if (row < 0)
        row = 0;
    mvprintw(row, 2, "Back) Return to previous menu");
}

int
require_admin(const char *action)
{
    if (g_session.is_admin)
        return 1;
    if (action && *action) {
        char msg[80];
        safe_copy(msg, sizeof msg, "Admin only: ");
        safe_append(msg, sizeof msg, action);
        wait_for_ack(msg);
        log_denied_action(action, "admin required");
    } else {
        wait_for_ack("Admin only.");
        log_denied_action("(unspecified)", "admin required");
    }
    return 0;
}

static int
input_has_data_timeout(int usec)
{
#if defined(__unix__)
    /* Use select() with a timeout to check for input */
    struct timeval tv;
    fd_set readfds;
    int fd;
    int result;

    fd = fileno(stdin);
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    tv.tv_sec = 0;
    tv.tv_usec = usec;
    
    result = select(fd + 1, &readfds, NULL, NULL, &tv);
    return (result > 0);
#else
    return 0;
#endif
}

static int
input_has_data(void)
{
    /* Use a longer timeout for slower serial terminals like VT220 on 211BSD */
    return input_has_data_timeout(500000); /* 500ms - generous for slow terminals */
}

void
show_help(const char *title, const char *body)
{
    draw_layout(title, "Help");
    draw_back_hint();
    mvprintw(5, 4, "%s", body);
    draw_menu_lines("[Space] Continue", "", "");
    wait_for_ack("Press any key to return.");
}

void show_layout_login_banner(void);
static void login_screen(void);

void
show_layout_login_banner(void)
{
    int i;
    int row;
    int banner_col = 2;
    int banner_width = 0;
    int min_leading = 256;

    draw_layout("Login", "");
    for (i = 0; i < LOGIN_BANNER_LINES; ++i) {
        const char *line = g_login_banner[i];
        int len = (int)strlen(line);
        int lead = 0;

        while (len > 0 && line[len - 1] == ' ')
            --len;
        while (lead < len && line[lead] == ' ')
            ++lead;
        if (lead < min_leading)
            min_leading = lead;
        if (len - lead > banner_width)
            banner_width = len - lead;
    }
    if (min_leading == 256)
        min_leading = 0;
    if (banner_width > COLS - 4)
        banner_width = COLS - 4;
    banner_col = (COLS - banner_width) / 2;
    if (banner_col < 2)
        banner_col = 2;

    for (i = 0; i < LOGIN_BANNER_LINES; ++i) {
        const char *line = g_login_banner[i];
        int len = (int)strlen(line);

        while (len > 0 && line[len - 1] == ' ')
            --len;
        if (len < min_leading)
            len = min_leading;
        mvprintw(5 + i, banner_col, "%.*s",
            banner_width, line + min_leading);
    }
    row = 12;   // leave room for banner
    mvprintw(row + 1, 2, "Welcome to the PDP-11 message boards.");
    mvprintw(row + 3, 2, "Enter user id.");

    platform_refresh();
    draw_menu_lines("", "", "");
}

static void
login_screen(void)
{
    if (perform_login(&g_session) == 0)
        reset_navigation(SCREEN_MAIN, "");
}

static void
update_status_line(void)
{
    char status[80];
    const char *group;
    int posts;

    group = current_group_name();
    if (group == NULL)
        group = "(no group)";
    posts = msgs_visible_message_count();
    if (g_session.current_group >= 0 && g_session.current_group < g_group_count) {
        if (posts == 0)
            load_messages_for_group(g_session.current_group);
        posts = msgs_visible_message_count();
    }
    status[0] = '\0';
    safe_append(status, sizeof status, "Group: ");
    safe_append(status, sizeof status, group);
    safe_append(status, sizeof status, "  Posts: ");
    safe_append_number(status, sizeof status, posts);
    mvprintw(4, 2, "%-*.*s", COLS - 4, COLS - 4, status);
}

static int
build_main_menu_entries(struct main_menu_entry *entries, int max_entries)
{
    int count;
    int i;

    if (max_entries <= 0)
        return 0;

    count = 0;

    for (i = 0; i < g_group_count && i < 10 && count < max_entries; ++i) {
        entries[count].key = (char)('0' + i);
        entries[count].action = MAIN_MENU_GROUP;
        entries[count].group_index = i;
        if (g_groups[i].name[0] != '\0')
            safe_copy(entries[count].label, sizeof entries[count].label, g_groups[i].name);
        else {
            safe_copy(entries[count].label, sizeof entries[count].label, "Group ");
            safe_append_number(entries[count].label, sizeof entries[count].label, i);
        }
        ++count;
    }

    if (count < max_entries) {
        entries[count].key = 'G';
        entries[count].action = MAIN_MENU_GROUP_MGMT;
        entries[count].group_index = -1;
        safe_copy(entries[count].label, sizeof entries[count].label, "Group Management");
        ++count;
    }

    if (count < max_entries) {
        entries[count].key = 'S';
        entries[count].action = MAIN_MENU_SETUP;
        entries[count].group_index = -1;
        safe_copy(entries[count].label, sizeof entries[count].label, "Setup");
        ++count;
    }

    if (count < max_entries) {
        entries[count].key = 'B';
        entries[count].action = MAIN_MENU_BACK;
        entries[count].group_index = -1;
        safe_copy(entries[count].label, sizeof entries[count].label, "Quit (Logout)");
        ++count;
    }

    return count;
}



static void
main_menu_screen(void)
{
    struct main_menu_entry entries[MAX_MAIN_MENU_ENTRIES];
    struct menu_item items[MAX_MAIN_MENU_ENTRIES];
    int entry_count;
    int choice;
    int i;
    int selected_index;
    int focus_index;
    int highlight;
    int action_index;

    while (1) {
        entry_count = build_main_menu_entries(entries, MAX_MAIN_MENU_ENTRIES);
        if (entry_count == 0)
            return;

        draw_layout("Main Menu - Select a Group to Browse Messages", "");
        draw_menu_lines("", "", "");
        for (i = 0; i < entry_count; ++i) {
            items[i].key = entries[i].key;
            items[i].label = entries[i].label;
        }
        highlight = g_last_highlight;
        if (highlight >= entry_count)
            highlight = entry_count - 1;
        if (highlight < 0)
            highlight = 0;
        selected_index = -1;
        focus_index = -1;
        choice = run_menu(8, items, entry_count, highlight, &selected_index, &focus_index, 0);
        if (choice == 0) {
            if (handle_back_navigation() && g_screen != SCREEN_MAIN)
                return;
            continue;
        }
        action_index = (selected_index >= 0) ? selected_index : focus_index;
        if (action_index < 0 || action_index >= entry_count)
            action_index = 0;
        g_last_highlight = action_index;

        switch (entries[action_index].action) {
        case MAIN_MENU_BACK:
            if (confirm_exit_prompt()) {
                logout_session();
                return;
            }
            break;
        case MAIN_MENU_GROUP:
            if (entries[action_index].group_index >= 0 &&
                entries[action_index].group_index < g_group_count) {
                select_group(entries[action_index].group_index);
                push_screen(SCREEN_POST_INDEX, g_groups[entries[action_index].group_index].name);
                return;
            }
            break;
        case MAIN_MENU_GROUP_MGMT:
            push_screen(SCREEN_GROUP_LIST, "Groups");
            return;
        case MAIN_MENU_SETUP:
            push_screen(SCREEN_SETUP, "Setup");
            return;
        }
    }
}

static const char *
current_group_name(void)
{
    if (g_session.current_group >= 0 && g_session.current_group < g_group_count)
        return g_groups[g_session.current_group].name;
    return NULL;
}

static void
select_group(int idx)
{
    if (idx >= 0 && idx < g_group_count) {
        g_session.current_group = idx;
        free_cached_messages();
    }
}

static void
add_group(void)
{
    char name[MAX_GROUP_NAME];
    char desc[MAX_GROUP_DESC];

    if (!require_admin("create groups"))
        return;
    if (g_group_count >= MAX_GROUPS) {
        wait_for_ack("Maximum groups reached.");
        return;
    }
    prompt_string("Group name:", name, sizeof name);
    if (name[0] == '\0')
        return;
    prompt_string("Description:", desc, sizeof desc);
    safe_copy(g_groups[g_group_count].name, sizeof g_groups[g_group_count].name, name);
    safe_copy(g_groups[g_group_count].description, sizeof g_groups[g_group_count].description, desc);
    g_groups[g_group_count].deleted = 0;
    ++g_group_count;
    save_groups();
}

static void
edit_group_details(int idx)
{
    char newname[MAX_GROUP_NAME];
    char newdesc[MAX_GROUP_DESC];

    if (idx < 0 || idx >= g_group_count)
        return;
    if (!require_admin("edit groups"))
        return;
    prompt_string("Group name:", newname, sizeof newname);
    prompt_string("Description:", newdesc, sizeof newdesc);
    if (newname[0] != '\0')
        safe_copy(g_groups[idx].name, sizeof g_groups[idx].name, newname);
    if (newdesc[0] != '\0')
        safe_copy(g_groups[idx].description, sizeof g_groups[idx].description, newdesc);
    save_groups();
}

static void
mark_delete_group(int idx)
{
    if (idx < 0 || idx >= g_group_count)
        return;
    if (!require_admin("delete groups"))
        return;
    g_groups[idx].deleted = !g_groups[idx].deleted;
    save_groups();
}

static void
expunge_groups(void)
{
    int i;
    int dst;

    if (!require_admin("expunge groups"))
        return;
    dst = 0;
    for (i = 0; i < g_group_count; ++i) {
        if (g_groups[i].deleted) {
            remove(g_groups[i].name);
            continue;
        }
        if (dst != i)
            g_groups[dst] = g_groups[i];
        ++dst;
    }
    g_group_count = dst;
    save_groups();
    if (g_session.current_group >= g_group_count)
        g_session.current_group = g_group_count - 1;
    free_cached_messages();
}

static void
handle_group_action(char action_key, int group_index)
{
    if (!g_session.is_admin) {
        wait_for_ack("Admins only.");
        log_denied_action("Group action", "admin required");
        return;
    }
    switch (action_key) {
    case 'C':
        add_group();
        break;
    case 'D':
        mark_delete_group(group_index);
        break;
    case 'E':
        edit_group_details(group_index);
        break;
    default:
        log_denied_action("Unknown group action", "admin required");
        break;
    }
}

static void
goto_group_prompt(void)
{
    char name[MAX_GROUP_NAME];
    int i;

    prompt_string("Goto group:", name, sizeof name);
    if (name[0] == '\0')
        return;
    for (i = 0; i < g_group_count; ++i) {
        if (strcasecmp(g_groups[i].name, name) == 0) {
            select_group(i);
            push_screen(SCREEN_POST_INDEX, g_groups[i].name);
            return;
        }
    }
    wait_for_ack("Group not found.");
}

static void
whereis_group(void)
{
    char needle[MAX_GROUP_NAME];
    int i;

    prompt_string("Search groups:", needle, sizeof needle);
    if (needle[0] == '\0')
        return;
    for (i = 0; i < g_group_count; ++i) {
        if (strstr(g_groups[i].name, needle) || strstr(g_groups[i].description, needle)) {
            select_group(i);
            push_screen(SCREEN_POST_INDEX, g_groups[i].name);
            wait_for_ack("Match highlighted.");
            return;
        }
    }
    wait_for_ack("No matches.");
}

static void
setup_screen(void)
{
    struct menu_item setup_menu[6];
    int choice;
    int selected_index;
    int focus_index;
    int highlight = 0;
    int menu_count = 3;

    setup_menu[0].key = 'S';
    setup_menu[0].label = "Edit Signature";
    setup_menu[1].key = 'N';
    setup_menu[1].label = "Change Password";
    setup_menu[2].key = 'B';
    setup_menu[2].label = "Back - Return to previous menu";
#ifdef TEST
    setup_menu[3].key = 'T';
    setup_menu[3].label = "Run Tests (admin only)";
    menu_count = 4;
#endif
    if (g_session.is_admin) {
        setup_menu[menu_count].key = 'L';
        setup_menu[menu_count].label = "Lock User";
        ++menu_count;
        setup_menu[menu_count].key = 'U';
        setup_menu[menu_count].label = "Unlock User";
        ++menu_count;
        setup_menu[menu_count].key = 'V';
        setup_menu[menu_count].label = "View Users";
        ++menu_count;
    }

    while (1) {
        draw_layout("Setup", "Configure session");
        mvprintw(5, 4, "Signature: %s", g_config.signature[0] ? g_config.signature : "(none)");
        mvprintw(6, 4, "Change your own password and signature.");
        selected_index = -1;
        focus_index = -1;
        choice = run_menu(9, setup_menu, menu_count, highlight, &selected_index, &focus_index, 0);
        if (focus_index >= 0)
            highlight = focus_index;
        if (choice == 0) {
            if (handle_back_navigation())
                return;
            continue;
        }
        if (selected_index >= 0)
            highlight = selected_index;

        switch (choice) {
        case 'S':
            edit_signature();
            break;
        case 'N':
            change_password();
            break;
        case 'L':
            if (g_session.is_admin)
                lock_unlock_user_account(1);
            else {
                wait_for_ack("Admins only.");
                log_denied_action("Lock user", "admin required");
            }
            break;
        case 'U':
            if (g_session.is_admin)
                lock_unlock_user_account(0);
            else {
                wait_for_ack("Admins only.");
                log_denied_action("Unlock user", "admin required");
            }
            break;
        case 'V':
            if (g_session.is_admin)
                show_user_list();
            else {
                wait_for_ack("Admins only.");
                log_denied_action("View users", "admin required");
            }
            break;
#ifdef TEST
        case 'T':
            if (g_session.is_admin)
                run_tests_menu(&g_session);
            else {
                wait_for_ack("Admins only.");
                log_denied_action("Run Tests", "admin required");
            }
            break;
#endif
        case 'B':
            if (handle_back_navigation())
                return;
            break;
        default:
            break;
        }
    }
}

static void
edit_signature(void)
{
    msgs_edit_body(g_config.signature, sizeof g_config.signature);
    save_config();
}

static void
change_password(void)
{
    char target[MAX_AUTHOR];
    int require_current = 1;

    if (g_session.username[0] == '\0') {
        wait_for_ack("You must be logged in.");
        return;
    }

    safe_copy(target, sizeof target, g_session.username);
    if (g_session.is_admin) {
        prompt_string("Change password for (leave blank for self):", target, sizeof target);
        if (target[0] == '\0')
            safe_copy(target, sizeof target, g_session.username);
        if (!is_valid_username(target)) {
            wait_for_ack("Invalid username.");
            return;
        }
        if (strcasecmp(target, g_session.username) != 0)
            require_current = 0;
    }

    change_password_for_user(target, require_current);
}

static char
group_menu_key_for_index(int idx)
{
    int count = 0;
    int ch;

    for (ch = '0'; ch <= '9'; ++ch) {
        if (count == idx)
            return (char)ch;
        ++count;
    }
    for (ch = 'A'; ch <= 'Z'; ++ch) {
        if (ch == 'B' || ch == 'C' || ch == 'D' || ch == 'E')
            continue;
        if (count == idx)
            return (char)ch;
        ++count;
    }
    for (ch = 'a'; ch <= 'z'; ++ch) {
        if (ch == 'b' || ch == 'c' || ch == 'd' || ch == 'e')
            continue;
        if (count == idx)
            return (char)ch;
        ++count;
    }
    for (ch = '!'; ch <= '/'; ++ch) {
        if (count == idx)
            return (char)ch;
        ++count;
    }
    return (char)('0' + (idx % 10));
}

static void
group_list_screen(void)
{
    struct menu_item menu_items[MAX_GROUPS + 4];
    char labels[MAX_GROUPS][GROUP_MENU_LABEL_LEN];
    int entry_type[MAX_GROUPS + 4];
    int entry_data[MAX_GROUPS + 4];
    int selected_index;
    int focus_index;
    int highlight = g_last_highlight;
    int choice;
    int entry_count;
    int i;
    int notice_row;
    const int menu_start_row = 7;

    if (highlight < 0)
        highlight = 0;

    while (1) {
        draw_layout("Group Management", "Browse groups");

        notice_row = LINES - PROMPT_ROW_OFFSET;
        if (notice_row < menu_start_row)
            notice_row = menu_start_row;
        mvprintw(notice_row, 4, "%-*.*s", COLS - 8, COLS - 8, "");
        if (g_group_count == 0)
            mvprintw(notice_row, 4, "%-*.*s", COLS - 8, COLS - 8, "No groups defined. Create one to begin.");
        else
            mvprintw(notice_row, 4, "%-*.*s", COLS - 8, COLS - 8, "Select a group to enter or choose an admin option.");
        {
            int verb_row = menu_start_row - 1;
            if (verb_row >= 0 && verb_row < LINES - PROMPT_ROW_OFFSET - 1)
                mvprintw(verb_row, 4, "%-*.*s", COLS - 6, COLS - 6, "C) Create group   D) Delete group   E) Edit group");
        }

        entry_count = 0;
        for (i = 0; i < g_group_count && entry_count < MAX_GROUPS; ++i) {
            labels[entry_count][0] = '\0';
            safe_copy(labels[entry_count], sizeof labels[entry_count], g_groups[i].name);
            if (g_groups[i].description[0] != '\0') {
                safe_append(labels[entry_count], sizeof labels[entry_count], " - ");
                safe_append(labels[entry_count], sizeof labels[entry_count], g_groups[i].description);
            }
            if (g_groups[i].deleted)
                safe_append(labels[entry_count], sizeof labels[entry_count], " [DELETED]");
            menu_items[entry_count].key = group_menu_key_for_index(entry_count);
            menu_items[entry_count].label = labels[entry_count];
            entry_type[entry_count] = GROUP_MENU_ENTRY_GROUP;
            entry_data[entry_count] = i;
            ++entry_count;
        }

        menu_items[entry_count].key = 'B';
        menu_items[entry_count].label = "Back - Return to previous menu";
        entry_type[entry_count] = GROUP_MENU_ENTRY_BACK;
        entry_data[entry_count] = -1;
        ++entry_count;

        if (highlight >= entry_count)
            highlight = entry_count - 1;
        if (highlight < 0)
            highlight = 0;

        selected_index = -1;
        focus_index = -1;
        choice = run_menu(menu_start_row, menu_items, entry_count, highlight, &selected_index, &focus_index, MENU_OPT_RETURN_UNHANDLED);

        if (choice == 0) {
            if (handle_back_navigation())
                return;
            highlight = (focus_index >= 0) ? focus_index : highlight;
            g_last_highlight = highlight;
            continue;
        }

        if (selected_index < 0 || selected_index >= entry_count)
            selected_index = (focus_index >= 0) ? focus_index : highlight;

        {
            int key = normalize_key(choice);
            if (key == 'C') {
                add_group();
                continue;
            }
            if (key == 'D') {
                if (selected_index >= 0 && selected_index < entry_count &&
                    entry_type[selected_index] == GROUP_MENU_ENTRY_GROUP)
                    mark_delete_group(entry_data[selected_index]);
                else
                    wait_for_ack("Select a group first.");
                continue;
            }
            if (key == 'E') {
                if (selected_index >= 0 && selected_index < entry_count &&
                    entry_type[selected_index] == GROUP_MENU_ENTRY_GROUP)
                    edit_group_details(entry_data[selected_index]);
                else
                    wait_for_ack("Select a group first.");
                continue;
            }
        }

        switch (entry_type[selected_index]) {
        case GROUP_MENU_ENTRY_GROUP:
            g_last_highlight = selected_index;
            select_group(entry_data[selected_index]);
            push_screen(SCREEN_POST_INDEX, g_groups[entry_data[selected_index]].name);
            return;
        case GROUP_MENU_ENTRY_CREATE:
            add_group();
            break;
        case GROUP_MENU_ENTRY_BACK:
        default:
            if (handle_back_navigation())
                return;
            break;
        }

        highlight = (focus_index >= 0) ? focus_index : highlight;
        g_last_highlight = highlight;
    }
}

static int
group_action_menu(int group_index)
{
    struct menu_item action_items[4];
    int count;
    int choice;
    int highlight = 0;
    int selected_index;
    int focus_index;
    int deleted;
    const int menu_row = 12;

    if (group_index < 0 || group_index >= g_group_count)
        return 0;

    while (1) {
        deleted = g_groups[group_index].deleted;
        draw_layout("Group Options", g_groups[group_index].name);
        mvprintw(8, 4, "Name: %s", g_groups[group_index].name);
        mvprintw(9, 4, "Description: %s", g_groups[group_index].description[0] ? g_groups[group_index].description : "(none)");
        mvprintw(10, 4, "Status: %s", deleted ? "Marked deleted" : "Active");

        count = 0;
        action_items[count].key = 'V';
        action_items[count].label = "View Messages";
        ++count;
        if (g_session.is_admin) {
            action_items[count].key = 'E';
            action_items[count].label = "Edit Group";
            ++count;
            action_items[count].key = 'D';
            action_items[count].label = deleted ? "Restore Group" : "Delete Group";
            ++count;
        }
        action_items[count].key = 'B';
        action_items[count].label = "Back to Group List";
        ++count;

        selected_index = -1;
        focus_index = -1;
        choice = run_menu(menu_row, action_items, count, highlight, &selected_index, &focus_index, 0);
        if (focus_index >= 0)
            highlight = focus_index;
        if (choice == 0) {
            return 0;
        }
        if (selected_index >= 0)
            highlight = selected_index;

        switch (choice) {
        case 'V':
            select_group(group_index);
            push_screen(SCREEN_POST_INDEX, g_groups[group_index].name);
            return 1;
        case 'E':
            if (g_session.is_admin)
                handle_group_action('E', group_index);
            else {
                wait_for_ack("Admins only.");
                log_denied_action("Edit group", "admin required");
            }
            break;
        case 'D':
            if (g_session.is_admin)
                handle_group_action('D', group_index);
            else {
                wait_for_ack("Admins only.");
                log_denied_action("Delete/restore group", "admin required");
            }
            break;
        case 'B':
        default:
            return 0;
        }
    }
}

static void
format_time(time_t t, char *buf, int buflen)
{
    struct tm *tmv;

    tmv = localtime(&t);
    if (tmv == NULL) {
        safe_copy(buf, buflen, "unknown");
        return;
    }
    if (buflen == 0)
        return;
    buf[0] = '\0';
    safe_append_two_digit(buf, buflen, tmv->tm_mon + 1);
    safe_append_char(buf, buflen, '/');
    safe_append_two_digit(buf, buflen, tmv->tm_mday);
    safe_append_char(buf, buflen, '/');
    safe_append_two_digit(buf, buflen, tmv->tm_year % 100);
    safe_append_char(buf, buflen, ' ');
    safe_append_two_digit(buf, buflen, tmv->tm_hour);
    safe_append_char(buf, buflen, ':');
    safe_append_two_digit(buf, buflen, tmv->tm_min);
}
