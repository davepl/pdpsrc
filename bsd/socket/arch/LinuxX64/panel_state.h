/*
 * panel_state.h - Linux x64 panel structure definition
 * Uses struct pt_regs for CPU register state capture
 */

#ifndef LINUXX64_PANEL_STATE_H
#define LINUXX64_PANEL_STATE_H

#include <stdint.h>

/* Include common packet header */
#include "../../panel_packet.h"

/* Define pt_regs structure to match Linux kernel definition */
struct pt_regs {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbp;
	uint64_t rbx;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t orig_rax;	/* Original RAX before system call */
	uint64_t rip;		/* instruction pointer */
	uint64_t cs;		/* code segment */
	uint64_t eflags;	/* flags register */
	uint64_t rsp;		/* stack pointer */
	uint64_t ss;		/* stack segment */
};

/* Linux x64 Panel structure */
struct linuxx64_panel_state {
    struct pt_regs ps_regs;        /* panel switches - Linux pt_regs structure */
};

/* Linux x64 Panel packet structure */
struct linuxx64_panel_packet {
    struct panel_packet_header header;
    struct linuxx64_panel_state panel_state;
};

#endif /* LINUXX64_PANEL_STATE_H */
