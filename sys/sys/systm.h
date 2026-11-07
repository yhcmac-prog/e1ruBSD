/*	$e1ruBSD: systm.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_SYSTM_H_
#define _SYS_SYSTM_H_

#include <sys/types.h>
#include <sys/param.h>

struct trapframe;
struct proc;
struct rtc_time;

/* subr_prf.c */
void	 kprintf(const char *fmt, ...);
void	 kprintf_nolog(const char *fmt, ...);
void	 panic(const char *fmt, ...);
int	 kvsnprintf(char *buf, size_t len, const char *fmt,
	    __builtin_va_list ap);

/* libkern */
void	*memcpy(void *dst, const void *src, size_t n);
void	*memset(void *dst, int c, size_t n);
int	 memcmp(const void *a, const void *b, size_t n);
size_t	 strlen(const char *s);
int	 strcmp(const char *a, const char *b);
int	 strncmp(const char *a, const char *b, size_t n);
char	*strncpy(char *dst, const char *src, size_t n);
size_t	 strlcpy(char *dst, const char *src, size_t n);
char	*strchr(const char *s, int c);

/* dev/acpi.c */
int	 acpi_init(void);
int	 acpi_poweroff(void);
int	 acpi_reboot(void);

/* dev/rtc.c */
void	 rtc_read(struct rtc_time *);

/* init_main.c */
void	 x86_boot(void);

/* curproc lives in sched.c */
extern struct proc	*curproc;

#endif	/* !_SYS_SYSTM_H_ */
