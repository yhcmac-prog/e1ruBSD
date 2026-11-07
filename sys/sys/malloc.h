/*	$e1ruBSD: malloc.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_MALLOC_H_
#define _SYS_MALLOC_H_

#include <sys/types.h>

/* Simple bucket allocator over the physical page allocator. */
void	*malloc(size_t size);
void	 free(void *ptr, size_t size);
void	 malloc_init(void);

/* Physical page allocator (pmap.c). */
uint64_t	 pm_alloc_page(void);		/* returns physical address, 0 = none */
void		 pm_free_page(uint64_t pa);
uint64_t	 pm_total_pages(void);
uint64_t	 pm_free_pages(void);

#endif	/* !_SYS_MALLOC_H_ */
