/* Wintosh-original i386 Darwin TLV smoke fixture; GPL-3.0-only. */
_Thread_local int value = 40;
_Thread_local int zero_value;

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	value += 2;
	zero_value += value;
	return zero_value;
}
