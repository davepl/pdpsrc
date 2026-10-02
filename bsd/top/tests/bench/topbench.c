/*
 * Native 2.11BSD top benchmark: run on the same target as every candidate.
 * cc -O -i -o topbench topbench.c
 * As root: ./topbench 12 /usr/ucb/top -s1
 *
 * Run every candidate in the same 80x24 vt100 pseudo-terminal.  The native
 * parent drains terminal output (no network in timed path), uses wait4 for
 * child CPU time, and observes allocated data+stack through /dev/kmem.
 * No measured program is modified.  Peak allocation is an observed maximum
 * after the first displayed frame, not a claim about every instant of startup.
 * See README.md for the quiet-gap detector and measurement limitations.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nlist.h>
#include <errno.h>

/* Do not depend on newer unistd.h/stdint.h on an older native compiler. */
off_t lseek();
int read(), write(), close(), fork(), dup2(), execve();
int getpid(), setpgrp(), kill();
void _exit();

static int child = -1, master = -1, kmem = -1;
static struct nlist nl[] = { { "_proc" }, { "_nproc" }, { "" } };
static struct proc proc;
static off_t procbase, childslot;
static unsigned short nproc;
static unsigned int data_max, stack_max;
static unsigned long combined_max;
static char slave[16];
static struct timeval begun, first, last_output;
static long output_bytes;
static int frames, title_seen, title_pos, escstate, pending_frame;
static char capture[4096];
static int capture_length;

static long
micros(tv)
struct timeval *tv;
{
    return tv->tv_sec * 1000000L + tv->tv_usec;
}

static long
elapsed(a, b)
struct timeval *a, *b;
{
    return (a->tv_sec - b->tv_sec) * 1000000L +
        a->tv_usec - b->tv_usec;
}

static int
readat(pos, buf, n)
off_t pos;
char *buf;
int n;
{
    return lseek(kmem, pos, L_SET) != (off_t)-1 &&
        read(kmem, buf, n) == n;
}

static void
observe()
{
    unsigned int i;
    unsigned long combined;
    /* Locate the child once. The proc slot stays stable until wait4 reaps it.
     * Do not read zombie size fields: its resource data overlays live fields.
     */
    if (!childslot) {
        for (i = 0; i < nproc; i++) {
            if (!readat(procbase + (long)i * sizeof(proc),
                    (char *)&proc, sizeof(proc))) return;
            if (proc.p_pid == child && proc.p_stat && proc.p_stat != SZOMB) {
                childslot = procbase + (long)i * sizeof(proc);
                break;
            }
        }
        if (!childslot) return;
    } else if (!readat(childslot, (char *)&proc, sizeof(proc))) return;
    if (proc.p_pid != child || !proc.p_stat || proc.p_stat == SZOMB) return;
    if ((unsigned int)proc.p_dsize > data_max) data_max = proc.p_dsize;
    if ((unsigned int)proc.p_ssize > stack_max) stack_max = proc.p_ssize;
    combined = (unsigned long)proc.p_dsize + proc.p_ssize;
    if (combined > combined_max) combined_max = combined;
}

static int
openpty()
{
    char path[16];
    char *bank, *unit;
    int fd;
    /* This BSD predates clone PTYs and posix_openpt. Opening a free master
     * reserves the matching slave without changing device ownership/modes.
     */
    strcpy(path, "/dev/ptyp0");
    for (bank = "pqrstuvwxyz"; *bank; bank++) {
        path[8] = *bank;
        for (unit = "0123456789abcdef"; *unit; unit++) {
            path[9] = *unit;
            fd = open(path, O_RDWR);
            if (fd >= 0) {
                strcpy(slave, path);
                slave[5] = 't';
                return fd;
            }
        }
    }
    return -1;
}

static void
interrupted(sig)
int sig;
{
    if (child > 0) kill(child, SIGKILL);
    if (master >= 0) close(master);
    _exit(128 + sig);
}

/* Recognize a real statistics display, ignoring initialization and sampling
 * notices. Different curses builds leave the hardware cursor at different
 * locations, so completion uses output quiescence in the main loop rather
 * than assuming a particular final cursor-address sequence.
 */
