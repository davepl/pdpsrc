/*
 * Window history in a vmf paging file, and copy mode reading it.
 *
 * Linked with the milestone program objects and tests/vmf/vmf.c, the 2.11BSD
 * paging library itself. grid-history.c is compiled a second time for this
 * test with write() redirected, so that a full /tmp can be simulated at a
 * chosen point; vmf's own writes are not redirected.
 */
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux-full.h"
#include <vmf.h>

#ifndef GRID_VMF_HISTORY
#error Run this regression with the milestone profile and TMUX_VMF_HISTORY_TEST.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

#define COLUMNS 80U

/* Fail the nth write made by grid-history.c from now; 0 never fails. */
static long	fail_write_after;
static long	history_writes;

ssize_t
history_test_write(int fd, const void *buf, size_t len)
{
	history_writes++;
	if (fail_write_after != 0 && --fail_write_after == 0) {
		errno = ENOSPC;
		return (-1);
	}
	return (write(fd, buf, len));
}

/* What line number n holds at column x, and how long it is. */
static u_char
line_char(u_int n, u_int x)
{
	return (33 + (n * 7 + x * 13) % 90);
}

static u_int
line_length(u_int n)
{
	return (1 + (n * 37) % COLUMNS);
}

static int
line_wrapped(u_int n)
{
	return (n % 3 == 0);
}

/*
 * Write line n on the top row, with colour so that the row has a style
 * array, then scroll it into the history.
 */
static void
push_line(struct grid *gd, u_int n)
{
	struct grid_cell	gc;
	u_int			x, length;

	length = line_length(n);
	memcpy(&gc, &grid_default_cell, sizeof gc);
	for (x = 0; x < length; x++) {
		gc.data = line_char(n, x);
		gc.fg = (n + x) % 8;
		gc.attr = (x % 5 == 0) ? GRID_ATTR_BRIGHT : 0;
		grid_view_set_cell(gd, x, 0, &gc);
	}
	if (line_wrapped(n))
		GRID_LINE(gd, gd->hsize)->flags |= GRID_LINE_WRAPPED;
	grid_view_scroll_region_up(gd, 0, gd->sy - 1);
}

/* History row py of gd must be line n, as plain characters. */
static void
expect_line(struct grid *gd, u_int py, u_int n)
{
	const struct grid_cell	*gc;
	u_int			 x, length;

	length = line_length(n);
	assert(py < gd->hsize);
	if (grid_line_cellsize(gd, py) != length) {
		fprintf(stderr, "row %u: length %u, expected %u (line %u)\n",
		    py, grid_line_cellsize(gd, py), length, n);
		abort();
	}
	assert((grid_line_flags(gd, py) & GRID_LINE_WRAPPED) ==
	    (line_wrapped(n) ? GRID_LINE_WRAPPED : 0));
	for (x = 0; x < COLUMNS; x++) {
		gc = grid_peek_cell(gd, x, py);
		if (gc->data != (x < length ? line_char(n, x) : ' ')) {
			fprintf(stderr, "row %u column %u: got %c (line %u)\n",
			    py, x, gc->data, n);
			abort();
		}
		assert(gc->attr == 0 && gc->flags == 0);
		assert(gc->fg == 8 && gc->bg == 8);
	}
	assert(grid_peek_utf8(gd, 0, py) == NULL);
}

static void
check_geometry(void)
{
	assert(GRID_HISTORY_RECORD * GRID_HISTORY_PERSEG <= BYTESPERSEG);
	assert(GRID_HISTORY_COLUMNS + 2 <= GRID_HISTORY_RECORD);
	assert(GRID_HISTORY_SEGMENTS * GRID_HISTORY_SLOTS <= MAXSEGNO);
	assert(GRID_HISTORY_ROWS == 480);
	assert(GRID_HISTORY_SLOTS <= sizeof (u_int) * 8);
	assert(GRID_HISTORY_COLUMNS == COLUMNS);
}

