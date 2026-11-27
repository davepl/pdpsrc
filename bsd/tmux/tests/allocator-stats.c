/* Exercise the actual optional arena walker with 16-bit address values. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t arena[32768];
static unsigned int reads;

static uint16_t
arena_word(uintptr_t address)
{
	assert(address <= 65534U && !(address & 1));
	reads++;
	assert(reads <= 32767U);
	return (arena[address / 2]);
}

#define TMUX_ALLOC_STATS_WORD(address) arena_word(address)
#include "allocator-ring.c"

static void
put(unsigned int address, unsigned int next, unsigned int busy)
{
	assert(!(address & 1) && !(next & 1) && next <= 65534U);
	arena[address / 2] = (uint16_t)(next | busy);
}

static void
reset(void)
{
	memset(arena, 0, sizeof arena);
	reads = 0;
	put(0x100, 0x102, 1);
	put(0x102, 0x200, 1);
	put(0x200, 0x220, 0);
	put(0x220, 0x230, 1);
	put(0x230, 0x260, 0);
	put(0x260, 0x2fe, 1);
	put(0x2fe, 0x100, 1);
}

static int
walk(unsigned int start, struct xmalloc_stats *stats)
{
	reads = 0;
	return (xmalloc_stats_walk(0x100, 0x200, 0x300, start, stats));
}

static void
check_totals(const struct xmalloc_stats *stats)
{
	assert(stats->freebytes == 76);
	assert(stats->largestfree == 46);
	assert(stats->usedbytes == 170);
	assert(stats->freeblocks == 2);
	assert(stats->usedblocks == 2);
	assert(stats->steps == 7 && reads == 7);
}

int
main(void)
{
	struct xmalloc_stats stats;
	unsigned int i;

	reset();
	assert(walk(0x220, &stats));
	check_totals(&stats);
	assert(walk(0x260, &stats));
	check_totals(&stats);

	/* Separate free headers are counted accurately before lazy coalescing. */
	put(0x230, 0x240, 0);
	put(0x240, 0x260, 0);
	assert(walk(0x220, &stats));
	assert(stats.freebytes == 74 && stats.largestfree == 30);
	assert(stats.freeblocks == 3 && stats.usedbytes == 170);
	assert(stats.steps == 8);

	/* The allocator also represents noncontiguous sbrk gaps as busy blocks. */
	reset();
	put(0x230, 0x260, 1);
	assert(walk(0x220, &stats));
	assert(stats.freebytes == 30 && stats.largestfree == 30);
	assert(stats.usedbytes == 216 && stats.usedblocks == 3);

	reset();
	put(0x260, 0x400, 1); /* Above the break. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x260, 0x80, 1); /* Below the BSS. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x260, 0x100, 1); /* A premature wrap. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x2fe, 0x200, 1); /* Wrap into the heap. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x2fe, 0x100, 0); /* The top sentinel must be busy. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x100, 0x104, 1); /* Static headers must be adjacent. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x102, 0x200, 0); /* Static headers must stay busy. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x102, 0x104, 1); /* Exactly two static headers. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x102, 0x260, 1); /* Passing the anchor after wrap is invalid. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x220, 0x230, 0); /* Permanent anchor must remain live. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x260, 0x260, 1); /* Self-cycle. */
	assert(!walk(0x220, &stats));
	reset();
	put(0x260, 0x230, 1); /* Backwards heap cycle. */
	assert(!walk(0x220, &stats));

	reads = 0;
	assert(!xmalloc_stats_walk(0x200, 0x200, 0x300, 0x220, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x300, 0x300, 0x220, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x201, 0x300, 0x220, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x200, 0x301, 0x220, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x200, 0x10000, 0x220, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x200, 0x300, 0x221, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x200, 0x300, 0x102, &stats));
	assert(!xmalloc_stats_walk(0x100, 0x200, 0x300, 0x300, &stats));
	assert(reads == 0);

	/* The largest possible 16-bit ring terminates within the fixed bound. */
	memset(arena, 0, sizeof arena);
	put(0, 2, 1);
	put(2, 4, 1);
	for (i = 4; i < 65532U; i += 2)
		put(i, i + 2, 1);
	put(65532U, 0, 1);
	reads = 0;
	assert(xmalloc_stats_walk(0, 4, 65534U, 4, &stats));
	assert(stats.usedbytes == 0 && stats.freebytes == 0);
	assert(stats.usedblocks == 32764U && stats.freeblocks == 0);
	assert(stats.steps == 32767U && reads == 32767U);

	puts("optional allocator ring statistics: passed");
	return (0);
}
