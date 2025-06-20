#include <stdio.h>
#include <stdlib.h>

/* VT220 soft font definitions for space, 'B' (ball), 'P' (paddle), 'X' (block) */
#define LOAD_GAME_SOFTFONT "\033P1;0;1;2;0;0{M0;????????????????/34;??????K?K???????/48;?B?B?B?B?B?B?B?B/56;~~~~~~~~~~~~~~~~\033\\"
#define SELECT_GAME_SOFTFONT "\033( M"
#define UNSELECT_SOFTFONT "\033(B"

/* Game area constants (1-based for VT220 cursor addressing) */
#define GAME_TOP 2        /* Line 2 */
#define GAME_BOTTOM 23    /* Line 23 */
#define GAME_LEFT 11      /* Column 11 */
#define GAME_RIGHT 70     /* Column 70 */
#define BLOCK_ROWS 4
#define BLOCK_COLS 60
#define PADDLE_WIDTH 5
#define PADDLE_Y 23       /* Paddle fixed at bottom */

/* Game state variables */
int score = 0;
int paddle_x = 33;        /* Paddle starts centered */
int prev_paddle_x = 33;   /* Previous paddle position */
int ball_x = 40;          /* Ball starts near center */
int ball_y = 20;          /* Ball starts above paddle */
int prev_ball_x = 40;     /* Previous ball position */
int prev_ball_y = 20;
int dx = 1;               /* Ball x-direction */
int dy = -1;              /* Ball y-direction */
char blocks[BLOCK_ROWS][BLOCK_COLS];  /* 1 = block present, 0 = destroyed */

/* Function prototypes */
void init_game();
void draw_initial_screen();
void update_game();
void erase_ball();
void draw_ball();
void erase_paddle();
void draw_paddle();
void update_score();
void erase_block(int row, int col);

int main() {
    /* Load soft font into VT220 */
    printf(LOAD_GAME_SOFTFONT);

    /* Hide cursor to prevent flicker */
    printf("\033[?25l");

    /* Set up game state */
    init_game();

    /* Draw initial static elements */
    draw_initial_screen();

    /* Main game loop */
    while (1) {
        /* Erase old positions */
        erase_ball();
        erase_paddle();

        /* Update positions and logic */
        update_game();

        /* Draw new positions */
        draw_ball();
        draw_paddle();

        /* Send all changes to terminal */
        fflush(stdout);

        /* Add a delay here if needed (e.g., usleep(10000)) */
    }

    /* Restore cursor (unreachable due to loop) */
    printf("\033[?25h");
    return 0;
}

/* Initialize game state */
void init_game() {
    int i, j;
    for (i = 0; i < BLOCK_ROWS; i++) {
        for (j = 0; j < BLOCK_COLS; j++) {
            blocks[i][j] = 1;  /* All blocks start present */
        }
    }
}

/* Draw static elements once at startup */
void draw_initial_screen() {
    int row, col;

    /* Clear screen once */
    printf("\033[2J");

    /* Draw score at top */
    printf("\033[1;1HScore: %d", score);

    /* Draw all blocks initially */
    for (row = 0; row < BLOCK_ROWS; row++) {
        printf("\033[%d;11H", row + 2);  /* Position cursor */
        printf(SELECT_GAME_SOFTFONT);
        for (col = 0; col < BLOCK_COLS; col++) {
            putchar(blocks[row][col] ? 'X' : ' ');  /* 'X' for block, space for empty */
        }
        printf(UNSELECT_SOFTFONT);
    }

    /* Draw initial paddle and ball */
    draw_paddle();
    draw_ball();
}

/* Erase ball from its old position */
void erase_ball() {
    printf(UNSELECT_SOFTFONT);  /* Select standard font for erasing */
    printf("\033[%d;%dH ", prev_ball_y, prev_ball_x);
    fflush(stdout);
}

/* Draw ball at its new position */
void draw_ball() {
    printf("\033[%d;%dH", ball_y, ball_x);
    printf(SELECT_GAME_SOFTFONT);
    putchar('B');  /* Ball sprite */
    printf(UNSELECT_SOFTFONT);
    fflush(stdout);
}

/* Erase paddle from its old position */
void erase_paddle() {
    int col;
    printf(UNSELECT_SOFTFONT);  /* Select standard font for erasing */
    printf("\033[%d;%dH", PADDLE_Y, prev_paddle_x);
    for (col = 0; col < PADDLE_WIDTH; col++) {
        putchar(' ');
    }
    fflush(stdout);
}

/* Draw paddle at its new position */
void draw_paddle() {
    int col;
    printf("\033[%d;%dH", PADDLE_Y, paddle_x);
    printf(SELECT_GAME_SOFTFONT);
    for (col = 0; col < PADDLE_WIDTH; col++) {
        putchar('P');  /* Paddle sprite */
    }
    printf(UNSELECT_SOFTFONT);
    fflush(stdout);
}

/* Update score display */
void update_score() {
    printf("\033[1;1HScore: %d", score);
    fflush(stdout);
}

/* Erase a destroyed block */
void erase_block(int row, int col) {
    printf(UNSELECT_SOFTFONT);  /* Select standard font for erasing */
    printf("\033[%d;%dH ", row + 2, col + 11);
    fflush(stdout);
}

/* Update game logic */
void update_game() {
    int new_x = ball_x + dx;
    int new_y = ball_y + dy;

    /* Store previous positions */
    prev_paddle_x = paddle_x;
    prev_ball_x = ball_x;
    prev_ball_y = ball_y;

    /* Simple paddle AI: follow ball */
    if (ball_x < paddle_x + 2 && paddle_x > GAME_LEFT) {
        paddle_x--;
    } else if (ball_x > paddle_x + 2 && paddle_x < GAME_RIGHT - PADDLE_WIDTH + 1) {
        paddle_x++;
    }

    /* Ball collisions */
    /* Walls */
    if (new_x < GAME_LEFT || new_x > GAME_RIGHT) {
        dx = -dx;
        new_x = ball_x + dx;
    }
    if (new_y < GAME_TOP) {
        dy = -dy;
        new_y = ball_y + dy;
    }
    /* Paddle or floor */
    else if (new_y == PADDLE_Y) {
        if (new_x >= paddle_x && new_x < paddle_x + PADDLE_WIDTH) {
            dy = -dy;
            new_y = ball_y + dy;
        } else {
            /* Reset ball if missed */
            ball_x = 40;
            ball_y = 20;
            dx = 1;
            dy = -1;
            return;
        }
    }
    /* Blocks */
    else if (new_y >= 2 && new_y <= 5) {
        int block_row = new_y - 2;
        int block_col = new_x - 11;
        if (block_row >= 0 && block_row < BLOCK_ROWS && 
            block_col >= 0 && block_col < BLOCK_COLS && 
            blocks[block_row][block_col]) {
            blocks[block_row][block_col] = 0;  /* Destroy block */
            score++;
            dy = -dy;
            new_y = ball_y + dy;
            erase_block(block_row, block_col);
            update_score();
        }
    }

    /* Update ball position */
    ball_x = new_x;
    ball_y = new_y;
}