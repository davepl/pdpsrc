/*
 * panel_state.h - macOS ARM64 panel structure definition
 * Uses Mach APIs for user-space CPU state access on Apple Silicon
 */

#ifndef MACOS_PANEL_STATE_H
#define MACOS_PANEL_STATE_H

#include <stdint.h>

/* Include common packet header */
#include "../../panel_packet.h"

/* macOS ARM64 Panel structure - CPU registers and system state */
struct macos_panel_state {
    /* ARM64 CPU registers */
    uint64_t pc;           /* Program counter */
    uint64_t sp;           /* Stack pointer */
    uint64_t fp;           /* Frame pointer */
    uint64_t lr;           /* Link register */
    uint64_t x0;           /* First argument/return register */
    uint64_t x1;           /* Second argument register */
    
    /* System state */
    uint32_t cpu_usage;    /* CPU usage percentage (0-100) */
    uint32_t memory_usage; /* Memory usage percentage (0-100) */
    uint32_t load_average; /* Load average * 100 */
    uint32_t thread_count; /* Number of threads in current process */
    
    /* Metadata */
    uint32_t architecture; /* Always 1 for ARM64 */
    uint32_t timestamp;    /* Timestamp of snapshot */
};

/* macOS Panel packet structure */
struct macos_panel_packet {
    struct panel_packet_header header;
    struct macos_panel_state panel_state;
};

#endif /* MACOS_PANEL_STATE_H */
