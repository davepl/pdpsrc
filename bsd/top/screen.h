/* Small termcap display; dimensions exclude no rows, but reserve the last
 * column so an update can never wrap or scroll at the right margin. */
extern int screen_rows, screen_cols;
extern char *screen_error;

/* start also handles resize; stop restores the original sgtty flags.
 * Each complete frame is begin, zero or more rows, finish.  A prompt can
 * replace a single row and call flush without erasing the other rows. */
int screen_start();
void screen_begin(), screen_row(), screen_finish(), screen_flush();
void screen_clear(), screen_stop();
