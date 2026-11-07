/*	$e1ruBSD: echo.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		printf("%s%s", argv[i], i + 1 < argc ? " " : "");
	printf("\n");
	return 0;
}
