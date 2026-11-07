/*	$e1ruBSD: stat.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

static const char *
type_name(uint32_t type)
{
	switch (type) {
	case 1:
		return "regular";
	case 2:
		return "directory";
	case 4:
		return "character";
	default:
		return "unknown";
	}
}

static void
octal(char *out, unsigned v)
{
	char tmp[12];
	int n = 0, i;

	tmp[n++] = '0';
	if (v != 0) {
		n = 0;
		while (v != 0) {
			tmp[n++] = (char)('0' + (v & 7));
			v >>= 3;
		}
	}
	for (i = 0; i < n; i++)
		out[i] = tmp[n - 1 - i];
	out[n] = '\0';
}

int
main(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) {
		printf("usage: stat file ...\n");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		struct e1stat st;
		char mode[12];

		if (stat(argv[i], &st) != 0) {
			printf("stat: %s: not found\n", argv[i]);
			rc = 1;
			continue;
		}
		octal(mode, st.st_mode);
		printf("%s: %s, %lu bytes, ino %lu, mode %s\n",
		    argv[i], type_name(st.st_type),
		    (unsigned long)st.st_size, (unsigned long)st.st_ino,
		    mode);
	}
	return rc;
}
