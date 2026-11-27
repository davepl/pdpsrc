/*
 * Compact milestone rows: one character byte per cell, with the three style
 * bytes per cell allocated only for rows holding a non-default style. These
 * checks use the real grid objects through the ordinary grid interface.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmux-full.h"

#ifndef GRID_COMPACT_ROWS
#error This regression requires the compact milestone rows.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

static void
put(struct grid *grid, u_int x, u_int y, u_int attr, u_int flags, u_int fg,
    u_int bg, u_int data)
{
	struct grid_cell cell;

	memset(&cell, 0, sizeof cell);
	cell.attr = attr;
	cell.flags = flags;
	cell.fg = fg;
	cell.bg = bg;
	cell.data = data;
	grid_set_cell(grid, x, y, &cell);
}

static void
plain(struct grid *grid, u_int x, u_int y, u_int data)
{
	put(grid, x, y, 0, 0, 8, 8, data);
}

static void
expect(struct grid *grid, u_int x, u_int y, u_int attr, u_int flags, u_int fg,
    u_int bg, u_int data)
{
	struct grid_cell cell;

	/* A peeked cell is only valid until the next peek: copy it. */
	memcpy(&cell, grid_peek_cell(grid, x, y), sizeof cell);
	assert(cell.attr == attr && cell.flags == flags);
	assert(cell.fg == fg && cell.bg == bg);
	assert(cell.data == data);
}

static void
expect_plain(struct grid *grid, u_int x, u_int y, u_int data)
{
	expect(grid, x, y, 0, 0, 8, 8, data);
}

/* Two dense plain 80x23 screens are the native acceptance workload. */
static void
check_dense_plain_screens(void)
{
	struct grid *grid;
	u_int x, y;

	grid = grid_create(80, 23, 0);
	for (y = 0; y < 23; y++) {
		assert(grid->linedata[y].chardata == NULL);
		for (x = 0; x < 80; x++)
			plain(grid, x, y, 'A' + (x + y) % 26);
		assert(grid->linedata[y].cellsize == 80);
		assert(grid->linedata[y].chardata != NULL);
		assert(grid->linedata[y].styledata == NULL);
	}
	for (y = 0; y < 23; y++) {
		for (x = 0; x < 80; x++)
			expect_plain(grid, x, y, 'A' + (x + y) % 26);
	}
	/* Beyond the stored cells every position reads as the default cell. */
	grid_clear(grid, 40, 0, 40, 1);
	assert(grid->linedata[0].cellsize == 40);
	expect_plain(grid, 39, 0, 'A' + 39 % 26);
	expect_plain(grid, 40, 0, ' ');
	expect_plain(grid, 79, 0, ' ');
	grid_destroy(grid);
}

static void
check_lazy_style(void)
{
	struct grid *grid;
	u_int x;

	grid = grid_create(80, 1, 0);
	for (x = 0; x < 10; x++)
		plain(grid, x, 0, 'a' + x);
	assert(grid->linedata[0].styledata == NULL);

	/* The first styled cell creates the array; other cells stay default. */
	put(grid, 5, 0, GRID_ATTR_REVERSE, 0, 8, 8, 'X');
	assert(grid->linedata[0].styledata != NULL);
	for (x = 0; x < 10; x++) {
		if (x == 5)
			expect(grid, x, 0, GRID_ATTR_REVERSE, 0, 8, 8, 'X');
		else
			expect_plain(grid, x, 0, 'a' + x);
	}

	/* Truncation keeps the array while a styled cell remains... */
	grid_clear(grid, 6, 0, 74, 1);
	assert(grid->linedata[0].cellsize == 6);
	assert(grid->linedata[0].styledata != NULL);
	expect(grid, 5, 0, GRID_ATTR_REVERSE, 0, 8, 8, 'X');
	/* ...and releases it as soon as none does. */
	grid_clear(grid, 5, 0, 75, 1);
	assert(grid->linedata[0].cellsize == 5);
	assert(grid->linedata[0].styledata == NULL);
	for (x = 0; x < 5; x++)
		expect_plain(grid, x, 0, 'a' + x);

	/* Overwriting the only styled cell leaves a harmless all-default array. */
	put(grid, 2, 0, 0, 0, 1, 8, 'c');
	assert(grid->linedata[0].styledata != NULL);
	plain(grid, 2, 0, 'c');
	expect_plain(grid, 2, 0, 'c');
	grid_clear(grid, 4, 0, 76, 1);
	assert(grid->linedata[0].styledata == NULL);

	/* Clearing inside a styled row resets style as well as data. */
	put(grid, 1, 0, GRID_ATTR_BRIGHT, 0, 2, 3, 'q');
	put(grid, 3, 0, GRID_ATTR_BRIGHT, 0, 2, 3, 'r');
	grid_clear(grid, 1, 0, 1, 1);
	expect_plain(grid, 1, 0, ' ');
	expect(grid, 3, 0, GRID_ATTR_BRIGHT, 0, 2, 3, 'r');
	grid_clear_lines(grid, 0, 1);
	assert(grid->linedata[0].chardata == NULL);
	assert(grid->linedata[0].styledata == NULL);
	assert(grid->linedata[0].cellsize == 0);
	grid_destroy(grid);
}

