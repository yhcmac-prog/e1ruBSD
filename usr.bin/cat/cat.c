/*	$e1ruBSD: cat.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

int
main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: cat file\n");
		return 1;
	}
	int fd = open(argv[1], O_RDONLY);

	if (fd < 0) {
		printf("cat: %s: not found\n", argv[1]);
		return 1;
	}
	char buf[4096];
	long n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		write(1, buf, n);
	close(fd);
	return 0;
}
