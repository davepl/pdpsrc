/* Test production format.h against printf's established field semantics.
 * Host builds exhaust signed16; NATIVE selects boundaries for the PDP-11. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "format.h"

static void
expect_fields(value, width)
int value, width;
{
    char actual[80], expected[80];
    struct output_line out;
    line_start(&out, actual, sizeof(actual));
    line_character(&out, '[');
    line_number(&out, value, width);
    line_character(&out, ']');
    line_finish(&out);
    /* This libc ignores a negative '*' width instead of left-justifying.
     * An explicit '-' flag and positive width express the same layout on
     * both native and modern printf, keeping the oracle independent. */
    if (width < 0) sprintf(expected, "[%-*d]", -width, value);
    else sprintf(expected, "[%*d]", width, value);
    if (strcmp(actual, expected)) {
        fprintf(stderr, "integer field: value=%d (hex %x), width=%d\n",
            value, (unsigned int)value, width);
        fprintf(stderr, "actual=<%s> expected=<%s>\n", actual, expected);
    }
    assert(!strcmp(actual, expected));
}

int
main()
{
    long value;
#ifdef NATIVE
    /* Exhaustive host coverage is supplemented by native representation
     * boundaries. Avoid spending target time repeating every interior value. */
    static int edges[] = {
        -32767 - 1, -32767, -10000, -9999, -1000, -999, -100, -99,
        -10, -9, -1, 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000,
        32766, 32767
    };
#endif
    int width, limit, size, i, length;
    char actual[80], expected[80], storage[48];
    struct output_line out;
    static char *names[] = { "", "dave", "eightchr", "longer-than-eight" };

#ifdef NATIVE
    for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        expect_fields(edges[i], 4);
        expect_fields(edges[i], -8);
    }
#else
    /* Exhaust the target's signed-int range; a host int may be wider. */
    for (value = -32768L; value <= 32767L; value++) {
        expect_fields((int)value, 4);
        expect_fields((int)value, -8);
    }
#endif
    for (i = 0; i < 4; i++)
        for (width = -10; width <= 10; width++)
            for (limit = 0; limit <= 10; limit++) {
                line_start(&out, actual, sizeof(actual));
                line_text(&out, names[i], width, limit);
                line_finish(&out);
                if (width < 0) {
                    if (limit)
                        sprintf(expected, "%-*.*s", -width, limit, names[i]);
                    else sprintf(expected, "%-*s", -width, names[i]);
                } else {
                    if (limit)
                        sprintf(expected, "%*.*s", width, limit, names[i]);
                    else sprintf(expected, "%*s", width, names[i]);
                }
                if (strcmp(actual, expected))
                    fprintf(stderr,
                        "string field: input=<%s> width=%d limit=%d\nactual=<%s> expected=<%s>\n",
                        names[i], width, limit, actual, expected);
                assert(!strcmp(actual, expected));
            }

    sprintf(expected, "[%5d %-8.8s%4d]", 42, "dave", -20);
    length = strlen(expected);
    for (size = 0; size <= 24; size++) {
        memset(storage, 'Z', sizeof(storage));
        line_start(&out, storage + 8, size);
        line_character(&out, '[');
        line_number(&out, 42, 5);
        line_character(&out, ' ');
        line_text(&out, "dave", -8, 8);
        line_number(&out, -20, 4);
        line_character(&out, ']');
        line_finish(&out);
        if (size) {
            int kept = size - 1 < length ? size - 1 : length;
            assert(!memcmp(storage + 8, expected, kept));
            assert(storage[8 + kept] == 0);
        }
        for (i = 0; i < 8; i++) assert(storage[i] == 'Z');
        for (i = 8 + size; i < (int)sizeof(storage); i++) assert(storage[i] == 'Z');
    }
#ifdef NATIVE
    puts("typed-row native tests passed: signed16 boundaries, padding, precision, bounds");
#else
    puts("typed-row tests passed: full signed16 range, padding, precision, bounds");
#endif
    return 0;
}
