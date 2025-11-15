#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <curses.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <time.h>

#if defined(__APPLE__) || defined(__MACH__) || defined(__linux__)
#include <unistd.h>
#else
extern int unlink();
extern int link();
extern int getpid();
extern unsigned sleep();
#endif

#ifndef A_REVERSE
#ifdef A_STANDOUT
#define A_REVERSE A_STANDOUT
#else
#define A_REVERSE 0
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

#ifdef LEGACY_CURSES
#ifndef remove
#define remove unlink
#endif
#define curs_set legacy_curs_set
#define attron legacy_attron
#define attroff legacy_attroff
static int legacy_curs_set(int visible);
static int legacy_attron(int attr);
static int legacy_attroff(int attr);
static int legacy_mvgetnstr(int y, int x, char *buf, int maxlen);
#endif

#define DATA_DIR "bbsdata"
#define GROUPS_FILE DATA_DIR "/groups.txt"
#define ADDRESS_FILE DATA_DIR "/addrbook.txt"
#define CONFIG_FILE DATA_DIR "/config.txt"
#define LOCK_FILE DATA_DIR "/.lock"
#define PROGRAM_TITLE "PDP-11 BBS"
#define PROGRAM_VERSION "0.2"
#define ADMIN_USER "admin"
#define ADMIN_PASS "2326"

#define MIN_COLS 80
#define MIN_ROWS 24
#define MENU_ROWS 5

/* Reduce memory usage significantly on PDP-11 */
#ifdef __pdp11__
#define MAX_GROUPS 16
#define MAX_GROUP_NAME 32
#define MAX_GROUP_DESC 48
#define MAX_MESSAGES 64
#define MAX_SUBJECT 64
#define MAX_BODY 1024
#define MAX_AUTHOR 32
#define MAX_ADDRESS 64
#define MAX_ADDRBOOK 32
#define MAX_CONFIG_VALUE 128
#define MAX_MENU_OPTIONS 16
#else
#define MAX_GROUPS 64
#define MAX_GROUP_NAME 48
#define MAX_GROUP_DESC 80
#define MAX_MESSAGES 256
#define MAX_SUBJECT 96
#define MAX_BODY 4096
#define MAX_AUTHOR 48
#define MAX_ADDRESS 128
#define MAX_ADDRBOOK 128
#define MAX_CONFIG_VALUE 256
#define MAX_MENU_OPTIONS 16
#endif

#define CTRL_KEY(x) ((x) & 0x1f)
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

struct group {
    char name[MAX_GROUP_NAME];
    char description[MAX_GROUP_DESC];
    int deleted;
};

struct message {
    int id;
    int parent_id;
    time_t created;
    int deleted;
    int answered;
    char author[MAX_AUTHOR];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
};

struct address_entry {
    char nickname[MAX_AUTHOR];
    char fullname[MAX_GROUP_DESC];
    char address[MAX_ADDRESS];
    int is_list;
};

struct config_data {
    char printer[MAX_CONFIG_VALUE];
    char signature[MAX_CONFIG_VALUE];
    char password[MAX_CONFIG_VALUE];
};

struct session {
    char username[MAX_AUTHOR];
    int is_admin;
    int current_group;
};

struct menu_option {
    char key;
    const char *label;
    enum screen_id target;
};

static const struct menu_option g_main_menu_options[] = {
    {'?', "Help", SCREEN_HELP},
    {'C', "Compose", SCREEN_COMPOSE},
    {'I', "Group Index", SCREEN_POST_INDEX},
    {'L', "Group List", SCREEN_GROUP_LIST},
    {'A', "Address Book", SCREEN_ADDRESS_BOOK},
    {'S', "Setup", SCREEN_SETUP},
    {'Q', "Quit", SCREEN_LOGIN},
    {'O', "Other Cmds", SCREEN_MAIN}
};

static const int g_main_menu_option_count =
    (int)(sizeof(g_main_menu_options) / sizeof(g_main_menu_options[0]));

static struct group g_groups[MAX_GROUPS];
static int g_group_count;
static struct address_entry g_addrbook[MAX_ADDRBOOK];
static int g_addr_count;
static struct config_data g_config;
static struct session g_session;
static enum screen_id g_screen = SCREEN_LOGIN;
static int g_running = 1;
static int g_show_extras = 0;
static int g_lock_depth = 0;
static int g_last_highlight = 0;
static struct message *g_cached_messages = NULL;
static int g_cached_message_count = 0;
static char g_compose_prefill_to[MAX_ADDRESS];
static int g_ui_ready = 0;

/* Static buffers to avoid stack overflow on PDP-11 */
static char g_compose_buffer_to[MAX_ADDRESS];
static char g_compose_buffer_cc[MAX_ADDRESS];
static char g_compose_buffer_attach[MAX_ADDRESS];
static char g_compose_buffer_subject[MAX_SUBJECT];
static char g_compose_buffer_body[MAX_BODY];
static struct message g_compose_newmsg;
static struct message g_temp_message; /* For load_messages_direct and copy_message_to_group */

static void ensure_data_dir(void);
static void init_config(void);
static void start_ui(void);
static void stop_ui(void);
static int screen_too_small(void);
static void require_screen_size(void);
static void logout_session(void);
static void draw_layout(const char *title, const char *status);
static void draw_menu_lines(const char *line1, const char *line2, const char *line3);
static void draw_menu_line_row(int row, const char *text, int preserve_border);
static void show_status(const char *msg);
static void wait_for_ack(const char *msg);
static void prompt_string(const char *label, char *buffer, int maxlen);
static int prompt_yesno(const char *question);
static void show_help(const char *title, const char *body);
static void login_screen(void);
static void main_menu_screen(void);
static void group_list_screen(void);
static void post_index_screen(void);
static void post_view_screen(int message_index);
static void compose_screen(struct message *reply_source, int forward_mode);
static void address_book_screen(void);
static void setup_screen(void);
static void draw_group_rows(int highlight);
static void draw_post_rows(int highlight);
static void draw_address_rows(int highlight);
static void draw_main_options(int highlight);
static void toggle_extras(void);
static void update_status_line(void);
static void render_body_text(const char *body, int start_row, int max_rows);
static void edit_body(char *buffer, int maxlen);
static void safe_copy(char *dst, size_t dstlen, const char *src);
static void safe_append(char *dst, size_t dstlen, const char *src);
static void safe_append_char(char *dst, size_t dstlen, char ch);
static void safe_append_number(char *dst, size_t dstlen, long value);
static void safe_append_two_digit(char *dst, size_t dstlen, int value);

