/*	$e1ruBSD: ps.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#include "ulib.h"

static const char *state_name(int s)
{
	switch (s) {
	case 1: return "embryo";
	case 2: return "runnable";
	case 3: return "running";
	case 4: return "sleep";
	case 5: return "zombie";
	default: return "?";
	}
}

int
main(int argc, char **argv)
{
	struct psinfo_ent ents[16];
	int n = psinfo(ents, 16);

	(void)argc;
	(void)argv;
	if (n < 0) {
		printf("ps: psinfo failed\n");
		return 1;
	}
	printf("%6s %-9s %s\n", "PID", "STATE", "COMMAND");
	for (int i = 0; i < n; i++)
		printf("%6d %-9s %s\n", ents[i].pid,
		    state_name(ents[i].state), ents[i].name);
	return 0;
}
