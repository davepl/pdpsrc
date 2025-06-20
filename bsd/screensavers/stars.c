/*
 * stars.c - Simple blinking stars screensaver for the VT100 terminal.
 *            Written in K&R style for 2.11BSD, etc.
 *
 *  Compile example (on 2.11BSD, might need -lm for math library):
 *    cc -o stars stars.c -lm
 *
 *  Press Ctrl-C to exit (SIGINT), which restores the cursor and
 *  resets the scrolling region.
 * 
 *  Author: Dave Plummer, 2024. 
 *  License: GPL 2.0
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#ifdef __NetBSD__
#include <unistd.h>
#endif

#define NUM_STARS 20
#define SCREEN_ROWS 24
#define SCREEN_COLS 80
#define STAR_CHAR '*'
#define SPACE_CHAR ' '
#define DELAY_COUNT 20000 /* Approximate delay loop count */

struct Star {
    int row;
    int col;
};

/* Global star array */
struct Star stars[NUM_STARS];

/* Function prototypes */
void cleanup();
void handle_signal();
int random_coord();
void draw_star();
void delay();

/* Cleanup function to reset terminal before exiting */
void cleanup() {
    printf("\033[2J\033[H"); /* Clear screen and reset cursor */
    printf("\033[?25h");     /* Show cursor */
    printf("Exiting...\n");
    fflush(stdout);
}

/* Signal handler to ensure cleanup on termination */
void handle_signal(sig)
int sig;
{
    cleanup();
    exit(0);
}

/* Random number generator for row/col */
int random_coord(max)
int max;
{
    return rand() % max + 1; /* Generate 1-based random coordinate */
}

/* Function to draw a star at a specific location */
void draw_star(row, col, ch)
int row, col;
char ch;
{
    printf("\033[%d;%dH%c", row, col, ch); /* Position cursor and draw character */
    fflush(stdout);
}

/* Delay function (busy loop) */
void delay()
{
    int i;
    for (i = 0; i < DELAY_COUNT; i++) {
        /* Empty loop to create delay */
    }
}

/* Main function */
int main()
{
    int i;

    /* Setup signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Clear the screen and hide the cursor */
    printf("\033[2J");
    printf("\033[?25l");
    fflush(stdout);

    /* Seed the random number generator */
    srand(time(0));

    /* Initialize stars with random positions and draw them */
    for (i = 0; i < NUM_STARS; i++) {
        stars[i].row = random_coord(SCREEN_ROWS);
        stars[i].col = random_coord(SCREEN_COLS);
        draw_star(stars[i].row, stars[i].col, STAR_CHAR);
    }

    /* Main animation loop */
    while (1) {
        for (i = 0; i < NUM_STARS; i++) {
            /* Erase the current star */
            draw_star(stars[i].row, stars[i].col, SPACE_CHAR);

            /* Generate a new position */
            stars[i].row = random_coord(SCREEN_ROWS);
            stars[i].col = random_coord(SCREEN_COLS);

            /* Draw the star at the new position */
            draw_star(stars[i].row, stars[i].col, STAR_CHAR);

            /* Delay after moving each star */
#ifdef __NetBSD__
            usleep(30000); /* 30ms delay on NetBSD */
#endif
        }
    }

    /* Cleanup (won't reach here due to infinite loop) */
    cleanup();
    return 0;
}
