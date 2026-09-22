/* cc -O -o test-fields tests/webtop-fields.c && ./test-fields */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../webtop-fields.h"
extern FILE *tmpfile();

int main()
{
    FILE *out;
    char line[128], tty[5];
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
    tty_short(tty, "console"); assert(!strcmp(tty, "cons"));
    tty_short(tty, "ttyp0"); assert(!strcmp(tty, "p0"));
    tty_short(tty, "tty00"); assert(!strcmp(tty, "00"));
    tty_short(tty, "tty"); assert(!strcmp(tty, "?"));
    tty_short(tty, "ttytoolong"); assert(!strcmp(tty, "?"));
    tty_short(tty, "tty\033x"); assert(!strcmp(tty, "?x"));

    out = tmpfile(); assert(out != NULL);
    process_row(out, 32767, "longusername", 127, -20, 4095, 4095, 4095,
        'R', 'C', 1000, 2147483647L, 1, "cons", "12345678901234567");
    process_row(out, 1, "root", 30, 0, 15, 10, 4,
        'S', 'S', 0, 99999999L, 100, "p0", "1234567890123456");
    process_row(out, 0, "root", -128, -128, -1, -1, -1,
        'Z', '-', -1, -1L, 60, "?", "1234567890123456");
    rewind(out);
    for (i = 0; i < 3; i++) {
        assert(fgets(line, sizeof(line), out) != NULL);
        assert(strlen(line) == 80); /* 79 columns plus newline. */
        assert(!strcmp(line + 63, "1234567890123456\n"));
        if (i == 0) {
            assert(!strncmp(line + 42, "100.0", 5));
            assert(strstr(line, "596523h") != NULL);
        }
        if (i == 1) assert(strstr(line, "999999.99") != NULL);
    }
    assert(fgets(line, sizeof(line), out) == NULL);
    fclose(out);
    puts("webtop field tests passed");
    return 0;
}
