#include <stdio.h>
#include <stddef.h>
#include "arch/NetBSDVAX/panel_state.h"

int main() {
    printf("=== VAX Panel Structure Detailed Analysis ===\n");
    printf("Platform: %s\n", 
#ifdef __APPLE__
           "macOS"
#elif defined(__NetBSD__)
           "NetBSD"
#elif defined(__linux__)
           "Linux"
#else
           "Unknown"
#endif
    );
    
    printf("\n=== Basic Type Sizes ===\n");
    printf("sizeof(unsigned int) = %zu bytes\n", sizeof(unsigned int));
    printf("sizeof(short) = %zu bytes\n", sizeof(short));
    printf("sizeof(u16_t) = %zu bytes\n", sizeof(u16_t));
    printf("sizeof(u32_t) = %zu bytes\n", sizeof(u32_t));
    
    printf("\n=== Structure Sizes ===\n");
    printf("sizeof(struct panel_packet_header) = %zu bytes\n", sizeof(struct panel_packet_header));
    printf("sizeof(struct vax_panel_state) = %zu bytes\n", sizeof(struct vax_panel_state));
    printf("sizeof(struct vax_panel_packet) = %zu bytes\n", sizeof(struct vax_panel_packet));
    
    printf("\n=== Expected vs Actual ===\n");
    printf("Header size: %zu bytes\n", sizeof(struct panel_packet_header));
    printf("Panel state size: %zu bytes\n", sizeof(struct vax_panel_state));
    printf("Total expected: %zu bytes\n", sizeof(struct panel_packet_header) + sizeof(struct vax_panel_state));
    printf("Actual packet size: %zu bytes\n", sizeof(struct vax_panel_packet));
    
    printf("\n=== Field Offsets ===\n");
    struct vax_panel_state s;
    printf("ps_address offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_address), sizeof(s.ps_address));
    printf("ps_data offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_data), sizeof(s.ps_data));
    printf("ps_psw offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_psw), sizeof(s.ps_psw));
    printf("ps_mser offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_mser), sizeof(s.ps_mser));
    printf("ps_cpu_err offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_cpu_err), sizeof(s.ps_cpu_err));
    printf("ps_mmr0 offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_mmr0), sizeof(s.ps_mmr0));
    printf("ps_mmr3 offset: %zu (size: %zu)\n", offsetof(struct vax_panel_state, ps_mmr3), sizeof(s.ps_mmr3));
    
    return 0;
}
