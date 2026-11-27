/*
 * Compile the actual grid.c and rename only its
 * xmalloc/xcalloc/xrealloc/xfree calls to grid_capacity_xmalloc and friends.
 * Link that private object in place of grid.o with the ordinary host program
 * objects. The guarded hooks below deliberately move every reallocation.
 * Run with and without TMUX_NATIVE_GRID_ALLOC=1; pass the same profile flags
 * to this test and grid.c. Only horizontal resize is used, so ordinary
 * screen.c never reallocates a tracked grid-line array itself.
 *
 * Three storage models are covered: exact full-profile cells, 16-cell rounded
 * full-profile cells, and the milestone's compact rows (one character byte per
 * cell in GRID_ROW_QUANTUM steps, plus a lazily allocated style array).
 */
#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux-full.h"

#if defined(GRID_COMPACT_ROWS)
#define EXPECT_COMPACT 1
#define EXPECT_ROUNDED 1
#define ROW_STORAGE(line) ((void *)(line)->chardata)
#define ROW_UNIT 1U
#elif defined(TMUX_NATIVE_211BSD) || defined(TMUX_NATIVE_GRID_ALLOC)
#define EXPECT_COMPACT 0
#define EXPECT_ROUNDED 1
#define ROW_STORAGE(line) ((void *)(line)->celldata)
#define ROW_UNIT sizeof(struct grid_cell)
#else
#define EXPECT_COMPACT 0
#define EXPECT_ROUNDED 0
#define ROW_STORAGE(line) ((void *)(line)->celldata)
#define ROW_UNIT sizeof(struct grid_cell)
#endif
/* Expected count for the exact, 16-cell rounded and compact storage models. */
#define EXPECTED(exact, rounded, compact) \
	(EXPECT_COMPACT ? (compact) : EXPECT_ROUNDED ? (rounded) : (exact))
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

#define GUARD_SIZE 32
#define TRACKED_BLOCKS 256

struct allocation {
	unsigned char *raw;
	void *data;
	size_t bytes;
};
static struct allocation allocations[TRACKED_BLOCKS];
static unsigned int cell_reallocations;
static int forbid_allocations;

static struct allocation *
find_allocation(const void *data)
{
	size_t i;

	for (i = 0; i < nitems(allocations); i++) {
		if (allocations[i].raw != NULL && allocations[i].data == data)
			return (&allocations[i]);
	}
	assert(!"untracked pointer or double free in grid allocator");
	return (NULL);
}

static void
check_guard(const struct allocation *allocation)
{
	size_t i;

	for (i = 0; i < GUARD_SIZE; i++) {
		assert(allocation->raw[i] == 0xc3);
		assert(((unsigned char *)allocation->data)[allocation->bytes + i]
		    == 0xd7);
	}
}

static void
check_all_guards(void)
{
	size_t i;

	for (i = 0; i < nitems(allocations); i++) {
		if (allocations[i].raw != NULL)
			check_guard(&allocations[i]);
	}
}

static void
check_no_allocations(void)
{
	size_t i;

	for (i = 0; i < nitems(allocations); i++)
		assert(allocations[i].raw == NULL);
}

void *
grid_capacity_xmalloc(size_t bytes)
{
	struct allocation *allocation;
	size_t i;

	assert(!forbid_allocations);
	assert(bytes != 0 && bytes <= SIZE_MAX - 2 * GUARD_SIZE);
	for (i = 0; i < nitems(allocations); i++) {
		if (allocations[i].raw == NULL)
			break;
	}
	assert(i < nitems(allocations));
	allocation = &allocations[i];
	allocation->raw = malloc(bytes + 2 * GUARD_SIZE);
	assert(allocation->raw != NULL);
	allocation->data = allocation->raw + GUARD_SIZE;
	allocation->bytes = bytes;
	memset(allocation->raw, 0xc3, GUARD_SIZE);
	memset(allocation->data, 0xa5, bytes);
	memset((unsigned char *)allocation->data + bytes, 0xd7, GUARD_SIZE);
	return (allocation->data);
}

void
grid_capacity_xfree(void *data)
{
	struct allocation *allocation;

	assert(data != NULL);
	allocation = find_allocation(data);
	check_guard(allocation);
	memset(data, 0xdd, allocation->bytes);
	free(allocation->raw);
	memset(allocation, 0, sizeof *allocation);
}

