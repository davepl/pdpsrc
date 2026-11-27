#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmux-full.h"

static void init_pane(struct window_pane *p, struct window *w,
    unsigned int rows, unsigned int history)
{
    memset(p, 0, sizeof *p);
    memset(w, 0, sizeof *w);
    options_init(&w->options, NULL);
    options_set_number(&w->options, "utf8", 0);
    w->sx = p->sx = 4;
    w->sy = p->sy = rows;
    p->window = w;
    p->screen = &p->base;
    p->flags = PANE_REDRAW;
    p->fd = -1;
    p->in = buffer_create(64);
    p->out = buffer_create(64);
    screen_init(&p->base, 4, rows, history);
    input_init(p);
}

static void free_pane(struct window_pane *p, struct window *w)
{
    assert(p->saved_grid == NULL);
    input_free(p);
    screen_free(&p->base);
    buffer_destroy(p->in);
    buffer_destroy(p->out);
    options_free(&w->options);
}

static void feed(struct window_pane *p, const char *bytes)
{
    buffer_write(p->in, bytes, strlen(bytes));
    input_parse(p);
}

static void row(struct grid *g, unsigned int y, const char *expected)
{
    unsigned int x;
    for (x = 0; x < g->sx; x++) {
        unsigned char got = grid_view_peek_cell(g, x, y)->data;
        if (got != (unsigned char) expected[x]) {
            fprintf(stderr, "row %u column %u: got %c, expected %c (hsize=%u)\n",
                y, x, got, expected[x], g->hsize);
            abort();
        }
    }
}

static void assert_no_history(struct window_pane *p)
{
    assert(p->base.grid->hlimit == 0);
    assert(p->base.grid->hsize == 0);
    assert((p->base.grid->flags & GRID_HISTORY) == 0);
}

static void zero_history(void)
{
    struct window_pane p;
    struct window w;
    struct grid *g;
    int i;
    init_pane(&p, &w, 3, 0);
    g = p.base.grid;
    feed(&p, "AAAA\r\nBBBB\r\nCCCC\r\n");
    assert_no_history(&p);
    row(g, 0, "BBBB"); row(g, 1, "CCCC"); row(g, 2, "    ");
    feed(&p, "DDDD");
    screen_resize(&p.base, 4, 2);
    assert_no_history(&p);
    row(g, 0, "CCCC"); row(g, 1, "DDDD");
    screen_resize(&p.base, 4, 4);
    assert_no_history(&p);
    row(g, 0, "CCCC"); row(g, 1, "DDDD");
    row(g, 2, "    "); row(g, 3, "    ");
    screen_resize(&p.base, 2, 4);
    row(g, 0, "CC"); row(g, 1, "DD");
    screen_resize(&p.base, 4, 4);
    row(g, 0, "CCCC"); row(g, 1, "DDDD");
    feed(&p, "\033[?1049h");
    assert(p.saved_grid != NULL);
    assert_no_history(&p);
    feed(&p, "\033[H1111\r\n2222\r\n3333\r\n4444\r\n");
    row(g, 0, "2222"); row(g, 1, "3333");
    row(g, 2, "4444"); row(g, 3, "    ");
    assert_no_history(&p);
    feed(&p, "\033[?1049l");
    assert(p.saved_grid == NULL);
    assert_no_history(&p);
    row(g, 0, "CCCC"); row(g, 1, "DDDD");
    row(g, 2, "    "); row(g, 3, "    ");
    feed(&p, "\033[4;1Htail\r\n");
    assert_no_history(&p);
    row(g, 0, "DDDD"); row(g, 1, "    ");
    row(g, 2, "tail"); row(g, 3, "    ");
    for (i = 0; i < 200; i++) {
        feed(&p, "next\r\n");
        assert_no_history(&p);
        row(g, 3, "    ");
    }
    free_pane(&p, &w);
    puts("zero history: scroll, resize, alternate screen, and 200 further scrolls passed");
}

static void positive_history(void)
{
    struct window_pane p;
    struct window w;
    struct grid *g;
    int i;
    init_pane(&p, &w, 3, 4);
    g = p.base.grid;
    feed(&p, "AAAA\r\nBBBB\r\nCCCC\r\n");
    assert(g->hsize == 1 && (g->flags & GRID_HISTORY));
    assert(grid_peek_cell(g, 0, 0)->data == 'A');
    row(g, 0, "BBBB"); row(g, 1, "CCCC"); row(g, 2, "    ");
    feed(&p, "\033[?1049h\033[H1111\r\n2222\r\n3333\r\n");
    assert(g->hsize == 1 && !(g->flags & GRID_HISTORY));
    feed(&p, "\033[?1049l");
    assert(g->hsize == 1 && (g->flags & GRID_HISTORY));
    row(g, 0, "BBBB"); row(g, 1, "CCCC"); row(g, 2, "    ");
    feed(&p, "DDDD\r\n");
    assert(g->hsize == 2);
    assert(grid_peek_cell(g, 0, 0)->data == 'A');
    assert(grid_peek_cell(g, 0, 1)->data == 'B');
    screen_resize(&p.base, 4, 4);
#ifdef GRID_VMF_HISTORY
    /* Rows in the history file stay there; the added row is blank. */
    assert(g->hsize == 2);
    row(g, 0, "CCCC"); row(g, 1, "DDDD");
    row(g, 2, "    "); row(g, 3, "    ");
#else
    assert(g->hsize == 1);
    row(g, 0, "BBBB"); row(g, 1, "CCCC");
    row(g, 2, "DDDD"); row(g, 3, "    ");
#endif
    screen_resize(&p.base, 4, 2);
    assert(g->hsize == 3);
    row(g, 0, "DDDD"); row(g, 1, "    ");
    for (i = 0; i < 200; i++) {
        feed(&p, "next\r\n");
        assert(g->hsize > 0 && g->hsize <= 4);
        row(g, 1, "    ");
    }
    free_pane(&p, &w);
    puts("positive history: retained lines, alternate screen, resize and limit collection passed");
}

static void one_row(void)
{
    struct window_pane p;
    struct window w;
    init_pane(&p, &w, 1, 0);
    feed(&p, "XXXX\r\n");
    assert_no_history(&p);
    row(p.base.grid, 0, "    ");
    free_pane(&p, &w);
    puts("one-row zero-history scroll cleared the row");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--one-row") == 0)
        one_row();
    else {
        zero_history();
        positive_history();
    }
    return 0;
}
