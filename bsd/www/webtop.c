/* webtop: one bounded, integer-only 2.11BSD snapshot, September 2026.
 * Kernel sampling follows the native Digby Tarvin / Johnny Billquist top.
 * No command arguments, shell, request parsing, or process argument display.
 * Built on the target with: cc -O -i -o webtop webtop.c
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/dk.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <sys/dir.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdp11_unistd.h"
#include <signal.h>
#include <pwd.h>
#include <utmp.h>
#include <nlist.h>
#include "webtop-fields.h"

#define CACHE "/tmp/webtop.cache"
#define NEWCACHE "/tmp/webtop.cache.new"
#define LOCK "/tmp/webtop.lock"
#define PERIOD 5
#define ROWS 22
#define MAXCACHE 8192L

static struct nlist nl[] = {
    { "_proc" }, { "_nproc" }, { "_hz" }, { "_cp_time" }, { "" }
};
struct pdata {
    long total, start;
    long inblock, oublock, reads, writes;
    struct timeval sampled;
    short pid, valid, cpu, has_tty;
    short fds, sep, overlay;
    dev_t tty;
    unsigned text;
    char comm[17];
};
static struct proc *pt;
static struct pdata *pd;
static int *idx;
static unsigned np, actual;
static int hz, km, mm, sw;
static unsigned long cpu0[CPUSTATES], cpu1[CPUSTATES];
static struct { unsigned uid; char name[9]; int used; } names[16];
static struct { dev_t dev; char name[5]; } ttymap[64];
static int nttymap;
extern char **environ;
static char *emptyenv[] = { 0 };

static int at(fd, pos, buf, size)
int fd;
off_t pos;
char *buf;
unsigned size;
{
    return lseek(fd, pos, L_SET) != (off_t)-1 &&
        read(fd, buf, size) == size;
}

static int safe(st)
struct stat *st;
{
    return S_ISREG(st->st_mode) && st->st_uid == 0 &&
        st->st_nlink == 1 && !(st->st_mode & 022);
}

/* /tmp must be a root-owned sticky directory. Root-owned existing names
 * cannot be replaced by other users. Never follow a precreated symlink. */
static int open_safe(path, flags)
char *path;
int flags;
{
    struct stat a, b;
    int fd;
    if (lstat(path, &a) < 0 || !safe(&a)) return -1;
    fd = open(path, flags, 0);
    if (fd < 0) return -1;
    if (fstat(fd, &b) < 0 || !safe(&b) || a.st_ino != b.st_ino ||
        a.st_dev != b.st_dev) { close(fd); return -1; }
    return fd;
}

static int cached(st)
struct stat *st;
{
    return lstat(CACHE, st) == 0 && safe(st) &&
        st->st_size > 0 && st->st_size <= MAXCACHE;
}

static int recent(when)
time_t when;
{
    time_t now;
    time(&now);
    return now >= when && now - when < PERIOD;
}

static int ctl(a, b, p, len)
int a, b;
char *p;
unsigned len;
{
    int mib[2];
    size_t n;
    mib[0] = a; mib[1] = b; n = len;
    return sysctl(mib, 2, p, &n, (char *)0, 0) >= 0 && n == len;
}

static long mapfree(which)
int which;
{
    int mib[2], i;
    size_t n;
    struct mapent *m;
    long sum;
    mib[0] = CTL_VM; mib[1] = which;
    n = 0;
    if (sysctl(mib, 2, (char *)0, &n, (char *)0, 0) < 0 ||
        n == 0 || n > 4096 || n % sizeof(*m)) return -1;
    m = (struct mapent *)malloc(n);
    if (m == NULL) return -1;
    if (sysctl(mib, 2, (char *)m, &n, (char *)0, 0) < 0) {
        free((char *)m); return -1;
    }
    sum = 0;
    for (i = 0; i < n / sizeof(*m); i++) sum += m[i].m_size;
    free((char *)m);
    return sum;
}

static char *username(uid)
unsigned uid;
{
    int i, slot;
    struct passwd *pw;
    static char number[8];
    slot = -1;
    for (i = 0; i < 16; i++) {
        if (names[i].used && names[i].uid == uid) return names[i].name;
        if (!names[i].used && slot < 0) slot = i;
    }
    if (slot >= 0 && (pw = getpwuid(uid)) != NULL) {
        names[slot].uid = uid; names[slot].used = 1;
        strncpy(names[slot].name, pw->pw_name, 8);
        names[slot].name[8] = 0;
        return names[slot].name;
    }
    sprintf(number, "%u", uid);
    return number;
}

