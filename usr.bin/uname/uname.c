/*	$e1ruBSD: uname.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	struct utsname un;
	int kflag = 0;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 'r')
			kflag = 1;
	}
	if (uname(&un) != 0) {
		printf("uname: failed\n");
		return 1;
	}
	if (kflag)
		printf("%s\n", un.release);
	else
		printf("%s %s %s\n", un.sysname, un.release, un.machine);
	return 0;
}
