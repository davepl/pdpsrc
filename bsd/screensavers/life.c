/*
 *  game_of_life.c - Conway's Game of Life screensaver for PDP-11/83 with VT220 terminal.
 *                   Displays an 80x24 grid where cells evolve based on Game of Life rules.
 *                   Written in K&R style for 2.11BSD, etc.
 *
 *  Compile example (on 2.11BSD, might need -lm for math library):
 *    cc -o game_of_life game_of_life.c -lm
 *
 *  Press Ctrl-C to exit (SIGINT), which restores the cursor and
 *  resets the scrolling region.
 * 
 *  Author: Davepl 2025
 *  License: GPL 2.0
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

/* Global variables for dynamic screen dimensions */
int SCREEN_WIDTH = 80;   /* Default fallback */
int SCREEN_HEIGHT = 24;  /* Default fallback */

#define ALIVE_CHAR 'O'
#define DEAD_CHAR ' '

static void restore_on_exit();

/* Function to get terminal dimensions */
void get_terminal_size()
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) SCREEN_WIDTH = ws.ws_col;
        if (ws.ws_row > 0) SCREEN_HEIGHT = ws.ws_row;
    }
#endif
}

/* Function prototypes */
void initialize_grid(int *current, int height, int width);
void initialize_test_pattern(int *current, int height, int width);
void copy_grid(int *source, int *dest, int height, int width);
int count_neighbors(int *grid, int row, int col, int height, int width);
void render_grid(int *grid, int height, int width);

int main(argc, argv)
int argc;
char *argv[];
{
    int *current_grid;
    int *next_grid;
    int row, col;

    /* Get terminal dimensions */
    get_terminal_size();

    /* Allocate memory for grids */
    current_grid = (int*)malloc(SCREEN_HEIGHT * SCREEN_WIDTH * sizeof(int));
    next_grid = (int*)malloc(SCREEN_HEIGHT * SCREEN_WIDTH * sizeof(int));
    
    if (!current_grid || !next_grid) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    /* Initialize the current grid */
    if (argc > 1 && argv[1][0] == 't') {
        /* Use test pattern if 't' argument is provided */
        initialize_test_pattern(current_grid, SCREEN_HEIGHT, SCREEN_WIDTH);
    } else {
        /* Use random initialization by default */
        initialize_grid(current_grid, SCREEN_HEIGHT, SCREEN_WIDTH);
    }

    /* Install signal handler for Ctrl-C (SIGINT) and SIGTERM. */
    (void) signal(SIGINT,  restore_on_exit);
    (void) signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    printf("\033[?25l");

    /* Set scrolling region to full screen height */
    printf("\033[1;%dr", SCREEN_HEIGHT);

    /* Clear screen once at the beginning */
    printf("\033[2J");

    fflush(stdout);

    /* Main loop: compute next generation and render */
    while (1) {
        /* Compute next generation */
        for (row = 0; row < SCREEN_HEIGHT; row++) {
            for (col = 0; col < SCREEN_WIDTH; col++) {
                int alive = current_grid[row * SCREEN_WIDTH + col];
                int neighbors = count_neighbors(current_grid, row, col, SCREEN_HEIGHT, SCREEN_WIDTH);

                if (alive) {
                    if (neighbors < 2 || neighbors > 3)
                        next_grid[row * SCREEN_WIDTH + col] = 0;  /* Cell dies */
                    else
                        next_grid[row * SCREEN_WIDTH + col] = 1;  /* Cell lives */
                } else {
                    if (neighbors == 3)
                        next_grid[row * SCREEN_WIDTH + col] = 1;  /* Cell becomes alive */
                    else
                        next_grid[row * SCREEN_WIDTH + col] = 0;  /* Cell remains dead */
                }
            }
        }

        /* Render the next generation */
        render_grid(next_grid, SCREEN_HEIGHT, SCREEN_WIDTH);

        /* Swap grids */
        copy_grid(next_grid, current_grid, SCREEN_HEIGHT, SCREEN_WIDTH);

        /* Small delay: Adjust to taste. */
        usleep(50000);
    }

    /* Normally never reached, but just in case */
    free(current_grid);
    free(next_grid);
    restore_on_exit(0);

    return 0;
}

