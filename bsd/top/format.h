/*
 * Typed fields for the frequently repeated process-row formatter.
 * The row's layout is fixed, so parsing a printf format on every refresh
 * is unnecessary. These helpers append directly to one bounded line.
 * Positive widths pad on the left; negative widths pad on the right.
 * Width is a minimum: large values remain visible instead of truncating.
 */
#ifndef TOP_LINE_FORMAT_H
#define TOP_LINE_FORMAT_H

struct output_line {
    char *next, *end;
};

static void
line_start(out, buffer, size)
struct output_line *out;
char *buffer;
unsigned int size;
{
    if (!size) { out->next = out->end = 0; return; }
    out->next = buffer;
    out->end = buffer + size - 1;
    *buffer = 0;
}

static void
line_text(out, text, width, limit)
register struct output_line *out;
register char *text;
int width, limit;
{
    int length, pad, left;
    /* Native cc needs explicit hints for the pointers/index used for each
     * character. Three word registers are available in each function. */
    register int i;

    if (!out->next) return;
    length = 0;
    /* limit gives string precision, such as eight characters for a user.
     * Zero means no precision limit. Padding uses the clipped length. */
    while (text[length] && (!limit || length < limit)) length++;
    left = width < 0;
    if (left) width = -width;
    pad = width - length;
    if (!left)
        for (i = 0; i < pad; i++)
            if (out->next < out->end) *out->next++ = ' ';
    for (i = 0; i < length; i++)
        if (out->next < out->end) *out->next++ = text[i];
    if (left)
        for (i = 0; i < pad; i++)
            if (out->next < out->end) *out->next++ = ' ';
}

static void
line_character(out, c)
struct output_line *out;
int c;
{
    if (out->next && out->next < out->end) *out->next++ = c;
}

static void
line_number(out, value, width)
struct output_line *out;
int value, width;
{
    register unsigned int magnitude, quotient;
    char number[7];
    register char *p;

    /* Native int is 16 bits: sign, five digits and NUL require seven
     * bytes. Unsigned subtraction handles -32768 without signed overflow. */
    magnitude = value < 0 ? 0 - (unsigned int)value : (unsigned int)value;
    p = number + sizeof(number) - 1;
    *p = 0;
    do {
        quotient = magnitude / 10;
        *--p = '0' + (magnitude - quotient * 10);
        magnitude = quotient;
    } while (magnitude);
    if (value < 0) *--p = '-';
    line_text(out, p, width, 0);
}

static void
line_finish(out)
struct output_line *out;
{
    /* Every append reserves this byte, even when the line was clipped. */
    if (out->next) *out->next = 0;
}

#endif
