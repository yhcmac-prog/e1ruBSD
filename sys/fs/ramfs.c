/*	$e1ruBSD: ramfs.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * ramfs: in-memory filesystem, mounted as root.  File data lives in a
 * per-node array of 4KB physical blocks (simple and safe under the bucket
 * allocator).  Vnodes are cached in the node and freed on vn_inactive.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <sys/malloc.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

#define	RN_MAXBLOCKS	128			/* 512KB max per file */

struct ramnode {
	int		 rn_type;
	char		 rn_name[MAXNAMELEN];
	struct ramnode	*rn_parent;
	struct ramnode	*rn_child;
	struct ramnode	*rn_next;
	uint64_t	 rn_ino;
	uint64_t	 rn_size;
	uint64_t	 rn_blocks[RN_MAXBLOCKS];
	struct vnode	*rn_vp;
};

static struct ramnode	*root_node;
static uint64_t		 ram_ino = 2;

static int	 ramfs_lookup(struct vnode *, const char *, struct vnode **);
static int	 ramfs_create(struct vnode *, const char *, struct vnode **);
static int	 ramfs_mkdir(struct vnode *, const char *, struct vnode **);
static int	 ramfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int	 ramfs_write(struct vnode *, uint64_t, const void *, size_t,
		    size_t *);
static int	 ramfs_getattr(struct vnode *, uint64_t *, enum vtype *);
static int	 ramfs_readdir(struct vnode *, uint64_t *, void *, size_t,
		    size_t *);
static int	 ramfs_inactive(struct vnode *);

static const struct vnodeops ramfs_vnops = {
	ramfs_lookup,
	ramfs_create,
	ramfs_mkdir,
	ramfs_read,
	ramfs_write,
	ramfs_getattr,
	ramfs_readdir,
	ramfs_inactive,
};

static struct ramnode *
vnode_to_node(struct vnode *vp)
{
	return vp->v_data;
}

static struct vnode *
node_to_vnode(struct ramnode *rn)
{
	if (rn->rn_vp != NULL)
		return rn->rn_vp;
	struct vnode *vp = malloc(sizeof(*vp));

	if (vp == NULL)
		return NULL;
	vp->v_type = (enum vtype)rn->rn_type;
	vp->v_mount = NULL;			/* patched by caller */
	vp->v_op = &ramfs_vnops;
	vp->v_data = rn;
	vp->v_size = rn->rn_size;
	vp->v_ino = rn->rn_ino;
	vp->v_refcnt = 0;
	rn->rn_vp = vp;
	return vp;
}

static struct ramnode *
ramnode_new(enum vtype type, const char *name, struct ramnode *parent)
{
	struct ramnode *rn = malloc(sizeof(*rn));

	if (rn == NULL)
		return NULL;
	memset(rn, 0, sizeof(*rn));
	rn->rn_type = (int)type;
	strlcpy(rn->rn_name, name, sizeof(rn->rn_name));
	rn->rn_parent = parent;
	rn->rn_ino = ram_ino++;
	return rn;
}

static int
ramfs_lookup(struct vnode *dvp, const char *name, struct vnode **vpp)
{
	struct ramnode *d = vnode_to_node(dvp);

	for (struct ramnode *c = d->rn_child; c; c = c->rn_next) {
		if (strcmp(c->rn_name, name) == 0) {
			struct vnode *vp = node_to_vnode(c);

			if (vp == NULL)
				return -1;
			vp->v_mount = dvp->v_mount;
			vp->v_size = c->rn_size;
			vn_ref(vp);
			*vpp = vp;
			return 0;
		}
	}
	return -1;
}

static int
ramfs_link_child(struct ramnode *d, struct ramnode *c)
{
	c->rn_next = d->rn_child;
	d->rn_child = c;
	c->rn_parent = d;
	return 0;
}

static int
ramfs_create(struct vnode *dvp, const char *name, struct vnode **vpp)
{
	struct ramnode *d = vnode_to_node(dvp);
	struct ramnode *rn = ramnode_new(VREG, name, d);

	if (rn == NULL)
		return -1;
	ramfs_link_child(d, rn);
	struct vnode *vp = node_to_vnode(rn);

	if (vp == NULL)
		return -1;
	vp->v_mount = dvp->v_mount;
	vn_ref(vp);
	*vpp = vp;
	return 0;
}

static int
ramfs_mkdir(struct vnode *dvp, const char *name, struct vnode **vpp)
{
	struct ramnode *d = vnode_to_node(dvp);
	struct ramnode *rn = ramnode_new(VDIR, name, d);

	if (rn == NULL)
		return -1;
	ramfs_link_child(d, rn);
	struct vnode *vp = node_to_vnode(rn);

	if (vp == NULL)
		return -1;
	vp->v_mount = dvp->v_mount;
	vn_ref(vp);
	*vpp = vp;
	return 0;
}

