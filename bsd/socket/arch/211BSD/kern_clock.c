/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)kern_clock.c	1.4 (2.11BSD GTE) 1997/2/14
 */

#include "param.h"
#include "../machine/psl.h"
#include "../machine/seg.h"

#include "user.h"
#include "proc.h"
#include "callout.h"
#include "dk.h"
#include "kernel.h"
#include "systm.h"

sys_wait_for_panel();

#define APR_BASE   0172300
#define APR_MASK   077777
#define OFFSET_MASK 07777

/* 
 * Returns 22-bit physical address for the given virtual pc and ps.
 * Must be called from kernel context. 
 * Returns 0 if mode unknown.
 */
unsigned long
decode_phys_addr(pc, ps)
    unsigned short pc;
    unsigned short ps;
{
    register int mode;
    register int segnum;
    register int apr_index;
    register unsigned short *apr_regs;
    register unsigned short seg_reg;
    register unsigned long seg_base;
    register unsigned short offset;
    register unsigned long phys_addr;

    /* APRs are memory-mapped at 0172300 */
    apr_regs = (unsigned short *)APR_BASE;

    /* Get processor mode: bits 14-15 of PS */
    mode = (ps >> 14) & 03;

    /* Segment number: bits 13-15 of PC */
    segnum = (pc >> 13) & 07;

    /* Default: use I-space (segments 0-7) for PC */
    if (mode == 0)           /* kernel */
        apr_index = segnum;
    else if (mode == 1)      /* supervisor */
        apr_index = 8 + segnum;
    else if (mode == 3)      /* user */
        apr_index = 8 + segnum;
    else
        return 0L;

    /* Read segment register, mask to 18 bits (as 2.11BSD expects) */
    seg_reg = apr_regs[apr_index] & APR_MASK;

    seg_base = (unsigned long)seg_reg;

    offset = pc & OFFSET_MASK;

    /* 22-bit physical address: (segment base << 6) | offset */
    phys_addr = (seg_base << 6) | offset;

    /* Mask to 22 bits just to be sure */
    phys_addr = phys_addr & 07777777L;

    return phys_addr;
}

/*
 * Panel state - holds values that would be used to simulate a front panel
 * Structure should be 6 bytes total (4 + 2) with tight packing
 */

struct panel_state {
	long ps_address;	/* panel switches - 32-bit address */
	short ps_data;		/* panel lamps - 16-bit data */
    short ps_psw;
    short ps_mser;
    short ps_cpu_err;
    short ps_mmr0;
    short ps_mmr3;
} panel = { 0L, 0 };

int panel_seq = 0;

/*
 * The hz hardware interval timer.
 * We update the events relating to real time.
 * Also gather statistics.
 *
 *	reprime clock
 *	implement callouts
 *	maintain user/system times
 *	maintain date
 *	profile
 */
