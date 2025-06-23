/* 
 * matrix2.c - Draws a matrix-like rain of characters on the screen.
 *            Trails are drawn with configurable length.
 *            Written in K&R style for 2.11BSD, etc.
 *
 *  Compile example (on 2.11BSD, might need -lm for math library):
 *    cc -o matrix matrix.c -lm
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

#define MAX_TRAILS 20

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

/* Structure to represent a trail */
struct Trail {
    int column;      /* Column where the trail is active */
    int head;        /* Current head position of the trail */
    int length;      /* Length of the trail */
    int active;      /* Whether this trail is active */
};

struct Trail trails[MAX_TRAILS];
int trail_timer = 0;

/* Signal handler to restore the cursor and reset scrolling region */
void restore_on_exit(signum)
int signum;
{
    /* Show the cursor again */
    printf("\033[?25h");
    /* Reset scrolling region to the entire screen */
    printf("\033[r");
    /* Move cursor to bottom of screen */
    printf("\033[%d;1H", SCREEN_HEIGHT);
    /* Optionally clear screen or any other cleanup */
    fflush(stdout);

    /* Exit gracefully */
    exit(0);
}

void initialize_trails()
{
    int i;
    for (i = 0; i < MAX_TRAILS; i++) {
        trails[i].active = 0;
    }
}

void start_new_trail(length)
int length;
{
    int i;
    for (i = 0; i < MAX_TRAILS; i++) {
        if (!trails[i].active) {
            trails[i].column = rand() % SCREEN_WIDTH;
            trails[i].head = 0; /* Start at the top of the screen */
            trails[i].length = length;
            trails[i].active = 1;
            break;
        }
    }
}

void update_trails()
{
    int i;
    char c;

    for (i = 0; i < MAX_TRAILS; i++) 
    {
        if (trails[i].active) 
        {
            int erase_pos = trails[i].head - trails[i].length;
            
            /* Draw the new head of the trail */
            if (trails[i].head < SCREEN_HEIGHT) {
                printf("\033[%d;%dH", trails[i].head + 1, trails[i].column + 1);
                c = '!' + (rand() % 94);
                putchar(c);
            }

            /* Erase the tail of the trail */
            if (erase_pos >= 0 && erase_pos < SCREEN_HEIGHT) {
                printf("\033[%d;%dH ", erase_pos + 1, trails[i].column + 1);
            }

            /* Move the trail downward */
            trails[i].head++;

            /* Deactivate the trail if it has moved off-screen */
            if (trails[i].head - trails[i].length >= SCREEN_HEIGHT) {
                trails[i].active = 0;
            }
        }
    }
}

int main()
{
    int trail_length = 14; /* Configurable length of the trail */
    int spawn_rate = 3;   /* Configurable spawn rate */

    /* Seed the random generator */
    srand(time((time_t *)0));

    /* Install signal handlers */
    signal(SIGINT, restore_on_exit);
    signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    printf("\033[?25l");

    /* Get the terminal size first */
    get_terminal_size();

    if (SCREEN_HEIGHT - 10 > trail_length)
        trail_length = SCREEN_HEIGHT - 10; /* Ensure trail length fits on screen */

    /* Set scrolling region to full screen using detected size */
    printf("\033[1;%dr", SCREEN_HEIGHT);

    /* Clear screen */
    printf("\033[2J");

    /* Initialize trails */
    initialize_trails();

    for (;;) {
        /* Start a new trail periodically */
        if (trail_timer % spawn_rate == 0) {
            start_new_trail(trail_length);
        }

        /* Update and draw trails */
        update_trails();

        /* Flush output */
        fflush(stdout);

        /* Small delay */
#if USE_DELAY
        usleep(40000);
#endif

        trail_timer++;
    }

    /* Normally never reached */
    restore_on_exit(0);
    return 0;
}
