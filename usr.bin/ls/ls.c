/*	$e1ruBSD: ls.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : ".";
	static char buf[16384];
	int n = readdir(path, buf, sizeof(buf));

	if (n <= 0) {
		printf("ls: %s: not found\n", path);
		return 1;
	}
	struct dirent *de = (struct dirent *)buf;

	for (int i = 0; i < n; i++) {
		if (de[i].d_name[0] == '\0')
			break;
		printf("%s%s\n", de[i].d_name,
		    de[i].d_type == DT_DIR ? "/" : "");
	}
	return 0;
}
