/*
 * panel_packet.h - Common panel packet header structure
 * Shared across all platform implementations
 */

#ifndef PANEL_PACKET_H
#define PANEL_PACKET_H

#if defined(pdp11) || defined(__pdp11__)
#define PANEL_NATIVE_PDP 1
typedef unsigned short panel_uint16_t;
typedef unsigned long panel_uint32_t;
#else
#include <stdint.h>
typedef uint16_t panel_uint16_t;
typedef uint32_t panel_uint32_t;
#endif

/* Common packet header structure */
#ifndef PANEL_NATIVE_PDP
#pragma pack(push, 1)
#endif
struct panel_packet_header {
    panel_uint16_t pp_byte_count; /* Size of the panel_state payload */
    panel_uint32_t pp_byte_flags; /* Panel type flags */
};
#ifndef PANEL_NATIVE_PDP
#pragma pack(pop)
#endif
#endif /* PANEL_PACKET_H */