static void trim_newline(char *text);
static int load_groups(void);
static int save_groups(void);
static int load_messages_for_group(int group_index);
static int save_messages_for_group(int group_index);
static int load_messages_direct(const char *group_name, struct message **messages, int *count);
static int save_messages_direct(const char *group_name, struct message *messages, int count);
static void free_cached_messages(void);
static int next_message_id(void);
static int copy_message_to_group(struct message *msg, const char *group_name);
static const char *current_group_name(void);
static void add_group(void);
static void rename_group(int idx);
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
static void load_address_book(void);
static void save_address_book(void);
static void add_address_entry(int is_list);
static void edit_address_entry(int idx);
static void delete_address_entry(int idx);
static void compose_to_entry(int idx);
static void load_config(void);
static void save_config(void);
static void edit_signature(void);
static void edit_printer(void);
static void change_password(void);
static int build_group_path(const char *group_name, char *path, int maxlen);

static int acquire_lock(void);
static void release_lock(void);
static void lock_guard(void);
static void unlock_guard(void);
static int read_key(void);
static int input_has_data(void);

static int g_pending_key = -1;

static void
trim_newline(char *text)
{
    int len;

    len = (int)strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[len - 1] = '\0';
        --len;
    }
}

static void
safe_copy(char *dst, size_t dstlen, const char *src)
{
    size_t i;

    if (dstlen == 0 || dst == NULL)
        return;
    if (src == NULL)
        src = "";
    for (i = 0; i < dstlen - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void
safe_append(char *dst, size_t dstlen, const char *src)
{
    size_t used;

    if (dst == NULL || dstlen == 0 || src == NULL)
        return;
    for (used = 0; used < dstlen - 1 && dst[used] != '\0'; ++used)
        ;
    while (*src && used < dstlen - 1) {
        dst[used++] = *src++;
    }
    dst[used] = '\0';
}

static void
safe_append_char(char *dst, size_t dstlen, char ch)
{
    char tmp[2];

    tmp[0] = ch;
    tmp[1] = '\0';
    safe_append(dst, dstlen, tmp);
}

static void
safe_append_number(char *dst, size_t dstlen, long value)
{
    char tmp[32];
    int i;
    int neg;
    long v;

    if (dst == NULL || dstlen == 0)
        return;
    v = value;
    neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0 && i < (int)sizeof(tmp) - 1) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg && i < (int)sizeof(tmp) - 1)
        tmp[i++] = '-';
    tmp[i] = '\0';
    while (i > 0) {
        --i;
        safe_append_char(dst, dstlen, tmp[i]);
    }
}

static void
safe_append_two_digit(char *dst, size_t dstlen, int value)
{
    char tmp[3];

    if (value < 0)
        value = 0;
    value %= 100;
    tmp[0] = (char)('0' + ((value / 10) % 10));
    tmp[1] = (char)('0' + (value % 10));
    tmp[2] = '\0';
    safe_append(dst, dstlen, tmp);
}

#ifdef LEGACY_CURSES
static int
legacy_curs_set(int visible)
{
    visible = visible;
    return 0;
}

static int
legacy_attron(int attr)
{
    if (attr & A_REVERSE)
        standout();
    return 0;
}

static int
legacy_attroff(int attr)
{
    if (attr & A_REVERSE)
        standend();
    return 0;
}

static int
legacy_mvgetnstr(int y, int x, char *buf, int maxlen)
{
    int len;
    int limit;
    int ch;

    if (buf == NULL || maxlen <= 0)
        return 0;
    len = 0;
    limit = maxlen;
    buf[0] = '\0';
    move(y, x);
    refresh();
    while (1) {
        ch = getch();
        if (ch == '\n' || ch == '\r') {
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
#endif

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
            compose_screen(NULL, 0);
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
            g_screen = SCREEN_MAIN;
            break;
        }
    }

    free_cached_messages();
    stop_ui();
    return EXIT_SUCCESS;
}

static void
ensure_data_dir(void)
{
    struct stat st;

    if (stat(DATA_DIR, &st) == -1) {
        if (mkdir(DATA_DIR, 0755) == -1) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s exists but is not a directory.\n", DATA_DIR);
        exit(EXIT_FAILURE);
    }
}

static void
init_config(void)
{
    g_config.printer[0] = '\0';
    g_config.signature[0] = '\0';
    g_config.password[0] = '\0';
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
    curs_set(0);
    g_ui_ready = 1;
}

static void
stop_ui(void)
{
    curs_set(1);
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
}

static void
draw_layout(const char *title, const char *status)
{
    int col;
    int menu_bar;

    clear();
    for (col = 0; col < COLS; ++col) {
        mvaddch(0, col, '-');
        mvaddch(LINES - 1, col, '-');
    }
    for (col = 0; col < LINES; ++col) {
        mvaddch(col, 0, '|');
        mvaddch(col, COLS - 1, '|');
    }
    mvaddch(0, 0, '+');
    mvaddch(0, COLS - 1, '+');
    mvaddch(LINES - 1, 0, '+');
    mvaddch(LINES - 1, COLS - 1, '+');

    mvprintw(1, 2, "%s %s", PROGRAM_TITLE, PROGRAM_VERSION);
    mvprintw(1, COLS - 25, "User: %s%s",
        g_session.username[0] ? g_session.username : "(not logged)",
        g_session.is_admin ? " (admin)" : "");
    mvprintw(2, 2, "%s", title);
    if (status && *status)
        mvprintw(2, COLS - (int)strlen(status) - 2, "%s", status);

    menu_bar = LINES - MENU_ROWS - 1;
    for (col = 1; col < COLS - 1; ++col)
        mvaddch(menu_bar, col, '=');
}

