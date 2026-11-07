/*	$e1ruBSD: hello.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

/* Demo program shipped inside packages/hello.pkg, not in the base image. */

#include "ulib.h"

int
main(int argc, char **argv)
{
	printf("hello from e1pkg");
	if (argc > 1) {
		printf(" (");
		for (int i = 1; i < argc; i++)
			printf("%s%s", argv[i], i + 1 < argc ? " " : "");
		printf(")");
	}
	printf("!\n");
	return 0;
}
