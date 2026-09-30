/*
 * panel_state.h - PDP-11 2.11BSD panel structure definition
 * Compatible with K&R C and 2.11BSD
 */

#ifndef PDP_PANEL_STATE_H
#define PDP_PANEL_STATE_H

/* Include common packet header */
#include "../../panel_packet.h"

/* PDP-11 Panel structure - must match kernel exactly */
struct pdp_panel_state {
    long ps_address;	/* panel switches - 32-bit address */
    short ps_data;		/* panel lamps - 16-bit data */
    short ps_psw;       /* processor status word */
    short ps_mser;      /* machine status register */
    short ps_cpu_err;   /* CPU error register */
    short ps_mmr0;      /* memory management register 0 */
    short ps_mmr3;      /* memory management register 3 */
};

/* PDP-11 Panel packet structure */
struct pdp_panel_packet {
    struct panel_packet_header header;
    struct pdp_panel_state panel_state;
};

#endif /* PDP_PANEL_STATE_H */
