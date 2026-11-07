/*	$e1ruBSD: syscalls.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * System call implementation.  User pointers are dereferenced directly
 * while CR3 holds the caller's map; every pointer is range-checked to
 * stay inside the user half.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/cons.h>
#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/trap.h>

#define	EINVAL	22
#define	ENOENT	2
#define	ENOMEM	12
#define	EBADF	9
#define	EFAULT	14

#define	USER_PTR_OK(p, n) \
	((uint64_t)(p) < 0x0000800000000000UL && (uint64_t)(p) + (n) < 0x0000800000000000UL)

int	 exec_load(struct proc *p, struct vnode *vp, char *argv[], int argc);
static const char *strrchr0(const char *s);

static int
fd_alloc(struct proc *p, struct file *f)
{
	for (int fd = 0; fd < NOFILE; fd++) {
		if (p->p_fds[fd] == NULL) {
			p->p_fds[fd] = f;
			return fd;
		}
	}
	return -1;
}

static int64_t
sys_read(struct proc *p, struct trapframe *tf)
{
	int fd = (int)tf->rdi;
	char *buf = (char *)tf->rsi;
	uint64_t len = tf->rdx;
	struct file *f;

	if (fd < 0 || fd >= NOFILE || p->p_fds[fd] == NULL)
		return -EBADF;
	if (!USER_PTR_OK(buf, len) || len > 65536)
		return -EFAULT;
	f = p->p_fds[fd];
	/* console line read */
	if (f->f_vnode->v_type == VCHR)
		return cons_read(buf, len > 1024 ? 1024 : len);
	size_t done = 0;
	if (vn_read(f, buf, len, &done))
		return -EFAULT;
	return done;
}

static int64_t
sys_write(struct proc *p, struct trapframe *tf)
{
	int fd = (int)tf->rdi;
	const char *buf = (const char *)tf->rsi;
	uint64_t len = tf->rdx;
	struct file *f;

	if (fd < 0 || fd >= NOFILE || p->p_fds[fd] == NULL)
		return -EBADF;
	if (!USER_PTR_OK(buf, len) || len > 65536)
		return -EFAULT;
	f = p->p_fds[fd];
	if (f->f_vnode->v_type == VCHR) {
		for (uint64_t i = 0; i < len; i++)
			cnputc(buf[i]);
		return len;
	}
	size_t done = 0;
	if (vn_write(f, buf, len, &done))
		return -EFAULT;
	return done;
}

static int64_t
sys_open(struct proc *p, struct trapframe *tf)
{
	const char *upath = (const char *)tf->rdi;
	int flags = (int)tf->rsi;
	char path[MAXPATHLEN];
	struct file *f;

	if (!USER_PTR_OK(upath, 1))
		return -EFAULT;
	strlcpy(path, upath, MAXPATHLEN);
	f = malloc(sizeof(*f));
	if (f == NULL)
		return -ENOMEM;
	if (vn_open(path, flags, &f) != 0) {
		free(f, sizeof(*f));
		return -ENOENT;
	}
	f->f_offset = 0;
	int fd = fd_alloc(p, f);
	if (fd < 0) {
		free(f, sizeof(*f));
		return -ENOMEM;
	}
	return fd;
}

static int64_t
sys_close(struct proc *p, struct trapframe *tf)
{
	int fd = (int)tf->rdi;

	if (fd < 0 || fd >= NOFILE || p->p_fds[fd] == NULL)
		return -EBADF;
	vn_unref(p->p_fds[fd]->f_vnode);
	free(p->p_fds[fd], sizeof(struct file));
	p->p_fds[fd] = NULL;
	return 0;
}

