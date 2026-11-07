/*	$e1ruBSD: init_main.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * MI bootstrap: called from locore.S once the kernel map is active and
 * the identity window is gone.  Brings up the world in dependency order
 * and hands control to init(8) through the scheduler.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bootinfo.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/vfs.h>
#include <sys/cons.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

struct bootinfo bootinfo;

void	 x86_boot(void);
void	 tar_extract_root(void);
int	 exec_load(struct proc *p, struct vnode *vp, char *argv[], int argc);

void
x86_boot(void)
{
	/* bootinfo sits at physical 0x6000, reachable via the direct map */
	memcpy(&bootinfo, (void *)P2V(0x6000), sizeof(bootinfo));

	if (bootinfo.bi_magic != BI_MAGIC) {
		panic("bad bootinfo magic 0x%x", bootinfo.bi_magic);
	}

	cons_init();
	cpu_init(&bootinfo);
	fpu_init();                          /* enable SSE early — clang uses it */

	kprintf("\n%s %s (RUNE) -- amd64\n", USP_NAME, USP_RELEASE);

	pmap_init(&bootinfo);
	malloc_init();
	autoconf();
	sched_init();

	kprintf("mounting root (ramfs on md0)\n");
	if (vfs_mountroot() != 0)
		panic("vfs_mountroot failed");
	tar_extract_root();
	kprintf("tar ok\n");

	/* PID 1: exec /init */
	struct proc *p = sched_newproc("init", NULL);
	if (p == NULL)
		panic("cannot create proc 1");

	struct vnode *vp;
	char *argv[2];
	argv[0] = "init";
	argv[1] = NULL;

	if (namei("/init", NULL, &vp) != 0)
		panic("/init not found in root ramfs");
	if (exec_load(p, vp, argv, 1) != 0)
		panic("exec /init failed");

	/* exec_load left the user pmap active; switch back to kernel for
	 * the scheduler setup — otherwise the boot-time physical stack
	 * pointer is invalid (user pmap has no identity mapping). */
	pmap_load_kernel();

	sched_enqueue(p);
	kprintf("boot complete: pid 1 (init) runnable\n");
	sched_start();				/* never returns */
	panic("sched_start returned");
}
