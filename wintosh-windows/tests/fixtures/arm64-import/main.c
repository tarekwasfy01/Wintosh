/* Original Wintosh fixture; GPL-3.0-only. */
typedef unsigned long size_t;

extern size_t strlen(const char *value);

typedef struct {
	unsigned long current;
	unsigned long maximum;
} guest_rlimit;

static long darwin_syscall3(long number, long argument0, long argument1,
	long argument2, unsigned long *error_flag)
{
	register long x0 __asm__("x0") = argument0;
	register long x1 __asm__("x1") = argument1;
	register long x2 __asm__("x2") = argument2;
	register long x16 __asm__("x16") = number;
	unsigned long nzcv;
	__asm__ volatile("svc #0x80\n\tmrs %0, nzcv"
		: "=r"(nzcv), "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x16)
		:
		: "memory", "cc");
	if (error_flag != 0)
		*error_flag = (nzcv >> 29) & 1;
	return x0;
}

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	if (argc < 1 || argv == 0 || argv[0] == 0)
		return 1;
	const long descriptor = darwin_syscall3(5, (long)argv[0], 0, 0, 0); // open O_RDONLY
	if (descriptor < 3)
		return 2;
	const long descriptor_flags = darwin_syscall3(92, descriptor, 1, 0, 0); // F_GETFD
	if (descriptor_flags < 0 || descriptor_flags > 1)
		return 3;
	if (darwin_syscall3(92, descriptor, 2, descriptor_flags, 0) != 0) // F_SETFD
		return 4;
	const long duplicate = darwin_syscall3(92, descriptor, 0, 100, 0); // F_DUPFD
	if (duplicate < 100 || darwin_syscall3(6, duplicate, 0, 0, 0) != 0 ||
		darwin_syscall3(6, descriptor, 0, 0, 0) != 0)
		return 5;
	guest_rlimit nofile = {0, 0};
	if (darwin_syscall3(194, 8, (long)&nofile, 0, 0) != 0 ||
		nofile.current == 0 || nofile.current > nofile.maximum ||
		darwin_syscall3(195, 8, (long)&nofile, 0, 0) != 0) // get/setrlimit RLIMIT_NOFILE
		return 6;
	unsigned long syscall_error = 0;
	if (darwin_syscall3(194, 8, 0, 0, &syscall_error) != 14 || syscall_error != 1 ||
		darwin_syscall3(54, 1, 0xdead, 0, &syscall_error) != 25 || syscall_error != 1)
		return 7;
	register long pipe_read __asm__("x0") = 0;
	register long pipe_write __asm__("x1") = 0;
	register long pipe_call __asm__("x16") = 42;
	__asm__ volatile("svc #0x80"
		: "+r"(pipe_read), "+r"(pipe_write), "+r"(pipe_call)
		:
		: "memory", "cc");
	static const char pipe_payload[] = "pipe";
	if (pipe_read < 0 || pipe_write < 0 ||
		darwin_syscall3(4, pipe_write, (long)pipe_payload,
			sizeof(pipe_payload) - 1, 0) != sizeof(pipe_payload) - 1)
		return 8;
	unsigned int available = 0;
	if (darwin_syscall3(54, pipe_read, 0x4004667f, (long)&available,
		&syscall_error) != 0 || syscall_error != 0 ||
		available != sizeof(pipe_payload) - 1)
		return 9;
	char pipe_result[sizeof(pipe_payload)] = {0};
	if (darwin_syscall3(3, pipe_read, (long)pipe_result,
		sizeof(pipe_result) - 1, 0) != sizeof(pipe_result) - 1 ||
		pipe_result[0] != 'p' || pipe_result[1] != 'i' ||
		pipe_result[2] != 'p' || pipe_result[3] != 'e' ||
		darwin_syscall3(6, pipe_read, 0, 0, 0) != 0 ||
		darwin_syscall3(6, pipe_write, 0, 0, 0) != 0)
		return 10;
	size_t (*volatile guest_strlen)(const char *) = strlen;
	if (guest_strlen(argv[0]) == 0)
		return 11;
	static const char marker[] = "ARM64_SVC_DESCRIPTOR_RLIMIT_IOCTL=PASS\n";
	return darwin_syscall3(4, 1, (long)marker, sizeof(marker) - 1, 0) ==
		sizeof(marker) - 1 ? 42 : 12;
}