static void
check_ring(void)
{
	struct grid	*gd;
	struct grid_cell gc;
	u_int		 n, py, first, blocks;

	gd = grid_create(COLUMNS, 4, 2000);
	assert(gd->hlimit == GRID_HISTORY_ROWS && (gd->flags & GRID_HISTORY));
	assert(gd->hslot != 0 && gd->hsize == 0);

	history_writes = 0;
	for (n = 0; n < 1500; n++) {
		push_line(gd, n);
		assert(gd->hsize == (n + 1 < GRID_HISTORY_ROWS ?
		    n + 1 : GRID_HISTORY_ROWS));
		/* Newest and oldest after every line; all of them at times. */
		first = n + 1 - gd->hsize;
		expect_line(gd, gd->hsize - 1, n);
		expect_line(gd, 0, first);
		if (n % 97 == 0 || n == 1499) {
			for (py = 0; py < gd->hsize; py++)
				expect_line(gd, py, first + py);
		}
	}
	/* Each segment's blocks were written once, in record-sized pieces. */
	blocks = (BYTESPERSEG + GRID_HISTORY_RECORD - 1) / GRID_HISTORY_RECORD;
	assert(history_writes == (long)(GRID_HISTORY_SEGMENTS * blocks));

	/* History rows cannot be written; the visible rows still can. */
	memcpy(&gc, &grid_default_cell, sizeof gc);
	gc.data = '!';
	grid_set_cell(gd, 0, 0, &gc);
	grid_clear(gd, 0, 0, 10, 1);
	grid_clear_lines(gd, 0, 2);
	grid_move_lines(gd, 0, 1, 1);
	expect_line(gd, 0, 1500 - GRID_HISTORY_ROWS);
	expect_line(gd, 1, 1500 - GRID_HISTORY_ROWS + 1);
	grid_view_set_cell(gd, 0, 0, &gc);
	assert(grid_view_peek_cell(gd, 0, 0)->data == '!');
	assert(grid_peek_cell(gd, 0, gd->hsize)->data == '!');

	grid_clear_history(gd);
	assert(gd->hsize == 0 && gd->hslot != 0);
	assert(grid_view_peek_cell(gd, 0, 0)->data == '!');
	push_line(gd, 7);
	assert(gd->hsize == 1);
	expect_line(gd, 0, 7);

	grid_destroy(gd);
	puts("history ring: 1500 lines through 480 rows, flags, lengths, "
	    "plain cells, read-only rows and clearing passed");
}

static void
check_interleaved(void)
{
	struct grid	*gd[5];
	u_int		 i, n, py, base;
	long		 swaps;

	for (i = 0; i < 5; i++)
		gd[i] = grid_create(COLUMNS, 3, GRID_HISTORY_ROWS);
	swaps = nswaps;
	for (n = 0; n < 700; n++) {
		for (i = 0; i < 5; i++) {
			push_line(gd[i], n * 5 + i);
			if (i > 0)
				expect_line(gd[i - 1], gd[i - 1]->hsize - 1,
				    n * 5 + i - 1);
		}
	}
	/* Five tails through fewer buffers: vmf must have paged. */
	assert(GRID_HISTORY_BUFFERS < 5 && nswaps > swaps + 100);
	for (py = 0; py < GRID_HISTORY_ROWS; py++) {
		for (i = 0; i < 5; i++) {
			base = 700 - GRID_HISTORY_ROWS;
			expect_line(gd[4 - i], py, (base + py) * 5 + 4 - i);
		}
	}
	for (i = 0; i < 5; i++)
		grid_destroy(gd[i]);
	printf("five grids interleaved through %d vmf buffers passed\n",
	    GRID_HISTORY_BUFFERS);
}

static void
check_slots(void)
{
	struct grid	*gd[GRID_HISTORY_SLOTS + 1], *fresh;
	u_int		 i;

	for (i = 0; i <= GRID_HISTORY_SLOTS; i++) {
		gd[i] = grid_create(COLUMNS, 2, 100);
		if (i < GRID_HISTORY_SLOTS) {
			assert(gd[i]->hslot == i + 1);
			assert(gd[i]->hlimit == 100);
		}
	}
	/* One more than there are slots: an ordinary grid without history. */
	assert(gd[GRID_HISTORY_SLOTS]->hslot == 0);
	assert(gd[GRID_HISTORY_SLOTS]->hlimit == 0);
	assert(!(gd[GRID_HISTORY_SLOTS]->flags & GRID_HISTORY));
	push_line(gd[GRID_HISTORY_SLOTS], 1);
	assert(gd[GRID_HISTORY_SLOTS]->hsize == 0);

	for (i = 0; i < 30; i++)
		push_line(gd[5], 1000 + i);
	grid_destroy(gd[5]);
	fresh = grid_create(COLUMNS, 2, 100);
	assert(fresh->hslot == 6 && fresh->hsize == 0);
	push_line(fresh, 2000);
	assert(fresh->hsize == 1);
	expect_line(fresh, 0, 2000);
	grid_destroy(fresh);

	/* Grids that ask for no history never take a slot. */
	fresh = grid_create(COLUMNS, 2, 0);
	assert(fresh->hslot == 0 && !(fresh->flags & GRID_HISTORY));
	grid_destroy(fresh);

	for (i = 0; i <= GRID_HISTORY_SLOTS; i++) {
		if (i != 5)
			grid_destroy(gd[i]);
	}
	puts("history slots: exhaustion, release and reuse passed");
}

