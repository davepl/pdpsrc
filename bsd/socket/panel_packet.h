/*
 * panel_packet.h - Common panel packet header structure
 * Shared across all platform implementations
 */

#ifndef PANEL_PACKET_H
#define PANEL_PACKET_H

/* Portable type definitions */
typedef unsigned short u16_t;   /* Always 16 bits */
typedef unsigned int   u32_t;   /* Always 32 bits */

/* Common packet header structure */

#pragma pack(push, 1)
struct panel_packet_header {
    u16_t pp_byte_count;    /* Size of the panel_state payload */
    u32_t pp_byte_flags;    /* Panel type flags (PANEL_PDP1170, PANEL_VAX, etc.) */
};
#pragma pack(pop)
#endif /* PANEL_PACKET_H */
