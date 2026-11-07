/*	$e1ruBSD: md.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * md(4): memory disk, backed by the ramdisk image stage2 placed at
 * RAMDISK_PA.  Read-only block device serving the root ramfs.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

static uint64_t md_pa, md_size;

void
md_init(uint64_t pa, uint64_t size)
{
	md_pa = pa;
	md_size = size;
	kprintf("md0: memory disk at 0x%x, %d KB\n",
	    (u_int)md_pa, (u_int)(md_size / 1024));
}

int
md_read(uint64_t off, void *buf, size_t len, size_t *donep)
{
	if (off >= md_size) {
		if (donep)
			*donep = 0;
		return 0;
	}
	if (off + len > md_size)
		len = md_size - off;
	memcpy(buf, (const void *)P2V(md_pa + off), len);
	if (donep)
		*donep = len;
	return 0;
}

uint64_t
md_size_bytes(void)
{
	return md_size;
}