static int64_t
sys_fork(struct proc *p, struct trapframe *tf)
{
	struct proc *np = sched_newproc(p->p_name, p);

	if (np == NULL)
		return -ENOMEM;
	np->p_pmap = pmap_new_user();
	if (np->p_pmap == NULL || pmap_copy_user(np->p_pmap, p->p_pmap)) {
		proc_free_resources(np);
		np->p_state = SUNUSED;
		return -ENOMEM;
	}
	np->p_brk = p->p_brk;
	np->p_cwd = p->p_cwd;
	if (np->p_cwd)
		vn_ref(np->p_cwd);
	for (int fd = 0; fd < NOFILE; fd++) {
		if (p->p_fds[fd]) {
			np->p_fds[fd] = malloc(sizeof(struct file));
			*np->p_fds[fd] = *p->p_fds[fd];
			vn_ref(np->p_fds[fd]->f_vnode);
		}
	}
	/*
	 * Child kernel stack: [trapframe][trapret].  swtch() restores the
	 * callee-saved registers from p_ctx and then returns through
	 * p_ctx.rsp, which must therefore point at the trapret slot — the
	 * same convention exec_load() uses.  trapret then pops the frame
	 * copied from the parent, so the child resumes with rax=0.
	 */
	uint64_t top = np->p_kstack + KSTACK_SIZE;
	top -= sizeof(struct trapframe);
	memcpy((void *)top, tf, sizeof(struct trapframe));
	((struct trapframe *)top)->rax = 0;		/* child returns 0 */
	((struct trapframe *)top)->rflags |= 0x200;
	top -= 8;
	*(uint64_t *)top = (uint64_t)trapret;
	np->p_ctx.rsp = top;
	np->p_state = SRUNNABLE;
	sched_enqueue(np);
	return np->p_pid;
}

static int64_t
sys_exec(struct proc *p, struct trapframe *tf)
{
	const char *upath = (const char *)tf->rdi;
	char **uargv = (char **)tf->rsi;
	char path[MAXPATHLEN];
	char argbuf[MAXARGS][128];
	char *argv[MAXARGS + 1];
	int argc = 0;
	struct vnode *vp;

	if (!USER_PTR_OK(upath, 1))
		return -EFAULT;
	strlcpy(path, upath, MAXPATHLEN);
	if (uargv != NULL) {
		for (int i = 0; i < MAXARGS; i++) {
			char *ua;
			if (!USER_PTR_OK(uargv + i, 8))
				break;
			ua = uargv[i];
			if (ua == NULL)
				break;
			if (!USER_PTR_OK(ua, 1))
				return -EFAULT;
			strlcpy(argbuf[i], ua, 128);
			argv[i] = argbuf[i];
			argc++;
		}
	}
	argv[argc] = NULL;
	if (namei(path, p, &vp) != 0)
		return -ENOENT;
	if (vp->v_type != VREG)
		return -ENOENT;
	if (exec_load(p, vp, argv, argc) != 0)
		return -ENOENT;

	/*
	 * exec_load filled p->p_tf, the template used when a process is
	 * first switched to.  This call, however, returns through the
	 * live trapframe on the current kernel stack, so adopt the new
	 * user context there as well — otherwise iretq would resume the
	 * old image inside the new address space.
	 */
	*tf = p->p_tf;

	/* rename from argv[0] basename */
	{
		const char *b = strrchr0(path);
		strlcpy(p->p_name, b, sizeof(p->p_name));
	}
	return 0;
}

static const char *
strrchr0(const char *s)
{
	const char *last = s;

	while (*s) {
		if (*s == '/')
			last = s + 1;
		s++;
	}
	return last;
}

static int64_t
sys_wait4(struct proc *p, struct trapframe *tf)
{
	int pid = (int)tf->rdi;
	int *ustatus = (int *)tf->rsi;

	for (;;) {
		struct proc *z = NULL;

		for (int i = 0; i < NPROC; i++) {
			struct proc *c = &proc_table[i];

			if (c->p_state == SZOMBIE && c->p_parent == p &&
			    (pid == -1 || c->p_pid == pid)) {
				z = c;
				break;
			}
		}
		if (z != NULL) {
			int code = z->p_exitcode;
			int zpid = z->p_pid;

			if (ustatus != NULL && USER_PTR_OK(ustatus, 4))
				*ustatus = code;
			z->p_state = SUNUSED;
			return zpid;
		}
		/* any live children? */
		int have = 0;
		for (int i = 0; i < NPROC; i++) {
			struct proc *c = &proc_table[i];
			if (c->p_state != SUNUSED && c->p_state != SZOMBIE &&
			    c->p_parent == p &&
			    (pid == -1 || c->p_pid == pid))
				have = 1;
		}
		if (!have)
			return -1;
		tsleep(p, "wait4", 0);
	}
}

