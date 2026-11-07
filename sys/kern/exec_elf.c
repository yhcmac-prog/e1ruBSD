/*	$e1ruBSD: exec_elf.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * ELF64 user program loader.
 *
 * PT_LOAD segments share 4KB pages at their boundaries (rodata tail and
 * data head land in the same page).  A freshly mapped page is zeroed via
 * the allocator; an already-present user page is reused as-is and only
 * the new segment's own bytes are copied into it.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vfs.h>
#include <sys/elf.h>
#include <sys/malloc.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

extern void	trapret(void);		/* locore.S */

#define	AT_NULL		0
#define	AT_PAGESZ	6

static int
vn_pread(struct vnode *vp, uint64_t off, void *buf, size_t len, size_t *donep)
{
	return vp->v_op->vn_read(vp, off, buf, len, donep);
}

static int
load_segment(struct pmap *pm, struct vnode *vp, struct elf64_phdr *ph)
{
	uint64_t va_start = pgrounddown(ph->p_vaddr);
	uint64_t va_end = pgroundup(ph->p_vaddr + ph->p_memsz);
	uint64_t file_left = ph->p_filesz;

	for (uint64_t va = va_start; va < va_end; va += PAGESIZE) {
		int fresh = pmap_extract_user(pm, va) == 0;
		uint64_t pa;

		if (fresh) {
			pa = pm_alloc_page();
			if (pa == 0)
				return -1;
			memset((void *)P2V(pa), 0, PAGESIZE);
			if (pmap_map_user(pm, va, pa, 1))
				return -1;
		} else {
			pa = pmap_extract_user(pm, va);
		}

		/* portion of this page covered by file data */
		uint64_t seg_off = va > ph->p_vaddr ? va - ph->p_vaddr : 0;
		uint64_t in_page = va < ph->p_vaddr ?
		    ph->p_vaddr - va : 0;
		uint64_t n = 0;

		if (file_left > 0) {
			n = PAGESIZE - in_page;
			if (n > file_left)
				n = file_left;
			size_t done = 0;
			if (vn_pread(vp, ph->p_offset + seg_off,
			    (char *)P2V(pa) + in_page, n, &done) || done != n)
				return -1;
			file_left -= done;
		}
	}
	return 0;
}

static uint64_t
build_user_stack(struct pmap *pm, char *argv[], int argc)
{
	uint64_t sp = USTACK_TOP;
	uint64_t arg_off[MAXARGS];
	uint64_t base;
	int slots;

	/* map the stack eagerly */
	for (uint64_t va = USTACK_TOP - USTACK_SIZE; va < USTACK_TOP;
	    va += PAGESIZE) {
		uint64_t pa = pm_alloc_page();

		if (pa == 0)
			return 0;
		memset((void *)P2V(pa), 0, PAGESIZE);
		if (pmap_map_user(pm, va, pa, 1))
			return 0;
	}

	/* argv/env strings at the very top */
	for (int i = argc - 1; i >= 0; i--) {
		size_t n = strlen(argv[i]) + 1;

		sp -= n;
		memcpy((void *)sp, argv[i], n);
		arg_off[i] = sp;
	}
	sp &= ~0xFUL;

	/* slot count: argc + argv[] + NULL + envp NULL + auxv(2 pairs) */
	slots = 1 + argc + 1 + 1 + 4;
	sp -= slots * 8;
	base = sp;

	uint64_t *vec = (uint64_t *)base;
	int k = 0;

	vec[k++] = argc;			/* argc */
	for (int i = 0; i < argc; i++)
		vec[k++] = arg_off[i];
	vec[k++] = 0;				/* argv NULL */
	vec[k++] = 0;				/* envp NULL */
	vec[k++] = AT_PAGESZ;			/* auxv */
	vec[k++] = PAGESIZE;
	vec[k++] = AT_NULL;
	vec[k++] = 0;
	return base;
}

int
exec_load(struct proc *p, struct vnode *vp, char *argv[], int argc)
{
	struct elf64_ehdr ehdr;
	struct elf64_phdr phdrs[16];
	size_t done = 0;
	struct pmap *npm = pmap_new_user();
	struct pmap *old;

	if (npm == NULL)
		return -1;
	if (vn_pread(vp, 0, &ehdr, sizeof(ehdr), &done) || done != sizeof(ehdr))
		goto fail;
	if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
	    ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
		goto fail;
	if (ehdr.e_ident[4] != ELFCLASS64 || ehdr.e_machine != 62)
		goto fail;
	if (ehdr.e_phnum > 16)
		goto fail;
	if (vn_pread(vp, ehdr.e_phoff, phdrs, ehdr.e_phnum * sizeof(phdrs[0]),
	    &done) || done != ehdr.e_phnum * sizeof(phdrs[0]))
		goto fail;

	uint64_t max_end = USER_BRK_BASE;

	for (int i = 0; i < ehdr.e_phnum; i++) {
		if (phdrs[i].p_type != PT_LOAD)
			continue;
		if (load_segment(npm, vp, &phdrs[i]))
			goto fail;
		if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_end)
			max_end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
	}

	/* switch to the new map first: the initial user stack is built by
	 * writing through user virtual addresses under the new CR3 */
	old = p->p_pmap;
	p->p_pmap = npm;
	p->p_brk = pgroundup(max_end);
	pmap_activate(npm);
	if (old)
		pmap_destroy_user(old);

	uint64_t sp = build_user_stack(npm, argv, argc);
	if (sp == 0)
		panic("exec: out of memory building user stack");

	memset(&p->p_tf, 0, sizeof(p->p_tf));
	p->p_tf.rip = ehdr.e_entry;
	p->p_tf.rsp = sp;
	p->p_tf.cs = UCSEL | 3;
	p->p_tf.ss = UDSEL | 3;
	p->p_tf.rflags = 0x202;			/* IF + reserved bit */

	/*
	 * Build the kernel stack so that swtch() lands in trapret, which
	 * iretqs into the user frame we just set up.  Stack layout (hi->lo):
	 *   [ trapframe ] [ trapret addr ] <-- p_ctx.rsp
	 * This mirrors sys_fork() in syscalls.c.
	 */
	{
		uint64_t top = p->p_kstack + KSTACK_SIZE;

		top -= sizeof(struct trapframe);
		memcpy((void *)top, &p->p_tf, sizeof(struct trapframe));
		top -= 8;
		*(uint64_t *)top = (uint64_t)(uintptr_t)trapret;
		p->p_ctx.rsp = top;
	}
	return 0;

fail:
	pmap_destroy_user(npm);
	return -1;
}