/*ARGSUSED*/
hardclock(dev,sp,r1,ov,nps,r0,pc,ps)
	dev_t dev;
	caddr_t sp, pc;
	int r1, ov, nps, r0, ps;
{
	register struct callout *p1;
	register struct proc *p;
	register int needsoft = 0;
	mapinfo map;

	savemap(map);		/* ensure normal mapping of kernel data */

	/*
	 * Update real-time timeout queue.
	 * At front of queue are some number of events which are ``due''.
	 * The time to these is <= 0 and if negative represents the
	 * number of ticks which have passed since it was supposed to happen.
	 * The rest of the q elements (times > 0) are events yet to happen,
	 * where the time for each is given as a delta from the previous.
	 * Decrementing just the first of these serves to decrement the time
	 * to all events.
	 */
	p1 = calltodo.c_next;
	while (p1) {
		if (--p1->c_time > 0)
			break;
		needsoft = 1;
		if (p1->c_time == 0)
			break;
		p1 = p1->c_next;
	}

	/*
	 * Charge the time out based on the mode the cpu is in.
	 * Here again we fudge for the lack of proper interval timers
	 * assuming that the current state has been around at least
	 * one tick.
	 */
	if (USERMODE(ps)) {
		if (u.u_prof.pr_scale)
			needsoft = 1;
		/*
		 * CPU was in user state.  Increment
		 * user time counter, and process process-virtual time
		 * interval timer. 
		 */
		u.u_ru.ru_utime++;
		if (u.u_timer[ITIMER_VIRTUAL - 1].it_value &&
		    !--u.u_timer[ITIMER_VIRTUAL - 1].it_value) {
			psignal(u.u_procp, SIGVTALRM);
			u.u_timer[ITIMER_VIRTUAL - 1].it_value =
			    u.u_timer[ITIMER_VIRTUAL - 1].it_interval;
		}
	} else {
		/*
		 * CPU was in system state.
		 */
		if (!noproc)
			u.u_ru.ru_stime++;
	}

	/*
	 * If the cpu is currently scheduled to a process, then
	 * charge it with resource utilization for a tick, updating
	 * statistics which run in (user+system) virtual time,
	 * such as the cpu time limit and profiling timers.
	 * This assumes that the current process has been running
	 * the entire last tick.
	 */
	if (noproc == 0) {
		p = u.u_procp;
		if (++p->p_cpu == 0)
			p->p_cpu--;
		if ((u.u_ru.ru_utime+u.u_ru.ru_stime+1) >
		    u.u_rlimit[RLIMIT_CPU].rlim_cur) {
			psignal(p, SIGXCPU);
			if (u.u_rlimit[RLIMIT_CPU].rlim_cur <
			    u.u_rlimit[RLIMIT_CPU].rlim_max)
				u.u_rlimit[RLIMIT_CPU].rlim_cur += 5 * hz;
		}
		if (u.u_timer[ITIMER_PROF - 1].it_value &&
		    !--u.u_timer[ITIMER_PROF - 1].it_value) {
			psignal(p, SIGPROF);
			u.u_timer[ITIMER_PROF - 1].it_value =
			    u.u_timer[ITIMER_PROF - 1].it_interval;
		}
	}

#ifdef UCB_METER
	gatherstats(pc,ps);
#endif

	/*
	 * Increment the time-of-day, process callouts at a very
	 * low cpu priority, so we don't keep the relatively  high
	 * clock interrupt priority any longer than necessary.
	 */
	if (adjdelta) 
		if (adjdelta > 0) {
			++lbolt;
			--adjdelta;
		} else {
			--lbolt;
			++adjdelta;
		}
	if (++lbolt >= hz) {
		lbolt -= hz;
		++time.tv_sec;
	}

	/*
	 * Update front panel display with current execution state.
	 * Keep this simple and safe - just use the pc parameter directly.
	 */
	panel.ps_address = decode_phys_addr((unsigned short)pc, (unsigned short)ps);
	panel.ps_data    = (short)(r0 & 0xFFFF);	
    panel.ps_psw     = (short)(ps & 0xFFFF);

	/*
	panel.ps_mser    = *(short *)017777744;
    panel.ps_cpu_err = *(short *)017777766;
    panel.ps_mmr0    = *(short *)017777572;
    panel.ps_mmr3    = *(short *)017777516;
	*/
    panel_seq++;
    wakeup((caddr_t)&panel_seq);

	if (needsoft && BASEPRI(ps)) {	/* if ps is high, just return */
		(void) _splsoftclock();
		softclock(pc,ps);
	}
	restormap(map);
}

sys_wait_for_panel()
{
	static int last_seq = 0;
    while (last_seq == panel_seq)
        tsleep((caddr_t)&panel_seq, PZERO | PCATCH, "waitseq", 0);

    last_seq = panel_seq;
    return last_seq;
}

#ifdef UCB_METER
int	dk_ndrive = DK_NDRIVE;
/*
 * Gather statistics on resource utilization.
 *
 * We make a gross assumption: that the system has been in the
 * state it is in (user state, kernel state, interrupt state,
 * or idle state) for the entire last time interval, and
 * update statistics accordingly.
 */
/*ARGSUSED*/
gatherstats(pc, ps)
	caddr_t pc;
	int ps;
{
	register int cpstate, s;

	/*
	 * Determine what state the cpu is in.
	 */
	if (USERMODE(ps)) {
		/*
		 * CPU was in user state.
		 */
		if (u.u_procp->p_nice > NZERO)
			cpstate = CP_NICE;
		else
			cpstate = CP_USER;
	} else {
		/*
		 * CPU was in system state.  If profiling kernel
		 * increment a counter.  If no process is running
		 * then this is a system tick if we were running
		 * at a non-zero IPL (in a driver).  If a process is running,
		 * then we charge it with system time even if we were
		 * at a non-zero IPL, since the system often runs
		 * this way during processing of system calls.
		 * This is approximate, but the lack of true interval
		 * timers makes doing anything else difficult.
		 */
		cpstate = CP_SYS;
		if (noproc && BASEPRI(ps))
			cpstate = CP_IDLE;
	}
	/*
	 * We maintain statistics shown by user-level statistics
	 * programs:  the amount of time in each cpu state, and
	 * the amount of time each of DK_NDRIVE ``drives'' is busy.
	 */
	cp_time[cpstate]++;
	for (s = 0; s < DK_NDRIVE; s++)
		if (dk_busy & (1 << s))
			dk_time[s]++;
}
#endif UCB_METER

