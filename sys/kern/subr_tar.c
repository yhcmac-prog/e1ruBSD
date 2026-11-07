/*	$e1ruBSD: subr_tar.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Populate the root ramfs from the ustar archive exposed by md(4).
 * This is the e1ruBSD equivalent of NetBSD's md(4) root ramdisk.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/malloc.h>

extern int	 md_read(uint64_t off, void *buf, size_t len, size_t *donep);
extern uint64_t	 md_size_bytes(void);

static uint64_t
octal(const char *s, int n)
{
	uint64_t v = 0;

	while (n > 0 && (*s == ' ' || *s == '0')) {
		s++;
		n--;
	}
	while (n > 0 && *s >= '0' && *s <= '7') {
		v = v * 8 + (*s - '0');
		s++;
		n--;
	}
	return v;
}

static int
tar_create(const char *path, struct vnode **vpp)
{
	char dir[MAXPATHLEN];
	char name[MAXNAMELEN];
	struct vnode *dvp;

	strlcpy(dir, path, sizeof(dir));
	char *slash = NULL;

	for (char *s = dir; *s; s++)
		if (*s == '/')
			slash = slash ? (slash < s ? s : slash) : s;
	(void)slash;
	/* find last slash */
	char *last = NULL;
	for (char *s = dir; *s; s++)
		if (*s == '/')
			last = s;
	if (last == NULL) {
		strlcpy(name, dir, sizeof(name));
		strlcpy(dir, "/", sizeof(dir));
	} else {
		strlcpy(name, last + 1, sizeof(name));
		*last = '\0';
		if (dir[0] == '\0')
			strlcpy(dir, "/", sizeof(dir));
	}
	if (namei(dir, NULL, &dvp) != 0)
		return -1;
	int err = dvp->v_op->vn_create(dvp, name, vpp);
	vn_unref(dvp);
	return err ? -1 : 0;
}

static int
tar_mkdir_path(const char *path)
{
	char dir[MAXPATHLEN];
	char name[MAXNAMELEN];
	struct vnode *dvp;

	strlcpy(dir, path, sizeof(dir));
	char *last = NULL;

	for (char *s = dir; *s; s++)
		if (*s == '/')
			last = s;
	if (last == NULL) {
		strlcpy(name, dir, sizeof(name));
		strlcpy(dir, "/", sizeof(dir));
	} else {
		strlcpy(name, last + 1, sizeof(name));
		*last = '\0';
		if (dir[0] == '\0')
			strlcpy(dir, "/", sizeof(dir));
	}
	if (namei(dir, NULL, &dvp) != 0)
		return -1;
	struct vnode *vp = NULL;
	int err = dvp->v_op->vn_mkdir(dvp, name, &vp);
	vn_unref(dvp);
	if (vp)
		vn_unref(vp);
	return err ? -1 : 0;
}

void
tar_extract_root(void)
{
	char hdr[512];
	char data[8192];
	uint64_t off = 0;
	uint64_t size = md_size_bytes();
	int files = 0, dirs = 0;

	while (off + 512 <= size) {
		size_t done = 0;

		if (md_read(off, hdr, 512, &done) || done != 512)
			break;
		if (hdr[0] == '\0')
			break;			/* end of archive */
		char name[101];
		memcpy(name, hdr, 100);
		name[100] = '\0';
		if (name[0] == '\0')
			break;
		/* strip trailing slash on dirs */
		int isdir = hdr[156] == '5';
		size_t nl = strlen(name);

		if (nl && name[nl - 1] == '/') {
			name[nl - 1] = '\0';
			isdir = 1;
		}
		uint64_t fsize = octal(hdr + 124, 12);
		off += 512;

		if (isdir) {
			if (tar_mkdir_path(name) == 0)
				dirs++;
		} else {
			struct vnode *vp = NULL;

			if (tar_create(name, &vp) == 0 && vp != NULL) {
				uint64_t left = fsize, doff = 0;

				while (left) {
					size_t chunk = left > sizeof(data) ?
					    sizeof(data) : left;
					size_t wdone = 0, rdone = 0;

					if (md_read(off + doff, data, chunk,
					    &rdone) || rdone != chunk)
						break;
					vp->v_op->vn_write(vp, doff, data,
					    chunk, &wdone);
					doff += wdone;
					left -= wdone;
				}
				vn_unref(vp);
				files++;
			}
			off += (fsize + 511) & ~511ULL;
		}
	}
	kprintf("rootfs: %d files, %d dirs installed from md0\n", files, dirs);
}
