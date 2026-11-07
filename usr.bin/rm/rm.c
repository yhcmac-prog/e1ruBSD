/*	$e1ruBSD: rm.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) {
		printf("usage: rm file ...\n");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		if (unlink(argv[i]) != 0) {
			printf("rm: %s: cannot remove\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