/*
 * Software priority level clock interrupt.
 * Run periodic events from timeout queue.
 */
softclock(pc, ps)
	caddr_t pc;
	int ps;
{
	for (;;) {
		register struct callout *p1;
		register caddr_t arg;
		register int (*func)();
		register int a, s;

		s = splhigh();
		if ((p1 = calltodo.c_next) == 0 || p1->c_time > 0) {
			splx(s);
			break;
		}
		arg = p1->c_arg; func = p1->c_func; a = p1->c_time;
		calltodo.c_next = p1->c_next;
		p1->c_next = callfree;
		callfree = p1;
		splx(s);
#ifdef INET
		if (ISSUPERADD(func))
			KScall(KERNELADD(func), sizeof(arg) + sizeof(a),
			    arg, a);
		else
#endif
		(*func)(arg, a);
	}
	/*
	 * If trapped user-mode and profiling, give it
	 * a profiling tick.
	 */
	if (USERMODE(ps)) {
		register struct proc *p = u.u_procp;

		if (u.u_prof.pr_scale)
			addupc(pc, &u.u_prof, 1);
		/*
		 * Check to see if process has accumulated
		 * more than 10 minutes of user time.  If so
		 * reduce priority to give others a chance.
		 */

		if (p->p_uid && p->p_nice == NZERO &&
		    u.u_ru.ru_utime > 10L * 60L * hz) {
			p->p_nice = NZERO+4;
				(void) setpri(p);
		}
	}
}

/*
 * Arrange that (*fun)(arg) is called in t/hz seconds.
 */
timeout(fun, arg, t)
	int (*fun)();
	caddr_t arg;
	register int t;
{
	register struct callout *p1, *p2, *pnew;
	register int s = splclock();

	if (t <= 0)
		t = 1;
	pnew = callfree;
	if (pnew == NULL)
		panic("timeout table overflow");
	callfree = pnew->c_next;
	pnew->c_arg = arg;
	pnew->c_func = fun;
	for (p1 = &calltodo; (p2 = p1->c_next) && p2->c_time < t; p1 = p2)
		if (p2->c_time > 0)
			t -= p2->c_time;
	p1->c_next = pnew;
	pnew->c_next = p2;
	pnew->c_time = t;
	if (p2)
		p2->c_time -= t;
	splx(s);
}

/*
 * untimeout is called to remove a function timeout call
 * from the callout structure.
 */
untimeout(fun, arg)
	int (*fun)();
	caddr_t arg;
{
	register struct callout *p1, *p2;
	register int s;

	s = splclock();
	for (p1 = &calltodo; (p2 = p1->c_next) != 0; p1 = p2) {
		if (p2->c_func == fun && p2->c_arg == arg) {
			if (p2->c_next && p2->c_time > 0)
				p2->c_next->c_time += p2->c_time;
			p1->c_next = p2->c_next;
			p2->c_next = callfree;
			callfree = p2;
			break;
		}
	}
	splx(s);
}

profil()
{
	register struct a {
		short	*bufbase;
		unsigned bufsize;
		unsigned pcoffset;
		unsigned pcscale;
	} *uap = (struct a *)u.u_ap;
	register struct uprof *upp = &u.u_prof;

	upp->pr_base = uap->bufbase;
	upp->pr_size = uap->bufsize;
	upp->pr_off = uap->pcoffset;
	upp->pr_scale = uap->pcscale;
}

/*
 * Compute number of hz until specified time.
 * Used to compute third argument to timeout() from an
 * absolute time.
 */
hzto(tv)
	register struct timeval *tv;
{
	register long ticks;
	register long sec;
	register int s = splhigh();

	/*
	 * If number of milliseconds will fit in 32 bit arithmetic,
	 * then compute number of milliseconds to time and scale to
	 * ticks.  Otherwise just compute number of hz in time, rounding
	 * times greater than representible to maximum value.
	 *
	 * Delta times less than 25 days can be computed ``exactly''.
	 * Maximum value for any timeout in 10ms ticks is 250 days.
	 */
	sec = tv->tv_sec - time.tv_sec;
	if (sec <= 0x7fffffff / 1000 - 1000)
		ticks = ((tv->tv_sec - time.tv_sec) * 1000 +
			(tv->tv_usec - time.tv_usec) / 1000) / (1000/hz);
	else if (sec <= 0x7fffffff / hz)
		ticks = sec * hz;
	else
		ticks = 0x7fffffff;
	splx(s);
#ifdef pdp11
	/* stored in an "int", so 16-bit max */
	if (ticks > 0x7fff)
		ticks = 0x7fff;
#endif
	return ((int)ticks);
}