void *
grid_capacity_xcalloc(size_t count, size_t size)
{
	void *data;

	assert(count != 0 && size != 0 && count <= SIZE_MAX / size);
	data = grid_capacity_xmalloc(count * size);
	memset(data, 0, count * size);
	return (data);
}

void *
grid_capacity_xrealloc(void *old, size_t count, size_t size)
{
	struct allocation *allocation;
	void *data;
	size_t bytes, copied;

	assert(count != 0 && size != 0 && count <= SIZE_MAX / size);
	bytes = count * size;
	data = grid_capacity_xmalloc(bytes);
	if (old != NULL) {
		allocation = find_allocation(old);
		check_guard(allocation);
		copied = allocation->bytes < bytes ? allocation->bytes : bytes;
		memcpy(data, old, copied);
		grid_capacity_xfree(old);
	}
	if (size == ROW_UNIT)
		cell_reallocations++;
	return (data);
}

static size_t
cell_capacity(unsigned int logical)
{
#ifdef GRID_COMPACT_ROWS
	if (logical <= GRID_ROW_QUANTUM)
		return (GRID_ROW_QUANTUM);
	return ((logical + GRID_ROW_QUANTUM - 1) / GRID_ROW_QUANTUM *
	    GRID_ROW_QUANTUM);
#else
	if (EXPECT_ROUNDED)
		return (((logical + 15U) / 16U) * 16U);
	return (logical);
#endif
}

static size_t
cell_capacity_bytes(unsigned int logical)
{
	return (cell_capacity(logical) * ROW_UNIT);
}

static void
check_line(struct grid *grid, unsigned int y)
{
	struct grid_line *line = &grid->linedata[y];

	if (line->cellsize != 0) {
		assert(ROW_STORAGE(line) != NULL);
		assert(find_allocation(ROW_STORAGE(line))->bytes >=
		    cell_capacity_bytes(line->cellsize));
#ifdef GRID_COMPACT_ROWS
		if (line->styledata != NULL) {
			assert(find_allocation(line->styledata)->bytes >=
			    cell_capacity(line->cellsize) * GRID_STYLE_BYTES);
		}
#endif
	}
	check_all_guards();
}

static void
expect_cell(struct grid *grid, unsigned int x, unsigned int y,
    const struct grid_cell *wanted)
{
	const struct grid_cell *cell = grid_peek_cell(grid, x, y);

	assert(cell->attr == wanted->attr && cell->flags == wanted->flags);
	assert(cell->fg == wanted->fg && cell->bg == wanted->bg);
	assert(cell->data == wanted->data);
}

static void
write_row(struct grid *grid, unsigned int y, unsigned int count)
{
	struct grid_cell cell;
	unsigned int x;

	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.attr = GRID_ATTR_BRIGHT;
	for (x = 0; x < count; x++) {
		cell.data = 'A' + x % 26;
		grid_set_cell(grid, x, y, &cell);
		assert(grid->linedata[y].cellsize == x + 1);
		check_line(grid, y);
	}
}

static void
expect_written(struct grid *grid, unsigned int x, unsigned int y,
    unsigned int original_x)
{
	struct grid_cell cell;

	memcpy(&cell, &grid_default_cell, sizeof cell);
	cell.attr = GRID_ATTR_BRIGHT;
	cell.data = 'A' + original_x % 26;
	expect_cell(grid, x, y, &cell);
}

static void
check_sequential_growth(void)
{
	struct grid *grid;
	unsigned int before, x;

	grid = grid_create(80, 1, 0);
	before = cell_reallocations;
	write_row(grid, 0, 80);
	assert(cell_reallocations - before == EXPECTED(80, 5, 1));
	assert(find_allocation(ROW_STORAGE(&grid->linedata[0]))->bytes ==
	    80 * ROW_UNIT);
#ifdef GRID_COMPACT_ROWS
	/* The bright test cells need the row's style array as well. */
	assert(find_allocation(grid->linedata[0].styledata)->bytes ==
	    80 * GRID_STYLE_BYTES);
#endif
	for (x = 0; x < 80; x++)
		expect_written(grid, x, 0, x);
	grid_destroy(grid);
	check_no_allocations();
}

