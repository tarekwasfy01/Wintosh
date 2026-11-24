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

static long darwin_syscall5(long number, long argument0, long argument1,
	long argument2, long argument3, long argument4, unsigned long *error_flag)
{
	register long x0 __asm__("x0") = argument0;
	register long x1 __asm__("x1") = argument1;
	register long x2 __asm__("x2") = argument2;
	register long x3 __asm__("x3") = argument3;
	register long x4 __asm__("x4") = argument4;
	register long x16 __asm__("x16") = number;
	unsigned long nzcv;
	__asm__ volatile("svc #0x80\n\tmrs %0, nzcv"
		: "=r"(nzcv), "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x16)
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
	if (darwin_syscall3(131, descriptor, 2 | 4, 0, 0) != 0 || // LOCK_EX | LOCK_NB
		darwin_syscall3(131, descriptor, 8, 0, 0) != 0) // LOCK_UN
		return 3;
	const long descriptor_flags = darwin_syscall3(92, descriptor, 1, 0, 0); // F_GETFD
	if (descriptor_flags < 0 || descriptor_flags > 1)
		return 4;
	if (darwin_syscall3(92, descriptor, 2, descriptor_flags, 0) != 0) // F_SETFD
		return 5;
	const long duplicate = darwin_syscall3(92, descriptor, 0, 100, 0); // F_DUPFD
	if (duplicate < 100 || darwin_syscall3(6, duplicate, 0, 0, 0) != 0 ||
		darwin_syscall3(6, descriptor, 0, 0, 0) != 0)
		return 6;
	guest_rlimit nofile = {0, 0};
	if (darwin_syscall3(194, 8, (long)&nofile, 0, 0) != 0 ||
		nofile.current == 0 || nofile.current > nofile.maximum ||
		darwin_syscall3(195, 8, (long)&nofile, 0, 0) != 0) // get/setrlimit RLIMIT_NOFILE
		return 7;
	if (darwin_syscall3(89, 0, 0, 0, 0) != 1024 ||
		darwin_syscall3(82, 0, 0, 0, 0) != 0 ||
		darwin_syscall3(151, 0, 0, 0, 0) <= 0 ||
		darwin_syscall3(147, 0, 0, 0, 0) <= 0)
		return 8;
	char login[256] = {0};
	if (darwin_syscall3(49, (long)login, sizeof(login), 0, 0) != 0 ||
		login[0] == '\0' || darwin_syscall3(50, (long)login, 0, 0, 0) != 0)
		return 9;
	unsigned int groups[4] = {0};
	const long group_count = darwin_syscall3(79, 0, 0, 0, 0);
	if (group_count != 1 || darwin_syscall3(79, 4, (long)groups, 0, 0) != 1 ||
		groups[0] != (unsigned int)darwin_syscall3(47, 0, 0, 0, 0) ||
		darwin_syscall3(23, darwin_syscall3(24, 0, 0, 0, 0), 0, 0, 0) != 0 ||
		darwin_syscall3(181, darwin_syscall3(47, 0, 0, 0, 0), 0, 0, 0) != 0 ||
		darwin_syscall3(182, darwin_syscall3(43, 0, 0, 0, 0), 0, 0, 0) != 0 ||
		darwin_syscall3(183, darwin_syscall3(25, 0, 0, 0, 0), 0, 0, 0) != 0)
		return 10;
	unsigned long syscall_error = 0;
	if (darwin_syscall3(194, 8, 0, 0, &syscall_error) != 14 || syscall_error != 1 ||
		darwin_syscall3(49, 0, sizeof(login), 0, &syscall_error) != 14 ||
		syscall_error != 1 ||
		darwin_syscall3(79, 1, 0, 0, &syscall_error) != 14 ||
		syscall_error != 1 ||
		darwin_syscall3(80, 1, 0, 0, &syscall_error) != 14 ||
		syscall_error != 1 ||
		darwin_syscall3(80, 1, (long)groups, 0, &syscall_error) != 1 ||
		syscall_error != 1 ||
		darwin_syscall3(54, 1, 0xdead, 0, &syscall_error) != 25 || syscall_error != 1)
		return 11;
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
		return 11;
	unsigned int available = 0;
	if (darwin_syscall3(54, pipe_read, 0x4004667f, (long)&available,
		&syscall_error) != 0 || syscall_error != 0 ||
		available != sizeof(pipe_payload) - 1)
		return 12;
	char pipe_result[sizeof(pipe_payload)] = {0};
	if (darwin_syscall3(3, pipe_read, (long)pipe_result,
		sizeof(pipe_result) - 1, 0) != sizeof(pipe_result) - 1 ||
		pipe_result[0] != 'p' || pipe_result[1] != 'i' ||
		pipe_result[2] != 'p' || pipe_result[3] != 'e' ||
		darwin_syscall3(6, pipe_read, 0, 0, 0) != 0 ||
		darwin_syscall3(6, pipe_write, 0, 0, 0) != 0)
		return 13;
	unsigned int write_fds[32] = {0};
	write_fds[0] = 1u << 1;
	struct { long seconds; long microseconds; } select_timeout = {0, 0};
	if (darwin_syscall5(93, 2, 0, (long)write_fds, 0,
		(long)&select_timeout, 0) != 1 || write_fds[0] != (1u << 1) ||
		select_timeout.seconds != 0 || select_timeout.microseconds != 0)
		return 14;
	if (darwin_syscall5(93, 1, 0xdead, 0, 0, 0, &syscall_error) != 14 ||
		syscall_error != 1)
		return 15;
	size_t (*volatile guest_strlen)(const char *) = strlen;
	if (guest_strlen(argv[0]) == 0)
		return 16;
	static const char marker[] = "ARM64_SVC_IDENTITY_GROUP_SELECT_DESCRIPTOR_PROCESS_RLIMIT_IOCTL=PASS\n";
	return darwin_syscall3(4, 1, (long)marker, sizeof(marker) - 1, 0) ==
		sizeof(marker) - 1 ? 42 : 17;
}
