#include <stdio.h>
#include "arch/211BSD/panel_state.h"
#include "arch/NetBSDVAX/panel_state.h"

/* The same wire layout must be used by the PDP sender and modern receiver. */
int main()
{
    if (sizeof(struct panel_packet_header) != 6 ||
        sizeof(struct pdp_panel_state) != 16 ||
        sizeof(struct pdp_panel_packet) != 22 ||
        sizeof(struct vax_panel_state) != 8 ||
        sizeof(struct vax_panel_packet) != 14) {
        fprintf(stderr, "Unexpected panel packet layout\n");
        return 1;
    }
    puts("Panel packet layouts: PASS");
    return 0;
}