static void
draw_menu_lines(const char *line1, const char *line2, const char *line3)
{
    int start;

    start = LINES - MENU_ROWS + 1;
    draw_menu_line_row(start, line1, 0);
    draw_menu_line_row(start + 1, line2, 0);
    draw_menu_line_row(start + 2, line3, 0);
    draw_menu_line_row(start + 3, "Arrows move  Return selects  ESC=Back  Letter=command", 1);
}

static void
draw_menu_line_row(int row, const char *text, int preserve_border)
{
    const char *msg;
    int col;
    int len;
    int maxcols;
    int i;

    msg = text ? text : "";
    col = 2;
    maxcols = COLS - 2;
    len = (int)strlen(msg);
    if (len > maxcols - col)
        len = maxcols - col;
    move(row, col);
    for (i = 0; i < len; ++i)
        addch(msg[i]);
    if (!preserve_border) {
        int blank = maxcols - (col + len);
        while (blank-- > 0)
            addch(' ');
    }
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

    row = LINES - MENU_ROWS - 3;
    mvprintw(row, 2, "%-*s", COLS - 4, "");
    mvprintw(row, 2, "%s", label);
#ifdef LEGACY_CURSES
    noecho();
#endif
    curs_set(1);
#ifdef LEGACY_CURSES
    legacy_mvgetnstr(row + 1, 4, buffer, maxlen - 1);
#else
    echo();
    mvgetnstr(row + 1, 4, buffer, maxlen - 1);
#endif
    noecho();
    curs_set(0);
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
        return ERR;
    
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
    /* Not an arrow key sequence */
    g_pending_key = next;
    return ch;
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
    mvprintw(5, 4, "%s", body);
    draw_menu_lines("[Space] Continue", "", "");
    wait_for_ack("Press any key to return.");
}

static void
login_screen(void)
{
    char user[MAX_AUTHOR];
    char pass[MAX_AUTHOR];

    draw_layout("Login", "");
    mvprintw(6, 4, "Welcome to the PDP-11 message boards.");
    mvprintw(8, 4, "Enter user id (admin requires password).");
    draw_menu_lines("Enter user id", "", "");
    prompt_string("User id:", user, sizeof user);
    if (user[0] == '\0')
        return;
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
    g_screen = SCREEN_MAIN;
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
    mvprintw(3, 2, "%-*s", COLS - 4, status);
}

static void
draw_main_options(int highlight)
{
    static int last_highlight = -1;
    static int initialized = 0;
    int count;
    int i;
    int row;
    char buf[128];

    count = g_main_menu_option_count;
    row = 6;
    
    /* Initial full render */
    if (!initialized) {
        for (i = 0; i < count; ++i) {
            move(row + i, 6);
            clrtoeol();
            if (i == highlight) {
                sprintf(buf, "\033[7m%c) %s\033[0m", g_main_menu_options[i].key,
                    g_main_menu_options[i].label);
            } else {
                sprintf(buf, "%c) %s", g_main_menu_options[i].key,
                    g_main_menu_options[i].label);
            }
            mvprintw(row + i, 6, "%s", buf);
        }
        last_highlight = highlight;
        initialized = 1;
    }
    /* Only redraw changed lines for efficiency */
    else if (last_highlight != highlight) {
        /* Clear and redraw old highlighted line as normal */
        if (last_highlight >= 0 && last_highlight < count) {
            move(row + last_highlight, 6);
            clrtoeol();
            sprintf(buf, "%c) %s", g_main_menu_options[last_highlight].key,
                g_main_menu_options[last_highlight].label);
            mvprintw(row + last_highlight, 6, "%s", buf);
        }
        
        /* Clear and redraw new highlighted line with reverse video */
        if (highlight >= 0 && highlight < count) {
            move(row + highlight, 6);
            clrtoeol();
            sprintf(buf, "\033[7m%c) %s\033[0m", g_main_menu_options[highlight].key,
                g_main_menu_options[highlight].label);
            mvprintw(row + highlight, 6, "%s", buf);
        }
        
        last_highlight = highlight;
    }
    if (g_show_extras) {
        mvprintw(row + count + 2, 6, "Extras: Release notes (TODO), Print Help (TODO)");
    }
    draw_menu_lines("? Help  C Compose  I Index  L Groups  A Address  S Setup", "O Other  Q Quit", "Use letter keys or arrows");
}

static void
main_menu_screen(void)
{
    int highlight;
    int ch;
    int option_count;

    highlight = g_last_highlight;
    option_count = g_main_menu_option_count;
    if (highlight < 0)
        highlight = 0;
    if (highlight >= option_count)
        highlight = option_count - 1;
    draw_layout("Main Menu", "");
    update_status_line();
    draw_main_options(highlight);
    refresh();

    while (1) {
        ch = read_key();
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
        } else if (ch == KEY_DOWN) {
            if (highlight < option_count - 1)
                ++highlight;
        } else {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
                ch = g_main_menu_options[highlight].key;
            if (ch == 'O' || ch == 'o') {
                toggle_extras();
            } else if (ch == 'Q' || ch == 'q') {
                if (prompt_yesno("Logout? y/n")) {
                    logout_session();
                    g_screen = SCREEN_LOGIN;
                    highlight = 0;
                    break;
                }
            } else if (ch == 'I' || ch == 'i') {
                g_screen = SCREEN_POST_INDEX;
                break;
            } else if (ch == 'L' || ch == 'l') {
                g_screen = SCREEN_GROUP_LIST;
                break;
            } else if (ch == 'C' || ch == 'c') {
                g_screen = SCREEN_COMPOSE;
                break;
            } else if (ch == 'A' || ch == 'a') {
                g_screen = SCREEN_ADDRESS_BOOK;
                break;
            } else if (ch == 'S' || ch == 's') {
                g_screen = SCREEN_SETUP;
                break;
            } else if (ch == '?') {
                show_help("Main Menu Help", "Use letter keys to trigger commands. Extras toggle additional commands.");
                break;
            }
        }
        /* Only redraw the menu options, not the entire layout */
        draw_main_options(highlight);
        refresh();
    }
    g_last_highlight = highlight;
}

