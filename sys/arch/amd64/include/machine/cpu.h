/*	$e1ruBSD: cpu.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

#include <sys/types.h>

struct bootinfo;
struct fxarea;
struct ctx;

/* Segment selectors in the kernel GDT (cpu.c). */
#define	KCSEL		0x08		/* kernel code */
#define	KDSEL		0x10		/* kernel data */
#define	UCSEL		0x18		/* user code (ring 3) */
#define	UDSEL		0x20		/* user data (ring 3) */
#define	GTSS		0x28		/* TSS descriptor slot */

/* Page table bits. */
#define	PTE_P		0x001UL
#define	PTE_RW		0x002UL
#define	PTE_USER	0x004UL
#define	PTE_PWT		0x008UL
#define	PTE_PCD		0x010UL
#define	PTE_A		0x020UL
#define	PTE_D		0x040UL
#define	PTE_PS		0x080UL
#define	PTE_COW		0x200UL		/* software: shared, copy on write */
#define	PTE_NX		0x8000000000000000UL

/* port I/O */
static inline void
outb(uint16_t port, uint8_t val)
{
	__asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t
inb(uint16_t port)
{
	uint8_t ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline void
outw(uint16_t port, uint16_t val)
{
	__asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint16_t
inw(uint16_t port)
{
	uint16_t ret;
	__asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline void
io_wait(void)
{
	outb(0x80, 0);
}

static inline void
cli(void)
{
	__asm__ volatile("cli");
}

static inline void
sti(void)
{
	__asm__ volatile("sti");
}

static inline void
hlt(void)
{
	__asm__ volatile("hlt");
}

/* cpu.c */
void	 cpu_init(struct bootinfo *bi);
void	 cpu_set_tss_rsp0(uint64_t rsp);
void	 fpu_init(void);
void	 fpu_save(struct fxarea *fa);
void	 fpu_restore(struct fxarea *fa);

/* locore.S */
void	 swtch(struct ctx *old, struct ctx *new);
void	 trapret(void);

#endif	/* !_MACHINE_CPU_H_ */
