/*
 * top -- a curses process monitor for 2.11BSD on the PDP-11.
 *
 * Written for the native 2.11BSD C compiler and libcurses (not ncurses).
 * The kernel interfaces were checked against this system's ps(1), vmstat(1)
 * and the 2.11BSD top by Digby Tarvin and its subsequent contributors.
 * The presentation follows the supplied mentec terminal reference.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/dk.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdp11_unistd.h"
#include <pwd.h>
#include <utmp.h>
#include <nlist.h>
#include <curses.h>

#define FIRSTPROC 7
#define MAXCOLS 160
#define MAXROWS 50
#define MAXCELLS 5000
#define NCACHE 32

struct row {
    short pid, uid, pri, nice;
    unsigned short text, data, stack;
    char state, swapped, valid;
    long ticks, delta, start;
    char command[MAXCOMLEN + 1];
};

struct history {
    short pid;
    char valid;
    long ticks, start;
};

struct username {
    short uid;
    char valid, name[9];
};

struct nlist symbols[] = {
    { "_proc" }, { "_nproc" }, { "_hz" }, { "_cp_time" }, { "" }
};

static struct proc *procs;
static struct row *rows;
static struct history *history;
static struct username names[NCACHE];
static struct user userbuf;
static struct mapent *coremap, *swapmap;
static size_t corebytes, swapbytes;
static int kmem = -1, mem = -1, swapfd = -1;
static unsigned int nproc;
static unsigned int row_capacity;
static int hz, count, running, sleeping, stopped, zombies;
static int order = 9, showidle = 1, limit = 0;
static int screen_on, want_quit, want_resize, help_on;
static int name_next;
static long delay_us = 1000000L;
static long cpu_old[CPUSTATES];
static double cpu[CPUSTATES];
static unsigned long totalmem, free_memory, totalswap, freeswap;
static struct timeval boottime;
static char hostname[64];
static char error_text[160];
static char status_text[160];
static struct sgttyb savedtty;
static int tty_saved;

static int readat(), sample(), compare(), keywait(), screen_start();
static void draw(), screen_stop(), putrow(), usage();

static int
readat(fd, pos, buf, length)
int fd;
off_t pos;
char *buf;
unsigned int length;
{
    if (fd < 0 || lseek(fd, pos, L_SET) == (off_t)-1)
        return 0;
    return read(fd, buf, length) == length;
}

static int
getvalue(group, item, buf, size)
int group, item;
char *buf;
size_t size;
{
    int mib[2];
    size_t got;
    mib[0] = group;
    mib[1] = item;
    got = size;
    /* 2.11BSD returns the byte count on success, unlike newer BSDs. */
    return sysctl(mib, 2, buf, &got, NULL, 0) >= 0 && got == size;
}

static int
map_init(item, dest, bytes)
int item;
struct mapent **dest;
size_t *bytes;
{
    int mib[2];
    mib[0] = CTL_VM;
    mib[1] = item;
    if (sysctl(mib, 2, NULL, bytes, NULL, 0) < 0 || *bytes == 0)
        return 0;
    *dest = (struct mapent *)malloc(*bytes);
    return *dest != NULL;
}

static int
map_free(item, map, bytes, unit, result)
int item;
struct mapent *map;
size_t bytes;
unsigned int unit;
unsigned long *result;
{
    unsigned int i;
    unsigned long sum;
    if (!getvalue(CTL_VM, item, (char *)map, bytes))
        return 0;
    sum = 0;
    for (i = 0; i < bytes / sizeof(struct mapent); i++) {
        if (map[i].m_size == 0)
            break;
        sum += (unsigned long)map[i].m_size;
    }
    *result = sum * unit;
    return 1;
}

