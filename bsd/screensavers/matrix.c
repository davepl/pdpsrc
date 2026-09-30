/* 
 * matrix.c - Draws a matrix-like rain of characters on the screen.
 *            then scroll the screen down (VT100 Reverse Index).
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

#define MAX_TRAILS 16

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


#define SELECT_MATRIX_SOFTFONT "\033( M"
#define UNSELECT_SOFTFONT "\033(B"


/* Structure to represent a trail */
struct Trail {
    int column;      /* Column where the trail is active */
    int rows_drawn;  /* Number of rows drawn so far */
    int length;      /* Length of the trail */
    int active;      /* Whether this trail is active */
};

struct Trail trails[MAX_TRAILS];
int trail_timer = 0;

/* Keep track of whether the Matrix softfont or the default ASCII ROM font is selected */
int softfontCharsetSelected = 0;


void loadMatrixSoftfont()
{
    int i;

    /*
    ** Strings to load a VT220 softfont providing the extra Matrix characters, select it and unselect it.
    ** This softfont contains 46 mirrored katakana as ASCII !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMN
    ** The real term is a Dynamically Replaceable Character Sets (DRCS) loaded into the terminal's
    ** memory using a Down-Line-Loading DRCS (DECDLD) control string. The font is built using Sixels.
    ** Font design by Philippe Majerus, January 2025
    */
    static const char* softfont[] = {
        // DECDLD parameters
        "\033P1;1;2{ M",
        // Characters sixels:
        /* ! */ "MQAyAAA\?/\?\?\?\?@A\?\?"    ";",
        /* " */ "\?ACwGOO\?/\?\?\?B\?\?\?\?"  ";",
        /* # */ "{CCECC[\?/\?@AAA\?\?\?"      ";",
        /* $ */ "\?CC{CC\?\?/AAABAAA\?"       ";",
        /* % */ "CC}ScCC\?/\?\?B\?\?@A\?"     ";",
        /* & */ "{CCC}CC\?/@AAA\?@A\?"        ";",
        /* ' */ "_gg}gg_\?/\?\?\?B\?\?\?\?"   ";",
        /* ( */ "{CCCCMO\?/\?@AAA\?\?\?"      ";",
        /* ) */ "CC{CCMO\?/\?\?\?@AA\?\?"     ";",
        /* * */ "{CCCCCC\?/BAAAAAA\?"         ";",
        /* + */ "C}CCC]C\?/\?\?@AAA\?\?"      ";",
        /* , */ "]_\?\?SSS\?/\?\?@AAAA\?"     ";",
        /* - */ "EIqaAAA\?/A@\?\?@AA\?"       ";",
        /* . */ "KSCCC}C\?/AAAAA@\?\?"        ";",
        /* / */ "Mo\?\?\?WE\?/\?\?@AAAA\?"    ";",
        /* 0 */ "{csSCMO\?/\?@AAAA\?\?"       ";",
        /* 1 */ "OQQ{SSO\?/\?\?\?@A\?\?\?"    ";",
        /* 2 */ "]_\?M\?\?M\?/\?\?@AAAA\?"    ";",
        /* 3 */ "GIIyIIG\?/\?\?\?@A\?\?\?"    ";",
        /* 4 */ "OOGG}\?\?\?/\?\?\?\?B\?\?\?" ";",
        /* 5 */ "GGG}GGG\?/\?\?\?@AA\?\?"     ";",
        /* 6 */ "\?CCCCC\?\?/AAAAAAA\?"       ";",
        /* 7 */ "MQaQAAA\?/A@\?@AA\?\?"       ";",
        /* 8 */ "CkSecCC\?/@\?\?B\?@@\?"      ";",
        /* 9 */ "Mo\?\?\?\?\?\?/\?\?@@AAA\?"  ";",
        /* : */ "\?wC\?[_\?\?/B\?\?\?\?@A\?"  ";",
        /* ; */ "CCCGGG}\?/AAAAAA@\?"         ";",
        /* < */ "]aAAAAA\?/\?\?@AAA\?\?"      ";",
        /* = */ "\?_OGCGO\?/@\?\?\?\?\?\?\?"  ";",
        /* > */ "CsC}CsC\?/@\?\?BA\?@\?"      ";",
        /* ? */ "EIQaaQA\?/\?\?A@\?\?\?\?"    ";",
        /* @ */ "CSQIII\?\?/AAA@@@@\?"        ";",
        /* A */ "\?o\?EW_\?\?/B@@AABA\?"      ";",
        /* B */ "MO_OG\?\?\?/A@\?@AAA\?"      ";",
        /* C */ "OQQQ}QO\?/AAAA@\?\?\?"       ";",
        /* D */ "WgGG}GG\?/\?\?@\?B\?\?\?"    ";",
        /* E */ "\?\?{CCC\?\?/AABAAAA\?"      ";",
        /* F */ "}QQQQQQ\?/BAAAAAA\?"         ";",
        /* G */ "WiIIIIG\?/\?\?@AAA\?\?"      ";",
        /* H */ "}\?\?\?\?\?]\?/\?@AAAA\?\?"  ";",
        /* I */ "_\?}\?\?}\?\?/\?@B\?\?@A\?"  ";",
        /* J */ "O_\?\?\?\?}\?/\?\?@@AAB\?"   ";",
        /* K */ "}AAAAA}\?/BAAAAAB\?"         ";",
        /* L */ "]aAAAAM\?/\?\?@AAA\?\?"      ";",
        /* M */ "]iIIIII\?/\?\?@AAA\?\?"      ";",
        /* N */ "]_\?\?CCC\?/\?\?@AAAA\?"     "\033\\"
        // Other characters are not defined in this softfont
    };

    for (i=0;i<sizeof(softfont)/sizeof(softfont[0]);i++)
    {
        printf("%s", softfont[i]);
    }
}

