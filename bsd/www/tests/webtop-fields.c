/* cc -O -o test-fields tests/webtop-fields.c && ./test-fields */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../webtop-fields.h"
extern FILE *tmpfile();

int main()
{
    FILE *out;
    char line[128], tty[5], value[16];
    int i;
    assert(cpu_tenths(0L, 1L, 0L, 60) == 0);
    assert(cpu_tenths(30L, 1L, 0L, 60) == 500);
    assert(cpu_tenths(90L, 2L, -500000L, 60) == 1000);
    assert(cpu_tenths(30L, 1L, 500000L, 60) == 333);
    assert(cpu_tenths(15L, 1L, -750000L, 60) == 1000);
    assert(cpu_tenths(6000L, 12L, 0L, 1000) == 500);
    assert(cpu_tenths(12000L, 12L, 0L, 1000) == 1000);
    assert(cpu_tenths(61L, 1L, 0L, 60) == 1000);
    assert(cpu_tenths(62L, 1L, 0L, 60) == -1);
    assert(cpu_tenths(-1L, 1L, 0L, 60) == -1);
    assert(cpu_tenths(1L, 0L, 0L, 60) == -1);
    assert(cpu_tenths(1L, -1L, 0L, 60) == -1);
    assert(cpu_tenths(1L, 13L, 0L, 60) == -1);
    assert(cpu_tenths(2147483647L, 1L, 0L, 60) == -1);
    assert(cpu_tenths(1L, 1L, 0L, 0) == -1);
    assert(io_tenths(0L, 1L, 0L, 60) == 0);
    assert(io_tenths(3L, 1L, 500000L, 60) == 20);
    assert(io_tenths(1L, 1L, 500000L, 60) == 6);
    assert(io_tenths(120000L, 12L, 0L, 1000) == 100000L);
    assert(io_tenths(2147483647L, 0L, 1000L, 1000) == 10000000L);
    assert(io_tenths(-1L, 1L, 0L, 60) == -1);
    assert(io_tenths(1L, 0L, 0L, 60) == -1);
    rate_text(value, 9999L); assert(!strcmp(value, "999.9"));
    rate_text(value, 10000L); assert(!strcmp(value, "1.0k"));
    rate_text(value, 999999L); assert(!strcmp(value, "99.9k"));
    rate_text(value, 1000000L); assert(!strcmp(value, "100k"));
    rate_text(value, 10000000L); assert(!strcmp(value, ">999k"));
    rate_text(value, -1L); assert(!strcmp(value, "-"));
    age_text(value, 59L); assert(!strcmp(value, "59s"));
    age_text(value, 61L); assert(!strcmp(value, "1m01s"));
    age_text(value, 3661L); assert(!strcmp(value, "1h01m"));
    age_text(value, 90000L); assert(!strcmp(value, "1d01h"));
    age_text(value, 2147483647L); assert(!strcmp(value, "24855d"));
    age_text(value, -1L); assert(!strcmp(value, "-"));
    age_text(value, -2L); assert(!strcmp(value, "?"));
    assert(process_age(1000L, 900L, 800L) == 100L);
    assert(process_age(1000L, 0L, 800L) == -1L);
    assert(process_age(1000L, 1001L, 800L) == -2L);
    assert(process_age(1000L, 700L, 800L) == -2L);
    tty_short(tty, "console"); assert(!strcmp(tty, "cons"));
    tty_short(tty, "ttyp0"); assert(!strcmp(tty, "p0"));
    tty_short(tty, "tty00"); assert(!strcmp(tty, "00"));
    tty_short(tty, "tty"); assert(!strcmp(tty, "?"));
    tty_short(tty, "ttytoolong"); assert(!strcmp(tty, "?"));
    tty_short(tty, "tty\033x"); assert(!strcmp(tty, "?x"));

    out = tmpfile(); assert(out != NULL);
    process_row(out, 32767, 32767, "longusername", 127, -20, 4095, 4095, 4095,
        'R', 'C', 1000, 2147483647L, 1, 2147483647L, "cons", 999,
        9999L, 10000000L, 1, 999, "12345678901234567");
    process_row(out, 1, 0, "root", 30, 0, 15, 10, 4,
        'S', 'S', 0, 99999999L, 100, 61L, "p0", 0,
        0L, 0L, 0, -1, "1234567890123456");
    process_row(out, 0, 0, "root", -128, -128, -1, -1, -1,
        'Z', '-', -1, -1L, 60, -1L, "?", -1,
        -1L, -1L, -1, -2, "1234567890123456");
    rewind(out);
    for (i = 0; i < 3; i++) {
        assert(fgets(line, sizeof(line), out) != NULL);
        assert(strlen(line) == 119); /* 118 columns plus newline. */
        assert(!strcmp(line + 102, "1234567890123456\n"));
        if (i == 0) {
            assert(!strncmp(line + 48, "100.0", 5));
            assert(strstr(line, "596523h") != NULL);
            assert(!strncmp(line + 82, "999.9 >999k   Y 999", 19));
        }
        if (i == 1) assert(strstr(line, "999999.99") != NULL);
    }
    assert(fgets(line, sizeof(line), out) == NULL);
    fclose(out);
    puts("webtop field tests passed");
    return 0;
}
