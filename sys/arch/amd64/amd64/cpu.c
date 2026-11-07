/*	$e1ruBSD: cpu.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * CPU bring-up: GDT with ring-3 segments and a TSS, IDT from the locore
 * stub table, 8259A PIC remap, and eager FPU save/restore helpers.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bootinfo.h>
#include <sys/proc.h>
#include <machine/cpu.h>
#include <machine/trap.h>
#include <machine/pmap.h>

/* GDT: null | kcode | kdata | ucode | udata | tss(2 slots) */
static uint64_t gdt[7];
static struct {
	uint16_t len;
	uint64_t base;
} __packed gdt_ptr;

struct tss {
	uint32_t rsv0;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t rsv1;
	uint64_t ist[7];
	uint64_t rsv2;
	uint16_t rsv3;
	uint16_t iopb;
} __packed;

static struct tss ktss;
static uint64_t idt[256 * 2];
static struct {
	uint16_t len;
	uint64_t base;
} __packed idt_ptr;

extern void *isr_table[256];
extern void isr_syscall(void);

static void
lgdt(void)
{
	uint16_t kd = KDSEL;
	uint16_t z = 0;

	gdt_ptr.len = sizeof(gdt) - 1;
	gdt_ptr.base = (uint64_t)gdt;
	__asm__ volatile("lgdt %0" :: "m"(gdt_ptr));

	/* reload DS/ES/SS with kernel data segment; CS unchanged */
	__asm__ volatile(
	    "movw %[kd], %%ax\n\t"
	    "movw %%ax, %%ds\n\t"
	    "movw %%ax, %%es\n\t"
	    "movw %%ax, %%ss\n\t"
	    "movw %[z], %%ax\n\t"
	    "movw %%ax, %%fs\n\t"
	    "movw %[z], %%ax\n\t"
	    "movw %%ax, %%gs\n\t"
	    : : [kd] "m"(kd), [z] "m"(z) : "ax", "memory"
	);
}

static void
lidt(void)
{
	idt_ptr.len = sizeof(idt) - 1;
	idt_ptr.base = (uint64_t)idt;
	__asm__ volatile("lidt %0" :: "m"(idt_ptr));
}

static void
gdt_set(int i, uint64_t base, uint64_t limit, uint64_t access, uint64_t flags)
{
	uint64_t lo, hi;

	lo = (limit & 0xFFFF) | (base & 0xFFFFFF) << 16 |
	    (access & 0xFF) << 40 | ((limit >> 16) & 0xF) << 48 |
	    (flags & 0xF) << 52 | (base >> 24 & 0xFF) << 56;
	hi = base >> 32;
	gdt[i] = lo | (hi << 32);
}

static void
gdt_tss(int i)
{
	uint64_t base = (uint64_t)&ktss;
	uint64_t limit = sizeof(ktss) - 1;
	uint32_t *d = (uint32_t *)&gdt[i];

	gdt[i] = limit | (base & 0xFFFFFF) << 16 | (uint64_t)0x89 << 40 |
	    ((limit >> 16) & 0xF) << 48 | (base >> 24 & 0xFF) << 56;
	d[2] = (uint32_t)(base >> 32);
	d[3] = 0;
}

static void
idt_set(int vec, void (*handler)(void), int dpl, int gate_type)
{
	uint64_t h = (uint64_t)handler;
	uint8_t ta = 0x80 | ((dpl & 3) << 5) | (gate_type & 0x0F);

	/*
	 * 64-bit IDT gate:
	 *   bytes 0-1   offset[15:0]
	 *   bytes 2-3   selector
	 *   byte  4     IST
	 *   byte  5     type_attr (P | DPL | gate type)
	 *   bytes 6-7   offset[31:16]
	 *   bytes 8-11  offset[63:32]
	 *   bytes 12-15 reserved
	 */
	idt[vec * 2] = (h & 0xFFFF) | ((uint64_t)KCSEL << 16) |
	    ((uint64_t)ta << 40) | ((h >> 16) & 0xFFFF) << 48;
	idt[vec * 2 + 1] = h >> 32;
}

void
cpu_init(struct bootinfo *bi)
{
	/* GDT with user segments + TSS */
	memset(gdt, 0, sizeof(gdt));
	gdt_set(1, 0, 0xFFFFF, 0x9A, 0xA);	/* kernel code: L=1 */
	gdt_set(2, 0, 0xFFFFF, 0x92, 0xC);	/* kernel data */
	gdt_set(3, 0, 0xFFFFF, 0xFA, 0xA);	/* user code: L=1, DPL=3 */
	gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC);	/* user data: DPL=3 */
	memset(&ktss, 0, sizeof(ktss));
	ktss.iopb = sizeof(ktss);
	gdt_tss(5);
	lgdt();
	{
		uint16_t gt = GTSS;
		__asm__ volatile("ltr %0" :: "m"(gt));
	}

	/* IDT */
	for (int i = 0; i < 48; i++)
		idt_set(i, isr_table[i], 0, 0x8E);
	for (int i = 48; i < 256; i++)
		idt_set(i, isr_table[i], 0, 0x8E);
	idt_set(T_SYSCALL, isr_syscall, 3, 0xEE);	/* ring-3 callable */
	/*
	 * #BP must also be callable from ring 3: `int3` from user mode with a
	 * DPL=0 gate raises #GP(0x1a) instead of entering the breakpoint
	 * handler (the CPU checks the gate DPL against CPL).
	 */
	idt_set(3, isr_table[3], 3, 0xEE);
	lidt();

	/* PIC: remap IRQ0-15 to 32-47, mask everything but IRQ0/1 */
	outb(0x20, 0x11); io_wait();
	outb(0x21, 0x20); io_wait();
	outb(0x21, 0x04); io_wait();
	outb(0x21, 0x01); io_wait();
	outb(0xA0, 0x11); io_wait();
	outb(0xA1, 0x28); io_wait();
	outb(0xA1, 0x02); io_wait();
	outb(0xA1, 0x01); io_wait();
	outb(0x21, 0xFC);			/* unmask IRQ0 (timer), IRQ1 (kbd) */
	outb(0xA1, 0xFF);

	(void)bi;
}

void
cpu_set_tss_rsp0(uint64_t rsp)
{
	ktss.rsp0 = rsp;
}

void
cpu_intr_enable(void)
{
	sti();
}

void
cpu_intr_disable(void)
{
	cli();
}

void
cpu_halt(void)
{
	sti();
	hlt();
}

/*
 * Eager FPU handling.  The kernel is built -mno-sse and only touches the
 * FPU through fxsave/fxrstor on context switch; userland gets SSE because
 * fpu_init() clears CR0.EM and sets CR4.OSFXSR|OSXMMEXCPT.
 */
void
fpu_init(void)
{
	uint64_t cr0, cr4;

	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 &= ~(1UL << 2);			/* clear EM */
	cr0 |= (1UL << 1);			/* set MP */
	__asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1UL << 9) | (1UL << 10);	/* OSFXSR | OSXMMEXCPT */
	__asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

	uint32_t mxcsr = 0x1F80;

	__asm__ volatile("fninit");
	__asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));
}

void
fpu_save(struct fxarea *fa)
{
	__asm__ volatile("fxsave %0" : "=m"(*fa));
}

void
fpu_restore(struct fxarea *fa)
{
	__asm__ volatile("fxrstor %0" :: "m"(*fa));
}
