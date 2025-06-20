/*
 *  sine.c - Draw multiple out-of-phase sine waves in a "biorhythm" style.
 *           Written in K&R style for 2.11BSD, etc.
 *
 *  Compile example (on 2.11BSD, might need -lm for math library):
 *    cc -o sine sine.c -lm
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
#include <math.h>

#if defined(__NetBSD__) || defined(__APPLE__) || defined(__linux__)
#define USE_DELAY 1
#include <unistd.h>
#else
#define USE_DELAY 0
#endif

#define NUMSINES 3  /* Number of sine waves */
#ifndef M_PI
#define M_PI 3.14159265358979323846  /* Define pi if not defined */
#endif

static void restore_on_exit();

int main()
{
    double angles[NUMSINES];
    double angle_steps[NUMSINES];
    int col, i;

    /* Initialize angles and steps for each sine wave */
    for (i = 0; i < NUMSINES; i++) {
        angles[i] = 1.5 * i * (M_PI / NUMSINES); /* Phase shift for each sine wave */
        angle_steps[i] = 0.1;             /* Fixed step size */
    }

    /* Install signal handler for Ctrl-C (SIGINT) and SIGTERM. */
    (void) signal(SIGINT,  restore_on_exit);
    (void) signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    printf("\033[?25l");

    /* Set scrolling region to full screen (1..24).
       Adjust if your terminal has more (or fewer) lines. */
    printf("\033[1;24r");

    /* Clear screen once at the beginning (optional) */
    printf("\033[2J");

    fflush(stdout);

    /* Main loop: increment angles and draw sine waves */
    while (1) {
        for (i = 0; i < NUMSINES; i++) {
            /* Compute column for this sine wave */
            col = 40 + (int)(35.0 * sin(angles[i]));
            if (col < 1)   col = 1;
            if (col > 80)  col = 80;

            /* Move cursor to top row, (col) in 1-based indexing */
            printf("\033[1;%dH", col);

            /* Print the '*' for this sine wave */
            putchar('*');

            /* Increment angle for wave motion */
            angles[i] += angle_steps[i];
        }

        /* Move cursor back to top-left (just to be sure) */
        printf("\033[1;1H");

        /* Issue Reverse Index to scroll entire screen down by 1 line */
        printf("\033M");

        /* Flush output so it shows up now */
        fflush(stdout);

        /* Small delay */
#if USE_DELAY
        usleep(50000); /* 50ms delay on NetBSD */
#endif

    }

    /* Normally never reached, but just in case */
    restore_on_exit(0);

    return 0;
}

/* Signal handler to restore the terminal when exiting */
static void restore_on_exit(signum)
int signum;
{
    /* Show the cursor again */
    printf("\033[?25h");

    /* Reset scrolling region (assuming 24 lines) */
    printf("\033[1;24r");

    /* Flush out any pending output */
    fflush(stdout);

    /* Exit gracefully */
    exit(0);
}
