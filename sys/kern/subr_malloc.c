/*	$e1ruBSD: subr_malloc.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Bucket allocator over the physical page allocator.  Buckets 16..4096
 * keep intrusive free lists inside free chunks; larger requests are
 * page-granular, physically contiguous, with the page count stored in the
 * first word of the first page.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

#define	NBUCKETS	9
static const size_t bucket_size[NBUCKETS] = {
	16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

static uint64_t bucket_head[NBUCKETS];		/* phys addrs of free chunks */

void
malloc_init(void)
{
	kprintf("malloc0: %d buckets, page pool ready\n", NBUCKETS);
}

static int
bucket_of(size_t size)
{
	for (int i = 0; i < NBUCKETS; i++)
		if (size <= bucket_size[i])
			return i;
	return -1;
}

static uint64_t
kv2pa(void *kv)
{
	return (uint64_t)kv - DMAP_BASE;
}

static void *
pa2kv(uint64_t pa)
{
	return (void *)P2V(pa);
}

void *
malloc(size_t size)
{
	int b = bucket_of(size);
	uint64_t pa, page;

	if (size == 0)
		size = 1;
	if (b >= 0) {
		if (bucket_head[b]) {
			pa = bucket_head[b];
			bucket_head[b] = *(uint64_t *)pa2kv(pa);
			return pa2kv(pa);
		}
		page = pm_alloc_page();
		if (page == 0)
			return NULL;
		/* carve the page into chunks, chain them */
		size_t csz = bucket_size[b];
		size_t n = PAGESIZE / csz;
		for (size_t i = 0; i + 1 < n; i++)
			*(uint64_t *)pa2kv(page + i * csz) =
			    page + (i + 1) * csz;
		*(uint64_t *)pa2kv(page + (n - 1) * csz) = 0;
		bucket_head[b] = page + csz;
		return pa2kv(page);
	}

	/* page-granular: contiguous run, page count in the first word */
	size_t npg = pgroundup(size + 16) / PAGESIZE;
	uint64_t *hdr;

	if (npg == 0)
		npg = 1;
	page = pm_alloc_pages((int)npg);
	if (page == 0)
		return NULL;
	hdr = pa2kv(page);
	hdr[0] = npg;				/* pages owned by this chunk */
	return (void *)((char *)hdr + 16);
}

void
free(void *ptr, size_t size)
{
	int b;
	uint64_t pa;

	if (ptr == NULL)
		return;
	pa = kv2pa(ptr);
	b = bucket_of(size);
	if (b >= 0) {
		/* bucket chunk: may sit anywhere inside its page */
		*(uint64_t *)ptr = bucket_head[b];
		bucket_head[b] = pa;
		return;
	}
	/* page-granular chunk: header lives 16 bytes before the pointer */
	uint64_t *hdr = (uint64_t *)((char *)ptr - 16);

	pm_free_run(pa - 16, (int)hdr[0]);
}
