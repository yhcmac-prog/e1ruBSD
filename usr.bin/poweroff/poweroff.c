/*	$e1ruBSD: poweroff.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("The system is going down now...\n");
	poweroff();
}
