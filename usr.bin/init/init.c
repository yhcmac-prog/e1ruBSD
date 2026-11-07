#include "ulib.h"

/*
 * init(8): first user-mode process.  Announce that the kernel reached
 * user mode, then hand the console to the shell.  If the exec fails we
 * stay alive and keep reporting instead of silently spinning.
 */
int
main(int argc, char **argv)
{
	static char *shell_argv[] = { "rsh", NULL };

	(void)argc;
	(void)argv;

	printf("\ninit: e1ruBSD user mode is running\n");
	execv("/bin/rsh", shell_argv);

	printf("init: exec /bin/rsh failed\n");
	for (;;)
		sleep_ms(1000);
}