static void
check_truncate_and_regrow(void)
{
	struct grid *grid;
	void *retained;
	unsigned int before, x;

	grid = grid_create(96, 1, 0);
	write_row(grid, 0, 80);
	retained = ROW_STORAGE(&grid->linedata[0]);
	grid_clear(grid, 17, 0, 63, 1);
	assert(grid->linedata[0].cellsize == 17);
#ifdef GRID_COMPACT_ROWS
	/* Seventeen bright cells remain, so the style array must stay. */
	assert(grid->linedata[0].styledata != NULL);
#endif
	before = cell_reallocations;
	grid_expand_line(grid, 0, 24);
	assert(cell_reallocations == before + EXPECTED(1, 0, 0));
	if (EXPECT_ROUNDED)
		assert(ROW_STORAGE(&grid->linedata[0]) == retained);
	for (x = 0; x < 17; x++)
		expect_written(grid, x, 0, x);
	for (x = 17; x < 24; x++)
		expect_cell(grid, x, 0, &grid_default_cell);

	/* Crossing a bank may shrink old spare space; the lower bound still holds. */
	grid_expand_line(grid, 0, 33);
	assert(cell_reallocations == before + EXPECTED(2, 1, 0));
	assert(find_allocation(ROW_STORAGE(&grid->linedata[0]))->bytes ==
	    cell_capacity_bytes(33));
	for (x = 17; x < 33; x++)
		expect_cell(grid, x, 0, &grid_default_cell);
	check_line(grid, 0);

	grid_clear(grid, 0, 0, 33, 1);
	assert(grid->linedata[0].cellsize == 0);
	assert(ROW_STORAGE(&grid->linedata[0]) != NULL);
#ifdef GRID_COMPACT_ROWS
	/* No styled cell is left: the style array is released at once. */
	assert(grid->linedata[0].styledata == NULL);
#endif
	grid_expand_line(grid, 0, 1);
	expect_cell(grid, 0, 0, &grid_default_cell);
	check_line(grid, 0);
	grid_destroy(grid);
	check_no_allocations();
}

static void
check_duplicate_odd_and_empty(void)
{
	struct grid *source, *copy;
	struct grid_cell changed;
	struct grid_utf8 utf8;
	void *source_empty_cells, *source_empty_utf8;
	unsigned int before, x;

	source = grid_create(96, 2, 0);
	copy = grid_create(96, 2, 0);
	write_row(source, 0, 80);
	grid_clear(source, 17, 0, 63, 1);
	write_row(source, 1, 9);
	memset(&utf8, 0xff, sizeof utf8);
	utf8.width = 1;
	utf8.data[0] = 'u';
	grid_set_utf8(source, 16, 0, &utf8);
	grid_set_utf8(source, 0, 1, &utf8);
	grid_clear(source, 0, 1, 9, 1);
	/* Model either logical storage length reaching zero while storage is kept. */
	source->linedata[1].utf8size = 0;
	source_empty_cells = ROW_STORAGE(&source->linedata[1]);
	source_empty_utf8 = source->linedata[1].utf8data;
	assert(source_empty_cells != NULL && source_empty_utf8 != NULL);
	grid_duplicate_lines(copy, 0, source, 0, 2);
	assert(copy->linedata[0].cellsize == 17);
	assert(ROW_STORAGE(&copy->linedata[0]) !=
	    ROW_STORAGE(&source->linedata[0]));
#ifdef GRID_COMPACT_ROWS
	assert(copy->linedata[0].styledata != NULL);
	assert(copy->linedata[0].styledata != source->linedata[0].styledata);
	assert(find_allocation(copy->linedata[0].styledata)->bytes ==
	    cell_capacity(17) * GRID_STYLE_BYTES);
	assert(copy->linedata[1].styledata == NULL);
#endif
	assert(copy->linedata[0].utf8data != source->linedata[0].utf8data);
	assert(memcmp(copy->linedata[0].utf8data,
	    source->linedata[0].utf8data, 17 * sizeof utf8) == 0);
	assert(find_allocation(ROW_STORAGE(&copy->linedata[0]))->bytes ==
	    cell_capacity_bytes(17));
	assert(copy->linedata[1].cellsize == 0 &&
	    ROW_STORAGE(&copy->linedata[1]) == NULL);
	assert(copy->linedata[1].utf8size == 0 && copy->linedata[1].utf8data == NULL);
	assert(find_allocation(source_empty_cells) != NULL);
	assert(find_allocation(source_empty_utf8) != NULL);

	memcpy(&changed, &grid_default_cell, sizeof changed);
	changed.data = '!';
	before = cell_reallocations;
	grid_set_cell(copy, 31, 0, &changed);
	assert(cell_reallocations == before + EXPECTED(1, 0, 0));
	for (x = 17; x < 31; x++)
		expect_cell(copy, x, 0, &grid_default_cell);
	expect_written(source, 0, 0, 0);
	grid_set_cell(copy, 0, 1, &changed);
	grid_set_utf8(copy, 0, 1, &utf8);
	assert(ROW_STORAGE(&copy->linedata[1]) != source_empty_cells);
	assert(copy->linedata[1].utf8data != source_empty_utf8);
#ifdef GRID_COMPACT_ROWS
	assert(((u_char *)source_empty_cells)[0] == 'A');
#else
	assert(((struct grid_cell *)source_empty_cells)[0].data == 'A');
#endif
	assert(((struct grid_utf8 *)source_empty_utf8)[0].data[0] == 'u');
	check_line(copy, 0);
	check_line(copy, 1);
	grid_destroy(source);
	grid_destroy(copy);
	check_no_allocations();
}

