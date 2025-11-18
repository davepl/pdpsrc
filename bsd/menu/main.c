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
#include <time.h>
#else
extern time_t time();
extern struct tm *localtime();
#endif
#include "data.h"
#include "platform.h"
#include "menu.h"

#if defined(__APPLE__) || defined(__MACH__) || defined(__linux__)
#include <unistd.h>
#else
extern int unlink();
extern int link();
extern int getpid();
extern unsigned sleep();
#endif
#if defined(__pdp11__)
#define remove unlink
#endif

#ifdef LEGACY_CURSES
static int legacy_mvgetnstr(int y, int x, char *buf, int maxlen);
#endif

#define ESC_KEY 27

enum screen_id {
    SCREEN_LOGIN,
    SCREEN_MAIN,
    SCREEN_GROUP_LIST,
    SCREEN_POST_INDEX,
    SCREEN_POST_VIEW,
    SCREEN_COMPOSE,
    SCREEN_ADDRESS_BOOK,
    SCREEN_SETUP,
    SCREEN_HELP
};

struct session {
    char username[MAX_AUTHOR];
    int is_admin;
    int current_group;
};

static const char *g_login_banner[] = {
"\n"
"    _____  _____  _____        ____  ____   _____  \n"
"   |  __ \\|  __ \\|  __ \\      |  _ \\|  _ \\ / ____| \n"
"   | |__) | |  | | |__) |_____| |_) | |_) | (___   \n"
"   |  ___/| |  | |  ___/______|  _ <|  _ < \\___ \\  \n"
"   | |    | |__| | |          | |_) | |_) |____) | \n"
"   |_|    |_____/|_|          |____/|____/|_____/  \n"
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

static struct session g_session;
static enum screen_id g_screen = SCREEN_LOGIN;
static int g_running = 1;
static int g_last_highlight = 0;
static char g_compose_prefill_to[MAX_ADDRESS];
static int g_ui_ready = 0;

static void
draw_highlighted_text(int row, int col, int width, int highlighted, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

#ifdef LEGACY_CURSES
    {
        int fill = width > 0 ? width : (int)strlen(buf);
        move(row, col);
        clrtoeol();
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

/* Static buffers to avoid stack overflow on PDP-11 */
static char g_compose_buffer_to[MAX_ADDRESS];
static char g_compose_buffer_cc[MAX_ADDRESS];
static char g_compose_buffer_attach[MAX_ADDRESS];
static char g_compose_buffer_subject[MAX_SUBJECT];
static char g_compose_buffer_body[MAX_BODY];
static struct message g_compose_newmsg;
static struct message g_compose_reply_source;
static struct message *g_compose_source = NULL;
static int g_compose_forward = 0;
static void start_ui(void);
static void stop_ui(void);
static int screen_too_small(void);
static void require_screen_size(void);
static void logout_session(void);
static void draw_layout(const char *title, const char *status);
static void draw_menu_lines(const char *line1, const char *line2, const char *line3);
static void show_status(const char *msg);
static void wait_for_ack(const char *msg);
static void prompt_string(const char *label, char *buffer, int maxlen);
static int prompt_yesno(const char *question);
static int is_valid_username(const char *text);
static void draw_post_commands_line(void);
static void format_post_age(time_t created, char *buf, int buflen);
static void show_help(const char *title, const char *body);
static void login_screen(void);
static void main_menu_screen(void);
static void group_list_screen(void);
static int group_action_menu(int group_index);
static char group_menu_key_for_index(int idx);
static void post_index_screen(void);
static void post_view_screen(int message_index);
static void compose_screen(struct message *reply_source, int forward_mode);
static void address_book_screen(void);
static void setup_screen(void);
static void draw_group_rows(int highlight);
static void handle_group_action(char action_key, int group_index);
static void draw_post_rows(int highlight);
static void draw_address_rows(int highlight);
static int build_main_menu_entries(struct main_menu_entry *entries, int max_entries);
static void update_status_line(void);
static void render_body_text(const char *body, int start_row, int max_rows);
static void edit_body(char *buffer, int maxlen);
static void draw_group_list_commands(void);
static const char *current_group_name(void);
static void add_group(void);
static void edit_group_details(int idx);
static void mark_delete_group(int idx);
static void expunge_groups(void);
static void select_group(int idx);
static void goto_group_prompt(void);
static void whereis_group(void);
static void take_address_from_message(struct message *msg);
static void save_message_to_group(struct message *msg);
static void delete_or_undelete(struct message *msg, int deleted);
static void expunge_messages(void);
static void search_messages(void);
static void add_address_entry(int is_list);
static void edit_address_entry(int idx);
static void delete_address_entry(int idx);
static void compose_to_entry(int idx);
static void edit_signature(void);
static void edit_printer(void);
static void change_password(void);
static int read_key(void);
static int input_has_data(void);
static int input_has_data_timeout(int usec);
static int is_back_key(int ch);
static int normalize_key(int ch);
static int confirm_exit_prompt(void);
static void draw_back_hint(void);
int require_admin(const char *action);
static void set_current_label(const char *label, enum screen_id id);
static void reset_navigation(enum screen_id screen, const char *label);
static void push_screen(enum screen_id next, const char *label);
static int pop_screen(void);
static void update_breadcrumb(void);
static const char *default_label_for_screen(enum screen_id id);
static int handle_back_navigation(void);
static void open_compose(struct message *src, int forward_mode);
static void open_post_view(int index);

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
    case SCREEN_ADDRESS_BOOK:
        return "Address Book";
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
static void
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
static int
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
static int
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

/* Draws text at specified position with optional reverse video highlighting.
 * Handles platform differences between legacy BSD curses and modern ncurses. */
int
main(int argc, char **argv)
{
    argc = argc;
    argv = argv;

    ensure_data_dir();
    init_config();
    load_config();
    load_groups();
    load_address_book();

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
            post_index_screen();
            break;
        case SCREEN_POST_VIEW:
            post_view_screen(g_last_highlight);
            break;
        case SCREEN_COMPOSE:
            compose_screen(g_compose_source, g_compose_forward);
            g_compose_source = NULL;
            g_compose_forward = 0;
            break;
        case SCREEN_ADDRESS_BOOK:
            address_book_screen();
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

static void
draw_layout(const char *title, const char *status)
{
    int menu_bar;
    char header_left[80];
    char header_right[80];

    clear();

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
        while ((int)strlen(header_right) < target_col)
            safe_append_char(header_right, sizeof header_right, ' ');
    }
    platform_draw_header_line(header_left, header_right);
    platform_draw_breadcrumb(g_breadcrumb);

    mvprintw(6, 4, "%s", title);
    if (status && *status)
        mvprintw(6, COLS - (int)strlen(status) - 3, "%s", status);
    
    /* Refresh curses buffer to screen, THEN draw DEC graphics border over it */
    refresh();
    platform_draw_border();

    menu_bar = LINES - MENU_ROWS - 1;
    platform_draw_separator(menu_bar);
}

static void
draw_menu_lines(const char *line1, const char *line2, const char *line3)
{
    (void)line1;
    (void)line2;
    (void)line3;
}


static void
show_status(const char *msg)
{
    int row;

    row = LINES - MENU_ROWS;
    mvprintw(row, 2, "%-*s", COLS - 4, msg ? msg : "");
    refresh();
}

static void
wait_for_ack(const char *msg)
{
    show_status(msg);
    getch();
}

static void
prompt_string(const char *label, char *buffer, int maxlen)
{
    int row;
    int col;

    row = LINES - PROMPT_ROW_OFFSET;
    move(row, 2);
    clrtoeol();
    mvprintw(row, 2, "%s ", label);
    col = 2 + (int)strlen(label) + 1;
    if (col >= COLS - 2)
        col = COLS - 3;
    move(row, col);
    platform_set_cursor(1);
    platform_read_input(row, col, buffer, maxlen - 1);
    platform_set_cursor(0);
    trim_newline(buffer);
}

static int
prompt_yesno(const char *question)
{
    char buf[8];

    prompt_string(question, buf, sizeof buf);
    if (buf[0] == 'y' || buf[0] == 'Y')
        return 1;
    return 0;
}

static int
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

static int
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

static int
is_back_key(int ch)
{
    if (ch == KEY_BACKSPACE || ch == CTRL_KEY('H'))
        return 1;
    if (ch == 127 || ch == '\b')
        return 1;
    return 0;
}

static int
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
    int row = LINES - PROMPT_ROW_OFFSET;

    prompt_string("Logout (Y/n):", buf, sizeof buf);
    move(row, 2);
    clrtoeol();
    refresh();
    if (buf[0] == '\0' || buf[0] == 'y' || buf[0] == 'Y')
        return 1;
    return 0;
}

static void
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
    } else {
        wait_for_ack("Admin only.");
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

static void
show_help(const char *title, const char *body)
{
    draw_layout(title, "Help");
    draw_back_hint();
    mvprintw(5, 4, "%s", body);
    draw_menu_lines("[Space] Continue", "", "");
    wait_for_ack("Press any key to return.");
}

static void
login_screen(void)
{
    char user[MAX_AUTHOR];
    char pass[MAX_AUTHOR];
    int i;
    int row;

    draw_layout("Login", "");
    for (i = 0; i < LOGIN_BANNER_LINES; ++i)
        mvprintw(4 + i, 2, "%s", g_login_banner[i]);
    row = 11;   // leave room for banner
    mvprintw(row + 1, 2, "Welcome to the PDP-11 message boards.");
    mvprintw(row + 3, 2, "Enter user id (admin requires password).");
    refresh();
    platform_draw_border();
    platform_draw_separator(LINES - MENU_ROWS - 1);
    platform_draw_breadcrumb(g_breadcrumb);
    draw_menu_lines("Enter user id", "", "");
    while (1) {
        prompt_string("User id:", user, sizeof user);
        if (user[0] == '\0') {
            clear();
            refresh();
            stop_ui();
            exit(0);
        }
        if (!is_valid_username(user)) {
            wait_for_ack("User id must be alphanumeric.");
            continue;
        }
        break;
    }
    if (strcasecmp(user, ADMIN_USER) == 0) {
        prompt_string("Admin password:", pass, sizeof pass);
        if (strcmp(pass, ADMIN_PASS) != 0) {
            wait_for_ack("Invalid password.");
            return;
        }
        g_session.is_admin = 1;
    } else {
        g_session.is_admin = 0;
    }
    safe_copy(g_session.username, sizeof g_session.username, user);
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
    posts = g_cached_message_count;
    if (g_session.current_group >= 0 && g_session.current_group < g_group_count) {
        if (posts == 0)
            load_messages_for_group(g_session.current_group);
        posts = g_cached_message_count;
    }
    status[0] = '\0';
    safe_append(status, sizeof status, "Group: ");
    safe_append(status, sizeof status, group);
    safe_append(status, sizeof status, "  Posts: ");
    safe_append_number(status, sizeof status, posts);
    mvprintw(4, 2, "%-*s", COLS - 4, status);
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
        choice = run_menu(8, items, entry_count, highlight, &selected_index, &focus_index);
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
    const int start_row = 8;

    if (highlight < 0)
        highlight = 0;

    while (1) {
        draw_layout("Group Management", "Browse groups");
        if (g_group_count == 0)
            mvprintw(start_row - 1, 4, "No groups defined. Create one to begin.");
        else
            mvprintw(start_row - 1, 4, "Select a group to enter or choose an admin option.");

        entry_count = 0;
        for (i = 0; i < g_group_count && entry_count < MAX_GROUPS; ++i) {
            labels[i][0] = '\0';
            safe_copy(labels[i], sizeof labels[i], g_groups[i].name);
            if (g_groups[i].description[0] != '\0') {
                safe_append(labels[i], sizeof labels[i], " - ");
                safe_append(labels[i], sizeof labels[i], g_groups[i].description);
            }
            if (g_groups[i].deleted)
                safe_append(labels[i], sizeof labels[i], " [DELETED]");
            menu_items[entry_count].key = group_menu_key_for_index(entry_count);
            menu_items[entry_count].label = labels[i];
            entry_type[entry_count] = GROUP_MENU_ENTRY_GROUP;
            entry_data[entry_count] = i;
            ++entry_count;
        }

        if (g_session.is_admin) {
            menu_items[entry_count].key = 'C';
            menu_items[entry_count].label = "Create Group";
            entry_type[entry_count] = GROUP_MENU_ENTRY_CREATE;
            entry_data[entry_count] = -1;
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
        choice = run_menu(start_row, menu_items, entry_count, highlight, &selected_index, &focus_index);

        if (choice == 0) {
            if (handle_back_navigation())
                return;
            highlight = (focus_index >= 0) ? focus_index : highlight;
            g_last_highlight = highlight;
            continue;
        }

        if (selected_index < 0 || selected_index >= entry_count)
            continue;

        switch (entry_type[selected_index]) {
        case GROUP_MENU_ENTRY_GROUP:
            g_last_highlight = selected_index;
            if (group_action_menu(entry_data[selected_index]))
                return;
            break;
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
        choice = run_menu(menu_row, action_items, count, highlight, &selected_index, &focus_index);
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
            else
                wait_for_ack("Admins only.");
            break;
        case 'D':
            if (g_session.is_admin)
                handle_group_action('D', group_index);
            else
                wait_for_ack("Admins only.");
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

static void
draw_post_rows(int highlight)
{
    int row;
    int i;
    char age[32];
    char subject_buf[96];

    row = 6;
#ifdef LEGACY_CURSES
    printf("\033[%d;%dHSubject", 6, 4);
    printf("\033[%d;%dHPoster", 6, 46);
    printf("\033[%d;%dHAge", 6, 64);
    fflush(stdout);
#else
    mvprintw(5, 3, "Subject");
    mvprintw(5, 45, "Poster");
    mvprintw(5, 63, "Age");
#endif
    if (g_cached_message_count == 0) {
#ifdef LEGACY_CURSES
        printf("\033[%d;%dHNo posts in this group yet.", 7, 4);
        fflush(stdout);
#else
        mvprintw(row, 3, "No posts in this group yet.");
#endif
        return;
    }
    if (highlight < 0)
        highlight = 0;
    if (highlight >= g_cached_message_count)
        highlight = g_cached_message_count - 1;
    for (i = 0; i < g_cached_message_count && row < LINES - MENU_ROWS - 2; ++i, ++row) {
        format_post_age(g_cached_messages[i].created, age, sizeof age);
        subject_buf[0] = '\0';
        if (g_cached_messages[i].deleted)
            safe_append(subject_buf, sizeof subject_buf, "(del) ");
        if (g_cached_messages[i].answered && !g_cached_messages[i].deleted)
            safe_append(subject_buf, sizeof subject_buf, "(ans) ");
        safe_append(subject_buf, sizeof subject_buf, g_cached_messages[i].subject);
        draw_highlighted_text(row, 3, COLS - 6, i == highlight,
            "%-40.40s  %-16.16s  %-10s",
            subject_buf[0] ? subject_buf : g_cached_messages[i].subject,
            g_cached_messages[i].author,
            age);
    }
}

static int
action_requires_group(void)
{
    if (g_session.current_group < 0 || g_session.current_group >= g_group_count) {
        wait_for_ack("Select a group first.");
        return 0;
    }
    return 1;
}

static void
draw_post_commands_line(void)
{
    int row = LINES - PROMPT_ROW_OFFSET;
    const int prompt_col = 4;
    move(row, 2);
    clrtoeol();
    if (g_session.is_admin) {
        if (g_cached_message_count > 0)
            mvprintw(row, prompt_col, "(N)ew Post  (R)eply  (D)elete  (B)ack");
        else
            mvprintw(row, prompt_col, "(N)ew Post  (B)ack");
    } else {
        if (g_cached_message_count > 0)
            mvprintw(row, prompt_col, "(N)ew Post  (R)eply  (B)ack");
        else
            mvprintw(row, prompt_col, "(N)ew Post  (B)ack");
    }
    refresh();
}

static void
format_post_age(time_t created, char *buf, int buflen)
{
    long days;
    time_t now;

    if (buflen <= 0 || buf == NULL)
        return;
    now = time(NULL);
    if (now <= created)
        days = 0;
    else
        days = (long)((now - created) / 86400);
    if (days <= 0) {
        safe_copy(buf, buflen, "today");
    } else {
        safe_copy(buf, buflen, "");
        safe_append_number(buf, buflen, days);
        if (days == 1)
            safe_append(buf, buflen, " day ago");
        else
            safe_append(buf, buflen, " days ago");
    }
}

static void
post_index_screen(void)
{
    int highlight;
    int ch;
    int key;

    if (!action_requires_group()) {
        pop_screen();
        push_screen(SCREEN_GROUP_LIST, "Groups");
        return;
    }

    if (load_messages_for_group(g_session.current_group) != 0) {
        wait_for_ack("Unable to load messages.");
        pop_screen();
        push_screen(SCREEN_GROUP_LIST, "Groups");
        return;
    }

    highlight = g_last_highlight;
    if (highlight >= g_cached_message_count)
        highlight = g_cached_message_count - 1;
    if (highlight < 0)
        highlight = 0;

    draw_layout("", "");
    draw_post_rows(highlight);
    draw_post_commands_line();
    refresh();

    while (1) {
        ch = read_key();
        if (is_back_key(ch)) {
            if (handle_back_navigation())
                break;
            continue;
        }
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
            draw_post_rows(highlight);
            refresh();
            continue;
        }
        if (ch == KEY_DOWN) {
            if (highlight < g_cached_message_count - 1)
                ++highlight;
            draw_post_rows(highlight);
            refresh();
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (g_cached_message_count > 0) {
                open_post_view(highlight);
                break;
            }
            continue;
        }
        key = normalize_key(ch);
        if (key == 'N') {
            open_compose(NULL, 0);
            break;
        }
        if (key == 'R') {
            if (g_cached_message_count > 0) {
                open_compose(&g_cached_messages[highlight], 0);
                break;
            }
            continue;
        }
        if (key == 'D') {
            if (!g_session.is_admin) {
                wait_for_ack("Admins only.");
                draw_post_commands_line();
                continue;
            }
            if (g_cached_message_count > 0) {
                int deleted = g_cached_messages[highlight].deleted ? 0 : 1;
                delete_or_undelete(&g_cached_messages[highlight], deleted);
            }
            draw_post_rows(highlight);
            refresh();
            continue;
        }
        if (key == 'B') {
            if (handle_back_navigation())
                break;
            continue;
        }
    }
    g_last_highlight = highlight;
}

static void
post_view_screen(int message_index)
{
    int ch;
    int key;
    struct message *msg;
    char stamp[64];

    if (message_index < 0 || message_index >= g_cached_message_count)
        return;
    msg = &g_cached_messages[message_index];

    while (1) {
        draw_layout("Post View", msg->subject);
        draw_back_hint();
        format_time(msg->created, stamp, sizeof stamp);
        mvprintw(5, 4, "From: %s", msg->author);
        mvprintw(6, 4, "Date: %s", stamp);
        mvprintw(7, 4, "Parent: %d", msg->parent_id);
        mvprintw(8, 4, "Status: %s", msg->deleted ? "Deleted" : (msg->answered ? "Answered" : "New"));
        render_body_text(msg->body, 10, LINES - MENU_ROWS - 12);
        draw_menu_lines("D Delete  U Undelete  R Reply  F Forward  S Save", "T Take Addr  V View Attach (TODO)  E Exit Attach (TODO)", "< Index  ? Help");
        refresh();
        ch = read_key();
        key = normalize_key(ch);
        if (is_back_key(ch) || key == 'B' || key == 'E') {
            if (handle_back_navigation())
                return;
        } else if (key == 'D') {
            delete_or_undelete(msg, 1);
        } else if (key == 'U') {
            delete_or_undelete(msg, 0);
        } else if (key == 'R') {
            open_compose(msg, 0);
            return;
        } else if (key == 'F') {
            open_compose(msg, 1);
            return;
        } else if (key == 'S') {
            save_message_to_group(msg);
        } else if (key == 'T') {
            take_address_from_message(msg);
        } else if (ch == '?') {
            show_help("Post View Help", "Use commands to reply, forward, or save the current message.");
        }
    }
}

static void
render_body_text(const char *body, int start_row, int max_rows)
{
    int row;
    int col;
    const char *p;

    row = start_row;
    col = 4;
    p = body;
    while (*p && row < start_row + max_rows) {
        if (*p == '\n') {
            ++row;
            col = 4;
            ++p;
            continue;
        }
        mvaddch(row, col, *p);
        ++col;
        if (col > COLS - 4) {
            col = 4;
            ++row;
        }
        ++p;
    }
}

static void
edit_body(char *buffer, int maxlen)
{
    char line[256];
    int len;

    buffer[0] = '\0';
    len = 0;
    wait_for_ack("Enter message body. '.' on its own line finishes.");
    while (1) {
        prompt_string("Body>", line, sizeof line);
        if (strcmp(line, ".") == 0)
            break;
        if (len + (int)strlen(line) + 2 >= maxlen) {
            wait_for_ack("Body full.");
            break;
        }
        safe_copy(buffer + len, maxlen - len, line);
        len += (int)strlen(line);
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }
}

static int
append_cached_message(struct message *msg)
{
    struct message *tmp;

    tmp = (struct message *)realloc(g_cached_messages,
        sizeof(struct message) * (g_cached_message_count + 1));
    if (tmp == NULL)
        return -1;
    g_cached_messages = tmp;
    g_cached_messages[g_cached_message_count] = *msg;
    ++g_cached_message_count;
    return 0;
}

static void
compose_screen(struct message *reply_source, int forward_mode)
{
    /* Use static buffers to avoid stack overflow on PDP-11 */
    struct message *newmsg = &g_compose_newmsg;
    char *to = g_compose_buffer_to;
    char *cc = g_compose_buffer_cc;
    char *attach = g_compose_buffer_attach;
    char *subject = g_compose_buffer_subject;
    char *body = g_compose_buffer_body;
    int field;
    int done;
    int ch;
    int parent;
    int key;

    if (!action_requires_group()) {
        if (pop_screen())
            push_screen(SCREEN_GROUP_LIST, "Groups");
        else
            reset_navigation(SCREEN_GROUP_LIST, "Groups");
        return;
    }

    if (load_messages_for_group(g_session.current_group) != 0) {
        wait_for_ack("Unable to load messages.");
        handle_back_navigation();
        return;
    }

    to[0] = '\0';
    cc[0] = '\0';
    attach[0] = '\0';
    body[0] = '\0';
    subject[0] = '\0';
    parent = 0;

    if (g_compose_prefill_to[0]) {
        safe_copy(to, MAX_ADDRESS, g_compose_prefill_to);
        g_compose_prefill_to[0] = '\0';
    }

    if (reply_source != NULL) {
        parent = reply_source->id;
        if (forward_mode)
            safe_copy(subject, MAX_SUBJECT, "Fwd: ");
        else
            safe_copy(subject, MAX_SUBJECT, "Re: ");
        safe_append(subject, MAX_SUBJECT, reply_source->subject);
        if (forward_mode) {
            safe_copy(body, MAX_BODY, "-------- Forwarded message --------\n");
            safe_append(body, MAX_BODY, reply_source->body);
        } else {
            safe_copy(body, MAX_BODY, "> ");
            safe_append(body, MAX_BODY, reply_source->body);
        }
    }

    field = 0;
    done = 0;

    while (!done) {
        draw_layout("Compose", "");
        draw_back_hint();
        mvprintw(5, 4, "To: %s", to);
        mvprintw(6, 4, "Cc: %s", cc);
        mvprintw(7, 4, "Attach: %s", attach);
        mvprintw(8, 4, "Subject: %s", subject);
        mvprintw(10, 4, "Body preview:");
        render_body_text(body, 11, LINES - MENU_ROWS - 13);
        draw_menu_lines("^X Send  ^C Cancel  ^O Postpone (TODO)  ^R Read File (TODO)", "^J Justify (TODO)  ^W WhereIs (TODO)  ^T Add Addr", "Enter edits field  Arrows move field");
        refresh();
        ch = read_key();
        key = normalize_key(ch);
        if (is_back_key(ch) || key == 'B') {
            handle_back_navigation();
            return;
        }
        if (ch == KEY_UP) {
            if (field > 0)
                --field;
        } else if (ch == KEY_DOWN) {
            if (field < 4)
                ++field;
        } else if (ch == '\n' || ch == '\r') {
            if (field == 0)
                prompt_string("To:", to, MAX_ADDRESS);
            else if (field == 1)
                prompt_string("Cc:", cc, MAX_ADDRESS);
            else if (field == 2)
                prompt_string("Attach:", attach, MAX_ADDRESS);
            else if (field == 3)
                prompt_string("Subject:", subject, MAX_SUBJECT);
            else if (field == 4)
                edit_body(body, MAX_BODY);
        } else if (ch == CTRL_KEY('X')) {
            if (subject[0] == '\0')
                safe_copy(subject, MAX_SUBJECT, "(no subject)");
            safe_copy(newmsg->author, sizeof newmsg->author,
                g_session.username[0] ? g_session.username : "guest");
            safe_copy(newmsg->subject, sizeof newmsg->subject, subject);
            if (body[0] == '\0')
                safe_copy(body, MAX_BODY, "(no text)\n");
            if (g_config.signature[0] != '\0') {
                if ((int)strlen(body) + (int)strlen(g_config.signature) + 10 < MAX_BODY) {
                    strcat(body, "\n");
                    strcat(body, g_config.signature);
                    strcat(body, "\n");
                }
            }
            safe_copy(newmsg->body, sizeof newmsg->body, body);
            newmsg->id = next_message_id();
            newmsg->parent_id = parent;
            newmsg->created = time(NULL);
            newmsg->deleted = 0;
            newmsg->answered = 0;
            if (reply_source != NULL && !forward_mode)
                reply_source->answered = 1;
            if (append_cached_message(newmsg) == 0) {
                save_messages_for_group(g_session.current_group);
                wait_for_ack("Message sent.");
            } else {
                wait_for_ack("Out of memory.");
            }
            done = 1;
        } else if (ch == CTRL_KEY('C')) {
            if (prompt_yesno("Discard draft? y/n"))
                done = 1;
        } else if (key == 'Q') {
            if (prompt_yesno("Discard draft? y/n"))
                done = 1;
        } else if (ch == CTRL_KEY('T')) {
            add_address_entry(0);
        }
    }
    pop_screen();
}

static void
delete_or_undelete(struct message *msg, int deleted)
{
    if (msg == NULL)
        return;
    msg->deleted = deleted;
    save_messages_for_group(g_session.current_group);
}

static void
save_message_to_group(struct message *msg)
{
    char target[MAX_GROUP_NAME];

    if (msg == NULL)
        return;
    prompt_string("Save to group:", target, sizeof target);
    if (target[0] == '\0')
        return;
    if (copy_message_to_group(msg, target) == 0) {
        msg->deleted = 1;
        save_messages_for_group(g_session.current_group);
        wait_for_ack("Message saved and marked deleted.");
    } else {
        wait_for_ack("Unable to save message.");
    }
}

static void
take_address_from_message(struct message *msg)
{
    char nick[MAX_AUTHOR];
    int idx;

    if (msg == NULL)
        return;
    if (g_addr_count >= MAX_ADDRBOOK) {
        wait_for_ack("Address book full.");
        return;
    }
    prompt_string("Nickname for address:", nick, sizeof nick);
    if (nick[0] == '\0')
        return;
    idx = g_addr_count++;
        safe_copy(g_addrbook[idx].nickname, sizeof g_addrbook[idx].nickname, nick);
        safe_copy(g_addrbook[idx].fullname, sizeof g_addrbook[idx].fullname, msg->author);
        safe_copy(g_addrbook[idx].address, sizeof g_addrbook[idx].address, msg->author);
    g_addrbook[idx].is_list = 0;
    save_address_book();
    wait_for_ack("Address added.");
}

static void
expunge_messages(void)
{
    int i;
    int dst;

    dst = 0;
    for (i = 0; i < g_cached_message_count; ++i) {
        if (g_cached_messages[i].deleted)
            continue;
        if (dst != i)
            g_cached_messages[dst] = g_cached_messages[i];
        ++dst;
    }
    g_cached_message_count = dst;
    save_messages_for_group(g_session.current_group);
}

static void
search_messages(void)
{
    char term[MAX_SUBJECT];
    int start;
    int i;

    prompt_string("Search subject:", term, sizeof term);
    if (term[0] == '\0')
        return;
    start = g_last_highlight + 1;
    for (i = 0; i < g_cached_message_count; ++i) {
        int idx = (start + i) % g_cached_message_count;
        if (strstr(g_cached_messages[idx].subject, term) ||
            strstr(g_cached_messages[idx].author, term)) {
            g_last_highlight = idx;
            wait_for_ack("Match selected.");
            return;
        }
    }
    wait_for_ack("No matches.");
}

static void
draw_address_rows(int highlight)
{
    int row;
    int i;

    row = 8;
    mvprintw(row - 1, 3, "%-4s %-12s %-20s %-32s", "No", "Nick", "Fullname", "Address/List");
    for (i = 0; i < g_addr_count && row < LINES - MENU_ROWS - 2; ++i, ++row) {
        draw_highlighted_text(row, 3, COLS - 5, i == highlight,
            "%-4d %-12s %-20.20s %-32.32s%s", i + 1, g_addrbook[i].nickname,
            g_addrbook[i].fullname, g_addrbook[i].address,
            g_addrbook[i].is_list ? " (list)" : "");
    }
}

static void
address_book_screen(void)
{
    int highlight;
    int ch;
    int key;

    highlight = 0;
    draw_layout("Address Book", "");
    draw_back_hint();
    draw_address_rows(highlight);
    draw_menu_lines("A Add  S Create List  V View/Update  D Delete", "C Compose To  T Take (TODO)  O Other", "< Main  ? Help");
    refresh();

    while (1) {
        ch = read_key();
        key = normalize_key(ch);
        if (is_back_key(ch)) {
            if (handle_back_navigation())
                break;
            continue;
        }
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
        } else if (ch == KEY_DOWN) {
            if (highlight < g_addr_count - 1)
                ++highlight;
        } else if (key == 'A') {
            add_address_entry(0);
        } else if (key == 'S') {
            add_address_entry(1);
        } else if (key == 'V' || ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            edit_address_entry(highlight);
        } else if (key == 'D') {
            delete_address_entry(highlight);
        } else if (key == 'C') {
            compose_to_entry(highlight);
            break;
        } else if (ch == '?') {
            show_help("Address Book Help", "Manage nicknames and distribution lists from this screen.");
        }
        draw_address_rows(highlight);
        refresh();
    }
}

static void
add_address_entry(int is_list)
{
    if (g_addr_count >= MAX_ADDRBOOK) {
        wait_for_ack("Address book full.");
        return;
    }
    prompt_string("Nickname:", g_addrbook[g_addr_count].nickname, MAX_AUTHOR);
    if (g_addrbook[g_addr_count].nickname[0] == '\0')
        return;
    prompt_string("Full name:", g_addrbook[g_addr_count].fullname, MAX_GROUP_DESC);
    prompt_string(is_list ? "List members:" : "Address:", g_addrbook[g_addr_count].address, MAX_ADDRESS);
    g_addrbook[g_addr_count].is_list = is_list;
    ++g_addr_count;
    save_address_book();
}

static void
edit_address_entry(int idx)
{
    if (idx < 0 || idx >= g_addr_count)
        return;
    prompt_string("Full name:", g_addrbook[idx].fullname, MAX_GROUP_DESC);
    prompt_string(g_addrbook[idx].is_list ? "List members:" : "Address:", g_addrbook[idx].address, MAX_ADDRESS);
    save_address_book();
}

static void
delete_address_entry(int idx)
{
    int i;

    if (idx < 0 || idx >= g_addr_count)
        return;
    for (i = idx; i < g_addr_count - 1; ++i)
        g_addrbook[i] = g_addrbook[i + 1];
    --g_addr_count;
    save_address_book();
}

static void
compose_to_entry(int idx)
{
    if (idx < 0 || idx >= g_addr_count)
        return;
    safe_copy(g_compose_prefill_to, sizeof g_compose_prefill_to, g_addrbook[idx].address);
    open_compose(NULL, 0);
}

static void
setup_screen(void)
{
    struct menu_item setup_menu[4];
    int choice;
    int selected_index;
    int focus_index;
    int highlight = 0;

    setup_menu[0].key = 'P';
    setup_menu[0].label = "Printer Command";
    setup_menu[1].key = 'G';
    setup_menu[1].label = "Edit Signature";
    setup_menu[2].key = 'N';
    setup_menu[2].label = "Change Password";
    setup_menu[3].key = 'B';
    setup_menu[3].label = "Back - Return to previous menu";

    while (1) {
        draw_layout("Setup", "Configure session");
        mvprintw(5, 4, "Printer: %s", g_config.printer[0] ? g_config.printer : "(default)");
        mvprintw(6, 4, "Signature: %s", g_config.signature[0] ? g_config.signature : "(none)");
        mvprintw(7, 4, "Password changes require admin privileges.");
        selected_index = -1;
        focus_index = -1;
        choice = run_menu(9, setup_menu, 4, highlight, &selected_index, &focus_index);
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
        case 'P':
            edit_printer();
            break;
        case 'G':
            edit_signature();
            break;
        case 'N':
            change_password();
            break;
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
    edit_body(g_config.signature, sizeof g_config.signature);
    save_config();
}

static void
edit_printer(void)
{
    prompt_string("Printer command:", g_config.printer, sizeof g_config.printer);
    save_config();
}

static void
change_password(void)
{
    char buf[MAX_CONFIG_VALUE];

    if (!g_session.is_admin) {
        wait_for_ack("Only admin may change password.");
        return;
    }
    prompt_string("New password:", buf, sizeof buf);
    if (buf[0] == '\0')
        return;
    safe_copy(g_config.password, sizeof g_config.password, buf);
    save_config();
}
static void
handle_group_action(char action_key, int group_index)
{
    if (!g_session.is_admin) {
        wait_for_ack("Admins only.");
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
        break;
    }
}
