/*	$e1ruBSD: stdio.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

static char *
fmt_num(char *p, char *end, unsigned long v, int base, int neg, int width,
    char pad, int left)
{
	char tmp[24];
	int i = 0, n;
	const char *digits = "0123456789abcdef";

	if (v == 0)
		tmp[i++] = '0';
	while (v) {
		tmp[i++] = digits[v % base];
		v /= base;
	}
	if (neg)
		tmp[i++] = '-';
	n = i;
	if (left) {
		while (i && p < end)
			*p++ = tmp[--i];
		while (n < width-- && p < end)
			*p++ = ' ';
		return p;
	}
	while (i < width && i < (int)sizeof(tmp))
		tmp[i++] = pad;
	while (i && p < end)
		*p++ = tmp[--i];
	return p;
}

static char *
fmt_pad(char *p, char *end, int count, char c)
{
	while (count-- > 0 && p < end)
		*p++ = c;
	return p;
}

static int
vsnprintf(char *buf, size_t len, const char *fmt, __builtin_va_list ap)
{
	char *p = buf, *end = buf + len - 1;

	while (*fmt && p < end) {
		int width = 0, left = 0;
		char pad = ' ';

		if (*fmt != '%') {
			*p++ = *fmt++;
			continue;
		}
		fmt++;
		/* flags: '-' left-justify, '0' zero-pad */
		for (;;) {
			if (*fmt == '-') {
				left = 1;
				fmt++;
			} else if (*fmt == '0') {
				pad = '0';
				fmt++;
			} else {
				break;
			}
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		switch (*fmt) {
		case 'd': {
			int v = __builtin_va_arg(ap, int);
			p = fmt_num(p, end, v < 0 ? -(unsigned long)v :
			    (unsigned long)v, 10, v < 0, width, pad, left);
			break;
		}
		case 'u': {
			unsigned v = __builtin_va_arg(ap, unsigned);
			p = fmt_num(p, end, v, 10, 0, width, pad, left);
			break;
		}
		case 'x': {
			unsigned v = __builtin_va_arg(ap, unsigned);
			p = fmt_num(p, end, v, 16, 0, width, pad, left);
			break;
		}
		case 'l':
			fmt++;
			switch (*fmt) {
			case 'd': {
				long v = __builtin_va_arg(ap, long);
				p = fmt_num(p, end, v < 0 ? -(unsigned long)v :
				    (unsigned long)v, 10, v < 0, width, pad, left);
				break;
			}
			case 'u': {
				unsigned long v =
				    __builtin_va_arg(ap, unsigned long);
				p = fmt_num(p, end, v, 10, 0, width, pad, left);
				break;
			}
			}
			break;
		case 'c':
			*p++ = (char)__builtin_va_arg(ap, int);
			break;
		case 's': {
			const char *s = __builtin_va_arg(ap, const char *);
			int n;

			if (s == NULL)
				s = "(null)";
			n = (int)strlen(s);
			if (!left)
				p = fmt_pad(p, end, width - n, ' ');
			while (*s && p < end)
				*p++ = *s++;
			if (left)
				p = fmt_pad(p, end, width - n, ' ');
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

int
printf(const char *fmt, ...)
{
	char buf[1024];

	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	__builtin_va_end(ap);
	write(1, buf, n);
	return n;
}

int
putchar(int c)
{
	write(1, &c, 1);
	return c;
}

int
puts(const char *s)
{
	size_t n = strlen(s);

	write(1, s, n);
	write(1, "\n", 1);
	return n + 1;
}

char *
gets(char *buf, int n)
{
	long got = read(0, buf, n - 1);

	if (got <= 0)
		return NULL;
	buf[got] = '\0';
	return buf;
}
