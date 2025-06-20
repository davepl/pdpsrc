#include <curses.h>

/* Function to draw a box using VT220 line-drawing characters */
void drawBox(int x1, int y1, int width, int height)
{
    int x, y;
    char hor = 'q'; /* Horizontal line ─ */
    char ver = 'x'; /* Vertical line │ */
    char tl = 'l';  /* Top-left corner ┌ */
    char tr = 'k';  /* Top-right corner ┐ */
    char bl = 'm';  /* Bottom-left corner └ */
    char br = 'j';  /* Bottom-right corner ┘ */

    /* Enable VT220 line-drawing characters */
    addstr("\033(0");

    /* Draw top border */
    move(y1, x1);
    addch(tl);
    for (x = 0; x < width - 2; x++)
        addch(hor);
    addch(tr);

    /* Draw sides */
    for (y = 1; y < height - 1; y++) {
        move(y1 + y, x1);
        addch(ver);
        move(y1 + y, x1 + width - 1);
        addch(ver);
    }

    /* Draw bottom border */
    if (height > 1) {
        move(y1 + height - 1, x1);
        addch(bl);
        for (x = 0; x < width - 2; x++)
            addch(hor);
        addch(br);
    }

    /* Disable VT220 line-drawing characters */
    addstr("\033(B");
}

int main()
{
    int max_width, max_height, x, y, width, height;

    initscr();              /* Initialize the screen */
    cbreak();
    noecho();

    /* Get screen dimensions */
    max_height = LINES;
    max_width = COLS;

    /* Initial box position and dimensions */
    x = 0;
    y = 0;
    width = max_width;
    height = max_height;

    /* Draw progressively smaller boxes */
    while (width > 0 && height > 0) {
        drawBox(x, y, width, height);
        width -= 2;          /* Decrease width */
        height -= 1;         /* Decrease height */
        x += 1;              /* Move box inward horizontally */
        y += 1;              /* Move box inward vertically */
    }

    refresh();              /* Update the screen */

    getch();                /* Wait for user input */
    endwin();               /* End curses mode */

    return 0;
}