/* Names only: never open a terminal. Bound both directory work and storage. */
static void readttys()
{
    DIR *dir;
    struct direct *entry;
    struct stat st;
    char path[MAXNAMLEN + 6];
    int scanned;
    dir = opendir("/dev");
    if (dir == NULL) return;
    scanned = 0;
    while (scanned++ < 512 && nttymap < 64 &&
        (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, "console") &&
            (strncmp(entry->d_name, "tty", 3) || !entry->d_name[3])) continue;
        strcpy(path, "/dev/");
        strncat(path, entry->d_name, MAXNAMLEN);
        if (lstat(path, &st) < 0 || !S_ISCHR(st.st_mode)) continue;
        ttymap[nttymap].dev = st.st_rdev;
        tty_short(ttymap[nttymap].name, entry->d_name);
        nttymap++;
    }
    closedir(dir);
}

static char *ttylabel(d)
struct pdata *d;
{
    int i;
    if (!d->valid) return "?";
    if (!d->has_tty) return "-";
    for (i = 0; i < nttymap; i++)
        if (ttymap[i].dev == d->tty) return ttymap[i].name;
    return "?";
}

static int sample()
{
    struct user u;
    struct proc check, *p;
    struct pdata *d;
    int i, j, count, fd, valid;
    short oldpid;
    long oldtime, oldstart, oldin, oldout, sec, usec;
    struct timeval oldsample;
    off_t pos, slot;
    if (!at(km, (off_t)nl[0].n_value, (char *)pt,
        np * sizeof(*pt))) return -1;
    count = 0;
    for (i = 0; i < np; i++) {
        p = &pt[i]; d = &pd[i];
        valid = d->valid; oldpid = d->pid;
        oldtime = d->total; oldstart = d->start;
        oldin = d->inblock; oldout = d->oublock;
        oldsample = d->sampled;
        memset((char *)d, 0, sizeof(*d));
        d->cpu = -1;
        d->reads = d->writes = -1;
        d->fds = d->sep = -1; d->overlay = -2;
        if (!p->p_stat) continue;
        idx[count++] = i; d->pid = p->p_pid;
        if (p->p_stat == SZOMB) {
            strcpy(d->comm, "[zombie]");
            d->total = p->p_ru.ru_utime + p->p_ru.ru_stime;
            continue;
        }
        strcpy(d->comm, p->p_pid == 0 ? "[swapper]" : "[unavailable]");
        slot = (off_t)nl[0].n_value + (long)i * sizeof(*p);
        fd = (p->p_flag & SLOAD) ? mm : sw;
        pos = (p->p_flag & SLOAD) ? ((off_t)p->p_addr << 6) :
            ((off_t)p->p_addr << 9);
        if (fd < 0 || !at(fd, pos, (char *)&u, sizeof(u)) ||
            (unsigned)u.u_procp != (unsigned)slot ||
            !at(km, slot, (char *)&check, sizeof(check)) ||
            check.p_pid != p->p_pid || check.p_stat != p->p_stat ||
            check.p_addr != p->p_addr ||
            ((check.p_flag ^ p->p_flag) & SLOAD)) continue;
        d->valid = 1; d->start = u.u_start; d->text = u.u_tsize;
        d->has_tty = u.u_ttyp != NULL; d->tty = u.u_ttyd;
        d->fds = 0;
        for (j = 0; j < NOFILE; j++) if (u.u_ofile[j]) d->fds++;
        d->sep = u.u_sep != 0;
        d->overlay = !u.u_ovdata.uo_nseg ? -1 :
            u.u_ovdata.uo_curov >= 0 && u.u_ovdata.uo_curov <= NOVL ?
            u.u_ovdata.uo_curov : -2;
        d->inblock = u.u_ru.ru_inblock; d->oublock = u.u_ru.ru_oublock;
        d->total = u.u_ru.ru_utime + u.u_ru.ru_stime;
        if (d->total < 0) d->total = 0;
        if (gettimeofday(&d->sampled, (struct timezone *)0) < 0)
            d->sampled.tv_sec = 0;
        if (valid && oldpid == p->p_pid && oldstart == d->start &&
            oldsample.tv_sec && d->sampled.tv_sec) {
            sec = d->sampled.tv_sec - oldsample.tv_sec;
            usec = d->sampled.tv_usec - oldsample.tv_usec;
            if (d->total >= oldtime)
                d->cpu = cpu_tenths(d->total - oldtime, sec, usec, hz);
            if (oldin >= 0 && d->inblock >= oldin)
                d->reads = io_tenths(d->inblock - oldin, sec, usec, hz);
            if (oldout >= 0 && d->oublock >= oldout)
                d->writes = io_tenths(d->oublock - oldout, sec, usec, hz);
        }
        if (u.u_comm[0]) {
            for (j = 0; j < 16 && u.u_comm[j]; j++)
                d->comm[j] = (u.u_comm[j] >= 32 && u.u_comm[j] <= 126) ?
                    u.u_comm[j] : '?';
            d->comm[j] = 0;
        }
    }
    return count;
}

