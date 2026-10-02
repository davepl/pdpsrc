/* Compare the actual production process-row block with the old printf row.
 * Values are deliberate native-width boundaries, not values chosen by the
 * new formatter. Both paths share fixed(), which has its own golden tests. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "format.h"
#include "fixed-under-test.h"

struct row {
    int pid, uid, pri, nice;
    unsigned int text, data, stack;
    char state, swapped, valid;
    long ticks, delta;
    char *command;
};

static int hz, interval_valid;
static char *name;
static char *
username(uid)
int uid;
{
    return name;
}

static void
new_row(result, r)
char *result;
struct row *r;
{
    struct output_line row_text;
    char line[256], field[4][24];
#include "row-block.h"
    strcpy(result, line);
}

static void
old_row(line, r)
char *line;
struct row *r;
{
    char field[5][24], times[48];
    if (r->valid)
        sprintf(times, "%8s %7s",
            fixed(field[0], (unsigned long)r->ticks, (unsigned long)hz, 2),
            interval_valid ? fixed(field[1], (unsigned long)r->delta,
                (unsigned long)hz, 2) : "-");
    else strcpy(times, "       ?       ?");
    sprintf(line, "%5d %-8.8s%4d%4d%5sK%5sK%5sK %c%c%s %s",
        r->pid, username(r->uid), r->pri, r->nice,
        fixed(field[2], (unsigned long)r->text, 16L, 1),
        fixed(field[3], (unsigned long)r->data, 16L, 1),
        fixed(field[4], (unsigned long)r->stack, 16L, 1),
        r->state, r->swapped ? 's' : ' ', times, r->command);
}

int
main()
{
    static struct row cases[] = {
        { 0, 0, 0, 0, 0, 0, 0, 'R', 0, 1, 0L, 0L, "swapper" },
        { 32767, 0, -32767 - 1, 32767, 65535U, 65535U, 65535U,
            'T', 1, 1, 2147483647L, 2147483647L, "abcdefghijklmn" },
        { 42, 0, 50, -20, 15, 16, 17, 'S', 0, 1, 5976L, 597L, "ksh" },
        { 999, 0, -10, 20, 4095, 4096, 4097, 'Z', 0, 0, 0L, 0L,
            "<unavailable>" }
    };
    static char *names[] = { "", "dave", "12345678", "more-than-eight" };
    static int rates[] = { 1, 50, 60, 100 };
    char actual[256], expected[256];
    int i, j, k, b, n;

    n = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            for (k = 0; k < 4; k++)
                for (b = 0; b < 2; b++) {
                    name = names[j];
                    hz = rates[k];
                    interval_valid = b;
                    old_row(expected, &cases[i]);
                    new_row(actual, &cases[i]);
                    if (strcmp(actual, expected)) {
                        fprintf(stderr,
                            "row case=%d name=%d hz=%d interval=%d\n",
                            i, j, hz, interval_valid);
                        fprintf(stderr, "actual=<%s>\nexpected=<%s>\n",
                            actual, expected);
                    }
                    assert(!strcmp(actual, expected));
                    n++;
                }
    printf("typed full-row tests passed: %d exact printf comparisons\n", n);
    return 0;
}