static void
check_round_trip(void)
{
	static const u_int attrs[] = { 0, 1, 0x10, 0x80, 0xa5, 0xff };
	static const u_int datas[] = { 0, ' ', 'Z', 0x7f, 0x80, 0xff };
	struct grid *grid;
	u_int a, d, flags, fg, bg, x;

	grid = grid_create(96, 1, 0);
	x = 0;
	for (a = 0; a < nitems(attrs); a++) {
		for (d = 0; d < nitems(datas); d++) {
			for (flags = 0; flags < 16; flags++) {
				for (fg = 0; fg < 16; fg++) {
					bg = (fg * 7 + flags + a) & 15;
					put(grid, x, 0, attrs[a], flags, fg, bg,
					    datas[d]);
					expect(grid, x, 0, attrs[a], flags, fg,
					    bg, datas[d]);
					x = (x + 37) % 96;
				}
			}
		}
	}
	grid_destroy(grid);
}

/* Growth across allocation steps keeps both arrays in step. */
static void
check_styled_growth(void)
{
	struct grid *grid;
	u_int x;

	grid = grid_create(200, 1, 0);
	put(grid, 0, 0, GRID_ATTR_UNDERSCORE, GRID_FLAG_FG256, 9, 4, 'u');
	plain(grid, 150, 0, 'p');
	assert(grid->linedata[0].cellsize == 151);
	put(grid, 199, 0, GRID_ATTR_ITALICS, GRID_FLAG_BG256, 0, 15, 'i');
	expect(grid, 0, 0, GRID_ATTR_UNDERSCORE, GRID_FLAG_FG256, 9, 4, 'u');
	for (x = 1; x < 150; x++)
		expect_plain(grid, x, 0, ' ');
	expect_plain(grid, 150, 0, 'p');
	for (x = 151; x < 199; x++)
		expect_plain(grid, x, 0, ' ');
	expect(grid, 199, 0, GRID_ATTR_ITALICS, GRID_FLAG_BG256, 0, 15, 'i');
	grid_destroy(grid);
}

static void
check_move_cells(void)
{
	struct grid *grid;

	grid = grid_create(80, 2, 0);
	/* A plain row moves without gaining a style array. */
	plain(grid, 0, 0, 'a');
	plain(grid, 1, 0, 'b');
	plain(grid, 2, 0, 'c');
	grid_move_cells(grid, 5, 0, 0, 3);
	assert(grid->linedata[0].styledata == NULL);
	expect_plain(grid, 0, 0, ' ');
	expect_plain(grid, 2, 0, ' ');
	expect_plain(grid, 5, 0, 'a');
	expect_plain(grid, 7, 0, 'c');

	/* A styled row moves style with data, overlapping in both directions. */
	plain(grid, 0, 1, 'a');
	put(grid, 1, 1, GRID_ATTR_REVERSE, 0, 3, 5, 'b');
	plain(grid, 2, 1, 'c');
	grid_move_cells(grid, 1, 0, 1, 3);
	expect_plain(grid, 0, 1, ' ');
	expect_plain(grid, 1, 1, 'a');
	expect(grid, 2, 1, GRID_ATTR_REVERSE, 0, 3, 5, 'b');
	expect_plain(grid, 3, 1, 'c');
	grid_move_cells(grid, 0, 1, 1, 3);
	expect_plain(grid, 0, 1, 'a');
	expect(grid, 1, 1, GRID_ATTR_REVERSE, 0, 3, 5, 'b');
	expect_plain(grid, 2, 1, 'c');
	expect_plain(grid, 3, 1, ' ');
	grid_destroy(grid);
}

static void
check_duplicate_and_compare(void)
{
	struct grid *source, *copy;
	u_int x;

	source = grid_create(80, 3, 0);
	copy = grid_create(80, 3, 0);
	for (x = 0; x < 80; x++)
		plain(source, x, 0, '0' + x % 10);
	for (x = 0; x < 33; x++)
		put(source, x, 1, x & 0xff, x & 7, x & 15, 15 - (x & 15), 'A' + x % 26);
	/* Row 2 stays unallocated. Flag 8 (UTF-8) would need UTF-8 data. */
	assert(grid_compare(source, copy) != 0);
	grid_duplicate_lines(copy, 0, source, 0, 3);
	assert(grid_compare(source, copy) == 0);
	assert(copy->linedata[0].chardata != source->linedata[0].chardata);
	assert(copy->linedata[0].styledata == NULL);
	assert(copy->linedata[1].styledata != NULL);
	assert(copy->linedata[1].styledata != source->linedata[1].styledata);
	assert(copy->linedata[2].chardata == NULL);
	for (x = 0; x < 33; x++)
		expect(copy, x, 1, x & 0xff, x & 7, x & 15, 15 - (x & 15), 'A' + x % 26);

	/* The copy is independent of its source. */
	plain(copy, 3, 0, '!');
	expect_plain(source, 3, 0, '3');
	assert(grid_compare(source, copy) != 0);
	plain(copy, 3, 0, '3');
	assert(grid_compare(source, copy) == 0);

	/* An all-default style array is equal to having none. */
	put(copy, 7, 0, GRID_ATTR_DIM, 0, 8, 8, '7');
	assert(copy->linedata[0].styledata != NULL);
	assert(grid_compare(source, copy) != 0);
	plain(copy, 7, 0, '7');
	assert(copy->linedata[0].styledata != NULL);
	assert(source->linedata[0].styledata == NULL);
	assert(grid_compare(source, copy) == 0);
	assert(grid_compare(copy, source) == 0);

	/* Style differences alone are differences. */
	put(copy, 9, 1, 9, 1, 9, 7, 'A' + 9);
	assert(grid_compare(source, copy) != 0);
	grid_destroy(source);
	grid_destroy(copy);
}