static void
toggle_extras(void)
{
    g_show_extras = !g_show_extras;
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

    if (!g_session.is_admin) {
        wait_for_ack("Only admin may create groups.");
        return;
    }
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
rename_group(int idx)
{
    char newname[MAX_GROUP_NAME];

    if (idx < 0 || idx >= g_group_count)
        return;
    prompt_string("New name:", newname, sizeof newname);
    if (newname[0] == '\0')
        return;
    safe_copy(g_groups[idx].name, sizeof g_groups[idx].name, newname);
    save_groups();
}

static void
mark_delete_group(int idx)
{
    if (idx < 0 || idx >= g_group_count)
        return;
    g_groups[idx].deleted = !g_groups[idx].deleted;
    save_groups();
}

static void
expunge_groups(void)
{
    int i;
    int dst;

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
            g_screen = SCREEN_POST_INDEX;
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
            wait_for_ack("Match highlighted.");
            return;
        }
    }
    wait_for_ack("No matches.");
}

static void
draw_group_rows(int highlight)
{
    static int last_highlight = -1;
    static int initialized = 0;
    int row;
    int i;
    const char *flag;
    char buf[256];

    row = 5;
    mvprintw(row - 1, 4, "%-4s %-24s %-40s", "No.", "Group", "Description");
    
    /* Initial full render */
    if (!initialized) {
        for (i = 0; i < g_group_count && i < LINES - MENU_ROWS - 2; ++i) {
            flag = g_groups[i].deleted ? "D" : " ";
            move(row + i, 4);
            clrtoeol();
            if (i == highlight) {
                sprintf(buf, "\033[7m%-4d %-24s %-40s %s\033[0m", i + 1, g_groups[i].name,
                    g_groups[i].description, flag);
            } else {
                sprintf(buf, "%-4d %-24s %-40s %s", i + 1, g_groups[i].name,
                    g_groups[i].description, flag);
            }
            mvprintw(row + i, 4, "%s", buf);
        }
        last_highlight = highlight;
        initialized = 1;
    }
    else if (last_highlight != highlight) {
        /* Clear and redraw old highlight as normal */
        if (last_highlight >= 0 && last_highlight < g_group_count) {
            flag = g_groups[last_highlight].deleted ? "D" : " ";
            move(5 + last_highlight, 4);
            clrtoeol();
            sprintf(buf, "%-4d %-24s %-40s %s", last_highlight + 1, g_groups[last_highlight].name,
                g_groups[last_highlight].description, flag);
            mvprintw(5 + last_highlight, 4, "%s", buf);
        }
        
        /* Clear and draw new highlight */
        if (highlight >= 0 && highlight < g_group_count) {
            flag = g_groups[highlight].deleted ? "D" : " ";
            move(5 + highlight, 4);
            clrtoeol();
            sprintf(buf, "\033[7m%-4d %-24s %-40s %s\033[0m", highlight + 1, g_groups[highlight].name,
                g_groups[highlight].description, flag);
            mvprintw(5 + highlight, 4, "%s", buf);
        }
        
        last_highlight = highlight;
    }
}

static void
group_list_screen(void)
{
    int highlight;
    int ch;

    highlight = g_last_highlight;
    if (highlight >= g_group_count)
        highlight = g_group_count - 1;
    if (highlight < 0)
        highlight = 0;

    draw_layout("Group List", "");
    draw_group_rows(highlight);
    draw_menu_lines("V View  C Create  D Delete  R Rename  E Expunge", "N Next  P Prev  G Goto  < Main  O Other  S Select", "W WhereIs  ? Help");
    refresh();

    while (1) {
        ch = read_key();
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
        } else if (ch == KEY_DOWN) {
            if (highlight < g_group_count - 1)
                ++highlight;
        } else if (ch == 'V' || ch == 'v' || ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            select_group(highlight);
            g_screen = SCREEN_POST_INDEX;
            break;
        } else if (ch == 'C' || ch == 'c') {
            add_group();
        } else if (ch == 'D' || ch == 'd') {
            mark_delete_group(highlight);
        } else if (ch == 'R' || ch == 'r') {
            rename_group(highlight);
        } else if (ch == 'E' || ch == 'e') {
            if (prompt_yesno("Expunge groups? y/n"))
                expunge_groups();
        } else if (ch == 'N' || ch == 'n') {
            if (highlight < g_group_count - 1)
                ++highlight;
        } else if (ch == 'P' || ch == 'p') {
            if (highlight > 0)
                --highlight;
        } else if (ch == 'G' || ch == 'g') {
            goto_group_prompt();
            break;
        } else if (ch == 'S' || ch == 's') {
            select_group(highlight);
            wait_for_ack("Group selected.");
        } else if (ch == 'W' || ch == 'w') {
            whereis_group();
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Return to main menu? y/n")) {
                g_screen = SCREEN_MAIN;
                break;
            }
        } else if (ch == '?') {
            show_help("Group List Help", "Use commands displayed to manage groups. Deleted groups show D flag until expunged.");
        } else if (ch == 'O' || ch == 'o') {
            toggle_extras();
        }
        draw_group_rows(highlight);
        refresh();
    }
    g_last_highlight = highlight;
}

static void
free_cached_messages(void)
{
    if (g_cached_messages != NULL) {
        free(g_cached_messages);
        g_cached_messages = NULL;
    }
    g_cached_message_count = 0;
}

