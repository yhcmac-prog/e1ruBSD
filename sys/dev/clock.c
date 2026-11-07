/*	$e1ruBSD: clock.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * 8254 PIT channel 0 at HZ, driving hardclock() from IRQ0.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sched.h>
#include <machine/cpu.h>

void
clock_init(void)
{
	uint32_t div = 1193182 / HZ;

	outb(0x43, 0x36);			/* ch0, lo/hi, mode 3 */
	outb(0x40, div & 0xFF);
	outb(0x40, (div >> 8) & 0xFF);
}

void
hardclock(void)
{
	outb(0x20, 0x20);			/* EOI */
	sched_tick();
}
