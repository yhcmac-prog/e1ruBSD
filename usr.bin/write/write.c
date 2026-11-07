/*	$e1ruBSD: write.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * write(1): create a file with the given text, one argument per space.
 * Usage: write file text ...
 */

#include "ulib.h"

int
main(int argc, char **argv)
{
	if (argc < 3) {
		printf("usage: write file text ...\n");
		return 1;
	}
	int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC);

	if (fd < 0) {
		printf("write: %s: cannot create\n", argv[1]);
		return 1;
	}
	for (int i = 2; i < argc; i++) {
		write(fd, argv[i], strlen(argv[i]));
		if (i + 1 < argc)
			write(fd, " ", 1);
	}
	write(fd, "\n", 1);
	close(fd);
	return 0;
}
