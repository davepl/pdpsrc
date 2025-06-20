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

#define WIDTH 80
#define HEIGHT 24
#define ALIVE_CHAR 'O'
#define DEAD_CHAR ' '

static void restore_on_exit();

/* Function prototypes */
void initialize_grid(int current[HEIGHT][WIDTH]);
void initialize_test_pattern(int current[HEIGHT][WIDTH]);
void copy_grid(int source[HEIGHT][WIDTH], int dest[HEIGHT][WIDTH]);
int count_neighbors(int grid[HEIGHT][WIDTH], int row, int col);
void render_grid(int grid[HEIGHT][WIDTH]);

int main(argc, argv)
int argc;
char *argv[];
{
    int current_grid[HEIGHT][WIDTH];
    int next_grid[HEIGHT][WIDTH];
    int row, col;

    /* Initialize the current grid */
    if (argc > 1 && argv[1][0] == 't') {
        /* Use test pattern if 't' argument is provided */
        initialize_test_pattern(current_grid);
    } else {
        /* Use random initialization by default */
        initialize_grid(current_grid);
    }

    /* Install signal handler for Ctrl-C (SIGINT) and SIGTERM. */
    (void) signal(SIGINT,  restore_on_exit);
    (void) signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    printf("\033[?25l");

    /* Set scrolling region to full screen (1..24).
       Adjust if your terminal has more (or fewer) lines. */
    printf("\033[1;24r");

    /* Clear screen once at the beginning */
    printf("\033[2J");

    fflush(stdout);

    /* Main loop: compute next generation and render */
    while (1) {
        /* Compute next generation */
        for (row = 0; row < HEIGHT; row++) {
            for (col = 0; col < WIDTH; col++) {
                int alive = current_grid[row][col];
                int neighbors = count_neighbors(current_grid, row, col);

                if (alive) {
                    if (neighbors < 2 || neighbors > 3)
                        next_grid[row][col] = 0;  /* Cell dies */
                    else
                        next_grid[row][col] = 1;  /* Cell lives */
                } else {
                    if (neighbors == 3)
                        next_grid[row][col] = 1;  /* Cell becomes alive */
                    else
                        next_grid[row][col] = 0;  /* Cell remains dead */
                }
            }
        }

        /* Render the next generation */
        render_grid(next_grid);

        /* Swap grids */
        copy_grid(next_grid, current_grid);

        /* Small delay: Adjust to taste. */
        usleep(50000);
    }

    /* Normally never reached, but just in case */
    restore_on_exit(0);

    return 0;
}

/* Initialize the grid with random alive (1) or dead (0) cells */
void initialize_grid(int current[HEIGHT][WIDTH])
{
    int row, col;
    int random_val;
    
    /* Seed random number generator with current time for variety */
    srand((unsigned int)time(NULL));

    /* First, clear the entire grid */
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < WIDTH; col++) {
            current[row][col] = 0;
        }
    }

    /* Now add some random live cells with a lower density */
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < WIDTH; col++) {
            random_val = rand() % 100;  /* Get value 0-99 */
            /* Only make about 15% of cells alive for more stable patterns */
            if (random_val < 15) {
                current[row][col] = 1;
            }
        }
    }
}

/* Initialize with a known test pattern for debugging */
void initialize_test_pattern(int current[HEIGHT][WIDTH])
{
    int row, col;
    
    /* Clear the entire grid first */
    for (row = 0; row < HEIGHT; row++) {
        for (col = 0; col < WIDTH; col++) {
            current[row][col] = 0;
        }
    }
    
    /* Add some known stable and oscillating patterns */
    
    /* Block (stable) at position (5,5) */
    current[5][5] = 1;
    current[5][6] = 1;
    current[6][5] = 1;
    current[6][6] = 1;
    
    /* Blinker (oscillator) at position (10,10) */
    current[10][9] = 1;
    current[10][10] = 1;
    current[10][11] = 1;
    
    /* Glider at position (15,15) */
    current[15][16] = 1;
    current[16][17] = 1;
    current[17][15] = 1;
    current[17][16] = 1;
    current[17][17] = 1;
    
    /* Toad (oscillator) at position (5,20) */
    current[5][20] = 1;
    current[5][21] = 1;
    current[5][22] = 1;
    current[6][19] = 1;
    current[6][20] = 1;
    current[6][21] = 1;
}

/* Copy source grid to destination grid */
void copy_grid(int source[HEIGHT][WIDTH], int dest[HEIGHT][WIDTH])
{
    int row;
    memcpy(dest, source, sizeof(int) * HEIGHT * WIDTH);
}

/* Count the number of alive neighbors for a cell at (row, col) */
int count_neighbors(int grid[HEIGHT][WIDTH], int row, int col)
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
                r = HEIGHT - 1;
            else if (r >= HEIGHT)
                r = 0;

            if (c < 0)
                c = WIDTH - 1;
            else if (c >= WIDTH)
                c = 0;

            count += grid[r][c];
        }
    }

    return count;
}

/* Render the grid to the terminal */
void render_grid(int grid[HEIGHT][WIDTH])
{
    int row, col;

    /* Move cursor to top-left */
    printf("\033[1;1H");

    /* Iterate through the grid and print characters */
    for (row = 0; row < HEIGHT - 1; row++) {  /* Leave last row for status */
        for (col = 0; col < WIDTH; col++) {
            if (grid[row][col])
                putchar(ALIVE_CHAR);
            else
                putchar(DEAD_CHAR);
        }
        if (row < HEIGHT - 2)  /* Don't print newline on last row */
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

    /* Reset scrolling region (assuming 24 lines) */
    printf("\033[r");

    /* Move cursor to bottom-left */
    printf("\033[24;1H");

    /* Flush out any pending output */
    fflush(stdout);

    /* Exit gracefully */
    exit(0);
}