static void
check_full_disk(void)
{
	struct grid	*gd, *other, *held[6];
	u_int		 n, py, blocks, rows;

	blocks = (BYTESPERSEG + GRID_HISTORY_RECORD - 1) / GRID_HISTORY_RECORD;

	/*
	 * Slots 0 to 5 already have blocks from the checks above. Hold those
	 * and work in slots that have none.
	 */
	for (n = 0; n < 6; n++) {
		held[n] = grid_create(COLUMNS, 2, 10);
		assert(held[n]->hslot == n + 1);
	}

	/* The fourth segment cannot be written: the ring stops at three. */
	gd = grid_create(COLUMNS, 3, GRID_HISTORY_ROWS);
	assert(gd->hslot == 7);
	fail_write_after = 3 * blocks + 2;
	rows = 3 * GRID_HISTORY_PERSEG;
	for (n = 0; n < 200; n++) {
		push_line(gd, n);
		assert(gd->flags & GRID_HISTORY);
		assert(gd->hsize == (n + 1 < rows ? n + 1 : rows));
		expect_line(gd, gd->hsize - 1, n);
	}
	assert(fail_write_after == 0 && gd->hlimit == rows);
	for (py = 0; py < rows; py++)
		expect_line(gd, py, 200 - rows + py);
	grid_destroy(gd);

	/* The same slot later, with room again, grows to the full ring. */
	gd = grid_create(COLUMNS, 3, GRID_HISTORY_ROWS);
	assert(gd->hslot == 7 && gd->hlimit == GRID_HISTORY_ROWS);
	for (n = 0; n < 600; n++)
		push_line(gd, n);
	assert(gd->hsize == GRID_HISTORY_ROWS);
	for (py = 0; py < GRID_HISTORY_ROWS; py += 7)
		expect_line(gd, py, 600 - GRID_HISTORY_ROWS + py);
	grid_destroy(gd);

	/* Not even the first segment: the grid carries on without history. */
	other = grid_create(COLUMNS, 3, GRID_HISTORY_ROWS);
	assert(other->hslot == 7);
	gd = grid_create(COLUMNS, 3, GRID_HISTORY_ROWS);
	assert(gd->hslot == 8);
	fail_write_after = 1;
	push_line(gd, 5);
	assert(gd->hsize == 0 && gd->hlimit == 0 && gd->hslot == 0);
	assert(!(gd->flags & GRID_HISTORY));
	push_line(gd, 6);
	push_line(gd, 7);
	assert(gd->hsize == 0);
	assert(grid_view_peek_cell(gd, 0, 0)->data == ' ');
	grid_destroy(gd);
	grid_destroy(other);
	for (n = 0; n < 6; n++)
		grid_destroy(held[n]);
	puts("full paging file: ring stops growing, later reuse, and no-history fallback passed");
}

/* A pane fed through the real input parser, as in zero-history.c. */
static void
init_pane(struct window_pane *wp, struct window *w, u_int rows)
{
	memset(wp, 0, sizeof *wp);
	memset(w, 0, sizeof *w);
	options_init(&w->options, NULL);
	options_set_number(&w->options, "utf8", 0);
	options_set_number(&w->options, "mode-keys", MODEKEY_EMACS);
	options_set_number(&w->options, "mode-mouse", 0);
	options_set_number(&w->options, "mode-fg", 0);
	options_set_number(&w->options, "mode-bg", 3);
	options_set_number(&w->options, "mode-attr", 0);
	w->sx = wp->sx = COLUMNS;
	w->sy = wp->sy = rows;
	wp->window = w;
	wp->screen = &wp->base;
	wp->fd = -1;
	wp->in = buffer_create(256);
	wp->out = buffer_create(256);
	screen_init(&wp->base, COLUMNS, rows, GRID_HISTORY_ROWS);
	input_init(wp);
}

