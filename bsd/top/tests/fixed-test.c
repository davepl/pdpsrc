/*
 * Golden decimal results for the production fixed-point formatter.
 * No expected value is calculated with the algorithm under test.
 *
 * PDP-11 unsigned long has 32 bits. A modern host may use 64, so all input
 * values stay within 0xffffffff and all scales stay within the native
 * call sites' range, except deliberate safe long-divisor boundaries.
 * These cases detect rounding errors, lost carries and truncated results. A 64-bit host cannot detect every
 * overflow that would occur on a 32-bit long; native execution checks
 * that ABI separately.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "fixed-under-test.h"

/* scale must be positive; zero has no defined fixed-point result.
 * Production scales are at most 32767. Extra long-divisor cases verify
 * the unsigned arithmetic beyond native int range while its intermediate
 * still fits 32 bits. With two digits, the largest universally safe scale
 * is 42735993: (scale-1)*100 + scale/2 = 4294967196. The next scale can
 * overflow, so it is deliberately outside this test's valid domain. */
struct example {
    unsigned long value, scale;
    int digits;
    char *expected;
};

static struct example examples[] = {
    { 0UL, 1UL, 1, "0.0" },
    { 0UL, 100UL, 2, "0.00" },
    { 994UL, 100UL, 1, "9.9" },
    { 995UL, 100UL, 1, "10.0" },
    { 996UL, 100UL, 1, "10.0" },
    { 994UL, 1000UL, 2, "0.99" },
    { 995UL, 1000UL, 2, "1.00" },
    { 1UL, 60UL, 2, "0.02" },
    { 59UL, 60UL, 2, "0.98" },
    { 60UL, 60UL, 2, "1.00" },
    { 5999UL, 60UL, 2, "99.98" },
    { 100UL, 100UL, 2, "1.00" },
    { 3UL, 16UL, 1, "0.2" },
    { 4UL, 16UL, 1, "0.3" },
    { 8UL, 16UL, 1, "0.5" },
    { 15UL, 16UL, 1, "0.9" },
    { 16UL, 16UL, 1, "1.0" },
    { 65535UL, 16UL, 1, "4095.9" },
    { 1UL, 1024UL, 1, "0.0" },
    { 512UL, 1024UL, 1, "0.5" },
    { 1536UL, 1024UL, 1, "1.5" },
    { 4095UL, 1024UL, 1, "4.0" },
    { 2147483648UL, 100UL, 2, "21474836.48" },
    { 4294967295UL, 100UL, 2, "42949672.95" },
    { 4294967295UL, 60UL, 2, "71582788.25" },
    { 4294967295UL, 1024UL, 1, "4194304.0" },
    { 4294967295UL, 1UL, 2, "4294967295.00" },
    { 32766UL, 32767UL, 2, "1.00" },
    { 4294967295UL, 32767UL, 2, "131076.00" },
    { 1UL, 1UL, 1, "1.0" },
    { 0UL, 1UL, 2, "0.00" },
    { 1UL, 1UL, 2, "1.00" },
    { 0UL, 2UL, 1, "0.0" },
    { 2UL, 2UL, 1, "1.0" },
    { 0UL, 2UL, 2, "0.00" },
    { 2UL, 2UL, 2, "1.00" },
    { 0UL, 3UL, 1, "0.0" },
    { 3UL, 3UL, 1, "1.0" },
    { 0UL, 3UL, 2, "0.00" },
    { 3UL, 3UL, 2, "1.00" },
    { 0UL, 60UL, 1, "0.0" },
    { 60UL, 60UL, 1, "1.0" },
    { 0UL, 60UL, 2, "0.00" },
    { 0UL, 1024UL, 1, "0.0" },
    { 1024UL, 1024UL, 1, "1.0" },
    { 0UL, 1024UL, 2, "0.00" },
    { 1024UL, 1024UL, 2, "1.00" },
    { 0UL, 32767UL, 1, "0.0" },
    { 32767UL, 32767UL, 1, "1.0" },
    { 0UL, 32767UL, 2, "0.00" },
    { 32767UL, 32767UL, 2, "1.00" },
    { 65534UL, 1UL, 1, "65534.0" },
    { 65535UL, 1UL, 1, "65535.0" },
    { 65536UL, 1UL, 1, "65536.0" },
    { 100000UL, 1UL, 1, "100000.0" },
    { 6553594UL, 100UL, 1, "65535.9" },
    { 6553596UL, 100UL, 1, "65536.0" },
    { 1UL, 20UL, 1, "0.1" },
    { 19UL, 20UL, 1, "1.0" },
    { 1UL, 200UL, 2, "0.01" },
    { 199UL, 200UL, 2, "1.00" },
    { 1UL, 3UL, 1, "0.3" },
    { 2UL, 3UL, 1, "0.7" },
    { 1UL, 3UL, 2, "0.33" },
    { 2UL, 3UL, 2, "0.67" },
    { 1UL, 21UL, 1, "0.0" },
    { 1UL, 19UL, 1, "0.1" },
    { 1UL, 199UL, 2, "0.01" },
    { 1UL, 201UL, 2, "0.00" },
    { 12UL, 16UL, 1, "0.8" },
    { 3UL, 60UL, 1, "0.1" },
    { 57UL, 60UL, 1, "1.0" },
    { 51UL, 1024UL, 1, "0.0" },
    { 52UL, 1024UL, 1, "0.1" },
    { 972UL, 1024UL, 1, "0.9" },
    { 973UL, 1024UL, 1, "1.0" },
    { 5UL, 1024UL, 2, "0.00" },
    { 6UL, 1024UL, 2, "0.01" },
    { 1023UL, 1024UL, 2, "1.00" },
    { 2147418112UL, 32767UL, 2, "65536.00" },
    { 4294836225UL, 65535UL, 2, "65535.00" },
    { 4294901760UL, 65536UL, 2, "65535.00" },
    { 4294967295UL, 65537UL, 2, "65535.00" },
    { 32767UL, 32768UL, 2, "1.00" },
    { 65534UL, 65535UL, 2, "1.00" },
    { 65535UL, 65536UL, 2, "1.00" },
    { 42735992UL, 42735993UL, 2, "1.00" },
    { 4294967295UL, 42735993UL, 2, "100.50" }
};

int main(void)
{
    char storage[40], *result;
    unsigned i, j;
    struct example *e;
    for (i = 0; i < sizeof(examples) / sizeof(examples[0]); i++) {
        e = &examples[i];
        /* Production callers allow 24 bytes per field. Guard both sides. */
        memset(storage, 'Z', sizeof(storage));
        result = fixed(storage + 8, e->value, e->scale, e->digits);
        assert(result == storage + 8);
        assert(memchr(result, 0, 24) != NULL);
        if (strcmp(result, e->expected)) {
            fprintf(stderr, "fixed(%lu, %lu, %d): expected %s, got %s\n",
                e->value, e->scale, e->digits, e->expected, result);
            return 1;
        }
        for (j = 0; j < 8; j++) {
            assert(storage[j] == 'Z');
            assert(storage[32 + j] == 'Z');
        }
    }
    printf("fixed-point tests passed: %u golden cases and buffer guards\n", i);
    return 0;
}
