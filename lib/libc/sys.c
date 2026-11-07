/*	$e1ruBSD: sys.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

static long
syscall4(long n, long a, long b, long c)
{
	long ret;

	__asm__ volatile("int $0x80"
	    : "=a"(ret)
	    : "a"(n), "D"(a), "S"(b), "d"(c)
	    : "memory");
	return ret;
}

void
exit(int code)
{
	syscall4(SYS_exit, code, 0, 0);
	for (;;)
		;
}

long
read(int fd, void *buf, unsigned long n)
{
	return syscall4(SYS_read, fd, (long)buf, (long)n);
}

long
write(int fd, const void *buf, unsigned long n)
{
	return syscall4(SYS_write, fd, (long)buf, (long)n);
}

int
open(const char *path, int flags)
{
	return (int)syscall4(SYS_open, (long)path, flags, 0);
}

int
close(int fd)
{
	return (int)syscall4(SYS_close, fd, 0, 0);
}

int
fork(void)
{
	return (int)syscall4(SYS_fork, 0, 0, 0);
}

int
execv(const char *path, char **argv)
{
	return (int)syscall4(SYS_exec, (long)path, (long)argv, 0);
}

int
wait(int *status)
{
	return (int)syscall4(SYS_wait4, -1, (long)status, 0);
}

int
getpid(void)
{
	return (int)syscall4(SYS_getpid, 0, 0, 0);
}

void *
brk_alloc(long incr)
{
	return (void *)syscall4(SYS_sbrk, incr, 0, 0);
}

int
readdir(const char *path, void *buf, unsigned long len)
{
	return (int)syscall4(SYS_readdir, (long)path, (long)buf, (long)len);
}

int
psinfo(struct psinfo_ent *ents, unsigned long nmax)
{
	return (int)syscall4(SYS_psinfo, (long)ents, (long)nmax, 0);
}

int
meminfo(struct meminfo *mi)
{
	return (int)syscall4(SYS_meminfo, (long)mi, 0, 0);
}

unsigned long
uptime_secs(void)
{
	return (unsigned long)syscall4(SYS_uptime, 0, 0, 0);
}

void
sleep_ms(unsigned long ms)
{
	syscall4(SYS_sleep, (long)ms, 0, 0);
}

void
reboot(void)
{
	syscall4(SYS_reboot, 0, 0, 0);
	for (;;)
		;
}

int
chdir(const char *path)
{
	return (int)syscall4(SYS_chdir, (long)path, 0, 0);
}

char *
getcwd(char *buf, size_t len)
{
	long n = syscall4(SYS_getcwd, (long)buf, (long)len, 0);

	return n < 0 ? NULL : buf;
}

int
uname(struct utsname *u)
{
	return (int)syscall4(SYS_uname, (long)u, 0, 0);
}
