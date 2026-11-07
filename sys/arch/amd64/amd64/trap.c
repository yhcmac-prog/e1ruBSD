/*	$e1ruBSD: trap.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Trap dispatcher: CPU exceptions, IRQ0/1, and the int 0x80 syscall gate.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <machine/cpu.h>
#include <machine/trap.h>
#include <machine/pmap.h>

extern void hardclock(void);
extern void pckbd_intr(void);
void	 syscall(struct trapframe *tf, struct proc *p);

static const char *exc_names[32] = {
	"divide error", "debug", "nmi", "breakpoint", "overflow",
	"bounds check", "invalid opcode", "device not available",
	"double fault", "fp segment", "invalid tss", "segment not present",
	"stack fault", "general protection", "page fault", "reserved",
	"fpu error", "alignment", "machine check", "simd fp", "virtualization",
	"control protection", "reserved", "reserved", "reserved", "reserved",
	"reserved", "reserved", "hypervisor injection", "vmm communication",
	"security", "reserved"
};

const char *
trap_type(int vec)
{
	return vec < 32 ? exc_names[vec] : "interrupt";
}

/*
 * Map an exception to a conventional signal number.  There is no signal
 * delivery yet, so a user process is terminated and wait4() reports
 * 128 + signal to its parent, in the usual shell convention.
 */
static int
trap_sig(int vec)
{
	switch (vec) {
	case 0:				/* #DE */
		return 8;		/* SIGFPE */
	case 1:				/* #DB */
	case 3:				/* #BP */
	case 4:				/* #OF */
		return 5;		/* SIGTRAP */
	case 6:				/* #UD */
		return 4;		/* SIGILL */
	default:
		return 11;		/* SIGSEGV */
	}
}

void
trap(struct trapframe *tf, struct proc *p)
{
	uint64_t vec = tf->vec;

	if (vec < 32) {
		if (p != NULL && (tf->cs & 3) == 3) {
			uint64_t cr2;

			__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
			/* a write to a COW page is resolved rather than fatal */
			if (vec == 14 && (tf->err & 3) == 3 && p->p_pmap != NULL &&
			    pmap_cow_fault(p->p_pmap, cr2) == 0)
				return;
			kprintf("pid %d (%s): %s at rip=%p err=%p cr2=%p "
			    "(signal %d)\n", p->p_pid, p->p_name,
			    exc_names[vec & 31], tf->rip, tf->err, cr2,
			    trap_sig((int)vec));
			p->p_exitcode = 128 + trap_sig((int)vec);
			sched_exit(p);		/* kill: never returns */
		}
		panic("%s: vec=%d err=%p rip=%p rsp=%p cs=%p",
		    exc_names[vec & 31], (int)vec, tf->err, tf->rip,
		    tf->rsp, tf->cs);
	}

	switch (vec) {
	case T_IRQ0 + 0:			/* timer */
		hardclock();
		return;
	case T_IRQ0 + 1:			/* keyboard */
		outb(0x20, 0x20);
		pckbd_intr();
		return;
	case T_SYSCALL:
		syscall(tf, p);
		return;
	case 255:
		return;				/* spurious */
	default:
		outb(0x20, 0x20);
		outb(0xA0, 0x20);
		return;
	}
}
