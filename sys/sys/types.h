/*	$e1ruBSD: types.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Fundamental types, freestanding (no host libc in kernel or userland).
 */

#ifndef _SYS_TYPES_H_
#define _SYS_TYPES_H_

typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;
typedef unsigned long	u_long;

typedef signed char	int8_t;
typedef short		int16_t;
typedef int		int32_t;
typedef long		int64_t;

typedef unsigned char	uint8_t;
typedef unsigned short	uint16_t;
typedef unsigned int	uint32_t;
typedef unsigned long	uint64_t;

typedef unsigned long	size_t;
typedef long		ssize_t;
typedef long		intptr_t;
typedef unsigned long	uintptr_t;
typedef long		ptrdiff_t;

typedef int		pid_t;
typedef unsigned int	mode_t;
typedef unsigned long	ino_t;
typedef long		off_t;
typedef unsigned int	useconds_t;

#define NULL		((void *)0)
#define	offsetof(s, m)	__builtin_offsetof(s, m)

/* compiler attributes, NetBSD style */
#ifndef __packed
#define	__packed	__attribute__((__packed__))
#endif
#ifndef __aligned
#define	__aligned(x)	__attribute__((__aligned__(x)))
#endif
#ifndef __dead
#define	__dead		__attribute__((__noreturn__))
#endif
#ifndef __unused
#define	__unused	__attribute__((__unused__))
#endif

typedef int	bool;
#define	true	1
#define	false	0

#endif	/* !_SYS_TYPES_H_ */