static int
setup()
{
    unsigned int i, swapblocks;
    unsigned long need;

    nlist("/unix", symbols);
    for (i = 0; i < 4; i++) {
        if (symbols[i].n_type == 0) {
            sprintf(error_text, "missing kernel symbol %s in /unix",
                symbols[i].n_name);
            return 0;
        }
    }
    if ((kmem = open("/dev/kmem", O_RDONLY)) < 0 ||
        (mem = open("/dev/mem", O_RDONLY)) < 0) {
        strcpy(error_text, "cannot open /dev/kmem or /dev/mem (run as root)");
        return 0;
    }
    swapfd = open("/dev/swap", O_RDONLY);
    if (!readat(kmem, (off_t)symbols[1].n_value,
            (char *)&nproc, sizeof(nproc)) ||
        !readat(kmem, (off_t)symbols[2].n_value, (char *)&hz, sizeof(hz)) ||
        nproc == 0 || hz <= 0) {
        strcpy(error_text, "cannot read kernel process count or clock rate");
        return 0;
    }
    /* size_t and int are 16 bits: check before multiplying for malloc. */
    row_capacity = nproc < 32 ? nproc : 32;
    need = (unsigned long)nproc *
        (sizeof(struct proc) + sizeof(struct history)) +
        (unsigned long)row_capacity * sizeof(struct row);
    if (need > 44000L) {
        strcpy(error_text, "process table is too large for this PDP-11 program");
        return 0;
    }
    procs = (struct proc *)malloc(nproc * sizeof(struct proc));
    rows = (struct row *)malloc(row_capacity * sizeof(struct row));
    history = (struct history *)calloc(nproc, sizeof(struct history));
    if (!procs || !rows || !history) {
        strcpy(error_text, "not enough data space for process tables");
        return 0;
    }
    if (!getvalue(CTL_HW, HW_PHYSMEM, (char *)&totalmem, sizeof(totalmem)) ||
        !getvalue(CTL_VM, VM_NSWAP, (char *)&swapblocks, sizeof(swapblocks)) ||
        !map_init(VM_COREMAP, &coremap, &corebytes) ||
        !map_init(VM_SWAPMAP, &swapmap, &swapbytes)) {
        strcpy(error_text, "cannot obtain memory and swap information via sysctl");
        return 0;
    }
    totalswap = (unsigned long)swapblocks * 512L;
    if (!getvalue(CTL_KERN, KERN_BOOTTIME,
            (char *)&boottime, sizeof(boottime))) {
        strcpy(error_text, "cannot obtain kernel boot time");
        return 0;
    }
    gethostname(hostname, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = 0;
    for (i = 0; hostname[i]; i++) {
        if (hostname[i] == '.') {
            hostname[i] = 0;
            break;
        }
        if (hostname[i] < ' ' || hostname[i] > '~')
            hostname[i] = '?';
    }
    return 1;
}

static char *
username(uid)
short uid;
{
    int i;
    struct passwd *pw;
    struct username *entry;
    for (i = 0; i < NCACHE; i++)
        if (names[i].valid && names[i].uid == uid)
            return names[i].name;
    entry = &names[name_next];
    name_next = (name_next + 1) % NCACHE;
    entry->uid = uid;
    entry->valid = 1;
    pw = getpwuid(uid);
    if (pw) {
        strncpy(entry->name, pw->pw_name, 8);
        entry->name[8] = 0;
        if (strlen(pw->pw_name) > 8)
            entry->name[7] = '+';
    } else
        sprintf(entry->name, "%u", (unsigned int)uid);
    return entry->name;
}

static int
sample(baseline)
int baseline;
{
    unsigned int i, j, newcap;
    int fd;
    off_t address, paddress;
    long ticks[CPUSTATES], diff[CPUSTATES], sum;
    struct proc check;
    struct proc *p;
    struct row *r;
    struct row *newrows;
    struct history *h;

    if (!readat(kmem, (off_t)symbols[0].n_value,
            (char *)procs, nproc * sizeof(struct proc)) ||
        !readat(kmem, (off_t)symbols[3].n_value,
            (char *)ticks, sizeof(ticks))) {
        strcpy(error_text, "kernel statistics read failed");
        return 0;
    }
    sum = 0;
    for (i = 0; i < CPUSTATES; i++) {
        diff[i] = ticks[i] - cpu_old[i];
        if (diff[i] < 0) diff[i] = 0;
        cpu_old[i] = ticks[i];
        sum += diff[i];
    }
    if (!baseline && sum > 0)
        for (i = 0; i < CPUSTATES; i++)
            cpu[i] = ((double)diff[i] / (double)sum) * 100.0;

    count = running = sleeping = stopped = zombies = 0;
    for (i = 0; i < nproc; i++) {
        p = &procs[i];
        h = &history[i];
        if (!p->p_stat) {
            h->valid = 0;
            continue;
        }
        if (count == row_capacity) {
            newcap = row_capacity + 16;
            if (newcap > nproc) newcap = nproc;
            if ((unsigned long)newcap * sizeof(struct row) > 32760L) {
                strcpy(error_text, "too many active processes for the data space");
                return 0;
            }
            newrows = (struct row *)realloc((char *)rows,
                newcap * sizeof(struct row));
            if (!newrows) {
                strcpy(error_text, "not enough memory for active processes");
                return 0;
            }
            rows = newrows;
            row_capacity = newcap;
        }
        r = &rows[count++];
        memset((char *)r, 0, sizeof(*r));
        r->pid = p->p_pid;
        r->uid = p->p_uid;
        switch (p->p_stat) {
        case SSLEEP: case SWAIT: r->state = 'S'; sleeping++; break;
        case SRUN: case SIDL: r->state = 'R'; running++; break;
        case SSTOP: r->state = 'T'; stopped++; break;
        case SZOMB: r->state = 'Z'; zombies++; break;
        default: r->state = '?'; sleeping++; break;
        }
        /* The live part of struct proc is overlaid by exit info in zombies. */
        if (p->p_stat == SZOMB) {
            r->ticks = p->p_ru.ru_utime + p->p_ru.ru_stime;
            r->valid = 1;
            strcpy(r->command, "<defunct>");
            h->valid = 0;
            continue;
        }
        r->pri = p->p_pri;
        r->nice = p->p_nice;
        r->data = p->p_dsize;
        r->stack = p->p_ssize;
        r->swapped = !(p->p_flag & SLOAD);
        strcpy(r->command, p->p_pid == 0 ? "[swapper]" : "[unavailable]");
        if (p->p_stat == SIDL) {
            h->valid = 0;
            continue;
        }
        fd = r->swapped ? swapfd : mem;
        address = (off_t)p->p_addr << (r->swapped ? 9 : 6);
        paddress = (off_t)symbols[0].n_value +
            (off_t)i * sizeof(struct proc);
        if (!readat(fd, address, (char *)&userbuf, sizeof(userbuf)) ||
            (unsigned int)userbuf.u_procp != (unsigned int)paddress ||
            !readat(kmem, paddress, (char *)&check, sizeof(check)) ||
            check.p_pid != p->p_pid || check.p_stat == 0 ||
            check.p_stat == SZOMB || check.p_addr != p->p_addr ||
            ((check.p_flag ^ p->p_flag) & SLOAD)) {
            h->valid = 0;
            continue;
        }
        r->valid = 1;
        r->text = userbuf.u_tsize;
        r->ticks = userbuf.u_ru.ru_utime + userbuf.u_ru.ru_stime;
        r->start = userbuf.u_start;
        if (h->valid && h->pid == r->pid && h->start == r->start &&
            r->ticks >= h->ticks && !baseline)
            r->delta = r->ticks - h->ticks;
        if (r->pid != 0) {
            for (j = 0; j < MAXCOMLEN && userbuf.u_comm[j]; j++) {
                r->command[j] = userbuf.u_comm[j];
                if (r->command[j] < ' ' || r->command[j] > '~')
                    r->command[j] = '?';
            }
            r->command[j] = 0;
            if (j == 0)
                strcpy(r->command, "[unknown]");
        }
        h->valid = 1;
        h->pid = r->pid;
        h->start = r->start;
        h->ticks = r->ticks;
    }
    if (!map_free(VM_COREMAP, coremap, corebytes, 64, &free_memory) ||
        !map_free(VM_SWAPMAP, swapmap, swapbytes, 512, &freeswap)) {
        strcpy(error_text, "cannot refresh memory allocation maps");
        return 0;
    }
    if (free_memory > totalmem) free_memory = totalmem;
    if (freeswap > totalswap) freeswap = totalswap;
    return 1;
}

static int
cmp_long(a, b)
long a, b;
{
    return a < b ? -1 : (a > b ? 1 : 0);
}

static int
compare(a, b)
struct row *a, *b;
{
    int n;
    n = 0;
    switch (order) {
    case 0: n = cmp_long((long)a->pid, (long)b->pid); break;
    case 1: n = cmp_long((long)a->uid, (long)b->uid); break;
    case 2: n = b->pri - a->pri; break;
    case 3: n = a->nice - b->nice; break;
    case 4: n = cmp_long((long)b->text, (long)a->text); break;
    case 5: n = cmp_long((long)b->data, (long)a->data); break;
    case 6: n = cmp_long((long)b->stack, (long)a->stack); break;
    case 7: n = a->state - b->state; break;
    case 9:
        n = cmp_long(b->delta, a->delta);
        if (n) break;
        /* Fall through: cumulative time breaks equal interval times. */
    case 8: n = cmp_long(b->ticks, a->ticks); break;
    case 10: n = strcmp(a->command, b->command); break;
    }
    if (!n) n = cmp_long((long)a->pid, (long)b->pid);
    return n;
}

static int
user_count()
{
    FILE *f;
    struct utmp u;
    int n;
    n = 0;
    f = fopen(_PATH_UTMP, "r");
    if (f) {
        while (fread((char *)&u, sizeof(u), 1, f) == 1)
            if (u.ut_name[0]) n++;
        fclose(f);
    }
    return n;
}

/* Never wrap at the right margin, including terminals without auto-margin. */
static void
putrow(y, text, reverse)
int y, reverse;
char *text;
{
    int x;
    if (y < 0 || y >= LINES) return;
    move(y, 0);
    if (reverse) standout();
    for (x = 0; text[x] && x < COLS - 1; x++)
        addch(text[x] >= ' ' && text[x] <= '~' ? text[x] : '?');
    if (reverse) standend();
}

static void
draw()
{
    char line[512], times[40], first[160], load[80];
    time_t now;
    struct tm *tm;
    long up;
    int i, y, users, shown, days, hours, minutes;
    double loads[3];
    struct row *r;

    time(&now);
    tm = localtime(&now);
    up = now - boottime.tv_sec;
    if (up < 0) up = 0;
    days = up / 86400L;
    hours = (up / 3600L) % 24;
    minutes = (up / 60L) % 60;
    users = user_count();
    sprintf(first, "%.16s - %02d:%02d:%02d up %2d days, %2d:%02d, %3d user%s",
        hostname, tm->tm_hour, tm->tm_min, tm->tm_sec,
        days, hours, minutes, users, users == 1 ? "" : "s");
    loads[0] = loads[1] = loads[2] = 0.0;
    if (getloadavg(loads, 3) == 3)
        sprintf(load, "load average: %.2f, %.2f, %.2f",
            loads[0], loads[1], loads[2]);
    else
        strcpy(load, "load average: unavailable");
    sprintf(line, "%s,  %s", first, load);
    erase();
    if (LINES < 9 || COLS < 40) {
        putrow(0, "top: enlarge terminal to at least 40x9", 0);
        putrow(1, "q quits; resize to continue", 0);
        move(0, 0);
        refresh();
        return;
    }
    if (strlen(line) >= COLS) {
        sprintf(line, "%.8s %02d:%02d:%02d up %dd %d:%02d, %d user%s, load %.2f %.2f %.2f",
            hostname, tm->tm_hour, tm->tm_min, tm->tm_sec,
            days, hours, minutes, users, users == 1 ? "" : "s",
            loads[0], loads[1], loads[2]);
    }
    putrow(0, line, 0);
    sprintf(line, "Tasks:%4d total, %3d running, %4d sleeping, %3d stopped, %3d zombie",
        count, running, sleeping, stopped, zombies);
    putrow(1, line, 0);
    sprintf(line, "Cpu  : %5.1f%% us, %5.1f%% ni, %5.1f%% sy, %5.1f%% id",
        cpu[0], cpu[1], cpu[2], cpu[3]);
    putrow(2, line, 0);
    sprintf(line, "Mem  : %7.1fK total, %7.1fK free, %7.1fK used (%2ld%%)",
        totalmem / 1024.0, free_memory / 1024.0,
        (totalmem - free_memory) / 1024.0,
        totalmem ? (totalmem - free_memory) * 100L / totalmem : 0L);
    putrow(3, line, 0);
    sprintf(line, "Swap : %7.1fK total, %7.1fK free, %7.1fK used (%2ld%%)",
        totalswap / 1024.0, freeswap / 1024.0,
        (totalswap - freeswap) / 1024.0,
        totalswap ? (totalswap - freeswap) * 100L / totalswap : 0L);
    putrow(4, line, 0);
    putrow(5, status_text, 0);
    putrow(6, "  PID USER      PR  NI  TEXT  DATA STACK S     TIME   DTIME COMMAND ", 1);
    qsort((char *)rows, count, sizeof(struct row), compare);
    shown = 0;
    for (i = 0, y = FIRSTPROC; i < count && y < LINES; i++) {
        r = &rows[i];
        if (!showidle && r->delta == 0 && r->state != 'R') continue;
        if (limit && shown >= limit) break;
        if (r->valid)
            sprintf(times, "%8.2f %7.2f", r->ticks / (double)hz,
                r->delta / (double)hz);
        else
            strcpy(times, "       ?       ?");
        sprintf(line, "%5d %-8.8s%4d%4d%5.1fK%5.1fK%5.1fK %c%c%s %s",
            r->pid, username(r->uid), r->pri, r->nice,
            (double)r->text / 16.0, (double)r->data / 16.0,
            (double)r->stack / 16.0, r->state,
            r->swapped ? 's' : ' ', times, r->command);
        putrow(y++, line, 0);
        shown++;
    }
    if (help_on) {
        for (i = 5; i < LINES; i++) { move(i, 0); clrtoeol(); }
        putrow(5, "Keys: q quit  SPACE refresh  ^L redraw  I hide/show idle", 1);
        putrow(7, "s change refresh delay (seconds)   o choose sort column", 0);
        putrow(8, "P interval CPU time   T total CPU time   N PID   M data size", 0);
        putrow(10, "Order: 0 PID  1 UID  2 PR  3 NI  4 TEXT  5 DATA  6 STACK", 0);
        putrow(11, "       7 state  8 TIME  9 DTIME  10 COMMAND", 0);
        putrow(13, "TIME is user + system CPU seconds; DTIME is since last sample.", 0);
        putrow(14, "An s after state means swapped out. ? means unavailable data.", 0);
        putrow(16, "h or ? closes help. Terminal size changes are automatic.", 0);
    }
    move(0, 0);
    refresh();
}

static void
on_signal(sig)
int sig;
{
    if (sig == SIGWINCH) want_resize = 1;
    else want_quit = 1;
}

static int
screen_start()
{
    struct winsize ws;
    int width, height;
    width = 80;
    height = 24;
    if (ioctl(0, TIOCGWINSZ, &ws) >= 0) {
        if (ws.ws_col) width = ws.ws_col;
        if (ws.ws_row) height = ws.ws_row;
    }
    if (width > MAXCOLS) width = MAXCOLS;
    if (height > MAXROWS) height = MAXROWS;
    if (width < 5) width = 5;
    if (height < 6) height = 6;
    if ((long)width * height > MAXCELLS) height = MAXCELLS / width;
    LINES = height;
    COLS = width;
    /* Release both old windows before allocating either replacement. */
    if (stdscr) { delwin(stdscr); stdscr = NULL; }
    if (curscr) { delwin(curscr); curscr = NULL; }
    if (initscr() == NULL) {
        strcpy(error_text, "curses cannot allocate the screen; use a smaller window");
        endwin();
        if (tty_saved) stty(0, &savedtty);
        return 0;
    }
    screen_on = 1;
    if (!CA) {
        strcpy(error_text, "terminal lacks cursor addressing; set TERM=vt100");
        return 0;
    }
    cbreak();
    noecho();
    nonl();
    scrollok(stdscr, FALSE);
    clearok(curscr, TRUE);
    signal(SIGWINCH, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    want_resize = 0;
    return 1;
}

static void
screen_stop()
{
    if (screen_on) {
        move(LINES - 1, 0);
        clrtoeol();
        refresh();
        endwin();
        screen_on = 0;
    }
    if (tty_saved) stty(0, &savedtty);
}

static int
keywait(usec)
long usec;
{
    struct timeval tv;
    fd_set fds;
    char c;
    int n;
    if (want_quit) return 'q';
    if (want_resize) return 0;
    tv.tv_sec = usec / 1000000L;
    tv.tv_usec = usec % 1000000L;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    n = select(1, &fds, NULL, NULL, &tv);
    if (n > 0) {
        if (read(0, &c, 1) != 1) {
            want_quit = 1;
            return 'q';
        }
        return c & 0177;
    }
    if (n < 0 && errno != EINTR) want_quit = 1;
    return want_quit ? 'q' : 0;
}

static int
prompt(label, answer, size)
char *label, *answer;
int size;
{
    char line[160];
    int n, c;
    n = 0;
    answer[0] = 0;
    while (!want_quit && !want_resize) {
        sprintf(line, "%s%s", label, answer);
        move(5, 0);
        clrtoeol();
        putrow(5, line, 0);
        refresh();
        c = keywait(1000000L);
        if (c == '\r' || c == '\n') return n > 0;
        if (c == 27) return 0;
        if (c == 127 || c == '\b') { if (n) answer[--n] = 0; }
        else if (c >= ' ' && c <= '~' && n < size - 1) {
            answer[n++] = c;
            answer[n] = 0;
        }
    }
    return 0;
}

static long
parse_delay(s)
char *s;
{
    long whole, fraction, scale, value;
    int digits;
    whole = fraction = 0;
    digits = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + *s++ - '0';
        if (whole > 1800) return -1L;
        digits++;
    }
    if (*s == '.') {
        s++;
        scale = 100000L;
        while (*s >= '0' && *s <= '9') {
            if (scale == 0) return -1L;
            fraction += (*s++ - '0') * scale;
            scale /= 10;
            digits++;
        }
    }
    value = whole * 1000000L + fraction;
    if (!digits || *s || value < 100000L || value > 1800000000L)
        return -1L;
    return value;
}

static int
parse_int(s, low, high)
char *s;
int low, high;
{
    char *end;
    long n;
    n = strtol(s, &end, 10);
    if (s == end || *end || n < low || n > high) return -1;
    return (int)n;
}

static void
usage()
{
    fprintf(stderr, "usage: top [-s seconds] [-o column] [-n rows] [-I] [-h]\n");
    fprintf(stderr, "  -s  refresh interval, 0.1 to 1800 seconds (default 1)\n");
    fprintf(stderr, "  -o  sort column 0..10; default 9 (DTIME then TIME)\n");
    fprintf(stderr, "  -n  maximum processes displayed (0 fills the screen)\n");
    fprintf(stderr, "  -I  initially hide idle processes; h shows interactive help\n");
}

int
main(argc, argv)
int argc;
char **argv;
{
    int c, result, number;
    long newdelay;
    char answer[32];

    while ((c = getopt(argc, argv, "hs:o:n:I")) != EOF) {
        switch (c) {
        case 's':
            delay_us = parse_delay(optarg);
            if (delay_us < 0) { usage(); return 1; }
            break;
        case 'o':
            order = parse_int(optarg, 0, 10);
            if (order < 0) { usage(); return 1; }
            break;
        case 'n':
            limit = parse_int(optarg, 0, 1000);
            if (limit < 0) { usage(); return 1; }
            break;
        case 'I': showidle = 0; break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }
    if (optind != argc) { usage(); return 1; }
    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "top: run from an interactive terminal\n");
        return 1;
    }
    if (!setup()) {
        fprintf(stderr, "top: %s\n", error_text);
        return 1;
    }
    tty_saved = gtty(0, &savedtty) == 0;
    result = 0;
    if (!sample(1) || !screen_start()) result = 1;
    else {
        /* Measure a real first interval, rather than CPU use since boot. */
        putrow(0, "top: sampling CPU activity...", 0);
        refresh();
        c = keywait(delay_us);
        while (!want_quit && c != 'q' && c != 'Q') {
            if (want_resize) {
                screen_stop();
                if (!screen_start()) { result = 1; break; }
            }
            if (!sample(0)) { result = 1; break; }
            draw();
            c = keywait(delay_us);
            status_text[0] = 0;
            switch (c) {
            case 'h': case '?': help_on = !help_on; break;
            case 'I': case 'i': showidle = !showidle; break;
            case 'P': order = 9; break;
            case 'T': order = 8; break;
            case 'N': order = 0; break;
            case 'M': order = 5; break;
            case 12: clearok(curscr, TRUE); break;
            case 's':
                if (prompt("Refresh seconds: ", answer, sizeof(answer))) {
                    newdelay = parse_delay(answer);
                    if (newdelay > 0) delay_us = newdelay;
                    else strcpy(status_text, "Delay must be 0.1 to 1800 seconds");
                }
                break;
            case 'o':
                if (prompt("Sort column (0..10): ", answer, sizeof(answer))) {
                    number = parse_int(answer, 0, 10);
                    if (number >= 0) order = number;
                    else strcpy(status_text, "Sort column must be 0 through 10");
                }
                break;
            }
        }
    }
    screen_stop();
    if (result) fprintf(stderr, "top: %s\n", error_text);
    close(kmem);
    close(mem);
    if (swapfd >= 0) close(swapfd);
    return result;
}
