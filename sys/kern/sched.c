/*	$e1ruBSD: sched.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Round-robin preemptive scheduler, one lwp per process.  The context
 * switch is a software swtch (locore.S): the interrupted trap frame stays
 * on the process kernel stack, so resuming unwinds naturally into iretq.
 *
 * Applied lessons:
 *   - a preempted process goes back to RUNNABLE or it is never scheduled
 *     again; the idle loop must be sti+hlt or it sleeps forever.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/trap.h>

struct proc	 proc_table[NPROC];
struct proc	*curproc = NULL;

static struct proc *runq_head, *runq_tail;
static struct ctx idle_ctx;		/* boot/idle context */
static volatile uint64_t hard_ticks;
static int next_pid = 1;

extern void	 trapret(void);
struct vnode	*vp_console(void);

/* kstack pool from pmap.c */
extern uint64_t	 pm_kstack_alloc(void);
extern void	 pm_kstack_free(uint64_t pa);

static void
runq_enqueue(struct proc *p)
{
	p->p_next = NULL;
	if (runq_tail)
		runq_tail->p_next = p;
	else
		runq_head = p;
	runq_tail = p;
}

static struct proc *
runq_dequeue(void)
{
	struct proc *p = runq_head;

	if (p == NULL)
		return NULL;
	runq_head = p->p_next;
	if (runq_head == NULL)
		runq_tail = NULL;
	p->p_next = NULL;
	return p;
}

void
sched_init(void)
{
	memset(proc_table, 0, sizeof(proc_table));
	runq_head = runq_tail = NULL;
	hard_ticks = 0;
}

struct proc *
sched_newproc(const char *name, struct proc *parent)
{
	struct proc *p;

	for (int i = 0; i < NPROC; i++) {
		if (proc_table[i].p_state == SUNUSED) {
			p = &proc_table[i];
			goto found;
		}
	}
	return NULL;
found:
	memset(p, 0, sizeof(*p));
	p->p_pid = next_pid++;
	p->p_state = SEMBRYO;
	strlcpy(p->p_name, name ? name : "?", sizeof(p->p_name));
	p->p_parent = parent;
	p->p_kstack = pm_kstack_alloc();
	p->p_quantum = SCHED_QUANTUM;
	p->p_cwd = NULL;
	/* fd 0/1/2 = console */
	for (int fd = 0; fd < 3; fd++) {
		p->p_fds[fd] = malloc(sizeof(struct file));
		p->p_fds[fd]->f_vnode = vp_console();
		p->p_fds[fd]->f_offset = 0;
		p->p_fds[fd]->f_flags = fd == 0 ? O_RDONLY : O_WRONLY;
	}
	vn_ref(p->p_fds[0]->f_vnode);
	vn_ref(p->p_fds[1]->f_vnode);
	return p;
}

void
proc_free_resources(struct proc *p)
{
	if (p->p_pmap) {
		pmap_load_kernel();
		pmap_destroy_user(p->p_pmap);
		p->p_pmap = NULL;
	}
	for (int fd = 0; fd < NOFILE; fd++) {
		if (p->p_fds[fd]) {
			vn_unref(p->p_fds[fd]->f_vnode);
			free(p->p_fds[fd], sizeof(struct file));
			p->p_fds[fd] = NULL;
		}
	}
	if (p->p_kstack) {
		pm_kstack_free(p->p_kstack);
		p->p_kstack = 0;
	}
}

struct proc *
proc_find(int pid)
{
	for (int i = 0; i < NPROC; i++)
		if (proc_table[i].p_pid == pid &&
		    proc_table[i].p_state != SUNUSED)
			return &proc_table[i];
	return NULL;
}

void
sched_enqueue(struct proc *p)
{
	cli();
	if (p->p_state != SRUNNABLE)
		p->p_state = SRUNNABLE;
	runq_enqueue(p);
}

uint64_t
ticks_now(void)
{
	return hard_ticks;
}

/*
 * Switch away from the current context to the head of the run queue,
 * or to the idle context when the queue is empty.  Interrupts must be
 * off.  Returns when this context is scheduled again.
 */
