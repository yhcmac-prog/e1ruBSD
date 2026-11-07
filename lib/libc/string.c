/*	$e1ruBSD: string.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

size_t
strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

int
strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int
strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *
strlcpy(char *dst, const char *src, size_t n)
{
	size_t len = strlen(src);
	size_t copy = len < n - 1 ? len : n - 1;

	memcpy(dst, src, copy);
	dst[copy] = '\0';
	return dst;
}

char *
strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return c == 0 ? (char *)s : NULL;
}

void *
memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;

	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

void *
memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

int
atoi(const char *s)
{
	int v = 0, neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}
