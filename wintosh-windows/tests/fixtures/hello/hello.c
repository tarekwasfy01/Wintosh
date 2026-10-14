/*
 * Wintosh replacement fixture. This is original test code and is not an
 * Apple SDK header or Apple binary.
 *
 * Copyright (C) 2026 Wintosh contributors
 * Licensed under the GNU General Public License, version 3.
 */

/* The source deliberately uses only the first supported Darwin surface. */
extern int puts(const char *);
extern void _exit(int);

int main(int argc, char **argv, char **envp)
{
	(void)argv;
	(void)envp;
	if (argc != 1)
		_exit(7);
	if (puts("hello from guest") < 0)
		_exit(8);
	return 0;
}
