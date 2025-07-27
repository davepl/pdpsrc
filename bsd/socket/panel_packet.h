/*
 * panel_packet.h - Common panel packet header structure
 * Shared across all platform implementations
 */

#ifndef PANEL_PACKET_H
#define PANEL_PACKET_H

#include <stdint.h>

/* Common packet header structure */
#pragma pack(push, 1)
struct panel_packet_header {
    uint16_t pp_byte_count;    /* Size of the panel_state payload */
    uint32_t pp_byte_flags;    /* Panel type flags (PANEL_PDP1170, PANEL_VAX, etc.) */
};
#pragma pack(pop)
#endif /* PANEL_PACKET_H */