static int compare(a, b)
int *a, *b;
{
    struct pdata *x, *y;
    x = &pd[*a]; y = &pd[*b];
    if (x->cpu != y->cpu) return x->cpu > y->cpu ? -1 : 1;
    if (x->total != y->total) return x->total > y->total ? -1 : 1;
    return x->pid - y->pid;
}

static int frame(out)
FILE *out;
{
    unsigned long budget, total, diff[CPUSTATES], pct;
    unsigned long ram;
    unsigned swapblocks;
    long freecore, freeswap, up, load, bootsec;
    struct loadavg av;
    struct timeval boot;
    struct tm *tm;
    struct utmp ut;
    FILE *uf;
    time_t now;
    char host[16], *dot;
    static char states[] = "?SWRIZT";
    int i, count, users, run, sleeping, stop, zombie;
    struct proc *p;
    struct pdata *d;
    km = mm = sw = -1;
    nlist("/unix", nl);
    for (i = 0; i < 4; i++) if (!nl[i].n_type) return 0;
    km = open("/dev/kmem", O_RDONLY, 0);
    mm = open("/dev/mem", O_RDONLY, 0);
    sw = open("/dev/swap", O_RDONLY, 0);
    if (km < 0 || mm < 0 ||
        !at(km, (off_t)nl[1].n_value, (char *)&actual, sizeof(actual)) ||
        !at(km, (off_t)nl[2].n_value, (char *)&hz, sizeof(hz)) ||
        hz < 1 || hz > 1000 || actual == 0) return 0;
    budget = sizeof(*pt) + sizeof(*pd) + sizeof(*idx);
    np = actual;
    if (np > 36000L / budget) np = 36000L / budget;
    if (np > 256) np = 256;
    pt = (struct proc *)malloc(np * sizeof(*pt));
    pd = (struct pdata *)calloc(np, sizeof(*pd));
    idx = (int *)malloc(np * sizeof(*idx));
    if (pt == NULL || pd == NULL || idx == NULL) return 0;
    if (!at(km, (off_t)nl[3].n_value, (char *)cpu0, sizeof(cpu0)) ||
        sample() < 0) return 0;
    sleep(1);
    if ((count = sample()) < 0 || !at(km, (off_t)nl[3].n_value,
        (char *)cpu1, sizeof(cpu1))) return 0;
    total = 0;
    for (i = 0; i < CPUSTATES; i++) {
        diff[i] = cpu1[i] - cpu0[i];
        if (diff[i] > 1000000L) return 0;
        total += diff[i];
    }
    close(km); close(mm); if (sw >= 0) close(sw);
    readttys();
    time(&now); tm = gmtime(&now);
    gethostname(host, sizeof(host)-1); host[sizeof(host)-1] = 0;
    dot = strchr(host, '.'); if (dot) *dot = 0;
    fprintf(out, "%s - %02d:%02d:%02d UTC", host,
        tm->tm_hour, tm->tm_min, tm->tm_sec);
    bootsec = 0;
    if (ctl(CTL_KERN, KERN_BOOTTIME, (char *)&boot, sizeof(boot))) {
        bootsec = boot.tv_sec;
        up = now - boot.tv_sec; if (up < 0) up = 0;
        fprintf(out, " up %ldd %02ld:%02ld", up / 86400L,
            up / 3600L % 24L, up / 60L % 60L);
    }
    users = 0; uf = fopen(_PATH_UTMP, "r");
    if (uf) {
        while (fread((char *)&ut, sizeof(ut), 1, uf) == 1)
            if (ut.ut_name[0]) users++;
        fclose(uf);
    }
    fprintf(out, ", %d user%s\n", users, users == 1 ? "" : "s");
    fprintf(out, "Load average:");
    if (ctl(CTL_VM, VM_LOADAVG, (char *)&av, sizeof(av)) && av.fscale > 0) {
        for (i = 0; i < 3; i++) {
            load = (long)av.ldavg[i] * 100L / av.fscale;
            fprintf(out, "%s %ld.%02ld", i ? "," : "", load/100, load%100);
        }
    } else fprintf(out, " unavailable");
    fprintf(out, "     CPU sampled over %lu.%lu seconds\n",
        total / hz, (total % hz) * 10L / hz);
    run = sleeping = stop = zombie = 0;
    for (i = 0; i < count; i++) {
        switch (pt[idx[i]].p_stat) {
        case SRUN: run++; break;
        case SSTOP: stop++; break;
        case SZOMB: zombie++; break;
        default: sleeping++; break;
        }
    }
    fprintf(out, "Tasks: %3d total, %d running, %d sleeping, %d stopped, %d zombie\n",
        count, run, sleeping, stop, zombie);
    fprintf(out, "Cpu  :");
    for (i = 0; i < CPUSTATES; i++) {
        pct = total ? diff[i] * 1000L / total : 0;
        fprintf(out, " %3lu.%lu%% %s%s", pct/10, pct%10,
            i == CP_USER ? "us" : i == CP_NICE ? "ni" :
            i == CP_SYS ? "sy" : "id", i == CPUSTATES-1 ? "\n" : ",");
    }
    freecore = mapfree(VM_COREMAP);
    if (ctl(CTL_HW, HW_PHYSMEM, (char *)&ram, sizeof(ram)) && freecore >= 0) {
        ram /= 1024; freecore /= 16;
        if (freecore > ram) freecore = ram;
        fprintf(out, "Mem  : %5luK total, %5ldK free, %5luK used (%lu%%)\n",
            ram, freecore, ram-freecore, ram ? (ram-freecore)*100L/ram : 0);
    } else fprintf(out, "Mem  : unavailable\n");
    freeswap = mapfree(VM_SWAPMAP);
    if (ctl(CTL_VM, VM_NSWAP, (char *)&swapblocks, sizeof(swapblocks)) &&
        freeswap >= 0) {
        ram = (unsigned long)swapblocks / 2; freeswap /= 2;
        if (freeswap > ram) freeswap = ram;
        fprintf(out, "Swap : %5luK total, %5ldK free, %5luK used (%lu%%)\n",
            ram, freeswap, ram-freeswap, ram ? (ram-freeswap)*100L/ram : 0);
    } else fprintf(out, "Swap : unavailable\n");
    fprintf(out, "\n%5s %5s %-8s %3s %3s %4s %4s %4s %s %s %5s %9s %8s %-4s %3s %5s %5s %3s %3s %s\n",
        "PID", "PPID", "USER", "PR", "NI", "TEXT", "DATA", "STK", "S", "M",
        "CPU%", "TIME", "AGE", "TTY", "FD", "R/s", "W/s", "I/D", "OVL", "COMMAND");
    qsort((char *)idx, count, sizeof(*idx), compare);
    for (i = 0; i < count && i < ROWS; i++) {
        p = &pt[idx[i]]; d = &pd[idx[i]];
        process_row(out,
            p->p_pid, p->p_ppid, username((unsigned)p->p_uid),
            p->p_stat == SZOMB ? 0 : p->p_pri,
            p->p_stat == SZOMB ? 0 : p->p_nice,
            d->valid ? d->text / 16 : -1,
            p->p_stat == SZOMB ? 0 : p->p_dsize / 16,
            p->p_stat == SZOMB ? 0 : p->p_ssize / 16,
            p->p_stat > 0 && p->p_stat <= 6 ? states[p->p_stat] : '?',
            p->p_stat == SZOMB ? '-' : (p->p_flag & SLOAD) ? 'C' : 'S',
            d->cpu, d->valid || p->p_stat == SZOMB ? d->total : -1L,
            hz, d->valid ? process_age((long)now, d->start, bootsec) : -1L,
            p->p_stat == SZOMB ? "-" : ttylabel(d), d->fds,
            d->reads, d->writes, d->sep, d->overlay, d->comm);
    }
    if (count > ROWS) fprintf(out, "... %d more processes; sorted by CPU%%, then TIME\n", count-ROWS);
    if (np < actual) fprintf(out, "Process table capped: %u of %u slots examined\n", np, actual);
    fprintf(out, "Memory: KiB; M: C=core, S=swapped; TIME: seconds (h=hours); CPU%%: interval\n");
    fprintf(out, "TTY: tty prefix omitted, cons=console; -=none/no sample; ?=unavailable\n");
    fprintf(out, "AGE: elapsed (?=inconsistent clock); FD: open descriptors; R/s,W/s: block operations/sec (k=1000)\n");
    fprintf(out, "I/D: Y=separate instruction/data spaces, N=combined; OVL: current overlay (-=none)\n");
    return !ferror(out);
}

