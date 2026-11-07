/*	$e1ruBSD: autoconf.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * autoconf(9)-style device configuration: a static config table walks the
 * mainbus and attaches each driver, printing dmesg lines in NetBSD format.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bootinfo.h>
#include <sys/device.h>
#include <machine/cpu.h>

#ifndef __STRING
#define	__STRING(x)	#x
#endif
#define	__XSTRING(x)	__STRING(x)

extern void clock_init(void);
extern void md_init(uint64_t pa, uint64_t size);
extern uint64_t md_size_bytes(void);
extern struct bootinfo bootinfo;

static struct device mainbus;

void
device_attached(const char *bus, struct device *dv, const char *info)
{
	if (info && *info)
		kprintf("%s%d at %s: %s\n", dv->dv_name, dv->dv_unit, bus, info);
	else
		kprintf("%s%d at %s\n", dv->dv_name, dv->dv_unit, bus);
}

static void
attach_cpu(void)
{
	static struct device dv = { "cpu", 0, NULL };

	device_attached("mainbus", &dv, "x86_64 (long mode)");
}

static void
attach_vga(void)
{
	static struct device dv = { "vga", 0, NULL };

	device_attached("mainbus", &dv, "80x25 text console");
}

static void
attach_com(void)
{
	static struct device dv = { "com", 0, NULL };

	device_attached("mainbus", &dv, "COM1 115200 console");
}

static void
attach_pit(void)
{
	static struct device dv = { "pit", 0, NULL };

	clock_init();
	device_attached("mainbus", &dv, "timer " __XSTRING(HZ) " Hz");
}

static void
attach_pckbc(void)
{
	static struct device dv = { "pckbc", 0, NULL };
	static struct device kbdv = { "pckbd", 0, NULL };

	device_attached("mainbus", &dv, NULL);
	device_attached("pckbc", &kbdv, "keyboard slot");
}

static void
attach_md(void)
{
	static struct device dv = { "md", 0, NULL };

	md_init(bootinfo.bi_ramdisk_pa, bootinfo.bi_ramdisk_size);
	(void)md_size_bytes();
	device_attached("mainbus", &dv, NULL);
}

void
autoconf(void)
{
	kprintf("mainbus0 (root)\n");
	attach_cpu();
	attach_vga();
	attach_com();
	attach_pit();
	attach_pckbc();
	attach_md();
}