static void
switch_to_next(void)
{
	struct proc *next = runq_dequeue();
	struct proc *prev = curproc;
	struct ctx *prev_ctx = prev ? &prev->p_ctx : &idle_ctx;

	if (next == NULL) {
		/* idle: kernel map, no user state */
		curproc = NULL;
		if (prev) {
			pmap_load_kernel();
			fpu_save(&prev->p_fpu);
			prev->p_fpu_valid = 1;
		}
		swtch(prev_ctx, &idle_ctx);
		return;
	}

	curproc = next;
	next->p_state = SRUNNING;
	next->p_quantum = SCHED_QUANTUM;
	cpu_set_tss_rsp0(next->p_kstack + KSTACK_SIZE);

	if (prev) {
		fpu_save(&prev->p_fpu);
		prev->p_fpu_valid = 1;
	}
	if (next->p_fpu_valid)
		fpu_restore(&next->p_fpu);
	else
		fpu_init();
	next->p_fpu_valid = 1;

	pmap_activate(next->p_pmap);
	swtch(prev_ctx, &next->p_ctx);
}

void
sched_yield(void)
{
	cli();
	if (curproc == NULL) {
		if (runq_head)
			switch_to_next();
		sti();
		return;
	}
	if (curproc->p_state != SRUNNING) {
		/* sleeping/zombie already switched: nothing to do here */
		switch_to_next();
		return;
	}
	curproc->p_state = SRUNNABLE;
	runq_enqueue(curproc);
	if (runq_head->p_next == NULL) {
		/* only us: keep running */
		runq_dequeue();
		curproc->p_state = SRUNNING;
		sti();
		return;
	}
	switch_to_next();
}

/*
 * Called from hardclock (interrupt context, IRQs off).
 */
void
sched_tick(void)
{
	hard_ticks++;

	/* wake timed sleepers */
	for (int i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		if (p->p_state == SSLEEPING && p->p_wakeup_at != 0 &&
		    hard_ticks >= p->p_wakeup_at) {
			p->p_wakeup_at = 0;
			p->p_timedout = 1;
			p->p_chan = NULL;
			p->p_state = SRUNNABLE;
			runq_enqueue(p);
		}
	}

	if (curproc != NULL && curproc->p_state == SRUNNING) {
		if (--curproc->p_quantum > 0)
			return;
		if (runq_head == NULL) {
			curproc->p_quantum = SCHED_QUANTUM;
			return;			/* alone: keep running */
		}
		/* preempt: the interrupted process goes back on the queue */
		curproc->p_state = SRUNNABLE;
		runq_enqueue(curproc);
		switch_to_next();
		return;
	}
	if (curproc == NULL && runq_head != NULL)
		switch_to_next();
}

void *
tsleep(void *chan, const char *msg, uint64_t ticks)
{
	(void)msg;
	cli();
	if (curproc == NULL)
		panic("tsleep without process");
	curproc->p_chan = chan;
	curproc->p_timedout = 0;
	curproc->p_wakeup_at = ticks ? hard_ticks + ticks : 0;
	curproc->p_state = SSLEEPING;
	switch_to_next();
	sti();
	return curproc->p_timedout ? (void *)1 : NULL;
}

void
wakeup(void *chan)
{
	for (int i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		if (p->p_state == SSLEEPING && p->p_chan == chan) {
			p->p_chan = NULL;
			p->p_wakeup_at = 0;
			p->p_state = SRUNNABLE;
			runq_enqueue(p);
		}
	}
}

/* sys_exit tail: become a zombie and switch away forever */
void
sched_exit(struct proc *p)
{
	cli();
	proc_free_resources(p);
	p->p_state = SZOMBIE;
	if (p->p_parent)
		wakeup(p->p_parent);
	switch_to_next();
	panic("sched_exit returned");
}

/* reap one zombie child of curproc, returns NULL if none */
struct proc *
sched_wake_child(void)
{
	for (int i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		if (p->p_state == SZOMBIE && p->p_parent == curproc)
			return p;
	}
	return NULL;
}

void
sched_start(void)
{
	cli();
	for (;;) {
		cli();
		if (runq_head) {
			switch_to_next();
			/* should never return */
			for (;;)
				hlt();
		}
		sti();
		hlt();
	}
}
