/*	$e1ruBSD: pmap.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * amd64 pmap: physical page pool from the BIOS E820 map plus a minimal
 * user address space layer (independent page trees rooted at PML4[0],
 * kernel higher-half shared through PML4[511]).
 *
 * Lessons from prior bring-ups applied here:
 *   - no identity mapping of low RAM after the handoff: everything goes
 *     through the direct map, so a kernel write can never be hijacked by
 *     a user PTE.
 *   - the VGA page in the direct map carries PCD (MMIO must not be cached).
 *   - user trees get PTE_USER on every level: U/S is ANDed across the
 *     whole walk, a missing bit anywhere makes the page supervisor-only.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bootinfo.h>
#include <machine/pmap.h>
#include <machine/cpu.h>

#define	KERNEL_PML4_PA	0x76000UL	/* built by locore.S */
#define	USER_VA_MAX	0x0000800000000000UL

extern char kern_end[];
uint64_t pm_kernel_end;

static uint64_t pm_free_head;		/* phys addr of first free frame */
static uint64_t pm_total, pm_free;
static struct bios_e820 e820[E820_MAX];
static int e820_count;
static uint64_t rd_pa, rd_end;

/* contiguous kstack pool: NPROC slots of KSTACK_SIZE carved from the top
 * of the highest usable RAM region before the free list is built */
#define	KS_POOL_SLOTS	16
static uint64_t ks_pool;
static uint32_t ks_used;

/*
 * Frame reference counts, indexed by physical page number.  Every frame
 * handed out by pm_alloc_page() starts at 1; fork shares read-only pages
 * (COW) instead of copying them, so a frame may be owned by several
 * address spaces until one of them writes to it.  The direct map covers
 * 1GB, hence at most 262144 frames.
 */
#define	MAX_PHYS_FRAMES	(0x40000000UL / PAGESIZE)
static uint16_t pm_ref[MAX_PHYS_FRAMES];

static inline int
refidx(uint64_t pa)
{
	uint64_t i = pa / PAGESIZE;

	return i < MAX_PHYS_FRAMES ? (int)i : -1;
}

static inline void
pm_ref_inc(uint64_t pa)
{
	int i = refidx(pa);

	if (i >= 0 && pm_ref[i] != 0xFFFF)
		pm_ref[i]++;
}

static inline void
pm_ref_set(uint64_t pa, uint16_t v)
{
	int i = refidx(pa);

	if (i >= 0)
		pm_ref[i] = v;
}

uint64_t
pm_kstack_alloc(void)
{
	for (int i = 0; i < KS_POOL_SLOTS; i++) {
		if (!(ks_used & (1U << i))) {
			ks_used |= 1U << i;
			/* return kernel virtual (DMAP) address, not phys */
			return P2V(ks_pool + (uint64_t)i * KSTACK_SIZE);
		}
	}
	return 0;
}

void
pm_kstack_free(uint64_t va)
{
	uint64_t pa = DMAP(va);
	int i = (int)((pa - ks_pool) / KSTACK_SIZE);

	if (i >= 0 && i < KS_POOL_SLOTS)
		ks_used &= ~(1U << i);
}

static inline uint64_t
rd64(uint64_t pa)
{
	return *(volatile uint64_t *)P2V(pa);
}

static inline void
wr64(uint64_t pa, uint64_t v)
{
	*(volatile uint64_t *)P2V(pa) = v;
}