/* Signal handler to restore the cursor and reset scrolling region */
void restore_on_exit(signum)
int signum;
{
    /* Ensure we don't leave the Matrix softfont selected */
    printf(UNSELECT_SOFTFONT);
    /* Show the cursor again */
    puts("\033[?25h");
    /* Reset scrolling region to the entire screen */
    puts("\033[r");
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
            trails[i].rows_drawn = 0; /* Start with no rows drawn */
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
            /* Draw only in the first row if the trail is active */
            if (trails[i].rows_drawn < trails[i].length) 
            {
                printf("\033[1;%dH", trails[i].column + 1);
                /* Preserve main's Katakana-only rain, selecting once. */
                c = (rand() % 46) + '!';
                {
                    if (!softfontCharsetSelected)
                    {
                        printf(SELECT_MATRIX_SOFTFONT);
                        softfontCharsetSelected = 1;
                    }
                    putchar(c);
                }
                
                /* Increment the number of rows drawn */
                trails[i].rows_drawn++;
            }
            else
            {       
                trails[i].active = 0;
            }
        }
    }

    /* Scroll the screen down by one row */
    printf("\033M");
}

int main()
{
    int trail_length = 8; /* Configurable length of the trail */
    int spawn_rate = 1;   /* Configurable spawn rate */

    puts("Starting!");

    /* Seed the random generator */
    srand(time((time_t *)0));

    /* Get the terminal size first */
    get_terminal_size();

    if (SCREEN_HEIGHT - 10 > trail_length)
        trail_length = SCREEN_HEIGHT - 10;
        
    /* Install signal handlers */
    signal(SIGINT, restore_on_exit);
    signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    puts("\033[?25l");

    /* Set scrolling region to full screen using detected size */
    printf("\033[1;%dr", SCREEN_HEIGHT);

    /* Load Matrix softfont */
    loadMatrixSoftfont();

    /* Clear screen */
    puts("\033[2J");

    /* Initialize trails */
    initialize_trails();

    /* Get the terminal size */
    get_terminal_size();

    for (;;) {
        /* Start a new trail periodically */
        if (trail_timer % spawn_rate == 0) {
            start_new_trail(trail_length);
        }

        /* Update and draw trails */
        update_trails();

        /* Reset cursor to top-left (optional) */
        puts("\033[1;1H");

        /* Flush output */
        fflush(stdout);

        /* Small delay */
#if USE_DELAY
        usleep(50000); /* 50ms delay on NetBSD */
#endif

        trail_timer++;
    }

    /* Normally never reached */
    restore_on_exit(0);
    return 0;
}