static int serve(cgi)
int cgi;
{
    int fd, n;
    struct stat st;
    time_t now;
    long age;
    char buf[512];
    fd = open_safe(CACHE, O_RDONLY);
    if (fd < 0) return 0;
    if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > MAXCACHE) {
        close(fd); return 0;
    }
    time(&now); age = now - st.st_mtime; if (age < 0) age = 0;
    if (cgi) {
        printf("HTTP/1.0 200 OK\nContent-Type: text/plain; charset=us-ascii\n");
        printf("Cache-Control: no-store\nX-Content-Type-Options: nosniff\n");
        printf("X-Snapshot-Age: %ld\nContent-Length: %ld\n\n", age, st.st_size);
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0) fwrite(buf, 1, n, stdout);
    close(fd);
    return 1;
}

int main()
{
    int cgi, fd, lockfd, made, ok;
    struct stat st;
    struct rlimit lim;
    FILE *out;
    time_t now;
    cgi = getenv("REQUEST_METHOD") != NULL || getenv("GATEWAY_INTERFACE") != NULL;
    environ = emptyenv;
    umask(077);
    lim.rlim_cur = lim.rlim_max = 0; setrlimit(RLIMIT_CORE, &lim);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, SIG_DFL); alarm(12);
    lockfd = -1;
    if (geteuid() != 0 || lstat("/tmp", &st) < 0 ||
        !S_ISDIR(st.st_mode) || st.st_uid != 0 || !(st.st_mode & S_ISVTX))
        goto respond;
    if (cached(&st) && recent(st.st_mtime)) goto respond;
    made = 0;
    lockfd = open(LOCK, O_RDWR|O_CREAT|O_EXCL, 0600);
    if (lockfd >= 0) made = 1;
    else lockfd = open_safe(LOCK, O_RDWR);
    if (lockfd < 0) goto respond;
    if (flock(lockfd, LOCK_EX|LOCK_NB) < 0) {
        close(lockfd); lockfd = -1;
        if (!cached(&st)) sleep(1);
        goto respond;
    }
    if (cached(&st) && recent(st.st_mtime)) goto unlock;
    if (!made && fstat(lockfd, &st) == 0 && st.st_size > 0 &&
        recent(st.st_mtime)) goto unlock;
    time(&now);
    lseek(lockfd, 0L, L_SET);
    if (write(lockfd, (char *)&now, sizeof(now)) != sizeof(now)) goto unlock;
    if (lstat(NEWCACHE, &st) == 0) {
        if (!safe(&st) || unlink(NEWCACHE) < 0) goto unlock;
    }
    fd = open(NEWCACHE, O_WRONLY|O_CREAT|O_EXCL, 0600);
    if (fd < 0) goto unlock;
    out = fdopen(fd, "w");
    if (out == NULL) { close(fd); unlink(NEWCACHE); goto unlock; }
    ok = frame(out);
    if (fflush(out) == EOF) ok = 0;
    if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > MAXCACHE) ok = 0;
    if (ok && fchmod(fd, 0644) < 0) ok = 0;
    if (fclose(out) == EOF) ok = 0;
    if (ok) {
        if (rename(NEWCACHE, CACHE) < 0) unlink(NEWCACHE);
    } else unlink(NEWCACHE);
unlock:
    flock(lockfd, LOCK_UN); close(lockfd); lockfd = -1;
respond:
    alarm(0);
    if (serve(cgi)) return 0;
    if (cgi) printf("HTTP/1.0 503 Service Unavailable\nContent-Type: text/plain\nCache-Control: no-store\nRetry-After: 5\n\n");
    printf("Snapshot temporarily unavailable. Please retry in 5 seconds.\n");
    return 1;
}
