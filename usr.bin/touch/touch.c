/*	$e1ruBSD: touch.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) {
		printf("usage: touch file ...\n");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], O_CREAT | O_WRONLY);

		if (fd < 0) {
			printf("touch: %s: cannot create\n", argv[i]);
			rc = 1;
			continue;
		}
		close(fd);
	}
	return rc;
}