static int64_t
sys_exit(struct proc *p, struct trapframe *tf)
{
	p->p_exitcode = (int)(tf->rdi & 0xFF);
	sched_exit(p);				/* never returns */
	return 0;
}

static int64_t
sys_getpid(struct proc *p, struct trapframe *tf)
{
	return p->p_pid;
}

static int64_t
sys_sbrk(struct proc *p, struct trapframe *tf)
{
	int64_t incr = (int64_t)tf->rdi;
	uint64_t old = p->p_brk;

	if (incr > 0) {
		uint64_t top = pgroundup(old + incr);
		if (top > USER_BRK_MAX)
			return -ENOMEM;
		for (uint64_t va = pgroundup(old); va < top; va += PAGESIZE) {
			uint64_t pa = pm_alloc_page();
			if (pa == 0)
				return -ENOMEM;
			memset((void *)P2V(pa), 0, PAGESIZE);
			if (pmap_map_user(p->p_pmap, va, pa, 1))
				return -ENOMEM;
		}
		p->p_brk = old + incr;
	} else if (incr < 0) {
		uint64_t bot = pgroundup(old + incr);
		for (uint64_t va = pgroundup(old + incr); va < pgroundup(old);
		    va += PAGESIZE)
			pmap_remove_user(p->p_pmap, va);
		p->p_brk = bot;
	}
	return old;
}

static int64_t
sys_uname(struct proc *p, struct trapframe *tf)
{
	struct utsname *u = (struct utsname *)tf->rdi;

	if (!USER_PTR_OK(u, sizeof(*u)))
		return -EFAULT;
	memset(u, 0, sizeof(*u));
	strlcpy(u->sysname, USP_NAME, sizeof(u->sysname));
	strlcpy(u->nodename, USP_NAME, sizeof(u->nodename));
	strlcpy(u->release, USP_RELEASE, sizeof(u->release));
	strlcpy(u->version, USP_VERSION, sizeof(u->version));
	strlcpy(u->machine, USP_MACHINE, sizeof(u->machine));
	return 0;
}

static int64_t
sys_readdir(struct proc *p, struct trapframe *tf)
{
	const char *upath = (const char *)tf->rdi;
	void *ubuf = (void *)tf->rsi;
	uint64_t len = tf->rdx;
	char path[MAXPATHLEN];
	struct vnode *vp;
	uint64_t cookie = 0;
	size_t done = 0;

	if (!USER_PTR_OK(upath, 1) || !USER_PTR_OK(ubuf, len))
		return -EFAULT;
	strlcpy(path, upath, MAXPATHLEN);
	if (namei(path, p, &vp) != 0)
		return -ENOENT;
	if (vp->v_type != VDIR)
		return -EINVAL;
	if (vp->v_op->vn_readdir(vp, &cookie, ubuf, len, &done))
		return -EFAULT;
	return done / 64;			/* struct dirent size */
}

static int64_t
sys_sleep(struct proc *p, struct trapframe *tf)
{
	uint64_t ms = tf->rdi;

	tsleep(p, "sleep", ms * HZ / 1000 + 1);
	return 0;
}

static int64_t
sys_reboot(struct proc *p, struct trapframe *tf)
{
	kprintf("rebooting...\n");
	for (;;) {
		outb(0x64, 0xFE);		/* 8042 pulse reset */
		for (volatile int i = 0; i < 1000000; i++)
			;
	}
	return 0;
}