static int
next_message_id(void)
{
    int i;
    int max_id;

    max_id = 0;
    for (i = 0; i < g_cached_message_count; ++i) {
        if (g_cached_messages[i].id > max_id)
            max_id = g_cached_messages[i].id;
    }
    return max_id + 1;
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
    static int last_highlight = -1;
    static int initialized = 0;
    int row;
    int i;
    char stamp[32];
    char status;
    char buf[256];

    row = 5;
    mvprintw(row - 1, 2, "%-4s %-2s %-5s %-16s %-40s", "No", "St", "ID", "Poster", "Subject");
    
    /* Initial full render */
    if (!initialized) {
        for (i = 0; i < g_cached_message_count && i < LINES - MENU_ROWS - 2; ++i) {
            status = g_cached_messages[i].deleted ? 'D' : (g_cached_messages[i].answered ? 'A' : 'N');
            move(row + i, 2);
            clrtoeol();
            if (i == highlight) {
                sprintf(buf, "\033[7m%-4d %-2c %-5d %-16.16s %-40.40s\033[0m",
                    i + 1, status, g_cached_messages[i].id,
                    g_cached_messages[i].author, g_cached_messages[i].subject);
            } else {
                sprintf(buf, "%-4d %-2c %-5d %-16.16s %-40.40s",
                    i + 1, status, g_cached_messages[i].id,
                    g_cached_messages[i].author, g_cached_messages[i].subject);
            }
            mvprintw(row + i, 2, "%s", buf);
        }
        last_highlight = highlight;
        initialized = 1;
    }
    else if (last_highlight != highlight) {
        /* Clear and redraw old highlight as normal */
        if (last_highlight >= 0 && last_highlight < g_cached_message_count) {
            status = g_cached_messages[last_highlight].deleted ? 'D' : (g_cached_messages[last_highlight].answered ? 'A' : 'N');
            move(5 + last_highlight, 2);
            clrtoeol();
            sprintf(buf, "%-4d %-2c %-5d %-16.16s %-40.40s",
                last_highlight + 1, status, g_cached_messages[last_highlight].id,
                g_cached_messages[last_highlight].author, g_cached_messages[last_highlight].subject);
            mvprintw(5 + last_highlight, 2, "%s", buf);
        }
        
        /* Clear and draw new highlight */
        if (highlight >= 0 && highlight < g_cached_message_count) {
            status = g_cached_messages[highlight].deleted ? 'D' : (g_cached_messages[highlight].answered ? 'A' : 'N');
            move(5 + highlight, 2);
            clrtoeol();
            sprintf(buf, "\033[7m%-4d %-2c %-5d %-16.16s %-40.40s\033[0m",
                highlight + 1, status, g_cached_messages[highlight].id,
                g_cached_messages[highlight].author, g_cached_messages[highlight].subject);
            mvprintw(5 + highlight, 2, "%s", buf);
        }
        
        last_highlight = highlight;
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
post_index_screen(void)
{
    int highlight;
    int ch;
    const char *group;

    if (!action_requires_group()) {
        g_screen = SCREEN_GROUP_LIST;
        return;
    }

    if (load_messages_for_group(g_session.current_group) != 0) {
        wait_for_ack("Unable to load messages.");
        g_screen = SCREEN_GROUP_LIST;
        return;
    }

    highlight = g_last_highlight;
    if (highlight >= g_cached_message_count)
        highlight = g_cached_message_count - 1;
    if (highlight < 0)
        highlight = 0;

    group = current_group_name();

    draw_layout("Post Index", group ? group : "");
    draw_post_rows(highlight);
    draw_menu_lines("V View  D Delete  U Undelete  R Reply  F Forward", "S Save  X Expunge  T Take Addr  Y Print (TODO)  W WhereIs", "O Other  < Groups  ? Help");
    refresh();

    while (1) {
        ch = read_key();
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
        } else if (ch == KEY_DOWN) {
            if (highlight < g_cached_message_count - 1)
                ++highlight;
        } else if (ch == 'V' || ch == 'v' || ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (g_cached_message_count > 0)
                post_view_screen(highlight);
        } else if (ch == 'D' || ch == 'd') {
            if (g_cached_message_count > 0)
                delete_or_undelete(&g_cached_messages[highlight], 1);
        } else if (ch == 'U' || ch == 'u') {
            if (g_cached_message_count > 0)
                delete_or_undelete(&g_cached_messages[highlight], 0);
        } else if (ch == 'R' || ch == 'r') {
            if (g_cached_message_count > 0)
                compose_screen(&g_cached_messages[highlight], 0);
        } else if (ch == 'F' || ch == 'f') {
            if (g_cached_message_count > 0)
                compose_screen(&g_cached_messages[highlight], 1);
        } else if (ch == 'S' || ch == 's') {
            if (g_cached_message_count > 0)
                save_message_to_group(&g_cached_messages[highlight]);
        } else if (ch == 'X' || ch == 'x') {
            expunge_messages();
        } else if (ch == 'T' || ch == 't') {
            if (g_cached_message_count > 0)
                take_address_from_message(&g_cached_messages[highlight]);
        } else if (ch == 'Y' || ch == 'y') {
            wait_for_ack("Printing not yet implemented.");
        } else if (ch == 'W' || ch == 'w') {
            search_messages();
        } else if (ch == 'O' || ch == 'o') {
            toggle_extras();
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Return to group list? y/n")) {
                g_screen = SCREEN_GROUP_LIST;
                break;
            }
        } else if (ch == '?') {
            show_help("Post Index Help", "Use commands to manage posts. Deleted posts are expunged with X.");
        }
        draw_post_rows(highlight);
        refresh();
    }
    g_last_highlight = highlight;
}