static void
check_moves(void)
{
	struct grid *grid;
	void *rows[3];
	unsigned int x;

	grid = grid_create(96, 4, 0);
	write_row(grid, 0, 17);
	write_row(grid, 1, 33);
	write_row(grid, 2, 1);
	write_row(grid, 3, 65);
	for (x = 0; x < nitems(rows); x++)
		rows[x] = ROW_STORAGE(&grid->linedata[x]);
	grid_move_lines(grid, 1, 0, 3);
	assert(ROW_STORAGE(&grid->linedata[0]) == NULL);
	for (x = 0; x < nitems(rows); x++) {
		assert(ROW_STORAGE(&grid->linedata[x + 1]) == rows[x]);
		check_line(grid, x + 1);
	}
	grid_move_lines(grid, 0, 2, 2);
	assert(ROW_STORAGE(&grid->linedata[0]) == rows[1]);
	assert(ROW_STORAGE(&grid->linedata[1]) == rows[2]);
	assert(ROW_STORAGE(&grid->linedata[2]) == NULL);
	assert(ROW_STORAGE(&grid->linedata[3]) == NULL);
	grid_clear_lines(grid, 0, 4);
	write_row(grid, 0, 17);
	grid_move_cells(grid, 5, 0, 0, 17);
	assert(grid->linedata[0].cellsize == 22);
	for (x = 0; x < 5; x++)
		expect_cell(grid, x, 0, &grid_default_cell);
	for (x = 5; x < 22; x++)
		expect_written(grid, x, 0, x - 5);
	grid_move_cells(grid, 30, 5, 0, 17);
	assert(grid->linedata[0].cellsize == 47);
	for (x = 0; x < 30; x++)
		expect_cell(grid, x, 0, &grid_default_cell);
	for (x = 30; x < 47; x++)
		expect_written(grid, x, 0, x - 30);
	check_line(grid, 0);
	grid_destroy(grid);
	check_no_allocations();
}

static void
check_horizontal_resize(void)
{
	struct screen screen;
	void *cells;
	unsigned int before;

	screen_init(&screen, 80, 1, 0);
	write_row(screen.grid, 0, 80);
	cells = ROW_STORAGE(&screen.grid->linedata[0]);
	before = cell_reallocations;
	screen_resize(&screen, 40, 1);
	assert(screen.grid->sx == 40 && screen.grid->linedata[0].cellsize == 80);
	assert(ROW_STORAGE(&screen.grid->linedata[0]) == cells);
	screen_resize(&screen, 80, 1);
	assert(cell_reallocations == before);
	assert(ROW_STORAGE(&screen.grid->linedata[0]) == cells);
	expect_written(screen.grid, 79, 0, 79);
	check_line(screen.grid, 0);
	screen_free(&screen);
	check_no_allocations();
}

static void
check_rounding_overflow(void)
{
	struct grid *grid;
	pid_t child;
	int status;

	if (!EXPECT_ROUNDED)
		return;
	child = fork();
	assert(child != -1);
	if (child == 0) {
		alarm(5);
		log_close();
		grid = grid_create(80, 1, 0);
		/* Reject count overflow before attempting any storage allocation. */
		forbid_allocations = 1;
		grid_expand_line(grid, 0, UINT_MAX);
		_exit(99);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
	check_no_allocations();
}

int
main(void)
{
	grid_init_default();
	check_sequential_growth();
	check_truncate_and_regrow();
	check_duplicate_odd_and_empty();
	check_moves();
	check_horizontal_resize();
	check_rounding_overflow();
	puts("grid capacity growth, truncation, duplication, moves and resize passed");
	return (0);
}
