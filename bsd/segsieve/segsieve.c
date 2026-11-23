/* Segmented odd-only sieve for 2.11BSD. Bit k represents 2*k + 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEG_BYTES 4096
#define SEG_BITS 32768U
/* floor(sqrt(50,000,000)) is 7071. There are 907 odd primes through it. */
#define ROOT_LIMIT 7071
#define MAX_PRIMES 907

struct strike {
    unsigned short prime;
    unsigned short next;
};

static struct strike active[MAX_PRIMES];
static unsigned char seed[ROOT_LIMIT / 16 + 1];
static unsigned char mask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static unsigned char pop4[16] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};

/* All positions, including the first strike beyond the segment, fit in
   an unsigned 16-bit int: the largest possible sum is 32767 + 7071. */
static unsigned int mark(unsigned char *bits, unsigned int n,
                         unsigned int j, unsigned int p)
{
    unsigned int lane, carry;
    register unsigned int byte, lane_mask;

    /* For dense strikes, use the original sieve's eight fixed-mask lanes.
       The first lane's overshoot is at most eight strides; reducing it
       gives the next strike for all lanes without a divide. */
    if (n == SEG_BITS && p < 64) {
        carry = 0;
        for (lane = 0; lane < 8; lane++, j += p) {
            lane_mask = mask[j & 7];
            for (byte = j >> 3; byte < SEG_BYTES; byte += p)
                bits[byte] |= lane_mask;
            if (lane == 0)
                carry = ((byte - SEG_BYTES) << 3) + (j & 7);
        }
        while (carry >= p)
            carry -= p;
        return carry;
    }

    while (j < n) {
        bits[j >> 3] |= mask[j & 7];
        j += p;
    }
    return j - n;
}

int sieve_of_eratosthenes(long limit, int print_primes, long *count_ptr)
{
    unsigned char *bits, *seed_bits;
    unsigned int n, bytes, p, used, i, b, composites;
    long total, base, end, square, count;

    total = (limit + 1L) >> 1;
    bytes = total < SEG_BITS ? (unsigned int)((total + 7) >> 3) : SEG_BYTES;
    bits = (unsigned char *)calloc(bytes, 1);
    if (bits == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }

    count = 1;                    /* 2 is the only even prime. */
    if (print_primes)
        printf("2 ");
    bits[0] = 1;                  /* Exclude 1. */
    p = 3;
    square = 4;                   /* odd-bit index of p*p */
    used = 0;
    seed_bits = bits;

    for (base = 0; base < total; base = end) {
        n = total - base < SEG_BITS ? (unsigned int)(total - base) : SEG_BITS;
        end = base + n;
        bytes = (n + 7U) >> 3;
        if (base != 0)
            memset(bits, 0, bytes);

        for (i = 0; i < used; i++)
            active[i].next = mark(bits, n, active[i].next, active[i].prime);

        /* Activate each prime in the segment containing its square.
           Advancing p by two advances the odd-bit index by 2*p + 2. */
        while (square < end) {
            if (!(seed_bits[p >> 4] & mask[(p >> 1) & 7])) {
                active[used].prime = p;
                active[used].next = mark(bits, n, (unsigned int)(square - base), p);
                used++;
            }
            square += p + p + 2;
            p += 2;
        }

        /* Every possible base prime is in segment zero. Preserve just
           that prefix before reusing the bitmap for subsequent segments. */
        if (base == 0 && end < total) {
            memcpy(seed, bits, sizeof(seed));
            seed_bits = seed;
        }

        composites = 0;
        for (i = 0; i < bytes; i++) {
            b = bits[i];
            composites += pop4[b & 15] + pop4[b >> 4];
        }
        count += n - composites;  /* unused bits in the tail are zero */

        if (print_primes) {
            for (i = 0; i < n; i++)
                if (!(bits[i >> 3] & mask[i & 7]))
                    printf("%ld ", ((base + i) << 1) + 1L);
        }
    }
    if (print_primes)
        printf("\n");
    free(bits);
    *count_ptr = count;
    return 1;
}
