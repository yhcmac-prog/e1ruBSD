/*	$e1ruBSD: vfs.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_VFS_H_
#define _SYS_VFS_H_

#include <sys/types.h>
#include <sys/param.h>

/*
 * Minimal vnode/vfs framework, modeled after NetBSD's but simplified:
 * every mount has one ops vector, vnodes reference per-fs private data.
 */

enum vtype { VNON, VREG, VDIR, VBLK, VCHR };

struct vnode;
struct uio;
struct mount;

struct vnodeops {
	int	(*vn_lookup)(struct vnode *dvp, const char *name,
		    struct vnode **vpp);
	int	(*vn_create)(struct vnode *dvp, const char *name,
		    struct vnode **vpp);
	int	(*vn_mkdir)(struct vnode *dvp, const char *name,
		    struct vnode **vpp);
	int	(*vn_read)(struct vnode *vp, uint64_t off, void *buf,
		    size_t len, size_t *donep);
	int	(*vn_write)(struct vnode *vp, uint64_t off, const void *buf,
		    size_t len, size_t *donep);
	int	(*vn_getattr)(struct vnode *vp, uint64_t *sizep,
		    enum vtype *typep);
	int	(*vn_readdir)(struct vnode *vp, uint64_t *cookiep, void *buf,
		    size_t len, size_t *donep);
	int	(*vn_inactive)(struct vnode *vp);
};

struct vnode {
	enum vtype		 v_type;
	struct mount		*v_mount;
	const struct vnodeops	*v_op;
	void			*v_data;
	uint64_t		 v_size;
	uint64_t		 v_ino;
	int			 v_refcnt;
};

struct vfsops {
	int	(*vfs_mount)(struct mount *mp);
	int	(*vfs_root)(struct mount *mp, struct vnode **vpp);
	int	(*vfs_unmount)(struct mount *mp);
};

struct mount {
	const struct vfsops	*m_ops;
	struct vnode		*m_root;
	void			*m_data;
};

struct file {
	struct vnode	*f_vnode;
	uint64_t	 f_offset;
	int		 f_flags;
};

/* one-shot readdir record, 64 bytes */
struct dirent {
	uint64_t	 d_ino;
	uint8_t		 d_type;
	char		 d_name[55];
} __packed;

/* vfs_subr.c */
int	 vfs_mountroot(void);
int	 namei(const char *path, struct proc *p, struct vnode **vpp);
int	 vn_open(const char *path, int flags, struct file **fp);
int	 vn_read(struct file *fp, void *buf, size_t len, size_t *donep);
int	 vn_write(struct file *fp, const void *buf, size_t len, size_t *donep);
void	 vn_ref(struct vnode *vp);
void	 vn_unref(struct vnode *vp);
struct vnode *vp_console(void);		/* character console vnode (fd 0-2) */
int	 root_mounted(void);
int	 vfs_create_path(const char *path, struct proc *p, struct vnode **vpp);
int	 vfs_path_of(struct vnode *vp, char *buf, size_t len);

/* fs/ramfs/ramfs.c */
const struct vfsops *ramfs_vfsops(void);
int	 ramfs_path(struct vnode *vp, char *buf, size_t len);

#endif	/* !_SYS_VFS_H_ */
