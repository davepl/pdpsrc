/*
 * panel_state.h - NetBSD VAX panel structure definition
 * Uses /dev/kmem for kernel memory access
 */

#ifndef VAX_PANEL_STATE_H
#define VAX_PANEL_STATE_H

#include <stdint.h>

/* Include common packet header */
#include "../../panel_packet.h"

/* NetBSD VAX Panel structure - similar to PDP-11 but for VAX */
#pragma pack(push, 1)
struct vax_panel_state {
    uint32_t ps_address;	/* panel switches - 32-bit address */
    uint32_t ps_data;		/* panel lamps - 16-bit data */
};
#pragma pack(pop)

/* VAX Panel packet structure */
struct vax_panel_packet {
    struct panel_packet_header header;
    struct vax_panel_state panel_state;
};

#endif /* VAX_PANEL_STATE_H */
