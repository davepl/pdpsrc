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

#define MAX_TRAILS 16
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24


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
    const char* softfont[] = {
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
    printf("\033[?25h");
    /* Reset scrolling region to the entire screen (1..24 or as needed) */
    printf("\033[1;24r");
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
                c = (rand() % (94+46));
                if (c < 94)
                {
                    // Normal ASCII character
                    c += '!';
                    if (softfontCharsetSelected)
                    {
                        printf(UNSELECT_SOFTFONT);
                        softfontCharsetSelected = 0;
                    }
                    putchar(c);
                }
                else
                {
                    // Mirrored Katakana character
                    c = c - 94 + '!';
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

    /* Seed the random generator */
    srand(time((long *)0));

    /* Install signal handlers */
    signal(SIGINT, restore_on_exit);
    signal(SIGTERM, restore_on_exit);

    /* Hide the cursor */
    printf("\033[?25l");

    /* Set scrolling region to full screen */
    printf("\033[1;24r");

    /* Load Matrix softfont */
    loadMatrixSoftfont();

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

        /* Reset cursor to top-left (optional) */
        printf("\033[1;1H");

        /* Flush output */
        fflush(stdout);

        /* Small delay */

        trail_timer++;
    }

    /* Normally never reached */
    restore_on_exit(0);
    return 0;
}
