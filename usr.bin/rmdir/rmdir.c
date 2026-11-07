/*	$e1ruBSD: rmdir.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	int rc = 0;

	if (argc < 2) {
		printf("usage: rmdir dir ...\n");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		if (rmdir(argv[i]) != 0) {
			printf("rmdir: %s: not empty or not a directory\n",
			    argv[i]);
			rc = 1;
		}
	}
	return rc;
}
