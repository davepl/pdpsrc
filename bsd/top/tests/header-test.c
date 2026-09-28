/* Test the actual production helper with changing external observations. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

static char hostname[] = "minerva";
static struct timeval boottime = {100, 0};
static int screen_cols = 132;
static time_t sample_time = 1000;
static int sample_users = 2, sample_result = 3;
static double sample_loads[3] = {0.10, 0.20, 0.30};
static int time_calls, user_calls, load_calls, calendar_calls, format_calls;

static time_t fake_time(time_t *dest)
{
    time_calls++;
    *dest = sample_time;
    return sample_time;
}
static int user_count(void) { user_calls++; return sample_users; }
static int fake_getloadavg(double *loads, int count)
{
    int i;
    load_calls++;
    assert(count == 3);
    for (i = 0; i < count && i < sample_result; i++) loads[i] = sample_loads[i];
    return sample_result;
}
static struct tm *fake_localtime(time_t *now)
{
    static struct tm tm;
    calendar_calls++;
    memset(&tm, 0, sizeof(tm));
    tm.tm_hour = 1; tm.tm_min = 2; tm.tm_sec = *now % 60;
    return &tm;
}
static int counted_sprintf(char *dest, const char *fmt, ...)
{
    va_list args;
    int result;
    format_calls++;
    va_start(args, fmt);
    result = vsprintf(dest, fmt, args);
    va_end(args);
    return result;
}
#define time fake_time
#define getloadavg fake_getloadavg
#define localtime fake_localtime
#undef sprintf
#define sprintf counted_sprintf
#include "header-under-test.h"
#undef time
#undef getloadavg
#undef localtime
#undef sprintf

static void check(char *expected, int cache_hit)
{
    static int calls;
    static char *stable;
    int old_calendar = calendar_calls, old_format = format_calls;
    char *text = header_text();
    calls++;
    assert(time_calls == calls && user_calls == calls && load_calls == calls);
    if (strcmp(text, expected)) fprintf(stderr, "case %d\nactual: %s\nexpect: %s\n", calls, text, expected);
    assert(!strcmp(text, expected));
    if (stable) assert(text == stable);
    stable = text;
    if (cache_hit) {
        assert(calendar_calls == old_calendar && format_calls == old_format);
    } else {
        assert(calendar_calls == old_calendar + 1 && format_calls > old_format);
    }
}

int main(void)
{
    check("minerva - 01:02:40 up  0 days,  0:15,   2 users,  load average: 0.10, 0.20, 0.30", 0);
    check("minerva - 01:02:40 up  0 days,  0:15,   2 users,  load average: 0.10, 0.20, 0.30", 1);
    screen_cols = 80;
    check("minerva 01:02:40 up 0d 0:15, 2 users, load 0.10, 0.20, 0.30", 0);
    check("minerva 01:02:40 up 0d 0:15, 2 users, load 0.10, 0.20, 0.30", 1);
    sample_time++;
    check("minerva 01:02:41 up 0d 0:15, 2 users, load 0.10, 0.20, 0.30", 0);
    sample_users = 1;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: 0.10, 0.20, 0.30", 0);
    sample_loads[2] = 0.40;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: 0.10, 0.20, 0.40", 0);
    sample_result = -1;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: unavailable", 0);
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: unavailable", 1);
    sample_result = 2;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: unavailable", 0);
    sample_result = 3;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: 0.10, 0.20, 0.40", 0);
    screen_cols = 132;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: 0.10, 0.20, 0.40", 0);
    sample_loads[0] = 0.11;
    check("minerva - 01:02:41 up  0 days,  0:15,   1 user,  load average: 0.11, 0.20, 0.40", 0);
    puts("header cache passed: every call samples all inputs; unchanged hits avoid");
    puts("calendar/format calls; time, users, loads, failure/recovery and resize invalidate");
    return 0;
}
