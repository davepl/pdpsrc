/*
 * panel_state.h - NetBSD VAX panel structure definition
 * Uses /dev/kmem for kernel memory access
 */

#ifndef VAX_PANEL_STATE_H
#define VAX_PANEL_STATE_H

/* Include common packet header */
#include "../../panel_packet.h"

/* NetBSD VAX Panel structure - similar to PDP-11 but for VAX */
struct vax_panel_state {
    long ps_address;	/* panel switches - 32-bit address */
    short ps_data;		/* panel lamps - 16-bit data */
    short ps_psw;       /* processor status word */
    short ps_mser;      /* machine status register */
    short ps_cpu_err;   /* CPU error register */
    short ps_mmr0;      /* memory management register 0 */
    short ps_mmr3;      /* memory management register 3 */
};

/* VAX Panel packet structure */
struct vax_panel_packet {
    struct panel_packet_header header;
    struct vax_panel_state panel_state;
};

#endif /* VAX_PANEL_STATE_H */
