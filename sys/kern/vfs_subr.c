/*	$e1ruBSD: vfs_subr.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * VFS framework: namei path resolution, mount table, file open/read/write,
 * and the character console vnode backing fd 0-2.  Modeled on NetBSD's
 * vfs_subr but deliberately tiny; ramfs is the only mount.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <sys/cons.h>
#include <sys/malloc.h>

static struct mount	 root_mount;
static int		 mounted;

/* ---------------- console vnode ---------------- */

static int
cons_read_vn(struct vnode *vp, uint64_t off, void *buf, size_t len,
    size_t *donep)
{
	*donep = cons_read(buf, len > 1024 ? 1024 : len);
	return 0;
}

static int
cons_write_vn(struct vnode *vp, uint64_t off, const void *buf, size_t len,
    size_t *donep)
{
	const char *s = buf;

	for (size_t i = 0; i < len; i++)
		cnputc(s[i]);
	*donep = len;
	return 0;
}

static int
cons_getattr(struct vnode *vp, uint64_t *sizep, enum vtype *typep)
{
	*sizep = 0;
	*typep = VCHR;
	return 0;
}

static int
cons_nop(void)
{
	return 0;
}

static const struct vnodeops cons_vops = {
	NULL,			/* lookup */
	NULL,			/* create */
	NULL,			/* mkdir */
	cons_read_vn,
	cons_write_vn,
	cons_getattr,
	NULL,			/* readdir */
	(int (*)(struct vnode *))cons_nop,
};

static struct vnode	 cons_vp = { VCHR, NULL, &cons_vops, NULL, 0, 0, 2 };

struct vnode *
vp_console(void)
{
	return &cons_vp;
}

/* ---------------- refcounting ---------------- */

void
vn_ref(struct vnode *vp)
{
	if (vp)
		vp->v_refcnt++;
}

void
vn_unref(struct vnode *vp)
{
	if (vp == NULL || vp == &cons_vp)
		return;
	if (--vp->v_refcnt <= 0 && vp->v_op->vn_inactive)
		vp->v_op->vn_inactive(vp);
}

int
root_mounted(void)
{
	return mounted;
}

int
vfs_mountroot(void)
{
	int err;

	root_mount.m_ops = ramfs_vfsops();
	root_mount.m_root = NULL;
	err = root_mount.m_ops->vfs_mount(&root_mount);
	if (err)
		return err;
	mounted = 1;
	return 0;
}

/* ---------------- namei ---------------- */

int
namei(const char *path, struct proc *p, struct vnode **vpp)
{
	struct vnode *vp;
	char comp[MAXNAMELEN];
	size_t i = 0;

	if (!mounted)
		return -1;
	vp = root_mount.m_root;
	vn_ref(vp);
	if (path[0] != '/' && p != NULL && p->p_cwd != NULL) {
		vn_unref(vp);
		vp = p->p_cwd;
		vn_ref(vp);
	}
	while (path[i]) {
		size_t j = 0;

		while (path[i] == '/')
			i++;
		if (!path[i])
			break;
		while (path[i] && path[i] != '/')
			comp[j++] = path[i++];
		comp[j] = '\0';
		if (strcmp(comp, ".") == 0)
			continue;
		if (vp->v_type != VDIR) {
			vn_unref(vp);
			return -1;
		}
		struct vnode *nv = NULL;
		if (vp->v_op->vn_lookup(vp, comp, &nv) != 0) {
			vn_unref(vp);
			return -1;
		}
		vn_unref(vp);
		vp = nv;
	}
	*vpp = vp;
	return 0;
}

/* create file/dirs along a path (tar extraction and O_CREAT) */
int
vfs_create_path(const char *path, struct proc *p, struct vnode **vpp)
{
	struct vnode *vp;
	char comp[MAXNAMELEN];
	size_t i = 0;

	if (!mounted)
		return -1;
	vp = root_mount.m_root;
	vn_ref(vp);
	while (path[i]) {
		size_t j = 0;
		int last;

		while (path[i] == '/')
			i++;
		if (!path[i])
			break;
		while (path[i] && path[i] != '/')
			comp[j++] = path[i++];
		comp[j] = '\0';
		last = path[i] == '\0';
		if (vp->v_type != VDIR) {
			vn_unref(vp);
			return -1;
		}
		struct vnode *nv = NULL;
		if (vp->v_op->vn_lookup(vp, comp, &nv) != 0) {
			if (vp->v_op->vn_create(vp, comp, &nv) != 0) {
				vn_unref(vp);
				return -1;
			}
		}
		vn_unref(vp);
		vp = nv;
		(void)last;
	}
	*vpp = vp;
	return 0;
}

/* ---------------- file layer ---------------- */

static int
split_dirname(char *path, char *name)
{
	char *slash = NULL;

	for (char *s = path; *s; s++)
		if (*s == '/')
			slash = s;
	if (slash == NULL) {
		strlcpy(name, path, MAXNAMELEN);
		path[0] = '\0';
		return 0;
	}
	strlcpy(name, slash + 1, MAXNAMELEN);
	if (slash == path)
		path[1] = '\0';			/* keep "/" */
	else
		*slash = '\0';
	return 0;
}

int
vn_open(const char *path, int flags, struct file **fp)
{
	struct vnode *vp;
	char buf[MAXPATHLEN], name[MAXNAMELEN];

	strlcpy(buf, path, sizeof(buf));
	if (namei(buf, NULL, &vp) != 0) {
		if (!(flags & O_CREAT))
			return -1;
		/* create: split into dir + name */
		strlcpy(buf, path, sizeof(buf));
		split_dirname(buf, name);
		if (buf[0] == '\0')
			strlcpy(buf, "/", sizeof(buf));
		struct vnode *dvp;
		if (namei(buf, NULL, &dvp) != 0 || dvp->v_type != VDIR)
			return -1;
		if (dvp->v_op->vn_create(dvp, name, &vp) != 0) {
			vn_unref(dvp);
			return -1;
		}
		vn_unref(dvp);
	}
	if (flags & O_TRUNC && vp->v_type == VREG) {
		/* ramfs truncate: rewrite zero bytes at offset 0 */
		if (vp->v_op->vn_write(vp, 0, NULL, 0, NULL) != 0)
			return -1;
	}
	/* steal the reference returned by namei/create */
	*fp = malloc(sizeof(struct file));
	if (*fp == NULL) {
		vn_unref(vp);
		return -1;
	}
	(*fp)->f_vnode = vp;
	(*fp)->f_offset = 0;
	(*fp)->f_flags = flags;
	return 0;
}

int
vn_read(struct file *fp, void *buf, size_t len, size_t *donep)
{
	int err = fp->f_vnode->v_op->vn_read(fp->f_vnode, fp->f_offset,
	    buf, len, donep);

	if (err == 0 && donep)
		fp->f_offset += *donep;
	return err;
}

int
vn_write(struct file *fp, const void *buf, size_t len, size_t *donep)
{
	int err = fp->f_vnode->v_op->vn_write(fp->f_vnode, fp->f_offset,
	    buf, len, donep);

	if (err == 0 && donep)
		fp->f_offset += *donep;
	return err;
}

/* full path of a vnode by walking the ramfs parent chain (ramfs nodes) */
int
vfs_path_of(struct vnode *vp, char *buf, size_t len)
{
	if (vp == NULL || vp->v_mount != &root_mount)
		return -1;
	return ramfs_path(vp, buf, len);
}
