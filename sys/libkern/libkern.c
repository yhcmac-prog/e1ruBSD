/*	$e1ruBSD: libkern.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * All routines here are written with volatile pointers to prevent
 * clang's auto-vectorizer from emitting SSE/AVX instructions that the
 * kernel has not enabled via CR4.OSFXSR yet (fpu_init runs much later).
 */

#include <sys/types.h>
#include <sys/systm.h>

void *
memcpy(void *dst, const void *src, size_t n)
{
	volatile uint8_t *d = (volatile uint8_t *)dst;
	const volatile uint8_t *s = (const volatile uint8_t *)src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *
memset(void *dst, int c, size_t n)
{
	volatile uint8_t *d = (volatile uint8_t *)dst;

	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

int
memcmp(const void *a, const void *b, size_t n)
{
	const volatile uint8_t *x = (const volatile uint8_t *)a;
	const volatile uint8_t *y = (const volatile uint8_t *)b;

	while (n--) {
		if (*x != *y)
			return *x - *y;
		x++;
		y++;
	}
	return 0;
}

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
strncpy(char *dst, const char *src, size_t n)
{
	size_t i = 0;

	for (; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}

size_t
strlcpy(char *dst, const char *src, size_t n)
{
	size_t len = strlen(src);

	if (n) {
		size_t copy = len < n - 1 ? len : n - 1;
		memcpy(dst, src, copy);
		dst[copy] = '\0';
	}
	return len;
}

char *
strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return c == 0 ? (char *)s : NULL;
}
