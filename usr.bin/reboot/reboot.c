/*	$e1ruBSD: reboot.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	reboot();
}
