/*	$e1ruBSD: trap.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _MACHINE_TRAP_H_
#define _MACHINE_TRAP_H_

#include <sys/types.h>

/*
 * Trapframe laid out by alltraps in locore.S.  Stack grows downward:
 * CPU pushes ss..err (if present), stub pushes vec, then GP registers.
 */
struct trapframe {
	uint64_t	r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t	rdi, rsi, rbp, rbx, rdx, rcx, rax;
	uint64_t	vec, err;
	uint64_t	rip, cs, rflags, rsp, ss;
};

/* exception vector numbers worth naming */
#define	T_DIVIDE	0
#define	T_PAGEFLT	14
#define	T_GPFLT		13
#define	T_IRQ0		32		/* first PIC vector */
#define	T_SYSCALL	0x80

/* trap.c */
struct proc;
void	 trap(struct trapframe *tf, struct proc *p);
const char *trap_type(int vec);

/* cpu.c */
void	 cpu_intr_enable(void);
void	 cpu_intr_disable(void);
void	 cpu_halt(void);

#endif	/* !_MACHINE_TRAP_H_ */
