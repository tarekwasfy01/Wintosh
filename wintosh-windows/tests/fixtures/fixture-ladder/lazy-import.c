/* Original Wintosh fixture; GPL-3.0-only. */
extern int puts(const char *value);
extern void exit(int status);

int main(void)
{
	if (puts("compiler-linked Mach-O import") < 0)
		exit(17);
	exit(42);
}