static int64_t
sys_psinfo(struct proc *p, struct trapframe *tf)
{
	struct psinfo_ent *ents = (struct psinfo_ent *)tf->rdi;
	uint64_t nmax = tf->rsi;
	int n = 0;

	if (!USER_PTR_OK(ents, nmax * sizeof(*ents)))
		return -EFAULT;
	for (int i = 0; i < NPROC && (uint64_t)n < nmax; i++) {
		struct proc *c = &proc_table[i];

		if (c->p_state == SUNUSED)
			continue;
		ents[n].pid = c->p_pid;
		ents[n].state = c->p_state;
		strlcpy(ents[n].name, c->p_name, sizeof(ents[n].name));
		n++;
	}
	return n;
}

static int64_t
sys_meminfo(struct proc *p, struct trapframe *tf)
{
	struct meminfo *mi = (struct meminfo *)tf->rdi;

	if (!USER_PTR_OK(mi, sizeof(*mi)))
		return -EFAULT;
	mi->mi_total = pm_total_pages() * PAGESIZE;
	mi->mi_free = pm_free_pages() * PAGESIZE;
	return 0;
}

static int64_t
sys_uptime(struct proc *p, struct trapframe *tf)
{
	return ticks_now() / HZ;
}

static int64_t
sys_chdir(struct proc *p, struct trapframe *tf)
{
	const char *upath = (const char *)tf->rdi;
	char path[MAXPATHLEN];
	struct vnode *vp;

	if (!USER_PTR_OK(upath, 1))
		return -EFAULT;
	strlcpy(path, upath, MAXPATHLEN);
	if (namei(path, p, &vp) != 0)
		return -ENOENT;
	if (vp->v_type != VDIR)
		return -EINVAL;
	if (p->p_cwd)
		vn_unref(p->p_cwd);
	p->p_cwd = vp;
	vn_ref(vp);
	vn_unref(vp);
	return 0;
}

static int64_t
sys_getcwd(struct proc *p, struct trapframe *tf)
{
	char *ubuf = (char *)tf->rdi;
	uint64_t len = tf->rsi;
	char path[MAXPATHLEN];

	if (!USER_PTR_OK(ubuf, len) || len < 2)
		return -EFAULT;
	if (p->p_cwd == NULL || p->p_cwd->v_data == NULL ||
	    vfs_path_of(p->p_cwd, path, sizeof(path)) != 0) {
		ubuf[0] = '/';
		ubuf[1] = '\0';
		return 1;
	}
	strlcpy(ubuf, path, len);
	return strlen(ubuf);
}

int64_t
syscall_dispatch(struct trapframe *tf, struct proc *p)
{
	switch (tf->rax) {
	case SYS_exit:		return sys_exit(p, tf);
	case SYS_fork:		return sys_fork(p, tf);
	case SYS_read:		return sys_read(p, tf);
	case SYS_write:		return sys_write(p, tf);
	case SYS_open:		return sys_open(p, tf);
	case SYS_close:		return sys_close(p, tf);
	case SYS_wait4:		return sys_wait4(p, tf);
	case SYS_exec:		return sys_exec(p, tf);
	case SYS_getpid:	return sys_getpid(p, tf);
	case SYS_sbrk:		return sys_sbrk(p, tf);
	case SYS_uname:		return sys_uname(p, tf);
	case SYS_readdir:	return sys_readdir(p, tf);
	case SYS_sleep:		return sys_sleep(p, tf);
	case SYS_reboot:	return sys_reboot(p, tf);
	case SYS_psinfo:	return sys_psinfo(p, tf);
	case SYS_meminfo:	return sys_meminfo(p, tf);
	case SYS_uptime:	return sys_uptime(p, tf);
	case SYS_chdir:		return sys_chdir(p, tf);
	case SYS_getcwd:	return sys_getcwd(p, tf);
	default:
		kprintf("pid %d: bad syscall %d\n", p ? p->p_pid : 0,
		    (int)tf->rax);
		return -EINVAL;
	}
}

void
syscall(struct trapframe *tf, struct proc *p)
{
	if (p == NULL)
		panic("syscall from kernel context");
	tf->rax = (uint64_t)syscall_dispatch(tf, p);
}