static void
feed(struct window_pane *wp, const char *bytes)
{
	buffer_write(wp->in, bytes, strlen(bytes));
	input_parse(wp);
}

static void
expect_text(struct grid *gd, u_int py, const char *text)
{
	u_int	x, length;

	length = strlen(text);
	for (x = 0; x < COLUMNS; x++) {
		if (grid_peek_cell(gd, x, py)->data !=
		    (u_char)(x < length ? text[x] : ' ')) {
			fprintf(stderr, "row %u column %u: got %c, wanted \"%s\"\n",
			    py, x, grid_peek_cell(gd, x, py)->data, text);
			abort();
		}
	}
}

static void
check_screen(void)
{
	struct window_pane	wp;
	struct window		w;
	struct grid		*gd;
	char			line[64];
	u_int			n;

	init_pane(&wp, &w, 6);
	gd = wp.base.grid;
	for (n = 0; n < 40; n++) {
		snprintf(line, sizeof line, "\033[3%umline %u\033[m\r\n", n % 8, n);
		feed(&wp, line);
	}
	/* 40 lines and a final newline on six rows: 35 lines scrolled off. */
	assert(gd->hsize == 35);
	for (n = 0; n < 35; n++) {
		snprintf(line, sizeof line, "line %u", n);
		expect_text(gd, n, line);
	}
	for (n = 35; n < 40; n++) {
		snprintf(line, sizeof line, "line %u", n);
		expect_text(gd, n, line);
		assert(grid_peek_cell(gd, 0, n)->fg == n % 8);
	}
	expect_text(gd, 40, "");

	/* The alternate screen neither adds to the history nor loses it. */
	feed(&wp, "\033[?1049h\033[H");
	for (n = 0; n < 20; n++)
		feed(&wp, "full screen program\r\n");
	assert(gd->hsize == 35 && !(gd->flags & GRID_HISTORY));
	feed(&wp, "\033[?1049l");
	assert(gd->hsize == 35 && (gd->flags & GRID_HISTORY));
	expect_text(gd, 34, "line 34");
	expect_text(gd, 35, "line 35");
	expect_text(gd, 39, "line 39");

	/* Shrinking stores the rows above the cursor; growing adds blanks. */
	screen_resize(&wp.base, COLUMNS, 3);
	assert(gd->hsize == 38 && gd->sy == 3);
	expect_text(gd, 37, "line 37");
	expect_text(gd, 38, "line 38");
	expect_text(gd, 39, "line 39");
	expect_text(gd, 40, "");
	screen_resize(&wp.base, COLUMNS, 6);
	assert(gd->hsize == 38 && gd->sy == 6);
	expect_text(gd, 38, "line 38");
	expect_text(gd, 43, "");
	feed(&wp, "after\r\n");
	expect_text(gd, 40, "after");
	assert(gd->hsize == 38);

	/* A scroll region that starts at the top also feeds the history. */
	feed(&wp, "\033[1;3r\033[3;1Hregion\r\n\033[r");
	assert(gd->hsize == 39);
	expect_text(gd, 38, "line 38");
	expect_text(gd, 39, "line 39");
	expect_text(gd, 40, "region");

	input_free(&wp);
	screen_free(&wp.base);
	buffer_destroy(wp.in);
	buffer_destroy(wp.out);
	options_free(&w.options);
	puts("screen history: parser scrolling, alternate screen, resize and regions passed");
}

