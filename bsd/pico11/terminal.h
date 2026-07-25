#ifndef TERMINAL_H
#define TERMINAL_H

#define KEY_UP       256
#define KEY_DOWN     257
#define KEY_LEFT     258
#define KEY_RIGHT    259
#define KEY_HOME     260
#define KEY_END      261
#define KEY_PGUP     262
#define KEY_PGDN     263
#define KEY_DELETE   264

int term_open(void);
void term_close(void);
void term_size(int *rows, int *cols);
int term_key(void);
void term_put(char *s);
void term_write(char *s, int n);
void term_move(int row, int col);
void term_clear(void);

#endif
