/*	$e1ruBSD: subr_prf.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Kernel printf and panic.  Output goes to the console (VGA + serial).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <machine/cpu.h>
#include <machine/trap.h>

static char *
fmt_num(char *p, char *end, uint64_t v, int base, int neg, int width,
    char pad)
{
	char tmp[24];
	int i = 0;
	const char *digits = "0123456789abcdef";

	if (v == 0)
		tmp[i++] = '0';
	while (v) {
		tmp[i++] = digits[v % base];
		v /= base;
	}
	if (neg)
		tmp[i++] = '-';
	while (i < width && i < (int)sizeof(tmp))
		tmp[i++] = pad;
	while (i && p < end)
		*p++ = tmp[--i];
	return p;
}

int
kvsnprintf(char *buf, size_t len, const char *fmt, __builtin_va_list ap)
{
	char *p = buf, *end = buf + len - 1;

	while (*fmt && p < end) {
		int width = 0;
		char pad = ' ';

		if (*fmt != '%') {
			*p++ = *fmt++;
			continue;
		}
		fmt++;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		switch (*fmt) {
		case 'l':
			fmt++;
			switch (*fmt) {
			case 'x': {
				uint64_t v = __builtin_va_arg(ap, uint64_t);
				p = fmt_num(p, end, v, 16, 0, width, pad);
				break;
			}
			case 'd': {
				int64_t v = __builtin_va_arg(ap, int64_t);
				p = fmt_num(p, end, v < 0 ? -v : v, 10, v < 0,
				    width, pad);
				break;
			}
			}
			break;
		case 'd': {
			int v = __builtin_va_arg(ap, int);
			p = fmt_num(p, end, v < 0 ? -(uint64_t)v : (uint64_t)v,
			    10, v < 0, width, pad);
			break;
		}
		case 'u': {
			unsigned v = __builtin_va_arg(ap, unsigned);
			p = fmt_num(p, end, v, 10, 0, width, pad);
			break;
		}
		case 'x': {
			unsigned v = __builtin_va_arg(ap, unsigned);
			p = fmt_num(p, end, v, 16, 0, width, pad);
			break;
		}
		case 'c':
			*p++ = (char)__builtin_va_arg(ap, int);
			break;
		case 's': {
			const char *s = __builtin_va_arg(ap, const char *);
			if (s == NULL)
				s = "(null)";
			while (*s && p < end)
				*p++ = *s++;
			break;
		}
		case 'p': {
			uint64_t v = (uint64_t)__builtin_va_arg(ap, void *);
			if (p < end)
				*p++ = '0';
			if (p < end)
				*p++ = 'x';
			p = fmt_num(p, end, v, 16, 0, 8, '0');
			break;
		}
		case '%':
			*p++ = '%';
			break;
		}
		if (*fmt)
			fmt++;
	}
	*p = '\0';
	return (int)(p - buf);
}

void
kprintf(const char *fmt, ...)
{
	char buf[1024];

	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	__builtin_va_end(ap);
	for (char *s = buf; *s; s++)
		cnputc(*s);
}

void
panic(const char *fmt, ...)
{
	char buf[512];

	cli();
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	__builtin_va_end(ap);
	kprintf("\npanic: %s\n-- halted --\n", buf);
	for (;;)
		hlt();
}
