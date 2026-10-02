#!/usr/bin/env python3
"""Host check of the actual production formatter against 64-bit arithmetic.

This supplements, rather than replaces, native compiler/library tests.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
source = (here.parent / "top.c").read_text()
start = source.index("static char *\nfixed(buf, value, scale, digits)")
end = source.index("\n}\n", start) + 3
helper = source[start:end]
test = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

HELPER

static unsigned long cases;
static void check(unsigned long value, unsigned long scale, int digits)
{
    struct { char before; char text[24]; char after; } actual;
    char expected[24];
    uint64_t rounded;
    unsigned int base = digits == 2 ? 100 : 10;
    actual.before = 'L'; actual.after = 'R';
    /* The independent host oracle can multiply the complete 32-bit value
     * in 64 bits; the target implementation must split it to stay safe. */
    rounded = ((uint64_t)value * base + scale / 2) / scale;
    sprintf(expected, "%lu.%0*u", (unsigned long)(rounded / base),
        digits, (unsigned int)(rounded % base));
    fixed(actual.text, value, scale, digits);
    if (strcmp(actual.text, expected)) {
        fprintf(stderr, "%lu / %lu digits %d: %s != %s\n",
            value, scale, digits, actual.text, expected);
        assert(0);
    }
    assert(actual.before == 'L' && actual.after == 'R');
    cases++;
}

int main(void)
{
    unsigned long scales[] = {16, 1024};
    unsigned long q[] = {0, 1, 4095, 65535, 65536, 0};
    unsigned long value, remainder, scale;
    unsigned int i, j;
    int digits;
    for (i = 0; i < 2; i++) {
        scale = scales[i];
        q[5] = 4294967295UL / scale;
        for (digits = 1; digits <= 2; digits++) {
            /* Every possible fractional residue at varied integer sizes,
             * including the highest complete 32-bit quotient. */
            for (j = 0; j < sizeof(q) / sizeof(q[0]); j++)
                for (remainder = 0; remainder < scale; remainder++) {
                    value = q[j] * scale + remainder;
                    if (value <= 4294967295UL) check(value, scale, digits);
                }
            for (value = 0; value <= 65535UL; value++)
                check(value, scale, digits);
            check(4294967295UL, scale, digits);
        }
    }
    check(0UL, 1UL, 1);
    check(4294967295UL, 1UL, 2);
    printf("fixed-point oracle passed %lu independent 64-bit cases\n", cases);
    return 0;
}
'''.replace("HELPER", helper)
with tempfile.TemporaryDirectory(prefix="top-fixed-oracle-") as temp:
    temp = Path(temp)
    (temp / "test.c").write_text(test)
    subprocess.run(["cc", "-std=gnu89", "-Wno-deprecated-non-prototype",
                    str(temp / "test.c"), "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True)
