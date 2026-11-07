/*	$e1ruBSD: shutdown.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	int mode = 0;				/* 0 poweroff, 1 reboot, 2 halt */

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0) {
			mode = 1;
		} else if (strcmp(argv[i], "-h") == 0) {
			mode = 2;
		} else if (strcmp(argv[i], "-p") == 0) {
			mode = 0;
		} else if (argv[i][0] == '-') {
			printf("usage: shutdown [-r|-h|-p] [time]\n");
			return 1;
		}
		/* a time argument is accepted and ignored: there is no
		 * wall-clock scheduler in this system */
	}
	if (mode == 1) {
		printf("The system is going to reboot now...\n");
		reboot();
	} else if (mode == 2) {
		printf("The system is going to halt now...\n");
		halt();
	}
	printf("The system is going down now...\n");
	poweroff();
}
