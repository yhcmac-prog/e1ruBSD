/*	$e1ruBSD: start.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * C runtime entry: pull argc/argv off the initial stack and run main().
 */

#include "ulib.h"

int
__libc_start(unsigned long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);

	return main(argc, argv);
}
