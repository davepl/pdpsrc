/* vt220_mosaic.c - 64-tile 6x2 DRCS demo for real VT220 on 2.11BSD */
/* cc vt220_mosaic.c -o mosaic ; ./mosaic */

#include <stdio.h>

int main() {
    int i, c;

    /* Clear screen and home cursor */
    printf("\033[H\033[J");

    /* Load 64 soft characters: 6 wide, 2 sixels high, 94-char set */
    /* We use chars ! through ` (0x21-0x60) */
    printf("\033P1;1;0!2?");   /* start DECDLD, 10-pixel wide, 2 high */
    
    for (i = 0; i < 64; i++) {
        c = i + 0x21;               /* char code ! to ` */
        if (i > 0) printf("!");     /* separator */
        printf("1z%c", c);          /* define this char */
        /* two sixels: high byte first, then low */
        printf("%c%c", (i >> 6) + '?', (i & 63) + '?');
    }
    printf("\033\\\\");   /* end DECDLD */

    /* Designate soft font as G0 and invoke it */
    printf("\033)0");     /* SCS G0 = DRCS */
    printf("\016");       /* SO - shift out, invoke G0 */

    /* Draw a quick test pattern */
    for (i = 0; i < 24; i++) {
        int x;
        for (x = 0; x < 80; x++) {
            int pat = (x * 63 / 80) ^ (i * 63 / 23);
            putchar(pat + 0x21);
        }
        putchar('\r');
        putchar('\n');
    }

    /* Back to normal ASCII so you can see prompt again */
    printf("\033(B\017");   /* USASCII G0 + SI */
    printf("Done - soft font loaded and used.\n");
    return 0;
}
