/* Exercise actual compact cells, colour APIs, SGR and embedded styles. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux-full.h"

#ifndef TMUX_MILESTONE
#error This regression requires the compact milestone profile.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

static void
check_fields(void)
{
	struct grid_cell cell, copy;
	struct grid *grid;
	const struct grid_cell *stored;
	unsigned int attr, flags, data, colour;

	assert(sizeof(struct grid_cell) == 4);
	grid = grid_create(1, 1, 0);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	for (attr = 0; attr < 256; attr++) {
		cell.attr = attr;
		for (flags = 0; flags < 16; flags++) {
			cell.flags = flags;
			for (data = 0; data < 256; data++) {
				cell.data = data;
				memcpy(&copy, &cell, sizeof copy);
				assert(copy.attr == attr && copy.flags == flags);
				assert(copy.data == data && copy.fg == 8 && copy.bg == 8);
			}
		}
	}
	for (colour = 0; colour < 16; colour++) {
		cell.fg = colour;
		cell.bg = 15 - colour;
		grid_set_cell(grid, 0, 0, &cell);
		stored = grid_peek_cell(grid, 0, 0);
		assert(stored->fg == colour && stored->bg == 15 - colour);
		assert(stored->attr == 255 && stored->flags == 15);
		assert(stored->data == 255);
	}
	grid_destroy(grid);
}

static void
check_colour_api_and_styles(void)
{
	struct grid_cell cell, defaults;
	unsigned int colour, expected;
	char style[64];

	memcpy(&defaults, &grid_default_cell, sizeof defaults);
	for (colour = 0; colour < 256; colour++) {
		expected = colour_256to16((u_char)colour);
		memcpy(&cell, &defaults, sizeof cell);
		cell.attr = 255;
		cell.flags = GRID_FLAG_PADDING|GRID_FLAG_UTF8;
		cell.data = 255;
		colour_set_fg(&cell, colour | 0x100);
		colour_set_bg(&cell, colour | 0x100);
		assert(cell.fg == expected && cell.bg == expected);
		assert(cell.flags == 15 && cell.attr == 255 && cell.data == 255);

		snprintf(style, sizeof style, "fg=colour%u,bg=colour%u", colour, colour);
		memcpy(&cell, &defaults, sizeof cell);
		screen_write_parsestyle(&defaults, &cell, style);
		assert(cell.fg == expected && cell.bg == expected);
		assert(cell.flags == (GRID_FLAG_FG256|GRID_FLAG_BG256));
	}
	for (colour = 0; colour <= 8; colour++) {
		colour_set_fg(&cell, colour);
		colour_set_bg(&cell, colour);
		assert(cell.fg == colour && cell.bg == colour);
		assert(!(cell.flags & (GRID_FLAG_FG256|GRID_FLAG_BG256)));
	}

	/* Palette black/bright black must remain distinct from terminal default. */
	colour_set_fg(&cell, 0x108);
	colour_set_bg(&cell, 0x108);
	screen_write_parsestyle(&defaults, &cell, "fg=default,bg=default");
	assert(cell.fg == 8 && cell.bg == 8);
	assert(!(cell.flags & (GRID_FLAG_FG256|GRID_FLAG_BG256)));
	colour_set_fg(&cell, 0x108);
	colour_set_bg(&cell, 0x108);
	screen_write_parsestyle(&defaults, &cell, "default");
	assert(cell.fg == 8 && cell.bg == 8);
	assert(!(cell.flags & (GRID_FLAG_FG256|GRID_FLAG_BG256)));

	colour_set_fg(&defaults, 0x108);
	colour_set_bg(&defaults, 0x109);
	memcpy(&cell, &grid_default_cell, sizeof cell);
	screen_write_parsestyle(&defaults, &cell, "fg=default,bg=default");
	assert(cell.fg == 8 && cell.bg == 9);
	assert(cell.flags == (GRID_FLAG_FG256|GRID_FLAG_BG256));
	memcpy(&cell, &grid_default_cell, sizeof cell);
	screen_write_parsestyle(&defaults, &cell, "default");
	assert(cell.fg == 8 && cell.bg == 9);
	assert(cell.flags == (GRID_FLAG_FG256|GRID_FLAG_BG256));
}

static void
feed(struct window_pane *pane, const char *bytes)
{
	buffer_write(pane->in, bytes, strlen(bytes));
	input_parse(pane);
}

static void
check_sgr(void)
{
	struct window_pane pane;
	struct window window;
	const struct grid_cell *cell;
	unsigned int colour, expected, fg_sgr, bg_sgr;
	char sequence[80];

	memset(&pane, 0, sizeof pane);
	memset(&window, 0, sizeof window);
	options_init(&window.options, NULL);
	options_set_number(&window.options, "utf8", 0);
	window.sx = pane.sx = 1;
	window.sy = pane.sy = 1;
	pane.window = &window;
	pane.screen = &pane.base;
	pane.flags = PANE_REDRAW;
	pane.fd = -1;
	pane.in = buffer_create(64);
	pane.out = buffer_create(64);
	screen_init(&pane.base, 1, 1, 0);
	input_init(&pane);
	for (colour = 0; colour < 256; colour++) {
		expected = colour_256to16((u_char)colour);
		snprintf(sequence, sizeof sequence,
		    "\033[H\033[0;38;5;%u;48;5;%umX", colour, colour);
		feed(&pane, sequence);
		cell = grid_view_peek_cell(pane.base.grid, 0, 0);
		assert(cell->data == 'X');
		assert(cell->fg == expected && cell->bg == expected);
		assert(cell->flags == (GRID_FLAG_FG256|GRID_FLAG_BG256));
	}
	for (colour = 0; colour < 16; colour++) {
		fg_sgr = colour < 8 ? 30 + colour : 90 + colour - 8;
		bg_sgr = colour < 8 ? 40 + colour : 100 + colour - 8;
		snprintf(sequence, sizeof sequence,
		    "\033[H\033[0;%u;%umX", fg_sgr, bg_sgr);
		feed(&pane, sequence);
		cell = grid_view_peek_cell(pane.base.grid, 0, 0);
		assert(cell->fg == colour && cell->bg == colour);
		assert(cell->flags == (colour < 8 ? 0 :
		    GRID_FLAG_FG256|GRID_FLAG_BG256));
	}
	feed(&pane, "\033[H\033[90;100;39;49mX");
	cell = grid_view_peek_cell(pane.base.grid, 0, 0);
	assert(cell->fg == 8 && cell->bg == 8);
	assert(!(cell->flags & (GRID_FLAG_FG256|GRID_FLAG_BG256)));
	input_free(&pane);
	screen_free(&pane.base);
	buffer_destroy(pane.in);
	buffer_destroy(pane.out);
	options_free(&window.options);
}

int
main(void)
{
	check_fields();
	check_colour_api_and_styles();
	check_sgr();
	puts("four-byte cells, all attributes/flags/data, 16 colours and 256-colour mappings passed");
	return (0);
}
