/*	$e1ruBSD: param.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#define	USP_NAME	"e1ruBSD"
#define	USP_RELEASE	"0.1"
#define	USP_VERSION	"e1ruBSD 0.1 (RUNE) #0"
#define	USP_MACHINE	"amd64"
#define	USP_MACHINE_ARCH "x86_64"

#define	PAGESIZE	4096
#define	PGSHIFT		12
#define	pgroundup(x)	(((x) + PAGESIZE - 1) & ~(PAGESIZE - 1))
#define	pgrounddown(x)	((x) & ~(PAGESIZE - 1))

/* Kernel virtual layout (higher half, classic x86_64 BSD style). */
#define	KERNEL_PHYS	0x100000UL			/* kernel load address */
#define	DMAP_BASE	0xFFFFFFFF80000000UL		/* direct map of phys 0 */
#define	KERNEL_BASE	0xFFFFFFFF80100000UL		/* kernel text */

#define	DMAP(va)	((va) - DMAP_BASE)		/* virt -> phys */
#define	P2V(pa)		((pa) + DMAP_BASE)		/* phys -> kernel virt */

/* User virtual layout. */
#define	USER_TEXT	0x00400000UL			/* user program base */
#define	USER_BRK_BASE	0x01000000UL			/* heap starts here */
#define	USER_BRK_MAX	0x08000000UL			/* heap limit */
#define	USTACK_TOP	0x7FFF0000UL			/* user stack top */
#define	USTACK_SIZE	0x00100000UL			/* 1MB user stack */

/* ramdisk physical location chosen by stage2. */
#define	RAMDISK_PA	0x400000UL

/* Process limits. */
#define	NPROC		16
#define	NOFILE		8
#define	MAXPATHLEN	256
#define	MAXNAMELEN	64
#define	MAXARGS		16
#define	KSTACK_PAGES	4
#define	KSTACK_SIZE	(KSTACK_PAGES * PAGESIZE)
#define	NGROUPS_MAX	0

/* Scheduler. */
#define	SCHED_QUANTUM	10		/* ticks (100Hz) per round */
#define	HZ		100

#endif	/* !_SYS_PARAM_H_ */