static void
consume(buf, n)
char *buf;
int n;
{
    int i, c;
    char *title;
    title = "COMMAND";
    for (i = 0; i < n; i++) {
        c = buf[i] & 0177;
        if (escstate == 1) {
            escstate = c == '[' ? 2 : 0;
            continue;
        }
        if (escstate == 2) {
            if (c >= 0100 && c <= 0176) escstate = 0;
            continue;
        }
        if (c == 033) { escstate = 1; continue; }
        if (c >= ' ' && c < 0177) {
            if (c == title[title_pos]) {
                if (++title_pos == 7) { title_seen = 1; title_pos = 0; }
            } else title_pos = c == 'C' ? 1 : 0;
        }
    }
}

int
main(argc, argv)
int argc;
char **argv;
{
    int seconds, slavefd, tty, n, status, waited, flags, sentquit, save;
    int monitorfd, deadline_hit, observer_failed;
    fd_set rd;
    struct timeval now, tv, forced_begun, last_observed;
    struct winsize ws;
    struct sgttyb sg;
    struct rusage usage;
    char buf[1024];
    char *env[5];
    char *capture_path;
    char *refresh_setting, *end;
    FILE *capture_file;
    long elapsed_us, forced_target, forced_sent, pending_input, forced_us;

    if (argc < 3 || (seconds = atoi(argv[1])) < 3 || seconds > 600) {
        fprintf(stderr, "usage: topbench seconds binary [top arguments]\n");
        return 2;
    }
    capture_path = getenv("TOPBENCH_CAPTURE");
    forced_target = forced_sent = 0;
    refresh_setting = getenv("TOPBENCH_REFRESHES");
    if (refresh_setting) {
        forced_target = strtol(refresh_setting, &end, 10);
        if (*end || end == refresh_setting ||
                forced_target < 1 || forced_target > 100000L) {
            fprintf(stderr, "TOPBENCH_REFRESHES must be 1 through 100000\n");
            return 2;
        }
    }
    nlist("/unix", nl);
    kmem = open("/dev/kmem", O_RDONLY);
    if (kmem < 0 || !nl[0].n_type || !nl[1].n_type) {
        fprintf(stderr, "topbench: kernel access failed\n");
        return 2;
    }
    /* _proc is the proc array itself, not a kernel pointer variable. */
    procbase = (off_t)nl[0].n_value;
    if (!readat((off_t)nl[1].n_value, (char *)&nproc, sizeof(nproc))) return 2;
    master = openpty();
    if (master < 0) { perror("pty"); return 2; }
    memset((char *)&ws, 0, sizeof(ws));
    ws.ws_row = 24;
    ws.ws_col = 80;
    ioctl(master, TIOCSWINSZ, &ws);
    signal(SIGINT, interrupted);
    signal(SIGTERM, interrupted);
    /* Kernel-symbol lookup and PTY setup belong to the observer, not top.
     * Start at fork so the measured interval includes exec and all startup.
     */
    gettimeofday(&begun, (struct timezone *)0);
    child = fork();
    if (child < 0) { perror("fork"); return 2; }
    if (!child) {
        close(kmem);
        close(master);
        tty = open("/dev/tty", O_RDWR);
        if (tty >= 0) { ioctl(tty, TIOCNOTTY, (char *)0); close(tty); }
        setpgrp(0, getpid());
        slavefd = open(slave, O_RDWR);
        if (slavefd < 0) _exit(126);
        ioctl(slavefd, TIOCSWINSZ, &ws);
        if (ioctl(slavefd, TIOCGETP, &sg) >= 0) {
            /* Start like a login tty. The tested application selects its
             * own raw/cbreak mode after curses records newline behavior. */
            sg.sg_flags = ECHO | CRMOD | XTABS | ANYP;
            sg.sg_erase = 0177;
            sg.sg_kill = 025;
            sg.sg_ispeed = sg.sg_ospeed = B9600;
            ioctl(slavefd, TIOCSETP, &sg);
        }
        dup2(slavefd, 0); dup2(slavefd, 1); dup2(slavefd, 2);
        if (slavefd > 2) close(slavefd);
        env[0] = "TERM=vt100";
        env[1] = "PATH=/bin:/usr/bin:/usr/ucb";
        env[2] = "HOME=/";
        env[3] = "USER=root";
        env[4] = (char *)0;
        execve(argv[2], &argv[2], env);
        _exit(127);
    }
    flags = 1;
    ioctl(master, FIONBIO, &flags);
    sentquit = 0;
    monitorfd = -1;
    deadline_hit = observer_failed = 0;
    last_observed = begun;
    for (;;) {
        FD_ZERO(&rd); FD_SET(master, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = forced_target && frames && !sentquit ? 1000L : 50000L;
        n = select(master + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
        if (n > 0) {
            n = read(master, buf, sizeof(buf));
            if (n > 0) {
                if (!sentquit) {
                    gettimeofday(&last_output, (struct timezone *)0);
                    pending_frame = 1;
                }
                output_bytes += n;
                if (capture_path && capture_length < sizeof(capture)) {
                    save = n;
                    if (save > sizeof(capture) - capture_length)
                        save = sizeof(capture) - capture_length;
                    memcpy(capture + capture_length, buf, save);
                    capture_length += save;
                }
                consume(buf, n);
            }
        }
        gettimeofday(&now, (struct timezone *)0);
        elapsed_us = elapsed(&now, &begun);
        /* Minimum supported tested interval is 100ms. A 50ms quiet gap
         * separates refresh bursts. Timestamp the last byte, not the end
         * of the extra confirmation interval. Count only visible renders.
         */
        if (!sentquit && title_seen && pending_frame &&
                elapsed(&now, &last_output) >= 50000L) {
            if (!frames) first = last_output;
            frames++;
            pending_frame = 0;
        }
        if (frames && !sentquit && (!forced_target ||
                elapsed(&now, &last_observed) >= 50000L)) {
            observe();
            last_observed = now;
        }
        if (forced_target && frames && !sentquit) {
            /* The parent retains its own controlling terminal. Opening the
             * child's slave here only lets us inspect its unread input.
             * FIONREAD stores a LONG, not a 16-bit int, on this BSD.
             */
            if (monitorfd < 0) monitorfd = open(slave, O_RDONLY);
            if (monitorfd < 0 ||
                    ioctl(monitorfd, FIONREAD, &pending_input) < 0) {
                observer_failed = 1;
                kill(child, SIGKILL);
                sentquit = 1;
            } else if (!pending_input) {
                /* Never queue more than one key. This avoids tty input
                 * overflow and counts requests even when curses emits no
                 * changed pixels. Each inspected top reads one key, then
                 * samples/renders before it reads the next key. Thus q is
                 * handled only AFTER the final requested update completes.
                 */
                if (forced_sent < forced_target) {
                    if (!forced_sent) forced_begun = now;
                    if (write(master, " ", 1) == 1) forced_sent++;
                    else if (errno != EWOULDBLOCK && errno != EINTR) {
                        observer_failed = 1;
                        kill(child, SIGKILL);
                        sentquit = 1;
                    }
                } else if (write(master, "q", 1) == 1) sentquit = 1;
            }
        }
        if (!sentquit && elapsed_us >= (long)seconds * 1000000L) {
            if (forced_target) deadline_hit = 1;
            write(master, "q", 1);
            sentquit = 1;
        }
        if (elapsed_us > ((long)seconds + 5L) * 1000000L) kill(child, SIGKILL);
        /* wait4 accounts only the tested child. Parent sampling, terminal
         * draining, and this control loop are not charged to its CPU total.
         */
        waited = wait4(child, &status, WNOHANG, &usage);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) { perror("wait4"); return 2; }
    }
    child = -1;
    close(master); close(kmem);
    if (monitorfd >= 0) close(monitorfd);
    forced_us = forced_sent ? elapsed(&now, &forced_begun) : 0L;
    if (capture_path) {
        capture_file = fopen(capture_path, "w");
        if (capture_file) {
            fwrite(capture, 1, capture_length, capture_file);
            fclose(capture_file);
        } else perror(capture_path);
    }
    printf("path=%s status=%d startup_us=%ld wall_us=%ld user_us=%ld system_us=%ld rendered_frames=%d output_bytes=%ld data_bytes=%ld stack_bytes=%ld data_stack_bytes=%ld maxrss_raw=%ld forced_requested=%ld forced_refreshes=%ld forced_us=%ld deadline_hit=%d observer_failed=%d\n",
        argv[2], status, frames ? elapsed(&first, &begun) : -1L,
        elapsed_us, micros(&usage.ru_utime), micros(&usage.ru_stime),
        frames, output_bytes, (long)data_max * 64L,
        (long)stack_max * 64L, combined_max * 64L, usage.ru_maxrss,
        forced_target, forced_sent, forced_us, deadline_hit, observer_failed);
    return status || !frames || deadline_hit || observer_failed ||
        (forced_target && forced_sent != forced_target);
}