/* Zero-history scrolling: freed rows move and new rows start unallocated. */
static void
check_scroll(void)
{
	struct grid *grid;
	u_char *second, *third;
	u_int x, y;

	grid = grid_create(80, 3, 0);
	for (y = 0; y < 3; y++) {
		for (x = 0; x < 80; x++)
			plain(grid, x, y, '1' + y);
	}
	put(grid, 0, 1, GRID_ATTR_BRIGHT, 0, 8, 8, '2');
	second = grid->linedata[1].chardata;
	third = grid->linedata[2].chardata;
	grid_move_lines(grid, 0, 1, 2);
	assert(grid->linedata[0].chardata == second);
	assert(grid->linedata[0].styledata != NULL);
	assert(grid->linedata[1].chardata == third);
	assert(grid->linedata[1].styledata == NULL);
	assert(grid->linedata[2].chardata == NULL);
	assert(grid->linedata[2].styledata == NULL);
	assert(grid->linedata[2].cellsize == 0);
	expect(grid, 0, 0, GRID_ATTR_BRIGHT, 0, 8, 8, '2');
	expect_plain(grid, 79, 0, '2');
	expect_plain(grid, 0, 1, '3');
	expect_plain(grid, 0, 2, ' ');
	plain(grid, 0, 2, '4');
	expect_plain(grid, 0, 2, '4');
	grid_destroy(grid);
}

/* The screen writer's wide-character and combining paths use peek and set. */
static void
check_screen_writer(void)
{
	struct screen screen;
	struct screen_write_ctx ctx;
	struct grid_cell cell;
	struct utf8_data wide, combining;
	const struct grid_utf8 *stored;

	screen_init(&screen, 20, 2, 0);
	screen_write_start(&ctx, NULL, &screen);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.data = 'a';
	screen_write_cell(&ctx, &cell, NULL);
	assert(screen.grid->linedata[0].styledata == NULL);

	/* U+4E2D is two columns wide: its second cell becomes padding. */
	memset(&wide, 0, sizeof wide);
	wide.data[0] = 0xe4;
	wide.data[1] = 0xb8;
	wide.data[2] = 0xad;
	wide.size = wide.have = 3;
	wide.width = 2;
	cell.flags |= GRID_FLAG_UTF8;
	screen_write_cell(&ctx, &cell, &wide);
	assert(screen.cx == 3);
	expect_plain(screen.grid, 0, 0, 'a');
	memcpy(&cell, grid_view_peek_cell(screen.grid, 1, 0), sizeof cell);
	assert(cell.flags == GRID_FLAG_UTF8);
	memcpy(&cell, grid_view_peek_cell(screen.grid, 2, 0), sizeof cell);
	assert(cell.flags & GRID_FLAG_PADDING);

	/* Overwriting the wide character clears its padding cell. */
	screen_write_cursormove(&ctx, 1, 0);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.data = 'b';
	screen_write_cell(&ctx, &cell, NULL);
	expect_plain(screen.grid, 1, 0, 'b');
	expect_plain(screen.grid, 2, 0, ' ');

	/* U+0301 combines onto the previous plain cell, converting it to UTF-8. */
	memset(&combining, 0, sizeof combining);
	combining.data[0] = 0xcc;
	combining.data[1] = 0x81;
	combining.size = combining.have = 2;
	combining.width = 0;
	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.flags |= GRID_FLAG_UTF8;
	screen_write_cell(&ctx, &cell, &combining);
	memcpy(&cell, grid_view_peek_cell(screen.grid, 1, 0), sizeof cell);
	assert(cell.flags == GRID_FLAG_UTF8 && cell.data == 'b');
	stored = grid_view_peek_utf8(screen.grid, 1, 0);
	assert(stored != NULL && stored->data[0] == 'b');
	assert(stored->data[1] == 0xcc && stored->data[2] == 0x81);
	screen_write_stop(&ctx);
	screen_free(&screen);
}

int
main(void)
{
	grid_init_default();
	check_dense_plain_screens();
	check_lazy_style();
	check_round_trip();
	check_styled_growth();
	check_move_cells();
	check_duplicate_and_compare();
	check_scroll();
	check_screen_writer();
	puts("compact rows: lazy styles, round trips, growth, moves, copies and writer paths passed");
	return (0);
}
