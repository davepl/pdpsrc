/* Optional native layout measurement; not part of the tmux executable. */
#include "tmux.h"
int
main(void)
{
    printf("cell=%u line=%u utf8=%u client=%u imsg=%u\n",
        sizeof(struct grid_cell), sizeof(struct grid_line),
        sizeof(struct grid_utf8), sizeof(struct client), sizeof(struct imsgbuf));
    printf("window=%u pane=%u session=%u option=%u tty=%u\n",
        sizeof(struct window), sizeof(struct window_pane), sizeof(struct session),
        sizeof(struct options_entry), sizeof(struct tty));
    return 0;
}
