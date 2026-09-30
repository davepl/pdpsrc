/* pdpfetch - a small, native 2.11BSD system summary. */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
/* sys/sysctl.h already includes sys/time.h and the unguarded time.h. */

#define VERSION "1.0"
#define ROWS 14
#define WIDTH 192
static char values[ROWS][WIDTH];
static char *labels[ROWS] = {
    "", "OS", "Kernel", "CPU", "Arch", "Uptime", "Memory",
    "Load", "Login shell", "Terminal", "Disk /", "Disk /usr",
    "Mounts", ""
};
static char *logo[ROWS] = {
    "  .------------------------.",
    "  | d i g i t a l   PDP-11 |",
    "  |------------------------|",
    "  |  o o . o  . o o .      |",
    "  |  ADDRESS / DATA        |",
    "  |------------------------|",
    "  |  [_] [_] [_] [_] [_]   |",
    "  |  LOAD  EXAM  CONT  RUN |",
    "  '------------------------'",
    "",
    "       2 . 1 1 B S D",
    "      small is beautiful",
    "",
    ""
};

/* Copy bounded, printable ASCII only: environment/host names are data. */
static void
copy(dst, src, len)
char *dst, *src;
int len;
{
    int c;
    if (src == NULL) src = "unknown";
    while (--len > 0 && *src) {
        c = (unsigned char)*src++;
        *dst++ = c >= 32 && c < 127 ? c : '?';
    }
    *dst = 0;
}

static int
query(group, item, ptr, len)
int group, item, len;
char *ptr;
{
    int mib[2];
    size_t size;
    mib[0] = group; mib[1] = item; size = len;
    /* This BSD returns a byte count on success, unlike modern BSDs. */
    return sysctl(mib, 2, ptr, &size, NULL, 0) < 0 ? -1 : 0;
}

static void
textquery(group, item, buf, len)
int group, item, len;
char *buf;
{
    buf[0] = 0;
    if (query(group, item, buf, len) < 0) copy(buf, "unknown", len);
    buf[len - 1] = 0;
}

static void
diskinfo(path, out)
char *path, *out;
{
    struct statfs fs;
    long total, used;
    if (statfs(path, &fs) < 0 || fs.f_bsize <= 0) return;
    /* Convert to KiB before any multiplication; disks exceed 16-bit ints. */
    if (fs.f_bsize >= 1024) {
        total = fs.f_blocks * (fs.f_bsize / 1024L);
        used = (fs.f_blocks - fs.f_bfree) * (fs.f_bsize / 1024L);
    } else {
        total = fs.f_blocks / (1024L / fs.f_bsize);
        used = (fs.f_blocks - fs.f_bfree) / (1024L / fs.f_bsize);
    }
    sprintf(out, "%ld / %ld KiB used", used, total);
}

