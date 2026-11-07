/*	$e1ruBSD: ulib.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Userland mini libc interface.  Kernel headers shared where harmless:
 * <sys/syscall.h> for numbers and small structures, <sys/types.h> for types.
 */

#ifndef _ULIB_H_
#define _ULIB_H_

#include <sys/types.h>
#include <sys/syscall.h>

/* POSIX-ish dirent for userland (DT_* from syscall.h) */
struct dirent {
	uint64_t	 d_ino;
	uint8_t		 d_type;
	char		 d_name[55];
} __attribute__((packed));

int	 main(int argc, char **argv);

/* sys.c */
void	 exit(int code) __attribute__((noreturn));
long	 read(int fd, void *buf, unsigned long n);
long	 write(int fd, const void *buf, unsigned long n);
int	 open(const char *path, int flags);
int	 close(int fd);
int	 fork(void);
int	 execv(const char *path, char **argv);
int	 wait(int *status);
int	 getpid(void);
void	*brk_alloc(long incr);
int	 readdir(const char *path, void *buf, unsigned long len);
int	 psinfo(struct psinfo_ent *ents, unsigned long nmax);
int	 meminfo(struct meminfo *mi);
unsigned long uptime_secs(void);
void	 sleep_ms(unsigned long ms);
void	 reboot(void) __attribute__((noreturn));
void	 poweroff(void) __attribute__((noreturn));
void	 halt(void) __attribute__((noreturn));
int	 chdir(const char *path);
char	*getcwd(char *buf, size_t len);
int	 uname(struct utsname *u);
int	 mkdir(const char *path);
int	 rmdir(const char *path);
int	 unlink(const char *path);
int	 chmod(const char *path, unsigned mode);
int	 stat(const char *path, struct e1stat *st);
void	 gettime(struct rtc_time *rt);

/* string.c */
size_t	 strlen(const char *s);
int	 strcmp(const char *a, const char *b);
int	 strncmp(const char *a, const char *b, size_t n);
char	*strlcpy(char *dst, const char *src, size_t n);
char	*strchr(const char *s, int c);
void	*memset(void *dst, int c, size_t n);
void	*memcpy(void *dst, const void *src, size_t n);
int	 atoi(const char *s);

/* stdio.c */
int	 printf(const char *fmt, ...);
int	 putchar(int c);
int	 puts(const char *s);
char	*gets(char *buf, int n);

#endif	/* !_ULIB_H_ */