static void
post_view_screen(int message_index)
{
    int ch;
    struct message *msg;
    char stamp[64];

    if (message_index < 0 || message_index >= g_cached_message_count)
        return;
    msg = &g_cached_messages[message_index];

    while (1) {
        draw_layout("Post View", msg->subject);
        format_time(msg->created, stamp, sizeof stamp);
        mvprintw(5, 4, "From: %s", msg->author);
        mvprintw(6, 4, "Date: %s", stamp);
        mvprintw(7, 4, "Parent: %d", msg->parent_id);
        mvprintw(8, 4, "Status: %s", msg->deleted ? "Deleted" : (msg->answered ? "Answered" : "New"));
        render_body_text(msg->body, 10, LINES - MENU_ROWS - 12);
        draw_menu_lines("D Delete  U Undelete  R Reply  F Forward  S Save", "T Take Addr  V View Attach (TODO)  E Exit Attach (TODO)", "< Index  ? Help");
        refresh();
        ch = read_key();
        if (ch == 'D' || ch == 'd') {
            delete_or_undelete(msg, 1);
        } else if (ch == 'U' || ch == 'u') {
            delete_or_undelete(msg, 0);
        } else if (ch == 'R' || ch == 'r') {
            compose_screen(msg, 0);
        } else if (ch == 'F' || ch == 'f') {
            compose_screen(msg, 1);
        } else if (ch == 'S' || ch == 's') {
            save_message_to_group(msg);
        } else if (ch == 'T' || ch == 't') {
            take_address_from_message(msg);
        } else if (ch == 'E' || ch == 'e') {
            break;
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Return to index? y/n"))
                break;
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

    if (!action_requires_group())
        return;

    if (load_messages_for_group(g_session.current_group) != 0) {
        wait_for_ack("Unable to load messages.");
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
        mvprintw(5, 4, "To: %s", to);
        mvprintw(6, 4, "Cc: %s", cc);
        mvprintw(7, 4, "Attach: %s", attach);
        mvprintw(8, 4, "Subject: %s", subject);
        mvprintw(10, 4, "Body preview:");
        render_body_text(body, 11, LINES - MENU_ROWS - 13);
        draw_menu_lines("^X Send  ^C Cancel  ^O Postpone (TODO)  ^R Read File (TODO)", "^J Justify (TODO)  ^W WhereIs (TODO)  ^T Add Addr", "Enter edits field  Arrows move field");
        refresh();
        ch = read_key();
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
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Discard draft? y/n"))
                done = 1;
        } else if (ch == CTRL_KEY('T')) {
            add_address_entry(0);
        }
    }
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
    static int last_highlight = -1;
    static int initialized = 0;
    int row;
    int i;
    char buf[256];

    row = 5;
    mvprintw(row - 1, 3, "%-4s %-12s %-20s %-32s", "No", "Nick", "Fullname", "Address/List");
    
    /* Initial full render */
    if (!initialized) {
        for (i = 0; i < g_addr_count && i < LINES - MENU_ROWS - 2; ++i) {
            move(row + i, 3);
            clrtoeol();
            if (i == highlight) {
                sprintf(buf, "\033[7m%-4d %-12s %-20.20s %-32.32s%s\033[0m", i + 1, g_addrbook[i].nickname,
                    g_addrbook[i].fullname, g_addrbook[i].address,
                    g_addrbook[i].is_list ? " (list)" : "");
            } else {
                sprintf(buf, "%-4d %-12s %-20.20s %-32.32s%s", i + 1, g_addrbook[i].nickname,
                    g_addrbook[i].fullname, g_addrbook[i].address,
                    g_addrbook[i].is_list ? " (list)" : "");
            }
            mvprintw(row + i, 3, "%s", buf);
        }
        last_highlight = highlight;
        initialized = 1;
    }
    else if (last_highlight != highlight) {
        /* Clear and redraw old highlight as normal */
        if (last_highlight >= 0 && last_highlight < g_addr_count) {
            move(5 + last_highlight, 3);
            clrtoeol();
            sprintf(buf, "%-4d %-12s %-20.20s %-32.32s%s", last_highlight + 1, g_addrbook[last_highlight].nickname,
                g_addrbook[last_highlight].fullname, g_addrbook[last_highlight].address,
                g_addrbook[last_highlight].is_list ? " (list)" : "");
            mvprintw(5 + last_highlight, 3, "%s", buf);
        }
        
        /* Clear and draw new highlight */
        if (highlight >= 0 && highlight < g_addr_count) {
            move(5 + highlight, 3);
            clrtoeol();
            sprintf(buf, "\033[7m%-4d %-12s %-20.20s %-32.32s%s\033[0m", highlight + 1, g_addrbook[highlight].nickname,
                g_addrbook[highlight].fullname, g_addrbook[highlight].address,
                g_addrbook[highlight].is_list ? " (list)" : "");
            mvprintw(5 + highlight, 3, "%s", buf);
        }
        
        last_highlight = highlight;
    }
}

static void
address_book_screen(void)
{
    int highlight;
    int ch;

    highlight = 0;
    draw_layout("Address Book", "");
    draw_address_rows(highlight);
    draw_menu_lines("A Add  S Create List  V View/Update  D Delete", "C Compose To  T Take (TODO)  O Other", "< Main  ? Help");
    refresh();

    while (1) {
        ch = read_key();
        if (ch == KEY_UP) {
            if (highlight > 0)
                --highlight;
        } else if (ch == KEY_DOWN) {
            if (highlight < g_addr_count - 1)
                ++highlight;
        } else if (ch == 'A' || ch == 'a') {
            add_address_entry(0);
        } else if (ch == 'S' || ch == 's') {
            add_address_entry(1);
        } else if (ch == 'V' || ch == 'v' || ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            edit_address_entry(highlight);
        } else if (ch == 'D' || ch == 'd') {
            delete_address_entry(highlight);
        } else if (ch == 'C' || ch == 'c') {
            compose_to_entry(highlight);
            break;
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Return to main menu? y/n")) {
                g_screen = SCREEN_MAIN;
                break;
            }
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
    g_screen = SCREEN_COMPOSE;
}

static void
setup_screen(void)
{
    int ch;

    while (1) {
        draw_layout("Setup", "");
        mvprintw(5, 4, "P) Printer command: %s", g_config.printer[0] ? g_config.printer : "(default)");
        mvprintw(6, 4, "G) Signature: %s", g_config.signature[0] ? g_config.signature : "(none)");
        mvprintw(7, 4, "N) Change password (admin only)");
        mvprintw(8, 4, "Other options (C Config, K Color, R Roles, F Rules, L LDAP) are TODO");
        draw_menu_lines("P Printer  G Signature  N Password", "C Config (TODO)  K Color (TODO)  R Roles (TODO)  F Rules (TODO)  L LDAP (TODO)", "< Main  ? Help");
        refresh();
        ch = read_key();
        if (ch == 'P' || ch == 'p') {
            edit_printer();
        } else if (ch == 'G' || ch == 'g') {
            edit_signature();
        } else if (ch == 'N' || ch == 'n') {
            change_password();
        } else if (ch == 'Q' || ch == 'q') {
            if (prompt_yesno("Return to main menu? y/n")) {
                g_screen = SCREEN_MAIN;
                return;
            }
        } else if (ch == '?') {
            show_help("Setup Help", "Configure printer, signature, and future options from here.");
        }
    }
}

static int
load_groups(void)
{
    FILE *fp;
    char line[256];
    char *name;
    char *desc;
    char *flag;

    g_group_count = 0;
    lock_guard();
    fp = fopen(GROUPS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return 0;
    }

    while (fgets(line, sizeof line, fp) != NULL && g_group_count < MAX_GROUPS) {
        trim_newline(line);
        name = strtok(line, "|");
        desc = strtok(NULL, "|");
        flag = strtok(NULL, "|");
        if (name == NULL)
            continue;
        safe_copy(g_groups[g_group_count].name, sizeof g_groups[g_group_count].name, name);
        if (desc)
            safe_copy(g_groups[g_group_count].description, sizeof g_groups[g_group_count].description, desc);
        else
            g_groups[g_group_count].description[0] = '\0';
        g_groups[g_group_count].deleted = (flag && *flag == 'D') ? 1 : 0;
        ++g_group_count;
    }
    fclose(fp);
    unlock_guard();
    return 0;
}

static int
save_groups(void)
{
    FILE *fp;
    int i;

    lock_guard();
    fp = fopen(GROUPS_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return -1;
    }
    for (i = 0; i < g_group_count; ++i)
        fprintf(fp, "%s|%s|%s\n", g_groups[i].name, g_groups[i].description,
            g_groups[i].deleted ? "D" : "");
    fclose(fp);
    unlock_guard();
    return 0;
}

static int
build_group_path(const char *group_name, char *path, int maxlen)
{
    char safe[MAX_GROUP_NAME];
    int i;

    for (i = 0; group_name[i] != '\0' && i < MAX_GROUP_NAME - 1; ++i) {
        if (isalnum((unsigned char)group_name[i]))
            safe[i] = tolower((unsigned char)group_name[i]);
        else
            safe[i] = '_';
    }
    safe[i] = '\0';
    if (maxlen <= 0)
        return -1;
    path[0] = '\0';
    safe_append(path, maxlen, DATA_DIR);
    safe_append_char(path, maxlen, '/');
    safe_append(path, maxlen, safe);
    safe_append(path, maxlen, ".msg");
    return 0;
}

static int
load_messages_for_group(int group_index)
{
    struct message *msgs;
    int count;

    if (group_index < 0 || group_index >= g_group_count)
        return -1;
    if (load_messages_direct(g_groups[group_index].name, &msgs, &count) != 0)
        return -1;
    free_cached_messages();
    g_cached_messages = msgs;
    g_cached_message_count = count;
    return 0;
}

static int
save_messages_for_group(int group_index)
{
    if (group_index < 0 || group_index >= g_group_count)
        return -1;
    return save_messages_direct(g_groups[group_index].name, g_cached_messages, g_cached_message_count);
}

static int
load_messages_direct(const char *group_name, struct message **messages, int *count)
{
    FILE *fp;
    char line[512];
    char path[256];
    struct message *list;
    int capacity;
    struct message *msg = &g_temp_message; /* Use static buffer to avoid stack overflow */

    *messages = NULL;
    *count = 0;
    build_group_path(group_name, path, sizeof path);

    lock_guard();
    fp = fopen(path, "r");
    if (fp == NULL) {
        unlock_guard();
        return 0;
    }

    capacity = 32;
    list = (struct message *)malloc(sizeof(struct message) * capacity);
    if (list == NULL) {
        fclose(fp);
        unlock_guard();
        return -1;
    }

    memset(msg, 0, sizeof *msg);
    while (fgets(line, sizeof line, fp) != NULL) {
        trim_newline(line);
        if (strncmp(line, "MSG ", 4) == 0) {
            msg->id = atoi(line + 4);;
        } else if (strncmp(line, "PARENT ", 7) == 0) {
            msg->parent_id = atoi(line + 7);;
        } else if (strncmp(line, "TIME ", 5) == 0) {
            msg->created = (time_t)atol(line + 5);;
        } else if (strncmp(line, "STATUS ", 7) == 0) {
            msg->deleted = (line[7] == 'D');
            msg->answered = (line[7] == 'A');
        } else if (strncmp(line, "AUTHOR ", 7) == 0) {
            safe_copy(msg->author, sizeof msg->author, line + 7);
        } else if (strncmp(line, "SUBJECT ", 8) == 0) {
            safe_copy(msg->subject, sizeof msg->subject, line + 8);
        } else if (strcmp(line, "BODY") == 0) {
            msg->body[0] = '\0';
            while (fgets(line, sizeof line, fp) != NULL) {
                trim_newline(line);
                if (strcmp(line, ".") == 0)
                    break;
                if ((int)strlen(msg->body) + (int)strlen(line) + 2 >= MAX_BODY)
                    break;
                strcat(msg->body, line);
                strcat(msg->body, "\n");
            }
        } else if (strcmp(line, "END") == 0) {
            if (*count >= capacity) {
                capacity *= 2;
                list = (struct message *)realloc(list, sizeof(struct message) * capacity);
                if (list == NULL) {
                    fclose(fp);
                    unlock_guard();
                    return -1;
                }
            }
            list[*count] = *msg;
            ++(*count);
            memset(msg, 0, sizeof *msg);
        }
    }

    fclose(fp);
    unlock_guard();
    *messages = list;
    return 0;
}

static int
save_messages_direct(const char *group_name, struct message *messages, int count)
{
    FILE *fp;
    char path[256];
    int i;

    build_group_path(group_name, path, sizeof path);
    lock_guard();
    fp = fopen(path, "w");
    if (fp == NULL) {
        unlock_guard();
        return -1;
    }
    for (i = 0; i < count; ++i) {
        fprintf(fp, "MSG %d\n", messages[i].id);
        fprintf(fp, "PARENT %d\n", messages[i].parent_id);
        fprintf(fp, "TIME %ld\n", (long)messages[i].created);
        fprintf(fp, "STATUS %c\n", messages[i].deleted ? 'D' : (messages[i].answered ? 'A' : 'N'));
        fprintf(fp, "AUTHOR %s\n", messages[i].author);
        fprintf(fp, "SUBJECT %s\n", messages[i].subject);
        fprintf(fp, "BODY\n%s", messages[i].body);
        if (messages[i].body[0] == '\0' || messages[i].body[strlen(messages[i].body) - 1] != '\n')
            fprintf(fp, "\n");
        fprintf(fp, ".\nEND\n");
    }
    fclose(fp);
    unlock_guard();
    return 0;
}

static int
copy_message_to_group(struct message *msg, const char *group_name)
{
    struct message *msgs;
    int count;
    struct message *copy = &g_temp_message; /* Use static buffer to avoid stack overflow */
    int rc;

    if (load_messages_direct(group_name, &msgs, &count) != 0)
        return -1;
    *copy = *msg;
    copy->id = 0;
    copy->parent_id = 0;
    copy->deleted = 0;
    copy->answered = 0;
    copy->created = time(NULL);
    copy->id = count > 0 ? msgs[count - 1].id + 1 : 1;
    msgs = (struct message *)realloc(msgs, sizeof(struct message) * (count + 1));
    if (msgs == NULL)
        return -1;
    msgs[count] = *copy;
    rc = save_messages_direct(group_name, msgs, count + 1);
    free(msgs);
    return rc;
}

static void
load_address_book(void)
{
    FILE *fp;
    char line[512];
    char *nick;
    char *full;
    char *addr;
    char *flag;

    g_addr_count = 0;
    lock_guard();
    fp = fopen(ADDRESS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return;
    }

    while (fgets(line, sizeof line, fp) != NULL && g_addr_count < MAX_ADDRBOOK) {
        trim_newline(line);
        nick = strtok(line, "|");
        full = strtok(NULL, "|");
        addr = strtok(NULL, "|");
        flag = strtok(NULL, "|");
        if (nick == NULL || addr == NULL)
            continue;
        safe_copy(g_addrbook[g_addr_count].nickname, sizeof g_addrbook[g_addr_count].nickname, nick);
        safe_copy(g_addrbook[g_addr_count].fullname, sizeof g_addrbook[g_addr_count].fullname, full ? full : "");
        safe_copy(g_addrbook[g_addr_count].address, sizeof g_addrbook[g_addr_count].address, addr);
        g_addrbook[g_addr_count].is_list = (flag && *flag == 'L') ? 1 : 0;
        ++g_addr_count;
    }
    fclose(fp);
    unlock_guard();
}

static void
save_address_book(void)
{
    FILE *fp;
    int i;

    lock_guard();
    fp = fopen(ADDRESS_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return;
    }
    for (i = 0; i < g_addr_count; ++i)
        fprintf(fp, "%s|%s|%s|%c\n", g_addrbook[i].nickname,
            g_addrbook[i].fullname, g_addrbook[i].address,
            g_addrbook[i].is_list ? 'L' : 'A');
    fclose(fp);
    unlock_guard();
}

static void
load_config(void)
{
    FILE *fp;
    char line[512];
    char *key;
    char *value;

    lock_guard();
    fp = fopen(CONFIG_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        trim_newline(line);
        key = strtok(line, "=");
        value = strtok(NULL, "");
        if (key == NULL || value == NULL)
            continue;
        if (strcmp(key, "printer") == 0)
            safe_copy(g_config.printer, sizeof g_config.printer, value);
        else if (strcmp(key, "signature") == 0)
            safe_copy(g_config.signature, sizeof g_config.signature, value);
        else if (strcmp(key, "password") == 0)
            safe_copy(g_config.password, sizeof g_config.password, value);
    }
    fclose(fp);
    unlock_guard();
}

static void
save_config(void)
{
    FILE *fp;

    lock_guard();
    fp = fopen(CONFIG_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return;
    }
    fprintf(fp, "printer=%s\n", g_config.printer);
    fprintf(fp, "signature=%s\n", g_config.signature);
    fprintf(fp, "password=%s\n", g_config.password);
    fclose(fp);
    unlock_guard();
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

static int
acquire_lock(void)
{
    char tmp[128];
    FILE *fp;
    int attempt;
    int pid;

    if (g_lock_depth > 0) {
        ++g_lock_depth;
        return 0;
    }

    pid = 1;
#ifdef __unix__
    pid = getpid();
#endif
    tmp[0] = '\0';
    safe_append(tmp, sizeof tmp, DATA_DIR);
    safe_append(tmp, sizeof tmp, "/.lock.");
    safe_append_number(tmp, sizeof tmp, pid);
    fp = fopen(tmp, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", (long)time(NULL));
    fclose(fp);

    for (attempt = 0; attempt < 5; ++attempt) {
        if (link(tmp, LOCK_FILE) == 0) {
            unlink(tmp);
            g_lock_depth = 1;
            return 0;
        }
#ifdef __unix__
        sleep(1);
#endif
    }
    unlink(tmp);
    return -1;
}

static void
release_lock(void)
{
    if (g_lock_depth == 0)
        return;
    --g_lock_depth;
    if (g_lock_depth == 0)
        unlink(LOCK_FILE);
}

static void
lock_guard(void)
{
    if (acquire_lock() != 0) {
        if (g_ui_ready)
            wait_for_ack("Unable to acquire global lock.");
        else
            fprintf(stderr, "Unable to acquire global lock.\n");
    }
}

static void
unlock_guard(void)
{
    release_lock();
}
