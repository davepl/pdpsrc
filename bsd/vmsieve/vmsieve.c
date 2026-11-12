/* File-backed odd-only sieve for 2.11BSD. Bit k represents 2*k + 1. */
#include <stdio.h>
#include <stdlib.h>
#include <vmf.h>

#define PAGE_BITS (8U * BYTESPERSEG)
#define CACHE_PAGES 8
#define BATCH_SIZE 128

struct strike {
    long next;                   /* absolute bit index of the next multiple */
    unsigned int prime;
};

static unsigned char mask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static unsigned char pop4[16] = {
    0, 1, 1, 2, 1, 2, 2, 3,
    1, 2, 2, 3, 2, 3, 3, 4
};

/* Translate a flat byte offset into a 512-page VM space and a page in it. */
static struct vseg *map_page(struct vspace *spaces, VADDR address)
{
    unsigned int page = VSEG(address);
    return vmmapseg(&spaces[page / MAXSEGNO], page % MAXSEGNO);
}

/* Mark multiples within one mapped page. Even the overshoot fits in 16 bits. */
static unsigned int mark(unsigned char *bits, unsigned int n,
                         unsigned int bit, unsigned int prime)
{
    for (; bit < n; bit += prime)
        bits[bit >> 3] |= mask[bit & 7];
    return bit;
}

/* Each batch makes a sweep of the same bitmap. Grouping primes amortizes
   each disk access while leaving their positions in flat coordinates. */
static void strike_batch(struct vspace *spaces, long total,
                         struct strike *batch, int used)
{
    long base, end;
    struct vseg *page;
    unsigned int n, bit;
    int i;

    base = batch[0].next;
    for (i = 1; i < used; i++)
        if (batch[i].next < base)
            base = batch[i].next;
    base &= ~((long)PAGE_BITS - 1L);
    for (; base < total; base = end) {
        end = base + PAGE_BITS;
        if (end > total)
            end = total;
        n = (unsigned int)(end - base);
        page = map_page(spaces, base >> 3);
        for (i = 0; i < used; i++) {
            if (batch[i].next < end) {
                bit = (unsigned int)(batch[i].next - base);
                bit = mark((unsigned char *)page->s_cinfo, n, bit, batch[i].prime);
                batch[i].next = base + bit;
            }
        }
        VMMODIFY(page);
    }
}

static void fill_bitmap(struct vspace *spaces, long total)
{
    struct strike batch[BATCH_SIZE];
    struct vseg *first;
    unsigned char *bits;
    unsigned int n, p;
    long square, next;
    int used;

    /* At the 50M limit every sieving prime is in page zero (p <= 7071).
       Pin it so its candidate bits survive mappings made by strike_batch. */
    first = map_page(spaces, 0L);
    vmlock(first);
    bits = (unsigned char *)first->s_cinfo;
    bits[0] = 1;                 /* exclude 1 */
    VMMODIFY(first);
    n = total < PAGE_BITS ? (unsigned int)total : PAGE_BITS;
    used = 0;
    p = 3;
    square = 4;                  /* bit index of p*p */
    while (square < total) {
        if (!(bits[p >> 4] & mask[(p >> 1) & 7])) {
            next = square;
            if (square < n)
                next = mark(bits, n, (unsigned int)square, p);
            if (next < total) {
                batch[used].prime = p;
                batch[used].next = next;
                used++;
                if (used == BATCH_SIZE) {
                    strike_batch(spaces, total, batch, used);
                    used = 0;
                }
            }
        }
        square += p + p + 2;
        p += 2;
    }
    vmunlock(first);
    if (used)
        strike_batch(spaces, total, batch, used);
}

static long count_bitmap(struct vspace *spaces, long total, int print_primes)
{
    struct vseg *page;
    unsigned char *bits;
    unsigned int n, bytes, i, b, composites;
    long base, count;

    count = 1;                   /* 2 is the only even prime */
    if (print_primes)
        printf("2 ");
    for (base = 0; base < total; base += PAGE_BITS) {
        page = map_page(spaces, base >> 3);
        bits = (unsigned char *)page->s_cinfo;
        n = total - base < PAGE_BITS ? (unsigned int)(total - base) : PAGE_BITS;
        bytes = (n + 7U) >> 3;
        composites = 0;
        for (i = 0; i < bytes; i++) {
            b = bits[i];
            composites += pop4[b & 15] + pop4[b >> 4];
        }
        count += n - composites; /* unused bits in the last byte are zero */
        if (print_primes) {
            for (i = 0; i < n; i++)
                if (!(bits[i >> 3] & mask[i & 7]))
                    printf("%ld ", ((base + i) << 1) + 1L);
        }
    }
    if (print_primes)
        printf("\n");
    return count;
}

int sieve_of_eratosthenes(long limit, int print_primes, long *count_ptr)
{
    static int initialized;
    struct vspace *spaces;
    long total, last_byte;
    int nspaces, opened, ok;

    if (!initialized) {
        if (vminit(CACHE_PAGES) < 0) {
            perror("vminit");
            return 0;
        }
        initialized = 1;
    }
    total = (limit + 1L) >> 1;
    last_byte = (total - 1L) >> 3;
    nspaces = VSEG(last_byte) / MAXSEGNO + 1;
    spaces = (struct vspace *)calloc(nspaces, sizeof(*spaces));
    if (spaces == NULL) {
        perror("calloc");
        return 0;
    }
    for (opened = 0; opened < nspaces; opened++) {
        if (vmopen(&spaces[opened], (char *)0) < 0) {
            perror("vmopen");
            break;
        }
    }
    ok = opened == nspaces;
    if (ok) {
        fill_bitmap(spaces, total);
        *count_ptr = count_bitmap(spaces, total, print_primes);
    }
    while (opened > 0)
        vmclose(&spaces[--opened]);
    free(spaces);
    return ok;
}