static void
check_copy_mode(void)
{
	struct window_pane	wp;
	struct window		w;
	struct session		s;
	struct client		c;
	struct paste_buffer	*pb;
	struct grid		*gd, *view;
	char			line[128], *big;
	u_int			n, y;

	memset(&s, 0, sizeof s);
	memset(&c, 0, sizeof c);
	options_init(&s.options, NULL);
	options_set_number(&s.options, "buffer-limit", 1);
	options_set_number(&s.options, "display-time", 750);
	paste_init_stack(&s.buffers);
	c.session = &s;
	screen_init(&c.status, COLUMNS, 1, 0);

	init_pane(&wp, &w, 10);
	gd = wp.base.grid;
	for (n = 0; n < 100; n++) {
		snprintf(line, sizeof line, "copy line %u\r\n", n);
		feed(&wp, line);
	}
	assert(gd->hsize == 91);

	assert(window_pane_set_mode(&wp, &window_copy_mode) == 0);
	view = wp.screen->grid;
	assert(wp.screen != &wp.base && view->hsize == 0 && view->hslot == 0);
	/* On entry the copy screen shows the pane; row 0 has the position. */
	for (y = 1; y < 9; y++) {
		snprintf(line, sizeof line, "copy line %u", 91 + y);
		expect_text(view, y, line);
	}

	/* One page up, then to the top: rows come from the paging file. */
	wp.mode->key(&wp, &c, KEYC_PPAGE);
	expect_text(view, 1, "copy line 84");
	expect_text(view, 9, "copy line 92");
	for (n = 0; n < 20; n++)
		wp.mode->key(&wp, &c, KEYC_PPAGE);
	expect_text(view, 1, "copy line 1");
	expect_text(view, 9, "copy line 9");

	/* Search forward for text that is only in the history. */
	wp.mode->key(&wp, &c, '\023');		/* C-s */
	for (n = 0; n < 7; n++)
		wp.mode->key(&wp, &c, "line 42"[n]);
	wp.mode->key(&wp, &c, '\r');
	for (y = 0; y < 10; y++) {
		if (grid_view_peek_cell(view, 10, y)->data == '4' &&
		    grid_view_peek_cell(view, 11, y)->data == '2')
			break;
	}
	assert(y < 10);

	/* Copy that line and the next from the start of each. */
	wp.mode->key(&wp, &c, '\001');		/* C-a */
	wp.mode->key(&wp, &c, '\000');		/* C-Space */
	wp.mode->key(&wp, &c, KEYC_DOWN);
	wp.mode->key(&wp, &c, '\005');		/* C-e */
	wp.mode->key(&wp, &c, 'w' | KEYC_ESCAPE);
	assert(wp.mode == NULL && wp.screen == &wp.base);
	pb = paste_get_top(&s.buffers);
	assert(pb != NULL);
	assert(pb->size == strlen("copy line 42\ncopy line 43"));
	assert(memcmp(pb->data, "copy line 42\ncopy line 43", pb->size) == 0);

	/* A selection of the whole history is cut off at the copy bound. */
	for (n = 100; n < 160; n++) {
		snprintf(line, sizeof line, "long line %u %s\r\n", n,
		    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
		feed(&wp, line);
	}
	assert(gd->hsize == 151);
	assert(window_pane_set_mode(&wp, &window_copy_mode) == 0);
	wp.mode->key(&wp, &c, '\000');
	for (n = 0; n < 30; n++)
		wp.mode->key(&wp, &c, KEYC_PPAGE);
	wp.mode->key(&wp, &c, 'w' | KEYC_ESCAPE);
	assert(wp.mode == NULL);
	pb = paste_get_top(&s.buffers);
	assert(pb != NULL && ARRAY_LENGTH(&s.buffers) == 1);
	assert(pb->size == 2048);
	assert(c.message_string != NULL &&
	    strstr(c.message_string, "2048") != NULL);
	big = xmalloc(4096);
	big[0] = '\0';
	/* The cursor was on row 9 of the top page: line 9 is the first. */
	for (n = 9; strlen(big) < 2048; n++) {
		if (n < 100)
			snprintf(line, sizeof line, "copy line %u\n", n);
		else {
			snprintf(line, sizeof line, "long line %u %s\n", n,
			    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
		}
		strlcat(big, line, 4096);
	}
	assert(memcmp(pb->data, big, 2048) == 0);
	xfree(big);

	status_message_clear(&c);
	screen_free(&c.status);
	paste_free_stack(&s.buffers);
	input_free(&wp);
	screen_free(&wp.base);
	buffer_destroy(wp.in);
	buffer_destroy(wp.out);
	options_free(&w.options);
	options_free(&s.options);
	puts("copy mode: paging, search and bounded copy from file history passed");
}

int
main(void)
{
	grid_init_default();
	check_geometry();
	check_ring();
	check_interleaved();
	check_slots();
	check_full_disk();
	check_screen();
	check_copy_mode();
	return (0);
}