static inline void
invlpg(uint64_t va)
{
	__asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

void
pmap_init(struct bootinfo *bi)
{
	uint64_t i;

	e820_count = 0;
	memset(pm_ref, 0, sizeof(pm_ref));
	pm_kernel_end = (uint64_t)kern_end - DMAP_BASE;
	rd_pa = bi->bi_ramdisk_pa;
	rd_end = rd_pa + bi->bi_ramdisk_size;

	/* copy the E820 map out of low memory */
	uint32_t n = bi->bi_e820_count;
	if (n > E820_MAX)
		n = E820_MAX;
	for (uint32_t j = 0; j < n; j++)
		e820[j] = *(struct bios_e820 *)P2V(BI_E820_PA + j * 24);
	e820_count = n;

	/* chain usable 4KB frames above 1MB into the free list */
	pm_free_head = 0;
	pm_total = pm_free = 0;
	for (int j = 0; j < e820_count; j++) {
		uint64_t base, len, start;

		if (e820[j].type != E820_RAM)
			continue;
		base = e820[j].base;
		len = e820[j].len;
		if (base + len > 0x40000000UL)		/* dmap covers 1GB */
			len = 0x40000000UL - base;
		if (base + len <= KERNEL_PHYS)
			continue;
		start = base < KERNEL_PHYS ? KERNEL_PHYS : base;
		start = pgroundup(start);
		/* carve the kstack pool out of the top of this region */
		if (base + len >= ks_pool + KS_POOL_SLOTS * KSTACK_SIZE ||
		    ks_pool == 0) {
			uint64_t reg_top = pgrounddown(base + len);
			uint64_t pool = reg_top - KS_POOL_SLOTS * KSTACK_SIZE;

			if (pool > start) {
				ks_pool = pool;
				ks_used = 0;
				len = pool - base;	/* exclude from free list */
			}
		}
		for (i = start; i + PAGESIZE <= base + len; i += PAGESIZE) {
			if (i >= KERNEL_PHYS && i < pgroundup(pm_kernel_end))
				continue;		/* kernel image */
			if (i >= rd_pa && i < pgroundup(rd_end))
				continue;		/* ramdisk */
			wr64(i, pm_free_head);
			pm_free_head = i;
			pm_total++;
			pm_free++;
		}
	}
	kprintf("pmap0: %d e820 entries, kernel end 0x%x, free %d/%d pages\n",
	    e820_count, (u_int)pm_kernel_end, (u_int)pm_free, (u_int)pm_total);
}

uint64_t
pm_alloc_page(void)
{
	uint64_t pa = pm_free_head;

	if (pa == 0)
		return 0;
	pm_free_head = rd64(pa);
	pm_free--;
	pm_ref_set(pa, 1);
	return pa;
}

/*
 * Allocate npg physically contiguous frames.  The free list is unordered
 * once arbitrary frees happen, so walk it and look for a run of adjacent
 * pages, extending the run in either direction.  Used by malloc() for
 * requests larger than one page.
 */
uint64_t
pm_alloc_pages(int npg)
{
	uint64_t cur, next, prev = 0;
	uint64_t runprev = 0;		/* list node before the first run node */
	uint64_t lo = 0, hi = 0;	/* address range of the run */
	int runlen = 0;

	if (npg <= 0)
		return 0;
	if (npg == 1)
		return pm_alloc_page();

	for (cur = pm_free_head; cur != 0; cur = next) {
		next = rd64(cur);
		if (runlen > 0 && cur == lo - PAGESIZE) {
			lo = cur;			/* extend downwards */
			runlen++;
		} else if (runlen > 0 && cur == hi + PAGESIZE) {
			hi = cur;			/* extend upwards */
			runlen++;
		} else {
			lo = hi = cur;
			runlen = 1;
			runprev = prev;
		}
		if (runlen == npg) {
			/* unlink every node of the run from the free list */
			if (runprev == 0)
				pm_free_head = next;
			else
				wr64(runprev, next);
			for (int i = 0; i < npg; i++)
				pm_ref_set(lo + (uint64_t)i * PAGESIZE, 1);
			pm_free -= npg;
			return lo;
		}
		prev = cur;
	}
	return 0;
}

void
pm_free_page(uint64_t pa)
{
	int i = refidx(pa);

	/* shared (COW) frames stay allocated until the last owner drops it */
	if (i >= 0) {
		if (pm_ref[i] > 1) {
			pm_ref[i]--;
			return;
		}
		pm_ref[i] = 0;
	}
	wr64(pa, pm_free_head);
	pm_free_head = pa;
	pm_free++;
}

void
pm_free_run(uint64_t pa, int npg)
{
	for (int i = 0; i < npg; i++)
		pm_free_page(pa + (uint64_t)i * PAGESIZE);
}

uint64_t
pm_total_pages(void)
{
	return pm_total;
}

uint64_t
pm_free_pages(void)
{
	return pm_free;
}

/* ---------------- user address spaces ---------------- */

struct pmap *
pmap_new_user(void)
{
	struct pmap *pm;
	uint64_t pml4;

	pm = malloc(sizeof(*pm));
	if (pm == NULL)
		return NULL;
	pml4 = pm_alloc_page();
	if (pml4 == 0) {
		free(pm, sizeof(*pm));
		return NULL;
	}
	memset((void *)P2V(pml4), 0, PAGESIZE);
	/* share the kernel higher half (PML4[511]) with every user process */
	wr64(pml4 + 511 * 8, rd64(KERNEL_PML4_PA + 511 * 8));
	pm->pm_pml4 = pml4;
	return pm;
}

static void
pmap_free_subtree(uint64_t pt_pa, int level)
{
	uint64_t *tab = (uint64_t *)P2V(pt_pa);
	int limit;

	/*
	 * PML4[511] (and everything below it) is the kernel higher half,
	 * shared by every user pmap.  Freeing it would tear down the
	 * direct map and the kernel text, so the user half stops at 511.
	 */
	limit = (level == 0) ? 511 : 512;

	for (int i = 0; i < limit; i++) {
		if (!(tab[i] & PTE_P))
			continue;
		if (tab[i] & PTE_PS)
			continue;
		uint64_t sub = tab[i] & ~0xFFFUL;
		if (level < 3)
			pmap_free_subtree(sub, level + 1);
		pm_free_page(sub);
	}
}

void
pmap_destroy_user(struct pmap *pm)
{
	if (pm == NULL)
		return;
	pmap_free_subtree(pm->pm_pml4, 0);
	pm_free_page(pm->pm_pml4);
	free(pm, sizeof(*pm));
}

void
pmap_activate(struct pmap *pm)
{
	uint64_t cr3 = pm ? pm->pm_pml4 : KERNEL_PML4_PA;

	__asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void
pmap_load_kernel(void)
{
	pmap_activate(NULL);
}

static int
pmap_ensure_mid(uint64_t *parent, int level)
{
	/* allocate an intermediate table with USER on every level */
	uint64_t pa, pte;

	if (*parent & PTE_P)
		return 0;
	pa = pm_alloc_page();
	if (pa == 0)
		return -1;
	memset((void *)P2V(pa), 0, PAGESIZE);
	pte = pa | PTE_P | PTE_RW | PTE_USER;
	*parent = pte;
	return 0;
}

int
pmap_map_user(struct pmap *pm, uint64_t va, uint64_t pa, int user_rw)
{
	uint64_t *pml4 = (uint64_t *)P2V(pm->pm_pml4);
	int i4, i3, i2, i1;

	if (va >= USER_VA_MAX)
		return -1;
	i4 = (va >> 39) & 511;
	i3 = (va >> 30) & 511;
	i2 = (va >> 21) & 511;
	i1 = (va >> 12) & 511;

	if (pmap_ensure_mid(&pml4[i4], 0))
		return -1;
	uint64_t *pdpt = (uint64_t *)P2V(pml4[i4] & ~0xFFFUL);
	if (pmap_ensure_mid(&pdpt[i3], 1))
		return -1;
	uint64_t *pd = (uint64_t *)P2V(pdpt[i3] & ~0xFFFUL);
	if (pmap_ensure_mid(&pd[i2], 2))
		return -1;
	uint64_t *pt = (uint64_t *)P2V(pd[i2] & ~0xFFFUL);

	pt[i1] = (pa & ~0xFFFUL) | PTE_P | PTE_USER |
	    (user_rw ? PTE_RW : 0);
	return 0;
}

/* Return the PTE for va, or NULL if any level along the walk is absent. */
static uint64_t *
pmap_pte(struct pmap *pm, uint64_t va)
{
	uint64_t *pml4 = (uint64_t *)P2V(pm->pm_pml4);
	int i4 = (va >> 39) & 511, i3 = (va >> 30) & 511;
	int i2 = (va >> 21) & 511, i1 = (va >> 12) & 511;

	if (va >= USER_VA_MAX || !(pml4[i4] & PTE_P))
		return NULL;
	uint64_t *pdpt = (uint64_t *)P2V(pml4[i4] & ~0xFFFUL);
	if (!(pdpt[i3] & PTE_P))
		return NULL;
	uint64_t *pd = (uint64_t *)P2V(pdpt[i3] & ~0xFFFUL);
	if (!(pd[i2] & PTE_P))
		return NULL;
	uint64_t *pt = (uint64_t *)P2V(pd[i2] & ~0xFFFUL);
	return &pt[i1];
}

uint64_t
pmap_extract_user(struct pmap *pm, uint64_t va)
{
	uint64_t *pte = pmap_pte(pm, va);

	return (pte != NULL && (*pte & PTE_P)) ? (*pte & ~0xFFFUL) : 0;
}

void
pmap_remove_user(struct pmap *pm, uint64_t va)
{
	uint64_t *pte = pmap_pte(pm, va);

	if (pte == NULL || !(*pte & PTE_P))
		return;
	pm_free_page(*pte & ~0xFFFUL);	/* drop this mapping's reference */
	*pte = 0;
	invlpg(va);
}

/* Mark a present user page read-only and shared (copy on write). */
static void
pmap_cow_mark(struct pmap *pm, uint64_t va)
{
	uint64_t *pte = pmap_pte(pm, va);

	if (pte != NULL && (*pte & PTE_P)) {
		*pte = (*pte & ~PTE_RW) | PTE_COW;
		invlpg(va);
	}
}

/*
 * COW write fault: the page is present but read-only because it is shared
 * with another address space.  Replace the mapping with a private copy and
 * drop our reference to the old frame.
 */
int
pmap_cow_fault(struct pmap *pm, uint64_t va)
{
	uint64_t *pte = pmap_pte(pm, va & ~0xFFFUL);
	uint64_t oldpa, npa;

	if (pte == NULL || !(*pte & PTE_P) || !(*pte & PTE_COW))
		return -1;
	oldpa = *pte & ~0xFFFUL;
	npa = pm_alloc_page();
	if (npa == 0)
		return -1;
	memcpy((void *)P2V(npa), (const void *)P2V(oldpa), PAGESIZE);
	*pte = npa | PTE_P | PTE_RW | PTE_USER;
	pm_free_page(oldpa);
	invlpg(va & ~0xFFFUL);
	return 0;
}

/* Share every present user page with dst (fork); scan only the regions we
 * ever map.  Both sides become read-only and COW, so the first writer on
 * either side gets a private copy in pmap_cow_fault(). */
int
pmap_copy_user(struct pmap *dst, struct pmap *src)
{
	uint64_t va, lo, hi;

	for (int region = 0; region < 2; region++) {
		if (region == 0) {		/* text/data/heap */
			lo = USER_TEXT;
			hi = USER_BRK_MAX;
		} else {			/* stack */
			lo = USTACK_TOP - USTACK_SIZE;
			hi = USTACK_TOP;
		}
		for (va = lo; va < hi; va += PAGESIZE) {
			uint64_t pa = pmap_extract_user(src, va);

			if (pa == 0)
				continue;
			if (pmap_map_user(dst, va, pa, 0))
				return -1;
			pmap_cow_mark(dst, va);
			pmap_cow_mark(src, va);
			pm_ref_inc(pa);
		}
	}
	return 0;
}
