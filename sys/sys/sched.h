/*	$e1ruBSD: sched.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_SCHED_H_
#define _SYS_SCHED_H_

#include <sys/types.h>

struct proc;
struct trapframe;

void		 sched_init(void);
struct proc	*sched_newproc(const char *name, struct proc *parent);
void		 sched_enqueue(struct proc *p);	/* put a RUNNABLE proc on the runq */
void		 sched_start(void);		/* enable preemption */
void		 sched_yield(void);		/* voluntary/preemptive switch */
void		 sched_tick(void);		/* called by hardclock */
void		 sched_exit(struct proc *p);	/* become zombie, switch away */
void		 proc_free_resources(struct proc *p);
struct proc	*sched_wake_child(void);	/* reap one zombie of curproc */

void		*tsleep(void *chan, const char *msg, uint64_t ticks);
void		 wakeup(void *chan);

uint64_t	 ticks_now(void);		/* absolute ticks since boot */
void		 hardclock(void);		/* IRQ0 handler (clock.c calls) */

#endif	/* !_SYS_SCHED_H_ */
