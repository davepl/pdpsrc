/* Shared with the native/host tests. Keep arithmetic within PDP-11 longs. */
static int cpu_tenths(ticks, sec, usec, hz)
long ticks, sec, usec;
int hz;
{
    long span;
    if (hz < 1 || hz > 1000 || ticks < 0 || sec < 0 || sec > 12 ||
        usec <= -1000000L || usec >= 1000000L) return -1;
    /* Round elapsed wall time to ticks; account for the two sample sweeps. */
    span = sec * hz + (usec * hz + 500000L) / 1000000L;
    if (usec < 0) span = sec * hz - (-usec * hz + 500000L) / 1000000L;
    if (span <= 0 || span > 12L * hz) return -1;
    /* Allow one tick of read/timer skew, but reject clock/counter jumps. */
    if (ticks > span + 1) return -1;
    if (ticks >= span) return 1000;
    return (int)(ticks * 1000L / span);
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

static void process_row(out, pid, user, pri, nice, text, data, stack,
    state, memory, cpu, ticks, hz, tty, comm)
FILE *out;
int pid, pri, nice, text, data, stack, cpu, hz;
char *user, state, memory, *tty, *comm;
long ticks;
{
    char cpustr[8], timestr[16], pri_s[8], nice_s[8];
    char text_s[8], data_s[8], stack_s[8];
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
    fprintf(out, "%5d %-8.8s %3s %3s %4s %4s %4s %c %c %5s %9s %-4.4s %.16s\n",
        pid, user, pri_s, nice_s, text_s, data_s, stack_s,
        state, memory, cpustr, timestr, tty, comm);
}
