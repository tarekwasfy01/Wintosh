/* Original Wintosh fixture; GPL-3.0-only. */
typedef unsigned long size_t;

extern size_t strlen(const char *value);

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	if (argc < 1 || argv == 0 || argv[0] == 0)
		return 1;
	size_t (*volatile guest_strlen)(const char *) = strlen;
	return guest_strlen(argv[0]) != 0 ? 42 : 1;
}