static void
collect()
{
    char host[64], user[32], release[32], model[32], kernel[256];
    char line[128], shell[80], term[80], *p;
    struct passwd *pw;
    struct timeval boot;
    struct loadavg load;
    FILE *fp;
    long phys, freebytes, elapsed, now, a[3];
    int i, patch, mounts;

    for (i = 0; i < ROWS; i++) copy(values[i], "unavailable", WIDTH);
    textquery(CTL_KERN, KERN_HOSTNAME, host, sizeof(host));
    pw = getpwuid(getuid());
    copy(user, pw ? pw->pw_name : NULL, sizeof(user));
    copy(shell, pw && pw->pw_shell && *pw->pw_shell ?
        pw->pw_shell : "/bin/sh", sizeof(shell));
    sprintf(values[0], "%s@%s", user, host);
    textquery(CTL_KERN, KERN_OSRELEASE, release, sizeof(release));
    sprintf(values[1], "%sBSD", release);
    fp = fopen("/VERSION", "r");
    if (fp != NULL) {
        if (fgets(line, sizeof(line), fp) != NULL &&
            sscanf(line, "Current Patch Level: %d", &patch) == 1)
            sprintf(values[1], "%sBSD (patch %d)", release, patch);
        fclose(fp);
    }
    textquery(CTL_KERN, KERN_VERSION, kernel, sizeof(kernel));
    p = strchr(kernel, '\n');
    if (p) *p = 0;
    /* Kernel build number is more readable than the full build date. */
    p = strchr(kernel, ':');
    if (p) *p = 0;
    copy(values[2], kernel, WIDTH);
    textquery(CTL_HW, HW_MODEL, model, sizeof(model));
    sprintf(values[3], "PDP-11/%s", model);
    copy(values[4], "16-bit, split I/D", WIDTH);
    if (query(CTL_KERN, KERN_BOOTTIME, (char *)&boot, sizeof(boot)) == 0) {
        now = time((time_t *)0);
        elapsed = now - boot.tv_sec;
        if (elapsed >= 0)
            sprintf(values[5], "%ldd %ldh %ldm", elapsed / 86400L,
                (elapsed / 3600L) % 24L, (elapsed / 60L) % 60L);
    }
    if (query(CTL_HW, HW_PHYSMEM, (char *)&phys, sizeof(phys)) == 0 &&
        query(CTL_HW, HW_USERMEM, (char *)&freebytes, sizeof(freebytes)) == 0) {
        /* 2.11BSD HW_USERMEM returns freemem, not installed user RAM. */
        if (phys > 0 && freebytes >= 0 && freebytes <= phys)
            sprintf(values[6], "%ld / %ld KiB used", (phys-freebytes)/1024L,
                phys/1024L);
    }
    if (query(CTL_VM, VM_LOADAVG, (char *)&load, sizeof(load)) == 0 &&
        load.fscale > 0) {
        for (i = 0; i < 3; i++)
            a[i] = ((long)load.ldavg[i] * 100L + load.fscale / 2) / load.fscale;
        sprintf(values[7], "%ld.%02ld %ld.%02ld %ld.%02ld",
            a[0]/100, a[0]%100, a[1]/100, a[1]%100, a[2]/100, a[2]%100);
    }
    copy(values[8], shell, WIDTH);
    copy(term, getenv("TERM"), sizeof(term));
    copy(values[9], term, WIDTH);
    diskinfo("/", values[10]);
    diskinfo("/usr", values[11]);
    mounts = getfsstat((struct statfs *)0, 0L, MNT_NOWAIT);
    if (mounts >= 0) sprintf(values[12], "%d filesystems", mounts);
    sprintf(values[13], "pdpfetch %s", VERSION);
}

int
main(argc, argv)
int argc;
char **argv;
{
    int i, color, plain, limit, available;
    char *term, clean[WIDTH];
    struct winsize ws;
    term = getenv("TERM");
    color = isatty(1) && term && strcmp(term, "dumb") &&
        (strncmp(term, "xterm", 5) == 0 ||
         strncmp(term, "screen", 6) == 0 ||
         strncmp(term, "tmux", 4) == 0 || strcmp(term, "ansi") == 0);
    if (getenv("NO_COLOR") != NULL) color = 0;
    plain = 0; available = 80;
    if (isatty(1) && ioctl(1, TIOCGWINSZ, (char *)&ws) == 0 && ws.ws_col > 0)
        available = ws.ws_col;
    if (available < 72) plain = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--no-color")) color = 0;
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--color")) color = 1;
        else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plain")) {
            plain = 1; color = 0;
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("pdpfetch %s\n", VERSION); return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            puts("Usage: pdpfetch [-n | -c] [-p] [-v] [-h]");
            puts("  -n  No color       -c  Force ANSI color");
            puts("  -p  Plain, no logo -v  Version  -h  Help");
            return 0;
        } else {
            fputs("pdpfetch: unknown option; use -h for help\n", stderr);
            return 1;
        }
    }
    if (plain) color = 0;
    collect();
    if (!plain) putchar('\n');
    for (i = 0; i < ROWS; i++) {
        if (!plain) printf("%s%-29s%s  ", color ? "\033[36m" : "",
            logo[i], color ? "\033[0m" : "");
        limit = available - (plain ? 0 : 31) - (*labels[i] ? 13 : 0) - 1;
        if (limit < 1) limit = 1;
        if (limit >= WIDTH) limit = WIDTH - 1;
        copy(clean, values[i], limit + 1);
        if (*labels[i]) printf("%s%-11s%s: %s\n", color ? "\033[1;33m" : "",
            labels[i], color ? "\033[0m" : "", clean);
        else printf("%s%s%s\n", color ? "\033[1m" : "", clean,
            color ? "\033[0m" : "");
    }
    if (!plain) putchar('\n');
    return ferror(stdout) ? 1 : 0;
}
