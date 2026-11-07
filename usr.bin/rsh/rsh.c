/*	$e1ruBSD: rsh.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * rsh(1): the e1ruBSD shell.  Builtins: help, clear, cd, pwd, exit.
 * Everything else is exec'd from /bin.
 */

#include "ulib.h"

#define	PROMPT	"e1ru$ "

static void
help(void)
{
	printf("builtins: help clear cd pwd exit\n");
	printf("commands:\n");
	printf("  ls cat echo write ps uptime uname clear reboot e1rufetch\n");
	printf("  mkdir rmdir rm touch chmod stat date\n");
	printf("  halt poweroff shutdown pkg\n");
	printf("try: pkg available; pkg install hello; hello\n");
}

static char *
skip_ws(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

int
main(int argc, char **argv)
{
	char line[256];

	(void)argc;
	(void)argv;

	for (;;) {
		printf("%s", PROMPT);
		if (gets(line, sizeof(line)) == NULL)
			continue;

		char *cmd = skip_ws(line);
		if (*cmd == '\0')
			continue;

		/* tokenize in place */
		char *args[16];
		int nargs = 0;
		char *p = cmd;

		while (*p && nargs < 15) {
			args[nargs++] = p;
			while (*p && *p != ' ' && *p != '\t')
				p++;
			if (*p) {
				*p++ = '\0';
				p = skip_ws(p);
			}
		}
		args[nargs] = NULL;
		if (nargs == 0)
			continue;

		if (strcmp(args[0], "help") == 0) {
			help();
			continue;
		}
		if (strcmp(args[0], "clear") == 0) {
			write(1, "\f", 1);
			continue;
		}
		if (strcmp(args[0], "exit") == 0)
			return 0;
		if (strcmp(args[0], "pwd") == 0) {
			char cwd[256];

			if (getcwd(cwd, sizeof(cwd)) != NULL)
				printf("%s\n", cwd);
			continue;
		}
		if (strcmp(args[0], "cd") == 0) {
			const char *dir = nargs > 1 ? args[1] : "/";
			if (chdir(dir) != 0)
				printf("cd: %s: not found\n", dir);
			continue;
		}

		/* external command from /bin */
		char path[128];
		strlcpy(path, "/bin/", sizeof(path));
		strlcpy(path + 5, args[0], sizeof(path) - 5);

		int pid = fork();
		if (pid == 0) {
			execv(path, args);
			execv(args[0], args);	/* allow absolute paths */
			printf("rsh: %s: command not found\n", args[0]);
			exit(127);
		}
		int status = 0;
		wait(&status);
		/* report traps the way a shell does: 128 + signal number */
		if (status != 0)
			printf("rsh: %s: exit status %d\n", args[0], status);
	}
}
