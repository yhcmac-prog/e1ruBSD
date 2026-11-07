/*	$e1ruBSD: chmod.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

static int
parse_mode(const char *s, unsigned *modep)
{
	unsigned v = 0;

	if (*s == '\0')
		return -1;
	for (; *s; s++) {
		if (*s < '0' || *s > '7')
			return -1;
		v = (v << 3) | (unsigned)(*s - '0');
	}
	*modep = v & 07777;
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned mode;
	int rc = 0;

	if (argc < 3) {
		printf("usage: chmod mode file ...\n");
		return 1;
	}
	if (parse_mode(argv[1], &mode) != 0) {
		printf("chmod: %s: invalid octal mode\n", argv[1]);
		return 1;
	}
	for (int i = 2; i < argc; i++) {
		if (chmod(argv[i], mode) != 0) {
			printf("chmod: %s: not found\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
