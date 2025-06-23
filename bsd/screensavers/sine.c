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
#include <sys/ioctl.h>

#if defined(__NetBSD__) || defined(__APPLE__) || defined(__linux__)
#define USE_DELAY 1
#include <unistd.h>
#else
#define USE_DELAY 0
#endif

/* Default fallback values if terminal size detection fails */
#define DEFAULT_WIDTH 80
#define DEFAULT_HEIGHT 24

#define NUMSINES 3  /* Number of sine waves */
#ifndef M_PI
#define M_PI 3.14159265358979323846  /* Define pi if not defined */
#endif

/* Global variables for screen dimensions */
int SCREEN_WIDTH = DEFAULT_WIDTH;
int SCREEN_HEIGHT = DEFAULT_HEIGHT;

/* Function to get terminal size */
void get_terminal_size()
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    
    /* Try to get window size using ioctl */
    if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        SCREEN_WIDTH = ws.ws_col;
        SCREEN_HEIGHT = ws.ws_row;
        return;
    }
#endif
    
    /* Fallback: try environment variables */
    {
        char *cols_env = getenv("COLUMNS");
        char *lines_env = getenv("LINES");
        
        if (cols_env != NULL) {
            int cols = atoi(cols_env);
            if (cols > 0) SCREEN_WIDTH = cols;
        }
        
        if (lines_env != NULL) {
            int lines = atoi(lines_env);
            if (lines > 0) SCREEN_HEIGHT = lines;
        }
    }
    
    /* If all else fails, use the defaults already set */
}

static void restore_on_exit();

int main()
{
    double angles[NUMSINES];
    double angle_steps[NUMSINES];
    int col, i;
    int center_col, amplitude;

    /* Get terminal dimensions */
    get_terminal_size();
    
    /* Calculate dynamic center and amplitude based on screen width */
    center_col = SCREEN_WIDTH / 2;
    amplitude = (SCREEN_WIDTH > 20) ? (SCREEN_WIDTH / 2 - 5) : (SCREEN_WIDTH / 3);

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

    /* Set scrolling region to full screen using detected size */
    printf("\033[1;%dr", SCREEN_HEIGHT);

    /* Clear screen once at the beginning (optional) */
    printf("\033[2J");

    fflush(stdout);

    /* Main loop: increment angles and draw sine waves */
    while (1) {
        for (i = 0; i < NUMSINES; i++) {
            /* Compute column for this sine wave using dynamic dimensions */
            col = center_col + (int)((double)amplitude * sin(angles[i]));
            if (col < 1)   col = 1;
            if (col > SCREEN_WIDTH)  col = SCREEN_WIDTH;

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

    /* Reset scrolling region */
    printf("\033[r");

    /* Move cursor to bottom of screen */
    printf("\033[%d;1H", SCREEN_HEIGHT);

    /* Flush out any pending output */
    fflush(stdout);

    /* Exit gracefully */
    exit(0);
}