static int
ramfs_read(struct vnode *vp, uint64_t off, void *buf, size_t len,
    size_t *donep)
{
	struct ramnode *rn = vnode_to_node(vp);

	if (off >= rn->rn_size) {
		if (donep)
			*donep = 0;
		return 0;
	}
	if (off + len > rn->rn_size)
		len = rn->rn_size - off;
	for (size_t got = 0; got < len; ) {
		uint64_t bi = (off + got) / PAGESIZE;
		uint64_t bo = (off + got) % PAGESIZE;
		size_t n = len - got;

		if (n > PAGESIZE - bo)
			n = PAGESIZE - bo;
		if (bi >= RN_MAXBLOCKS || rn->rn_blocks[bi] == 0)
			memset((char *)buf + got, 0, n);
		else
			memcpy((char *)buf + got,
			    (const void *)P2V(rn->rn_blocks[bi] + bo), n);
		got += n;
	}
	if (donep)
		*donep = len;
	return 0;
}

static int
ramfs_write(struct vnode *vp, uint64_t off, const void *buf, size_t len,
    size_t *donep)
{
	struct ramnode *rn = vnode_to_node(vp);
	size_t put = 0;

	/*
	 * vn_open() truncates by calling this with a zero-length, offset 0
	 * request; release every data block and reset the size.
	 */
	if (len == 0 && off == 0) {
		for (int i = 0; i < RN_MAXBLOCKS; i++) {
			if (rn->rn_blocks[i] != 0) {
				pm_free_page(rn->rn_blocks[i]);
				rn->rn_blocks[i] = 0;
			}
		}
		rn->rn_size = 0;
		vp->v_size = 0;
		if (donep)
			*donep = 0;
		return 0;
	}

	while (put < len) {
		uint64_t bi = (off + put) / PAGESIZE;
		uint64_t bo = (off + put) % PAGESIZE;
		size_t n = len - put;

		if (bi >= RN_MAXBLOCKS)
			break;
		if (n > PAGESIZE - bo)
			n = PAGESIZE - bo;
		if (rn->rn_blocks[bi] == 0) {
			uint64_t pa = pm_alloc_page();

			if (pa == 0)
				break;
			memset((void *)P2V(pa), 0, PAGESIZE);
			rn->rn_blocks[bi] = pa;
		}
		memcpy((void *)P2V(rn->rn_blocks[bi] + bo), (const char *)buf + put, n);
		put += n;
	}
	if (off + put > rn->rn_size)
		rn->rn_size = off + put;
	vp->v_size = rn->rn_size;
	if (donep)
		*donep = put;
	return put == len ? 0 : -1;
}

static int
ramfs_getattr(struct vnode *vp, uint64_t *sizep, enum vtype *typep)
{
	struct ramnode *rn = vnode_to_node(vp);

	*sizep = rn->rn_size;
	*typep = (enum vtype)rn->rn_type;
	return 0;
}

static int
ramfs_readdir(struct vnode *vp, uint64_t *cookiep, void *buf, size_t len,
    size_t *donep)
{
	struct ramnode *d = vnode_to_node(vp);
	struct ramnode *c = d->rn_child;
	size_t out = 0;
	uint64_t skip = cookiep ? *cookiep : 0;

	for (uint64_t i = 0; c; c = c->rn_next, i++) {
		struct dirent de;

		if (i < skip)
			continue;
		if (out + sizeof(de) > len)
			break;
		memset(&de, 0, sizeof(de));
		de.d_ino = c->rn_ino;
		de.d_type = c->rn_type == (int)VDIR ? DT_DIR : DT_FILE;
		strlcpy(de.d_name, c->rn_name, sizeof(de.d_name));
		memcpy((char *)buf + out, &de, sizeof(de));
		out += sizeof(de);
	}
	if (cookiep)
		*cookiep = skip + out / sizeof(struct dirent);
	*donep = out;
	return 0;
}

static int
ramfs_inactive(struct vnode *vp)
{
	struct ramnode *rn = vnode_to_node(vp);

	if (rn) {
		rn->rn_vp = NULL;
		/* nodes and blocks stay allocated: root ramfs is permanent */
	}
	return 0;
}

static int
ramfs_mount(struct mount *mp)
{
	root_node = ramnode_new(VDIR, "", NULL);	/* root directory */
	if (root_node == NULL)
		return -1;
	struct vnode *rvp = node_to_vnode(root_node);

	if (rvp == NULL)
		return -1;
	rvp->v_mount = mp;
	vn_ref(rvp);
	mp->m_root = rvp;
	mp->m_data = root_node;
	return 0;
}

static int
ramfs_root(struct mount *mp, struct vnode **vpp)
{
	vn_ref(mp->m_root);
	*vpp = mp->m_root;
	return 0;
}

static const struct vfsops ramfs_ops = {
	ramfs_mount,
	ramfs_root,
	NULL,
};

const struct vfsops *
ramfs_vfsops(void)
{
	return &ramfs_ops;
}

/* absolute path of a vnode, walking rn_parent */
int
ramfs_path(struct vnode *vp, char *buf, size_t len)
{
	struct ramnode *rn = vnode_to_node(vp);
	char tmp[MAXPATHLEN];
	size_t pos = sizeof(tmp) - 1;

	tmp[pos] = '\0';
	while (rn != NULL && rn->rn_parent != NULL) {
		size_t n = strlen(rn->rn_name);

		if (pos < n + 1)
			break;
		pos -= n;
		memcpy(tmp + pos, rn->rn_name, n);
		if (pos > 0)
			tmp[--pos] = '/';
		rn = rn->rn_parent;
	}
	tmp[--pos] = '/';
	strlcpy(buf, tmp + pos, len);
	return 0;
}
