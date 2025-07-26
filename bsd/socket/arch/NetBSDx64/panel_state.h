/*
 * panel_state.h - NetBSD x64 panel structure definition
 * Uses kvm library for kernel memory access
 */

#ifndef NETBSDX64_PANEL_STATE_H
#define NETBSDX64_PANEL_STATE_H

#include <stdint.h>

/* Include common packet header */
#include "../../panel_packet.h"

/* Define clockframe structure to match kernel definition */
#pragma pack(push, 1)
struct clockframe {
	uint64_t cf_rdi;
	uint64_t cf_rsi;
	uint64_t cf_rdx;
	uint64_t cf_rcx;
	uint64_t cf_r8;
	uint64_t cf_r9;
	uint64_t cf_r10;
	uint64_t cf_r11;
	uint64_t cf_r12;
	uint64_t cf_r13;
	uint64_t cf_r14;
	uint64_t cf_r15;
	uint64_t cf_rbp;
	uint64_t cf_rbx;
	uint64_t cf_rax;
	uint64_t cf_gs;
	uint64_t cf_fs;
	uint64_t cf_es;
	uint64_t cf_ds;
	uint64_t cf_trapno;	/* trap type (always T_PROTFLT for clock interrupts, but unused here) */
	uint64_t cf_err;	/* error code (0 for clock interrupts) */
	uint64_t cf_rip;	/* instruction pointer at interrupt */
	uint64_t cf_cs;		/* code segment at interrupt */
	uint64_t cf_rflags;	/* flags register at interrupt */
	uint64_t cf_rsp;	/* stack pointer at interrupt */
	uint64_t cf_ss;		/* stack segment at interrupt */
};

/* NetBSD x64 Panel structure */
struct netbsdx64_panel_state {
    struct clockframe ps_frame;        /* panel switches - NetBSD clockframe structure */
};

/* NetBSD x64 Panel packet structure */
struct netbsdx64_panel_packet {
    struct panel_packet_header header;
    struct netbsdx64_panel_state panel_state;
};

#pragma pack(pop)

#endif /* NETBSDX64_PANEL_STATE_H */
