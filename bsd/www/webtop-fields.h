/* Shared with the native/host tests. Keep arithmetic within PDP-11 longs. */
static long sample_ticks(sec, usec, hz)
long sec, usec;
int hz;
{
    long span;
    if (hz < 1 || hz > 1000 || sec < 0 || sec > 12 ||
        usec <= -1000000L || usec >= 1000000L) return -1;
    /* Round elapsed wall time to ticks; account for the two sample sweeps. */
    span = sec * hz + (usec * hz + 500000L) / 1000000L;
    if (usec < 0) span = sec * hz - (-usec * hz + 500000L) / 1000000L;
    if (span <= 0 || span > 12L * hz) return -1;
    return span;
}

static int cpu_tenths(ticks, sec, usec, hz)
long ticks, sec, usec;
int hz;
{
    long span;
    span = sample_ticks(sec, usec, hz);
    if (ticks < 0 || span < 0) return -1;
    /* Allow one tick of read/timer skew, but reject clock/counter jumps. */
    if (ticks > span + 1) return -1;
    if (ticks >= span) return 1000;
    return (int)(ticks * 1000L / span);
}

/* Block operations/second in tenths. Split the product to avoid overflow. */
static long io_tenths(delta, sec, usec, hz)
long delta, sec, usec;
int hz;
{
    long span, scale, whole, value;
    span = sample_ticks(sec, usec, hz);
    if (delta < 0 || span < 0) return -1;
    scale = hz * 10L; whole = delta / span;
    if (whole > 9999999L / scale) return 10000000L;
    value = whole * scale + (delta % span) * scale / span;
    return value > 9999999L ? 10000000L : value;
}

static void rate_text(out, rate)
char *out;
long rate;
{
    if (rate < 0) strcpy(out, "-");
    else if (rate < 10000L) sprintf(out, "%ld.%ld", rate / 10, rate % 10);
    else if (rate < 1000000L)
        sprintf(out, "%ld.%ldk", rate / 10000L, rate / 1000L % 10);
    else if (rate < 10000000L) sprintf(out, "%ldk", rate / 10000L);
    else strcpy(out, ">999k");
}

static long process_age(now, start, boot)
long now, start, boot;
{
    if (start <= 0) return -1;
    if (start > now || (boot > 0 && start < boot)) return -2;
    return now - start;
}

static void age_text(out, age)
char *out;
long age;
{
    if (age == -2) strcpy(out, "?");
    else if (age < 0) strcpy(out, "-");
    else if (age < 60) sprintf(out, "%lds", age);
    else if (age < 3600) sprintf(out, "%ldm%02lds", age / 60, age % 60);
    else if (age < 86400L)
        sprintf(out, "%ldh%02ldm", age / 3600, age / 60 % 60);
    else if (age < 86400000L)
        sprintf(out, "%ldd%02ldh", age / 86400L, age / 3600 % 24);
    else sprintf(out, "%ldd", age / 86400L);
}

static void tty_short(out, name)
char *out, *name;
{
    int i;
    if (!strcmp(name, "console")) name = "cons";
    else if (!strncmp(name, "tty", 3)) name += 3;
    if (!*name || strlen(name) > 4) name = "?";
    for (i = 0; name[i] && i < 4; i++)
        out[i] = name[i] >= 32 && name[i] <= 126 ? name[i] : '?';
    out[i] = 0;
}

static void process_row(out, pid, ppid, user, pri, nice, text, data, stack,
    state, memory, cpu, ticks, hz, age, tty, fds, reads, writes, sep, overlay, comm)
FILE *out;
int pid, ppid, pri, nice, text, data, stack, cpu, hz, fds, sep, overlay;
char *user, state, memory, *tty, *comm;
long ticks, age, reads, writes;
{
    char cpustr[8], timestr[16], pri_s[8], nice_s[8];
    char text_s[8], data_s[8], stack_s[8];
    char ages[12], fds_s[8], read_s[8], write_s[8], ovl_s[8];
    long sec;
    if (cpu < 0 || cpu > 1000) strcpy(cpustr, "-");
    else sprintf(cpustr, "%d.%d", cpu / 10, cpu % 10);
    if (ticks < 0 || hz < 1) strcpy(timestr, "-");
    else {
        sec = ticks / hz;
        if (sec > 999999L) sprintf(timestr, "%ldh", sec / 3600L);
        else sprintf(timestr, "%ld.%02ld", sec, (ticks % hz) * 100L / hz);
    }
    if (pri < -99 || pri > 999) strcpy(pri_s, "?");
    else sprintf(pri_s, "%d", pri);
    if (nice < -99 || nice > 999) strcpy(nice_s, "?");
    else sprintf(nice_s, "%d", nice);
    if (text < 0 || text > 9999) strcpy(text_s, "-");
    else sprintf(text_s, "%d", text);
    if (data < 0 || data > 9999) strcpy(data_s, "-");
    else sprintf(data_s, "%d", data);
    if (stack < 0 || stack > 9999) strcpy(stack_s, "-");
    else sprintf(stack_s, "%d", stack);
    age_text(ages, age); rate_text(read_s, reads); rate_text(write_s, writes);
    if (fds < 0 || fds > 999) strcpy(fds_s, "-");
    else sprintf(fds_s, "%d", fds);
    if (overlay == -1) strcpy(ovl_s, "-");
    else if (overlay < 0 || overlay > 999) strcpy(ovl_s, "?");
    else sprintf(ovl_s, "%d", overlay);
    fprintf(out, "%5d %5d %-8.8s %3s %3s %4s %4s %4s %c %c %5s %9s %8s %-4.4s %3s %5s %5s %3s %3s %.16s\n",
        pid, ppid, user, pri_s, nice_s, text_s, data_s, stack_s,
        state, memory, cpustr, timestr, ages, tty, fds_s, read_s, write_s,
        sep < 0 ? "-" : sep ? "Y" : "N", ovl_s, comm);
}
