/*	$e1ruBSD: uptime.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	unsigned long s = uptime_secs();

	(void)argc;
	(void)argv;
	printf("up %lu seconds, %lu:%02lu\n", s, s / 60, s % 60);
	return 0;
}
