/*	$e1ruBSD: pmap.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _MACHINE_PMAP_H_
#define _MACHINE_PMAP_H_

#include <sys/types.h>
#include <sys/bootinfo.h>

/*
 * pmap: the machine-dependent virtual memory interface, NetBSD naming.
 * Kernel direct map is built during locore bootstrap; this layer manages
 * the physical page pool and per-process user address spaces.
 */

struct pmap {
	uint64_t	pm_pml4;	/* physical address of PML4 */
};

void		 pmap_init(struct bootinfo *bi);
void		 pmap_free_bootinfo_pages(void);

/* user address spaces */
struct pmap	*pmap_new_user(void);
void		 pmap_destroy_user(struct pmap *pm);
void		 pmap_activate(struct pmap *pm);	/* load CR3 */
void		 pmap_load_kernel(void);		/* CR3 = kernel map */

int		 pmap_map_user(struct pmap *pm, uint64_t va, uint64_t pa,
		    int user_rw);	/* returns 0 or -1 */
uint64_t	 pmap_extract_user(struct pmap *pm, uint64_t va);
void		 pmap_remove_user(struct pmap *pm, uint64_t va);
int		 pmap_copy_user(struct pmap *dst, struct pmap *src);
int		 pmap_cow_fault(struct pmap *pm, uint64_t va);

/* physical page pool (frame granularity) */
uint64_t	 pm_alloc_page(void);
void		 pm_free_page(uint64_t pa);
uint64_t	 pm_alloc_pages(int npg);	/* physically contiguous run */
void		 pm_free_run(uint64_t pa, int npg);

/* physical access during early boot (identity window still present) */
void		 pmap_drop_low_identity(void);

extern uint64_t	 pm_kernel_end;		/* physical end of kernel image */

#endif	/* !_MACHINE_PMAP_H_ */
