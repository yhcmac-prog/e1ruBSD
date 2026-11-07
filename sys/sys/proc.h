/*	$e1ruBSD: proc.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_PROC_H_
#define _SYS_PROC_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vfs.h>
#include <machine/trap.h>

/*
 * Process states (xv6-style single-lwp processes, BSD naming).
 */
#define	SUNUSED		0
#define	SEMBRYO		1
#define	SRUNNABLE	2
#define	SRUNNING	3
#define	SSLEEPING	4
#define	SZOMBIE		5

/*
 * ctx layout MUST match swtch() in locore.S exactly.
 * Offsets: 0=r15 8=r14 16=r13 24=r12 32=r11 40=r10 48=r9 56=r8
 *          64=rdi 72=rsi 80=rbp 88=rbx 96=rsp
 */
struct ctx {
	uint64_t	r15;
	uint64_t	r14;
	uint64_t	r13;
	uint64_t	r12;
	uint64_t	r11;
	uint64_t	r10;
	uint64_t	r9;
	uint64_t	r8;
	uint64_t	rdi;
	uint64_t	rsi;
	uint64_t	rbp;
	uint64_t	rbx;
	uint64_t	rsp;
};

struct fxarea {
	uint8_t		area[512];
} __aligned(16);

struct proc {
	int		 p_pid;
	int		 p_state;
	char		 p_name[16];

	struct pmap	*p_pmap;	/* user page table */
	uint64_t	 p_kstack;	/* kernel virtual base of kstack */
	struct ctx	 p_ctx;
	struct trapframe p_tf;		/* canonical frame for fork/exec */

	void		*p_chan;	/* sleep channel */
	uint64_t	 p_wakeup_at;	/* absolute tick for timed sleep */
	int		 p_timedout;
	int		 p_killed;
	int		 p_exitcode;
	struct proc	*p_parent;

	struct vnode	*p_cwd;
	struct file	*p_fds[NOFILE];
	uint64_t	 p_brk;

	struct fxarea	 p_fpu;		/* eager FPU save area */
	int		 p_fpu_valid;

	int		 p_quantum;
	struct proc	*p_next;	/* run queue / zombie list link */
};

/* proc list walk (syscalls.c psinfo) */
extern struct proc	 proc_table[NPROC];
struct proc		*proc_find(int pid);

#endif	/* !_SYS_PROC_H_ */
