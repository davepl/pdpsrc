/* Grid equality must not read absent cells or depend on cell padding. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmux-full.h"

static void
check_empty_and_partial(void)
{
	struct grid *a, *b, *tall;
	struct grid_cell cell, other;
	unsigned int length, x;

	a = grid_create(80, 1, 0);
	b = grid_create(80, 1, 0);
	tall = grid_create(80, 2, 0);
#ifdef GRID_COMPACT_ROWS
	assert(a->linedata[0].chardata == NULL && a->linedata[0].styledata == NULL);
	assert(b->linedata[0].chardata == NULL && b->linedata[0].styledata == NULL);
#else
	assert(a->linedata[0].celldata == NULL);
	assert(b->linedata[0].celldata == NULL);
#endif
	assert(grid_compare(a, b) == 0);
	assert(grid_compare(a, tall) != 0);
	assert(grid_compare(tall, a) != 0);
	grid_destroy(tall);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	for (length = 1; length <= 3; length += 2) {
		for (x = 0; x < length; x++) {
			cell.data = 'a' + x;
			grid_set_cell(a, x, 0, &cell);
			grid_set_cell(b, x, 0, &cell);
		}
		assert(a->linedata[0].cellsize == length);
		assert(b->linedata[0].cellsize == length);
		assert(grid_compare(a, b) == 0);
		memcpy(&other, &cell, sizeof other);
		other.data = '!';
		grid_set_cell(b, length - 1, 0, &other);
		assert(grid_compare(a, b) != 0);
		grid_set_cell(b, length - 1, 0, &cell);
		assert(grid_compare(a, b) == 0);
	}
	grid_set_cell(a, 79, 0, &cell);
	assert(grid_compare(a, b) != 0);
	grid_set_cell(b, 79, 0, &cell);
	assert(grid_compare(a, b) == 0);
	other.data = '?';
	grid_set_cell(b, 79, 0, &other);
	assert(grid_compare(a, b) != 0);
	grid_destroy(a);
	grid_destroy(b);
}

static void
check_hidden_after_resize(void)
{
	struct screen a, b;
	struct grid_cell cell;

	screen_init(&a, 80, 1, 0);
	screen_init(&b, 80, 1, 0);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.data = 'a';
	grid_set_cell(a.grid, 79, 0, &cell);
	cell.data = 'b';
	grid_set_cell(b.grid, 79, 0, &cell);
	assert(grid_compare(a.grid, b.grid) != 0);
	screen_resize(&a, 40, 1);
	screen_resize(&b, 40, 1);
	assert(a.grid->linedata[0].cellsize == 80);
	assert(b.grid->linedata[0].cellsize == 80);
	assert(grid_compare(a.grid, b.grid) == 0);
	screen_resize(&a, 80, 1);
	screen_resize(&b, 80, 1);
	assert(grid_compare(a.grid, b.grid) != 0);
	screen_free(&a);
	screen_free(&b);
}

#ifdef TMUX_MILESTONE
static void
check_padding(void)
{
	struct grid_cell used, a, b;
	struct grid *ga, *gb;
	unsigned char *mask, *bytes;
	size_t i;
	int changed;

	/* Discover the actual host/native allocation order, without endian guesses. */
	memset(&used, 0, sizeof used);
	used.attr = 255;
	used.flags = 15;
	used.fg = 15;
	used.bg = 15;
	used.data = 255;
	mask = (unsigned char *)&used;
	memcpy(&a, &grid_default_cell, sizeof a);
	memcpy(&b, &a, sizeof b);
	bytes = (unsigned char *)&b;
	changed = 0;
	for (i = 0; i < sizeof b; i++) {
		if ((unsigned char)~mask[i] != 0)
			changed = 1;
		bytes[i] ^= (unsigned char)~mask[i];
	}
	assert(changed && memcmp(&a, &b, sizeof a) != 0);
	assert(a.attr == b.attr && a.flags == b.flags);
	assert(a.fg == b.fg && a.bg == b.bg && a.data == b.data);
	ga = grid_create(80, 1, 0);
	gb = grid_create(80, 1, 0);
	grid_set_cell(ga, 0, 0, &a);
	grid_set_cell(gb, 0, 0, &b);
	assert(grid_compare(ga, gb) == 0);
	grid_destroy(ga);
	grid_destroy(gb);
}
#endif

int
main(void)
{
	check_empty_and_partial();
	check_hidden_after_resize();
#ifdef TMUX_MILESTONE
	check_padding();
#endif
	puts("grid comparison handles empty/partial rows, dimensions, hidden cells and padding");
	return (0);
}
