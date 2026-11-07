/*	$e1ruBSD: mkdir.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

static int
mkdir_p(const char *path)
{
	char buf[256];
	size_t len = strlen(path);
	int rc = 0;

	if (len >= sizeof(buf)) {
		printf("mkdir: %s: path too long\n", path);
		return 1;
	}
	strlcpy(buf, path, sizeof(buf));
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(buf) != 0 && strcmp(buf, "/") != 0) {
			/* may already exist; a later component will tell */
		}
		*p = '/';
	}
	if (mkdir(buf) != 0 && strcmp(buf, "/") != 0) {
		printf("mkdir: %s: cannot create\n", buf);
		rc = 1;
	}
	return rc;
}

int
main(int argc, char **argv)
{
	int pflag = 0, i = 1, rc = 0;

	if (argc > 1 && strcmp(argv[1], "-p") == 0) {
		pflag = 1;
		i = 2;
	}
	if (i >= argc) {
		printf("usage: mkdir [-p] dir ...\n");
		return 1;
	}
	for (; i < argc; i++) {
		if (pflag) {
			rc |= mkdir_p(argv[i]);
			continue;
		}
		if (mkdir(argv[i]) != 0) {
			printf("mkdir: %s: cannot create\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