/* Initialize the grid with random alive (1) or dead (0) cells */
void initialize_grid(int *current, int height, int width)
{
    int row, col;
    int random_val;
    
    /* Seed random number generator with current time for variety */
    srand((unsigned int)time(NULL));

    /* First, clear the entire grid */
    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            current[row * width + col] = 0;
        }
    }

    /* Now add some random live cells with a lower density */
    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            random_val = rand() % 100;  /* Get value 0-99 */
            /* Only make about 15% of cells alive for more stable patterns */
            if (random_val < 15) {
                current[row * width + col] = 1;
            }
        }
    }
}

/* Initialize with a known test pattern for debugging */
void initialize_test_pattern(int *current, int height, int width)
{
    int row, col;
    
    /* Clear the entire grid first */
    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            current[row * width + col] = 0;
        }
    }
    
    /* Add some known stable and oscillating patterns */
    
    /* Block (stable) at position (5,5) */
    if (height > 6 && width > 6) {
        current[5 * width + 5] = 1;
        current[5 * width + 6] = 1;
        current[6 * width + 5] = 1;
        current[6 * width + 6] = 1;
    }
    
    /* Blinker (oscillator) at position (10,10) */
    if (height > 10 && width > 11) {
        current[10 * width + 9] = 1;
        current[10 * width + 10] = 1;
        current[10 * width + 11] = 1;
    }
    
    /* Glider at position (15,15) */
    if (height > 17 && width > 17) {
        current[15 * width + 16] = 1;
        current[16 * width + 17] = 1;
        current[17 * width + 15] = 1;
        current[17 * width + 16] = 1;
        current[17 * width + 17] = 1;
    }
    
    /* Toad (oscillator) at position (5,20) */
    if (height > 6 && width > 22) {
        current[5 * width + 20] = 1;
        current[5 * width + 21] = 1;
        current[5 * width + 22] = 1;
        current[6 * width + 19] = 1;
        current[6 * width + 20] = 1;
        current[6 * width + 21] = 1;
    }
}

/* Copy source grid to destination grid */
void copy_grid(int *source, int *dest, int height, int width)
{
    memcpy(dest, source, sizeof(int) * height * width);
}

/* Count the number of alive neighbors for a cell at (row, col) */
int count_neighbors(int *grid, int row, int col, int height, int width)
{
    int count = 0;
    int dr, dc;
    int r, c;

    for (dr = -1; dr <= 1; dr++) {
        for (dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0)
                continue;  /* Skip the cell itself */

            r = row + dr;
            c = col + dc;

            /* Wrap around the edges (toroidal array) */
            if (r < 0)
                r = height - 1;
            else if (r >= height)
                r = 0;

            if (c < 0)
                c = width - 1;
            else if (c >= width)
                c = 0;

            count += grid[r * width + c];
        }
    }

    return count;
}

/* Render the grid to the terminal */
void render_grid(int *grid, int height, int width)
{
    int row, col;

    /* Move cursor to top-left */
    printf("\033[1;1H");

    /* Iterate through the grid and print characters */
    for (row = 0; row < height - 1; row++) {  /* Leave last row for status */
        for (col = 0; col < width; col++) {
            if (grid[row * width + col])
                putchar(ALIVE_CHAR);
            else
                putchar(DEAD_CHAR);
        }
        if (row < height - 2)  /* Don't print newline on last row */
            putchar('\n');
    }

    /* Flush output to ensure it appears on the screen */
    fflush(stdout);
}

/* Signal handler to restore the terminal when exiting */
static void restore_on_exit(signum)
int signum;
{
    /* Show the cursor again */
    printf("\033[?25h");

    /* Reset scrolling region */
    printf("\033[r");

    /* Move cursor to bottom-left */
    printf("\033[%d;1H", SCREEN_HEIGHT);

    /* Flush out any pending output */
    fflush(stdout);

    /* Exit gracefully */
    exit(0);
}
