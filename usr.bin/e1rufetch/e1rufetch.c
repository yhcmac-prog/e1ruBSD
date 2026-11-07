/*	$e1ruBSD: e1rufetch.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * e1rufetch(1): system information card, e1 family style.
 */

#include "ulib.h"

static const char *logo[] = {
	"  _____  ____  _   _  ____  ____  _   _ ",
	" | ____||  _ \\| | | ||  _ \\|  _ \\| | | |",
	" | |__  | |_) | | | || |_) | |_) | | | |",
	" |  __| |  _ <| |_| ||  _ <|  _ <| | | |",
	" | |___| |_) |  _  || |_) | |_) | |_| |",
	" |_____|____/|_| |_||____/|____/ \\___/ ",
	NULL
};

int
main(int argc, char **argv)
{
	struct utsname un;
	struct meminfo mi;

	(void)argc;
	(void)argv;
	uname(&un);
	meminfo(&mi);

	int i = 0;
	const char **l = logo;

	for (; *l; l++, i++) {
		printf("%s  ", *l);
		switch (i) {
		case 0:
			printf("%s %s (RUNE)", un.sysname, un.release);
			break;
		case 1:
			printf("arch: %s", un.machine);
			break;
		case 2:
			printf("uptime: %lu s", uptime_secs());
			break;
		case 3:
			printf("memory: %luM total, %luM free",
			    mi.mi_total / (1024 * 1024),
			    mi.mi_free / (1024 * 1024));
			break;
		case 4:
			printf("shell: rsh");
			break;
		case 5:
			printf("e1 family -- POSIX-flavored BSD");
			break;
		}
		printf("\n");
	}
	return 0;
}
