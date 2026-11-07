/*	$e1ruBSD: syscall.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_SYSCALL_H_
#define _SYS_SYSCALL_H_

/* int 0x80 syscall numbers, rdi/rsi/rdx arguments, rax = number, ret rax */
#define	SYS_exit	1
#define	SYS_fork	2
#define	SYS_read	3
#define	SYS_write	4
#define	SYS_open	5
#define	SYS_close	6
#define	SYS_wait4	7
#define	SYS_exec	8
#define	SYS_getpid	9
#define	SYS_sbrk	10
#define	SYS_uname	11
#define	SYS_readdir	12
#define	SYS_sleep	13
#define	SYS_reboot	14
#define	SYS_psinfo	15
#define	SYS_meminfo	16
#define	SYS_uptime	17
#define	SYS_chdir	18
#define	SYS_getcwd	19
#define	SYS_remove	20		/* unlink a file */
#define	SYS_mkdir	21
#define	SYS_rmdir	22
#define	SYS_chmod	23
#define	SYS_stat	24
#define	SYS_gettime	25

#define	NSYSCALL	25

/* SYS_reboot mode argument */
#define	REBOOT_REBOOT	0
#define	REBOOT_POWEROFF	1
#define	REBOOT_HALT	2

/* open(2) flags */
#define	O_RDONLY	0x0000
#define	O_WRONLY	0x0001
#define	O_RDWR		0x0002
#define	O_ACCMODE	0x0003
#define	O_CREAT		0x0100
#define	O_TRUNC		0x0200
#define	O_APPEND	0x0400

/* dirent d_type */
#define	DT_FILE		1
#define	DT_DIR		2

struct utsname {
	char	sysname[32];
	char	nodename[32];
	char	release[32];
	char	version[32];
	char	machine[32];
};

struct psinfo_ent {
	int	pid;
	int	state;
	char	name[16];
};

struct meminfo {
	uint64_t	mi_total;
	uint64_t	mi_free;
};

/* SYS_gettime: wall clock from the CMOS RTC (UTC, no timezone handling). */
struct rtc_time {
	int	year;		/* full year, e.g. 2026 */
	int	mon;		/* 1..12 */
	int	day;		/* 1..31 */
	int	hour;		/* 0..23 */
	int	min;		/* 0..59 */
	int	sec;		/* 0..59 */
	int	wday;		/* 0 = Sunday */
};

/* SYS_stat: minimal metadata for one path. */
struct e1stat {
	uint64_t	st_size;
	uint64_t	st_ino;
	uint32_t	st_mode;	/* permission bits */
	uint32_t	st_type;	/* enum vtype */
};

#endif	/* !_SYS_SYSCALL_H_ */
