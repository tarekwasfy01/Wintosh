/*
 * Stage 3 automatic libc host-symbol boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#define DARLING_WINDOWS_PTHREAD_COMPAT_MACROS 1
#include "darling_windows_stdio.h"

#include <windows.h>
#include <ws2tcpip.h>

#include <csignal>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>

namespace {
	volatile std::sig_atomic_t raised_signal = 0;

	void* PthreadSmokeStart(void* argument)
	{
		return static_cast<char*>(argument) + 5;
	}
	std::atomic_bool cancellation_worker_started{false};
	std::atomic<int> cancellation_cleanup_calls{0};
	void CancellationCleanup(void*)
	{
		cancellation_cleanup_calls.fetch_add(1, std::memory_order_relaxed);
	}
	void* PthreadCancellationStart(void*)
	{
		cancellation_worker_started.store(true, std::memory_order_release);
		pthread_cleanup_push(&CancellationCleanup, nullptr);
		int old_state = -1;
		int old_type = -1;
		if (darling_windows_pthread_setcancelstate(1, &old_state) != 0 ||
			darling_windows_pthread_setcanceltype(0, &old_type) != 0)
			return nullptr;
		for (int attempt = 0; attempt < 500; ++attempt) {
			Sleep(1);
			if (darling_windows_pthread_setcancelstate(0, &old_state) != 0)
				return nullptr;
			darling_windows_pthread_testcancel();
			if (darling_windows_pthread_setcancelstate(1, &old_state) != 0)
				return nullptr;
		}
		pthread_cleanup_pop(0);
		return nullptr;
	}
	struct CancellationLockContext final {
		void* mutex;
		std::atomic_bool* started;
	};
	void* PthreadCancellationLockStart(void* argument)
	{
		auto& context = *static_cast<CancellationLockContext*>(argument);
		context.started->store(true, std::memory_order_release);
		darling_windows_pthread_mutex_lock(context.mutex);
		return nullptr;
	}
	struct CancellationConditionContext final {
		void* condition;
		void* mutex;
		std::atomic_bool* started;
	};
	void* PthreadCancellationConditionStart(void* argument)
	{
		auto& context = *static_cast<CancellationConditionContext*>(argument);
		if (darling_windows_pthread_mutex_lock(context.mutex) != 0)
			return nullptr;
		context.started->store(true, std::memory_order_release);
		darling_windows_pthread_cond_wait(context.condition, context.mutex);
		darling_windows_pthread_mutex_unlock(context.mutex);
		return nullptr;
	}
	volatile long pthread_attr_detached_calls = 0;
	void* PthreadAttrDetachedStart(void*)
	{
		++pthread_attr_detached_calls;
		return nullptr;
	}

	struct CondSmokeContext final {
		void* condition;
		void* mutex;
		volatile long* ready;
	};

	void* PthreadConditionSmokeStart(void* argument)
	{
		auto& context = *static_cast<CondSmokeContext*>(argument);
		darling_windows_pthread_mutex_lock(context.mutex);
		*context.ready = 1;
		darling_windows_pthread_cond_signal(context.condition);
		darling_windows_pthread_mutex_unlock(context.mutex);
		return nullptr;
	}

	volatile long tls_destructor_calls = 0;
	void PthreadTlsDestructor(void*)
	{
		++tls_destructor_calls;
	}
	struct TlsThreadContext final {
		std::uint64_t key;
		const void* value;
	};
	void* PthreadTlsThreadStart(void* argument)
	{
		auto& context = *static_cast<TlsThreadContext*>(argument);
		darling_windows_pthread_setspecific(context.key, context.value);
		return nullptr;
	}
	volatile long pthread_once_calls = 0;
	void PthreadOnceInitializer()
	{
		++pthread_once_calls;
	}

	void CaptureSignal(int signal_number)
	{
		raised_signal = signal_number;
	}
}

int main()
{
	const bool normalized_libc_symbols =
		darling_windows_host_symbol("malloc") != 0 &&
		darling_windows_host_symbol("calloc") != 0 &&
		darling_windows_host_symbol("free") != 0 &&
		darling_windows_host_symbol("memcpy") != 0 &&
		darling_windows_host_symbol("memset") != 0 &&
		darling_windows_host_symbol("memmove") != 0 &&
		darling_windows_host_symbol("memcmp") != 0 &&
		darling_windows_host_symbol("bzero") != 0 &&
		darling_windows_host_symbol("bcopy") != 0 &&
		darling_windows_host_symbol("asprintf") != 0 &&
		darling_windows_host_symbol("memchr") != 0 &&
		darling_windows_host_symbol("memmem") != 0;
	std::cout << "DARWIN_NORMALIZED_LIBC_SYMBOLS="
		<< (normalized_libc_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_string_symbols =
		darling_windows_host_symbol("strlen") != 0 &&
		darling_windows_host_symbol("strnlen") != 0 &&
		darling_windows_host_symbol("strcmp") != 0 &&
		darling_windows_host_symbol("strcasecmp") != 0 &&
		darling_windows_host_symbol("strcpy") != 0 &&
		darling_windows_host_symbol("strncpy") != 0 &&
		darling_windows_host_symbol("strcat") != 0 &&
		darling_windows_host_symbol("strncat") != 0 &&
		darling_windows_host_symbol("strchr") != 0 &&
		darling_windows_host_symbol("strrchr") != 0 &&
		darling_windows_host_symbol("strstr") != 0;
	const bool normalized_string_extra_symbols =
		darling_windows_host_symbol("strspn") != 0 &&
		darling_windows_host_symbol("strcspn") != 0 &&
		darling_windows_host_symbol("strpbrk") != 0 &&
		darling_windows_host_symbol("strtok") != 0 &&
		darling_windows_host_symbol("strtok_r") != 0 &&
		darling_windows_host_symbol("strdup") != 0 &&
		darling_windows_host_symbol("strerror") != 0 &&
		darling_windows_host_symbol("strerror_r") != 0 &&
		darling_windows_host_symbol("snprintf") != 0 &&
		darling_windows_host_symbol("vsnprintf") != 0 &&
		darling_windows_host_symbol("vasprintf") != 0 &&
		darling_windows_host_symbol("strlcpy") != 0 &&
		darling_windows_host_symbol("strlcat") != 0 &&
		darling_windows_host_symbol("strcasestr") != 0;
	std::cout << "DARWIN_NORMALIZED_STRING_SYMBOLS="
		<< (normalized_string_symbols && normalized_string_extra_symbols ? "PASS" : "FAIL") << "\n";
	darling_timespec first{};
	darling_timespec second{};
	SetEnvironmentVariableA("DARLING_HOST_API_TEST", "present");
	const char* environment = darling_windows_getenv("DARLING_HOST_API_TEST");
	const std::string environment_value = environment == nullptr ? "" : environment;
	int* errno_address = darling_windows_errno();
	const bool errno_ok = errno_address != nullptr &&
		darling_windows_host_symbol("_errno") != 0 &&
		darling_windows_host_symbol("___error") != 0;
	if (errno_address != nullptr)
		*errno_address = 61;
	const bool errno_roundtrip_ok = errno_ok && *darling_windows_errno() == 61;
	const bool environment_mutation_ok = darling_windows_setenv("DARLING_HOST_API_MUTATION", "first", 1) == 0 &&
		darling_windows_setenv("DARLING_HOST_API_MUTATION", "second", 0) == 0 &&
		std::string(darling_windows_getenv("DARLING_HOST_API_MUTATION")) == "first" &&
		darling_windows_setenv("DARLING_HOST_API_MUTATION", "second", 1) == 0 &&
		std::string(darling_windows_getenv("DARLING_HOST_API_MUTATION")) == "second" &&
		darling_windows_unsetenv("DARLING_HOST_API_MUTATION") == 0 &&
		darling_windows_getenv("DARLING_HOST_API_MUTATION") == nullptr;
	char putenv_assignment[] = "DARLING_HOST_API_PUTENV=present";
	const bool putenv_ok = darling_windows_host_symbol("_putenv") != 0 &&
		darling_windows_putenv(putenv_assignment) == 0 &&
		std::string(darling_windows_getenv("DARLING_HOST_API_PUTENV")) == "present" &&
		darling_windows_unsetenv("DARLING_HOST_API_PUTENV") == 0;
	bool environ_ok = darling_windows_host_symbol("_environ") != 0 &&
		darling_windows_environ != nullptr;
	if (environ_ok) {
		bool found = false;
		for (char** entry = darling_windows_environ; *entry != nullptr; ++entry) {
			found = found || std::strcmp(*entry, "DARLING_HOST_API_TEST=present") == 0;
		}
		environ_ok = found;
	}
	const bool nsget_environ_ok = darling_windows_host_symbol("___NSGetEnviron") != 0 &&
		darling_windows_NSGetEnviron() == &darling_windows_environ;
	void* allocation = darling_windows_malloc(32);
	if (allocation != nullptr) {
		darling_windows_memset(allocation, 0, 32);
		darling_windows_memcpy(allocation, "DARLING-HEAP", 12);
	}
	char temporary_path[MAX_PATH]{};
	GetTempPathA(MAX_PATH, temporary_path);
	const std::string unique_suffix = std::to_string(GetCurrentProcessId());
	const std::string file_path = std::string(temporary_path) +
		"darling-host-api-" + unique_suffix + ".txt";
	const std::string renamed_file_path = std::string(temporary_path) +
		"darling-host-api-renamed-" + unique_suffix + ".txt";
	const int file_descriptor = darling_windows_open(file_path.c_str(), 0x0201, 0600);
	const char file_payload[] = "ABI";
	const int file_written = file_descriptor >= 0 ?
		darling_windows_write(file_descriptor, file_payload, sizeof(file_payload) - 1) : -1;
	const int duplicate_descriptor = file_descriptor >= 0 ? darling_windows_dup(file_descriptor) : -1;
	const int duplicate_written = duplicate_descriptor >= 0 ?
		darling_windows_write(duplicate_descriptor, file_payload, sizeof(file_payload) - 1) : -1;
	const std::int64_t file_end = file_descriptor >= 0 ?
		darling_windows_lseek(file_descriptor, 0, 2) : -1;
	const std::int64_t file_start = file_descriptor >= 0 ?
		darling_windows_lseek(file_descriptor, 0, 0) : -1;
	const bool truncate_ok = file_descriptor >= 0 && darling_windows_ftruncate(file_descriptor, 3) == 0 &&
		darling_windows_lseek(file_descriptor, 0, 2) == 3;
	const bool fpathconf_ok = file_descriptor >= 0 &&
		darling_windows_fpathconf(file_descriptor, 4) == 255 &&
		darling_windows_fpathconf(file_descriptor, 18) == 64 &&
		darling_windows_fpathconf(-1, 4) == -1;
	if (file_descriptor >= 0) {
		darling_windows_close(file_descriptor);
	}
	if (duplicate_descriptor >= 0) {
		darling_windows_close(duplicate_descriptor);
	}
	const bool pid_ok = darling_windows_getpid() == static_cast<int>(GetCurrentProcessId());
	const bool normalized_identity_symbols_ok =
		darling_windows_host_symbol("getpid") != 0 &&
		darling_windows_host_symbol("getuid") != 0 &&
		darling_windows_host_symbol("geteuid") != 0 &&
		darling_windows_host_symbol("getgid") != 0 &&
		darling_windows_host_symbol("getegid") != 0;
	std::cout << "DARWIN_NORMALIZED_IDENTITY_SYMBOLS="
		<< (normalized_identity_symbols_ok ? "PASS" : "FAIL") << "\n";
	const bool process_group_ok = darling_windows_host_symbol("_getpgrp") != 0 &&
		darling_windows_host_symbol("_getpgid") != 0 &&
		darling_windows_host_symbol("_getsid") != 0 &&
		darling_windows_host_symbol("_setpgid") != 0 &&
		darling_windows_host_symbol("_setsid") != 0 &&
		darling_windows_getpgrp() == darling_windows_getpid() &&
		darling_windows_getpgid(0) == darling_windows_getpid() &&
		darling_windows_getpgid(darling_windows_getpid()) == darling_windows_getpid() &&
		darling_windows_getpgid(-1) == -1 &&
		darling_windows_getsid(0) == darling_windows_getpid() &&
		darling_windows_getsid(-1) == -1 &&
		darling_windows_setpgid(0, 0) == 0 &&
		darling_windows_setpgid(-1, -1) == -1 &&
		darling_windows_setsid() == darling_windows_getpid() &&
		darling_windows_setsid() == -1;
	int worker_session = -1;
	std::thread session_worker([&worker_session] {
		worker_session = darling_windows_setsid();
	});
	session_worker.join();
	const bool thread_local_session_ok = worker_session == darling_windows_getpid();
	std::uint32_t real_uid = 0, effective_uid = 0, saved_uid = 0;
	std::uint32_t real_gid = 0, effective_gid = 0, saved_gid = 0;
	const int uid = darling_windows_getuid();
	const bool identity_triplet_ok = darling_windows_host_symbol("_getresuid") != 0 &&
		darling_windows_host_symbol("_getresgid") != 0 &&
		darling_windows_getresuid(&real_uid, &effective_uid, &saved_uid) == 0 &&
		darling_windows_getresgid(&real_gid, &effective_gid, &saved_gid) == 0 &&
		real_uid == effective_uid && effective_uid == saved_uid &&
		real_gid == effective_gid && effective_gid == saved_gid &&
		darling_windows_getresuid(nullptr, &effective_uid, &saved_uid) == -1;
	std::uint32_t groups[1] = {0};
	const bool supplementary_groups_ok = darling_windows_host_symbol("_getgroups") != 0 &&
		darling_windows_host_symbol("_setgroups") != 0 &&
		darling_windows_getgroups(0, nullptr) == 1 &&
		darling_windows_getgroups(1, groups) == 1 && groups[0] == static_cast<std::uint32_t>(darling_windows_getgid()) &&
		darling_windows_getgroups(1, nullptr) == -1 &&
		darling_windows_setgroups(0, nullptr) == -1;
	const bool identity_mutation_ok = darling_windows_host_symbol("_setuid") != 0 &&
		darling_windows_host_symbol("_seteuid") != 0 &&
		darling_windows_host_symbol("_setgid") != 0 &&
		darling_windows_host_symbol("_setegid") != 0 &&
		darling_windows_setuid(uid) == 0 && darling_windows_seteuid(uid) == 0 &&
		darling_windows_setgid(uid) == 0 && darling_windows_setegid(uid) == 0 &&
		darling_windows_setuid(-1) == -1 && darling_windows_setuid(uid + 1) == -1 &&
		darling_windows_host_symbol("_setreuid") != 0 && darling_windows_host_symbol("_setregid") != 0 &&
		darling_windows_host_symbol("_setresuid") != 0 && darling_windows_host_symbol("_setresgid") != 0 &&
		darling_windows_setreuid(uid, uid) == 0 && darling_windows_setregid(uid, uid) == 0 &&
		darling_windows_setresuid(uid, uid, uid) == 0 && darling_windows_setresgid(uid, uid, uid) == 0 &&
		darling_windows_setreuid(uid + 1, uid) == -1;
	const bool exit_symbol_ok = darling_windows_host_symbol("_exit") != 0 &&
		darling_windows_host_symbol("exit") != 0;
	const bool parent_pid_ok = darling_windows_getppid() > 0;
	STARTUPINFOW wait_startup{};
	wait_startup.cb = sizeof(wait_startup);
	PROCESS_INFORMATION wait_process{};
	wchar_t wait_command[] = L"cmd.exe /d /c exit 7";
	const bool wait_spawned = CreateProcessW(nullptr, wait_command, nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &wait_startup, &wait_process) != FALSE;
	if (wait_spawned) {
		CloseHandle(wait_process.hThread);
	}
	int wait_status = -1;
	const bool waitpid_ok = wait_spawned && darling_windows_host_symbol("_waitpid") != 0 &&
		darling_windows_waitpid(static_cast<int>(wait_process.dwProcessId), &wait_status, 0) ==
		static_cast<int>(wait_process.dwProcessId) && wait_status == (7 << 8);
	if (wait_spawned) {
		CloseHandle(wait_process.hProcess);
	}
	if (!wait_spawned) {
		wait_status = 1;
	}
	const bool identity_ok = uid >= 0 && darling_windows_geteuid() == uid &&
		darling_windows_getgid() == uid && darling_windows_getegid() == uid;
	char login_buffer[256]{};
	const char* login = darling_windows_getlogin();
	const bool login_ok = login != nullptr && login[0] != '\0' &&
		darling_windows_getlogin_r(login_buffer, sizeof(login_buffer)) == 0 &&
		std::strcmp(login, login_buffer) == 0 &&
		darling_windows_host_symbol("_setlogin") != 0 &&
		darling_windows_setlogin(login) == 0 && darling_windows_setlogin(nullptr) == -1;
	char executable_path[4096]{};
	std::uint32_t executable_path_size = sizeof(executable_path);
	const bool executable_path_ok = darling_windows_host_symbol("___NSGetExecutablePath") != 0 &&
		darling_windows_NSGetExecutablePath(executable_path, &executable_path_size) == 0 &&
		executable_path_size > 1 && std::strstr(executable_path, ".exe") != nullptr;
	const std::string original_program_name = darling_windows_getprogname() == nullptr ?
		std::string{} : darling_windows_getprogname();
	darling_windows_setprogname("darling-host-api-smoke");
	const bool program_name_ok = darling_windows_host_symbol("_getprogname") != 0 &&
		darling_windows_host_symbol("_setprogname") != 0 &&
		darling_windows_getprogname() != nullptr &&
		std::strcmp(darling_windows_getprogname(), "darling-host-api-smoke") == 0;
	darling_windows_setprogname(original_program_name.c_str());
	darling_rusage usage{};
	darling_rusage children_usage{};
	const bool usage_ok = darling_windows_host_symbol("_getrusage") != 0 &&
		darling_windows_getrusage(0, &usage) == 0 &&
		usage.ru_maxrss > 0 && usage.ru_minflt >= 0 &&
		darling_windows_getrusage(-1, &children_usage) == 0 &&
		usage.ru_utime.tv_sec >= 0 && usage.ru_utime.tv_usec < 1'000'000 &&
		usage.ru_stime.tv_sec >= 0 && usage.ru_stime.tv_usec < 1'000'000 &&
		darling_windows_getrusage(99, &usage) == -1;
	darling_tms process_times{};
	const auto elapsed_ticks = darling_windows_times(&process_times);
	const bool times_ok = darling_windows_host_symbol("_times") != 0 && elapsed_ticks >= 0 &&
		process_times.tms_utime >= 0 && process_times.tms_stime >= 0 &&
		process_times.tms_cutime == 0 && process_times.tms_cstime == 0 &&
		darling_windows_sysconf(3) == 100;
	void* dynamic_module = darling_windows_dlopen("kernel32.dll", 0);
	void* dynamic_symbol = dynamic_module == nullptr ? nullptr :
		darling_windows_dlsym(dynamic_module, "_GetCurrentProcessId");
	darling_dl_info dynamic_info{};
	const bool dladdr_ok = dynamic_symbol != nullptr &&
		darling_windows_host_symbol("_dladdr") != 0 &&
		darling_windows_dladdr(dynamic_symbol, &dynamic_info) == 1 &&
		dynamic_info.dli_fname != nullptr && dynamic_info.dli_fname[0] != '\0' &&
		dynamic_info.dli_fbase != nullptr;
	const auto dyld_count = darling_windows_dyld_image_count();
	const char* dyld_name = dyld_count == 0 ? nullptr :
		darling_windows_dyld_get_image_name(0);
	const bool dyld_images_ok = darling_windows_host_symbol("_dyld_image_count") != 0 &&
		darling_windows_host_symbol("_dyld_get_image_name") != 0 &&
		darling_windows_host_symbol("_dyld_get_image_header") != 0 &&
		darling_windows_host_symbol("_dyld_get_image_vmaddr_slide") != 0 &&
		dyld_count > 0 && dyld_name != nullptr && dyld_name[0] != '\0' &&
		darling_windows_dyld_get_image_header(0) != nullptr &&
		darling_windows_dyld_get_image_vmaddr_slide(0) == 0;
	const bool dynamic_loader_ok = darling_windows_host_symbol("_dlopen") != 0 &&
		darling_windows_host_symbol("_dlsym") != 0 &&
		darling_windows_host_symbol("_dlclose") != 0 &&
		darling_windows_host_symbol("_dlerror") != 0 && dynamic_module != nullptr &&
		darling_windows_host_symbol("dlopen") != 0 &&
		darling_windows_host_symbol("dlsym") != 0 &&
		darling_windows_host_symbol("dlclose") != 0 &&
		darling_windows_host_symbol("dlerror") != 0 &&
		darling_windows_host_symbol("dladdr") != 0 &&
		dynamic_symbol != nullptr && darling_windows_dlerror() == nullptr &&
		darling_windows_dlsym(dynamic_module, "_DefinitelyMissingSymbol") == nullptr &&
		darling_windows_dlerror() != nullptr && darling_windows_dlclose(dynamic_module) == 0;
	SYSTEM_INFO system_info{};
	GetSystemInfo(&system_info);
	const bool page_size_ok = darling_windows_getpagesize() == static_cast<int>(system_info.dwPageSize) &&
		darling_windows_getpagesize() > 0;
	const auto configured_processors = darling_windows_sysconf(57);
	const auto online_processors = darling_windows_sysconf(58);
	const auto physical_pages = darling_windows_sysconf(200);
	const auto available_pages = darling_windows_sysconf(201);
	const bool sysconf_ok = configured_processors > 0 && online_processors > 0 &&
		physical_pages > 0 && available_pages > 0 && available_pages <= physical_pages &&
		darling_windows_sysconf(4) == darling_windows_getdtablesize() &&
		darling_windows_sysconf(29) == darling_windows_getpagesize() &&
		darling_windows_sysconf(-1) == -1;
	char confstr_path[128]{};
	const std::size_t confstr_required = darling_windows_confstr(1, nullptr, 0);
	const std::size_t confstr_written = darling_windows_confstr(1, confstr_path,
		sizeof(confstr_path));
	char confstr_short[8]{};
	const bool confstr_ok = darling_windows_host_symbol("_confstr") != 0 &&
		confstr_required == confstr_written && confstr_required > 1 &&
		std::strcmp(confstr_path, "/usr/bin:/bin:/usr/sbin:/sbin") == 0 &&
		darling_windows_confstr(65537, nullptr, 0) > 1 &&
		darling_windows_confstr(1, confstr_short, sizeof(confstr_short)) == confstr_required &&
		confstr_short[sizeof(confstr_short) - 1] == '\0' &&
		darling_windows_confstr(99999, nullptr, 0) == 0;
	const bool pathconf_ok = darling_windows_host_symbol("_pathconf") != 0 &&
		darling_windows_host_symbol("_fpathconf") != 0 &&
		darling_windows_pathconf(".", 4) == 255 &&
		darling_windows_pathconf(".", 5) == 4096 &&
		darling_windows_pathconf(".", 11) == 0 &&
		darling_windows_pathconf(nullptr, 4) == -1 &&
		darling_windows_pathconf(".", 99999) == -1;
	const bool descriptor_limit_ok = darling_windows_host_symbol("_getdtablesize") != 0 &&
		darling_windows_host_symbol("getdtablesize") != 0 &&
		darling_windows_getdtablesize() == 1024;
	darling_rlimit nofile_limit{};
	darling_rlimit stack_limit{};
	darling_rlimit invalid_limit{2, 1};
	darling_rlimit core_limit{4096, 8192};
	darling_rlimit core_roundtrip{};
	const bool rlimit_ok = darling_windows_host_symbol("_getrlimit") != 0 &&
		darling_windows_host_symbol("getrlimit") != 0 &&
		darling_windows_host_symbol("_setrlimit") != 0 &&
		darling_windows_getrlimit(8, &nofile_limit) == 0 &&
		nofile_limit.rlim_cur == 1024 && nofile_limit.rlim_max == 1024 &&
		darling_windows_getrlimit(3, &stack_limit) == 0 &&
		stack_limit.rlim_cur == 8ull * 1024ull * 1024ull &&
		darling_windows_getrlimit(99, &stack_limit) == -1 &&
		darling_windows_getrlimit(3, nullptr) == -1 &&
		darling_windows_setrlimit(4, &invalid_limit) == -1 &&
		darling_windows_setrlimit(4, &core_limit) == 0 &&
		darling_windows_getrlimit(4, &core_roundtrip) == 0 &&
		core_roundtrip.rlim_cur == 4096 && core_roundtrip.rlim_max == 8192;
	const int original_priority = darling_windows_getpriority(0, 0);
	const bool priority_ok = darling_windows_host_symbol("_getpriority") != 0 &&
		darling_windows_host_symbol("_setpriority") != 0 && original_priority >= -20 &&
		darling_windows_setpriority(0, 0, 0) == 0 &&
		darling_windows_getpriority(0, 0) >= -20 &&
		darling_windows_getpriority(0, static_cast<std::uint32_t>(GetCurrentProcessId())) >= -20 &&
		darling_windows_setpriority(0, static_cast<std::uint32_t>(GetCurrentProcessId()), 0) == 0 &&
		darling_windows_setpriority(0, 0, original_priority) == 0 &&
		darling_windows_getpriority(99, 0) == -1 &&
		darling_windows_setpriority(0, 0, 99) == -1;
	const int old_umask = darling_windows_umask(0777);
	const bool umask_ok = darling_windows_host_symbol("_umask") != 0 &&
		darling_windows_host_symbol("umask") != 0 && old_umask == 022 &&
		darling_windows_umask(027) == 0777 && darling_windows_umask(old_umask) == 027;
	double load_average[3]{};
	const bool loadavg_ok = darling_windows_host_symbol("_getloadavg") != 0 &&
		darling_windows_host_symbol("getloadavg") != 0 &&
		darling_windows_getloadavg(load_average, 3) == 3 &&
		load_average[0] >= 0.0 && load_average[1] == load_average[0] &&
		darling_windows_getloadavg(nullptr, 1) == -1;
	std::size_t page_sizes[2]{};
	const bool page_sizes_ok = darling_windows_host_symbol("_getpagesizes") != 0 &&
		darling_windows_host_symbol("getpagesizes") != 0 &&
		darling_windows_getpagesizes(nullptr, 0) == 1 &&
		darling_windows_getpagesizes(page_sizes, 2) == 1 &&
		page_sizes[0] == static_cast<std::size_t>(darling_windows_getpagesize()) &&
		darling_windows_getpagesizes(nullptr, 1) == -1;
	int sysctl_cpu = 0;
	std::size_t sysctl_cpu_size = sizeof(sysctl_cpu);
	char sysctl_machine[64]{};
	std::size_t sysctl_machine_size = sizeof(sysctl_machine);
	const bool sysctl_ok = darling_windows_host_symbol("_sysctlbyname") != 0 &&
		darling_windows_sysctlbyname("hw.ncpu", &sysctl_cpu, &sysctl_cpu_size, nullptr, 0) == 0 &&
		sysctl_cpu > 0 && sysctl_cpu_size == sizeof(sysctl_cpu) &&
		darling_windows_sysctlbyname("hw.machine", sysctl_machine, &sysctl_machine_size, nullptr, 0) == 0 &&
		std::strlen(sysctl_machine) > 0 &&
		darling_windows_sysctlbyname("unknown.darling.key", nullptr, &sysctl_machine_size, nullptr, 0) == -1;
	const int sysctl_cpu_mib[] = {6, 3};
	int sysctl_mib_cpu = 0;
	std::size_t sysctl_mib_cpu_size = sizeof(sysctl_mib_cpu);
	const int sysctl_machine_mib[] = {6, 1};
	char sysctl_mib_machine[64]{};
	std::size_t sysctl_mib_machine_size = sizeof(sysctl_mib_machine);
	const bool sysctl_mib_ok = darling_windows_host_symbol("_sysctl") != 0 &&
		darling_windows_sysctl(sysctl_cpu_mib, 2, &sysctl_mib_cpu, &sysctl_mib_cpu_size, nullptr, 0) == 0 &&
		sysctl_mib_cpu > 0 &&
		darling_windows_sysctl(sysctl_machine_mib, 2, sysctl_mib_machine, &sysctl_mib_machine_size, nullptr, 0) == 0 &&
		std::strlen(sysctl_mib_machine) > 0 &&
		darling_windows_sysctl(nullptr, 0, nullptr, &sysctl_mib_machine_size, nullptr, 0) == -1;
	const bool scheduling_ok = darling_windows_sched_yield() == 0 && darling_windows_sleep(0) == 0;
	std::uint8_t random_bytes[32]{};
	darling_windows_arc4random_buf(random_bytes, sizeof(random_bytes));
	const auto random_value = darling_windows_arc4random();
	std::cout << "DARWIN_NORMALIZED_RANDOM_SYMBOLS="
		<< ((darling_windows_host_symbol("arc4random") != 0 &&
			darling_windows_host_symbol("arc4random_buf") != 0 &&
			darling_windows_host_symbol("getentropy") != 0 &&
			darling_windows_host_symbol("sched_yield") != 0 &&
			darling_windows_host_symbol("sleep") != 0 &&
			darling_windows_host_symbol("raise") != 0) ? "PASS" : "FAIL") << "\n";
	const bool random_ok = darling_windows_host_symbol("_arc4random") != 0 &&
		darling_windows_host_symbol("_arc4random_buf") != 0 &&
		darling_windows_host_symbol("arc4random") != 0 &&
		darling_windows_host_symbol("arc4random_buf") != 0 &&
		darling_windows_host_symbol("getentropy") != 0 &&
		darling_windows_host_symbol("sched_yield") != 0 &&
		darling_windows_host_symbol("sleep") != 0 &&
		darling_windows_host_symbol("raise") != 0 &&
		(random_value != 0 || random_bytes[0] != 0 || random_bytes[1] != 0 || random_bytes[2] != 0 || random_bytes[3] != 0);
	std::uint8_t entropy_bytes[32]{};
	const bool entropy_ok = darling_windows_host_symbol("_getentropy") != 0 &&
		darling_windows_getentropy(entropy_bytes, sizeof(entropy_bytes)) == 0 &&
		darling_windows_getentropy(nullptr, 257) == -1 &&
		(entropy_bytes[0] != 0 || entropy_bytes[1] != 0 || entropy_bytes[2] != 0 || entropy_bytes[3] != 0);
	const bool kill_ok = darling_windows_kill(static_cast<int>(GetCurrentProcessId()), 0) == 0 &&
		darling_windows_kill(-1, 0) == -1;
	std::signal(SIGINT, CaptureSignal);
	const bool raise_ok = darling_windows_raise(SIGINT) == 0 && raised_signal == SIGINT;
	std::signal(SIGINT, SIG_DFL);
	char hostname[256]{};
	const bool hostname_ok = darling_windows_gethostname(hostname, sizeof(hostname)) == 0 && hostname[0] != '\0';
	char terminal_name[32]{};
	darling_winsize terminal_size{};
	const bool terminal_names_ok = darling_windows_host_symbol("_ctermid") != 0 &&
		darling_windows_host_symbol("_ioctl") != 0 &&
		darling_windows_host_symbol("_ttyname_r") != 0 && darling_windows_host_symbol("_ttyname") != 0 &&
		darling_windows_ctermid(terminal_name) == terminal_name && std::strcmp(terminal_name, "CONIN$") == 0 &&
		darling_windows_ttyname_r(99, terminal_name, sizeof(terminal_name)) == 25 &&
		darling_windows_ttyname(99) == nullptr &&
		darling_windows_ioctl(99, 0x40087468ul, &terminal_size) != 0;
	const int directory_descriptor = darling_windows_open(".", 0x00100000, 0);
	char directory_buffer[sizeof(darling_dirent) * 2]{};
	std::int64_t directory_base = 0;
	const auto directory_bytes = directory_descriptor >= 0 ? darling_windows_getdirentries(
		directory_descriptor, directory_buffer, sizeof(directory_buffer), &directory_base) : -1;
	const auto* directory_entry = reinterpret_cast<const darling_dirent*>(directory_buffer);
	const bool directory_entries_ok = darling_windows_host_symbol("_getdirentries") != 0 &&
		darling_windows_host_symbol("_getdirentries64") != 0 &&
		directory_bytes >= static_cast<std::int64_t>(sizeof(darling_dirent)) &&
		directory_entry->d_namlen > 0 && directory_entry->d_reclen == sizeof(darling_dirent) &&
		directory_base > 0;
	if (directory_descriptor >= 0) darling_windows_close(directory_descriptor);
	char domain[256]{};
	const bool domainname_ok = darling_windows_host_symbol("_getdomainname") != 0 &&
		darling_windows_getdomainname(domain, sizeof(domain)) == 0 && domain[0] != '\0' &&
		darling_windows_getdomainname(nullptr, sizeof(domain)) == -1;
	darling_utsname system_name{};
	const bool uname_ok = darling_windows_uname(&system_name) == 0 &&
		std::strcmp(system_name.sysname, "Darwin") == 0 &&
		std::strcmp(system_name.machine, "x86_64") == 0 &&
		system_name.nodename[0] != '\0';
	const bool clock_ok = darling_windows_clock_gettime(6, &first) == 0 &&
		darling_windows_clock_gettime(6, &second) == 0 &&
		!(second.tv_sec < first.tv_sec ||
		(second.tv_sec == first.tv_sec && second.tv_nsec < first.tv_nsec));
	darling_mach_timebase_info mach_timebase{};
	const auto mach_time_first = darling_windows_mach_absolute_time();
	const auto mach_time_second = darling_windows_mach_absolute_time();
	const bool mach_time_ok = darling_windows_host_symbol("_mach_absolute_time") != 0 &&
		darling_windows_host_symbol("_mach_continuous_time") != 0 &&
		darling_windows_host_symbol("_mach_timebase_info") != 0 &&
		darling_windows_mach_timebase_info(&mach_timebase) == 0 &&
		mach_timebase.numer != 0 && mach_timebase.denom != 0 &&
		mach_time_second >= mach_time_first;
	darling_timespec wall_resolution{};
	darling_timespec monotonic_resolution{};
	const bool clock_resolution_ok = darling_windows_clock_getres(0, &wall_resolution) == 0 &&
		darling_windows_clock_getres(6, &monotonic_resolution) == 0 &&
		wall_resolution.tv_nsec == 100 && monotonic_resolution.tv_nsec == 1;
	darling_timespec sleep_request{0, 1'000'000};
	darling_timespec sleep_remaining{};
	darling_timespec absolute_base{};
	darling_windows_clock_gettime(6, &absolute_base);
	const std::uint64_t absolute_deadline =
		static_cast<std::uint64_t>(absolute_base.tv_sec) * 1'000'000'000ull +
		static_cast<std::uint64_t>(absolute_base.tv_nsec) + 1'000'000ull;
	darling_timespec absolute_request{
		static_cast<std::int64_t>(absolute_deadline / 1'000'000'000ull),
		static_cast<std::int64_t>(absolute_deadline % 1'000'000'000ull)};
	darling_timespec absolute_remaining{};
	darling_timespec past_request{0, 0};
	const bool clock_nanosleep_ok =
		darling_windows_host_symbol("clock_nanosleep") != 0 &&
		darling_windows_host_symbol("clock_nanosleep_nocancel") != 0 &&
		darling_windows_clock_nanosleep(6, 0, &sleep_request, &sleep_remaining) == 0 &&
		sleep_remaining.tv_sec == 0 && sleep_remaining.tv_nsec == 0 &&
		darling_windows_clock_nanosleep(6, 1, &absolute_request, &absolute_remaining) == 0 &&
		absolute_remaining.tv_sec == 0 && absolute_remaining.tv_nsec == 0 &&
		darling_windows_clock_nanosleep(6, 1, &past_request, nullptr) == 0 &&
		darling_windows_clock_nanosleep(99, 0, &sleep_request, nullptr) == 22;
	const bool sleep_ok = darling_windows_nanosleep(&sleep_request, &sleep_remaining) == 0 &&
		sleep_remaining.tv_sec == 0 && sleep_remaining.tv_nsec == 0 && clock_nanosleep_ok;
	darling_timeval wall_time{};
	const bool wall_time_ok = darling_windows_gettimeofday(&wall_time, nullptr) == 0 &&
		wall_time.tv_sec > 0 && wall_time.tv_usec >= 0 && wall_time.tv_usec < 1'000'000 &&
		darling_windows_usleep(1000) == 0;
	const bool environment_ok = environment != nullptr && environment_value == "present";
	const bool allocation_ok = allocation != nullptr &&
		std::memcmp(allocation, "DARLING-HEAP", 12) == 0;
	const bool string_ok = darling_windows_strlen("DARLING-HEAP") == 12 &&
		darling_windows_strcmp("darling", "darling") == 0;
	char string_buffer[32]{};
	const bool string_helpers_ok = darling_windows_strcpy(string_buffer, "darling") == string_buffer &&
		std::strcmp(string_buffer, "darling") == 0 &&
		darling_windows_strncpy(string_buffer, "darling-win", 6) == string_buffer &&
		std::strncmp(string_buffer, "darlin", 6) == 0 &&
		darling_windows_strchr("darling", 'l') != nullptr &&
		darling_windows_strrchr("darling", 'g') != nullptr;
	const bool string_search_ok = darling_windows_strstr("darling-windows", "windows") != nullptr &&
		darling_windows_strspn("123abc", "0123456789") == 3 &&
		darling_windows_strcspn("darling-windows", "-") == 7 &&
		darling_windows_strpbrk("darling-windows", "sw") != nullptr &&
		darling_windows_strcasestr("Darling-Windows", "WINDOWS") != nullptr;
	const unsigned char binary_data[] = {0x10, 0x20, 0x30, 0x40, 0x50};
	const unsigned char binary_needle[] = {0x30, 0x40};
	const bool memory_search_ok = darling_windows_strnlen("darling", 4) == 4 &&
		darling_windows_strnlen("darling", 32) == 7 &&
		darling_windows_memchr(binary_data, 0x40, sizeof(binary_data)) == binary_data + 3 &&
		darling_windows_memmem(binary_data, sizeof(binary_data), binary_needle, sizeof(binary_needle)) == binary_data + 2;
	const bool case_insensitive_ok = darling_windows_strcasecmp("Darling", "darling") == 0 &&
		darling_windows_strncasecmp("Darling-Host", "darling-runtime", 7) == 0;
	char tokenize_buffer[] = "darling,windows,runtime";
	char* tokenize_state = nullptr;
	const char* token_one = darling_windows_strtok_r(tokenize_buffer, ",", &tokenize_state);
	const char* token_two = darling_windows_strtok_r(nullptr, ",", &tokenize_state);
	const char* token_three = darling_windows_strtok_r(nullptr, ",", &tokenize_state);
	const bool tokenize_ok = token_one != nullptr && token_two != nullptr && token_three != nullptr &&
		std::strcmp(token_one, "darling") == 0 && std::strcmp(token_two, "windows") == 0 &&
		std::strcmp(token_three, "runtime") == 0;
	char append_buffer[32] = "darling";
	const bool string_append_ok = darling_windows_strcat(append_buffer, "-windows") == append_buffer &&
		std::strcmp(append_buffer, "darling-windows") == 0 &&
		darling_windows_strncat(append_buffer, "-runtime", 4) == append_buffer &&
		std::strcmp(append_buffer, "darling-windows-run") == 0;
	char bounded_buffer[8]{};
	const auto bounded_copy_length = darling_windows_strlcpy(bounded_buffer, "darling-runtime", sizeof(bounded_buffer));
	const bool bounded_copy_ok = bounded_copy_length == 15 && std::strcmp(bounded_buffer, "darling") == 0;
	const auto bounded_concat_length = darling_windows_strlcat(bounded_buffer, "-x", sizeof(bounded_buffer));
	const bool bounded_string_ok = bounded_copy_ok && bounded_concat_length == 9 &&
		std::strcmp(bounded_buffer, "darling") == 0;
	char move_buffer[] = "0123456789";
	const bool memory_overlap_ok = darling_windows_memmove(move_buffer + 2, move_buffer, 8) == move_buffer + 2 &&
		std::strcmp(move_buffer, "0101234567") == 0 &&
		darling_windows_memcmp(move_buffer, "0101234567", 10) == 0;
	char bsd_buffer[] = "0123456789";
	darling_windows_bcopy(bsd_buffer, bsd_buffer + 2, 8);
	char zero_buffer[] = "abcdef";
	darling_windows_bzero(zero_buffer, sizeof(zero_buffer));
	char cc_buffer[8]{};
	const char cc_source[] = "abc:def";
	const bool bsd_memory_ok = std::strcmp(bsd_buffer, "0101234567") == 0 &&
		zero_buffer[0] == '\0' && darling_windows_memccpy(cc_buffer, cc_source, ':', sizeof(cc_source)) == cc_buffer + 4 &&
		std::memcmp(cc_buffer, "abc:", 4) == 0;
	unsigned char explicit_zero_buffer[32];
	std::memset(explicit_zero_buffer, 0xA5, sizeof(explicit_zero_buffer));
	darling_windows_explicit_bzero(explicit_zero_buffer, sizeof(explicit_zero_buffer));
	const bool explicit_zero_ok = darling_windows_host_symbol("_explicit_bzero") != 0 &&
		std::all_of(std::begin(explicit_zero_buffer), std::end(explicit_zero_buffer),
			[](unsigned char value) { return value == 0; });
	char* duplicate_string = darling_windows_strdup("darling-runtime");
	char* limited_duplicate = darling_windows_strndup("darling-runtime", 7);
	const bool duplicate_string_ok = duplicate_string != nullptr && limited_duplicate != nullptr &&
		std::strcmp(duplicate_string, "darling-runtime") == 0 &&
		std::strcmp(limited_duplicate, "darling") == 0;
	darling_windows_free(duplicate_string);
	darling_windows_free(limited_duplicate);
	void* zero_allocation = darling_windows_calloc(4, sizeof(std::uint32_t));
	const bool calloc_ok = zero_allocation != nullptr &&
		std::memcmp(zero_allocation, "\0\0\0\0\0\0\0\0", 8) == 0;
	void* resized_allocation = darling_windows_malloc(8);
	if (resized_allocation != nullptr) darling_windows_memcpy(resized_allocation, "retain", 7);
	resized_allocation = darling_windows_realloc(resized_allocation, 32);
	const bool realloc_ok = resized_allocation != nullptr &&
		std::memcmp(resized_allocation, "retain", 7) == 0;
	darling_windows_free(zero_allocation);
	darling_windows_free(resized_allocation);
	char* parse_end = nullptr;
	const bool parsing_ok = darling_windows_strtol("-42tail", &parse_end, 10) == -42 &&
		parse_end != nullptr && std::strcmp(parse_end, "tail") == 0 &&
		darling_windows_strtoll("7fffffffffffffff", &parse_end, 16) == 0x7fffffffffffffffLL &&
		darling_windows_strtoul("ff", &parse_end, 16) == 255ul;
	const bool floating_parsing_ok = darling_windows_strtod("3.125tail", &parse_end) == 3.125 &&
		parse_end != nullptr && std::strcmp(parse_end, "tail") == 0 &&
		darling_windows_strtof("2.5", &parse_end) == 2.5f &&
		darling_windows_strtold("1.25", &parse_end) == 1.25L;
	char formatted[64]{};
	const int formatted_length = darling_windows_snprintf(formatted, sizeof(formatted),
		"darling-%d-%s", 42, "windows");
	const bool formatting_ok = formatted_length == 18 &&
		std::strcmp(formatted, "darling-42-windows") == 0;
	char* dynamic_format = nullptr;
	const int dynamic_length = darling_windows_asprintf(&dynamic_format,
		"darling-%s-%d", "heap", 64);
    const bool dynamic_format_ok = dynamic_format != nullptr && dynamic_length == 15 &&
		std::strcmp(dynamic_format, "darling-heap-64") == 0;
	darling_windows_free(dynamic_format);
	const bool strerror_ok = std::string(darling_windows_strerror(2)).find("No such file") != std::string::npos &&
		std::string(darling_windows_strerror(9)).find("Bad file") != std::string::npos &&
		std::string(darling_windows_strerror(22)).find("Invalid") != std::string::npos;
	char strerror_buffer[64]{};
	const bool strerror_r_ok = darling_windows_strerror_r(2, strerror_buffer,
		sizeof(strerror_buffer)) == 0 && std::string(strerror_buffer).find("No such file") != std::string::npos;
	const bool file_values_ok = file_written == 3 && duplicate_written == 3 && file_end == 6 && file_start == 0 && truncate_ok;
	const bool file_access_ok = darling_windows_access(file_path.c_str(), 0) == 0;
	(void)DeleteFileA(renamed_file_path.c_str());
	const int rename_result = file_access_ok ?
		darling_windows_rename(file_path.c_str(), renamed_file_path.c_str()) : -1;
	const DWORD rename_error = GetLastError();
	const bool rename_skip = file_access_ok && rename_result != 0 &&
		(rename_error == ERROR_ACCESS_DENIED || rename_error == ERROR_PRIVILEGE_NOT_HELD);
	const bool file_rename_ok = file_access_ok && (rename_result == 0 || rename_skip);
	const bool file_remove_ok = file_rename_ok && darling_windows_unlink(
		(rename_skip ? file_path : renamed_file_path).c_str()) == 0;
	if (rename_skip)
		std::cerr << "FILE_RENAME=SKIP ACCESS_DENIED\n";
	const bool file_ok = file_values_ok && file_remove_ok;
	std::string temporary_template = std::string(temporary_path) + "darling-mkstemp-XXXXXX";
	const int temporary_descriptor = darling_windows_mkstemp(temporary_template.data());
	const bool mkstemp_ok = temporary_descriptor >= 0 &&
		temporary_template.find('X') == std::string::npos &&
		darling_windows_access(temporary_template.c_str(), 0) == 0 &&
		darling_windows_host_symbol("_mkstemp") != 0 &&
		darling_windows_close(temporary_descriptor) == 0 &&
		darling_windows_unlink(temporary_template.c_str()) == 0;
	std::string suffix_template = std::string(temporary_path) + "darling-mkstemps-XXXXXX.suf";
	const int suffix_descriptor = darling_windows_mkstemps(suffix_template.data(), 4);
	const bool mkstemps_ok = suffix_descriptor >= 0 &&
		suffix_template.find('X') == std::string::npos &&
		suffix_template.size() >= 4 && suffix_template.substr(suffix_template.size() - 4) == ".suf" &&
		darling_windows_access(suffix_template.c_str(), 0) == 0 &&
		darling_windows_host_symbol("_mkstemps") != 0 &&
		darling_windows_close(suffix_descriptor) == 0 &&
		darling_windows_unlink(suffix_template.c_str()) == 0;
	std::string directory_template = std::string(temporary_path) + "darling-mkdtemp-XXXXXX";
	const bool mkdtemp_ok = darling_windows_mkdtemp(directory_template.data()) != nullptr &&
		directory_template.find('X') == std::string::npos &&
		GetFileAttributesA(directory_template.c_str()) != INVALID_FILE_ATTRIBUTES &&
		(darling_windows_host_symbol("_mkdtemp") != 0) &&
		RemoveDirectoryA(directory_template.c_str()) != 0;
	int pipe_descriptors[2]{-1, -1};
	const char pipe_payload[] = "PIPE";
	const bool pipe_ok = darling_windows_pipe(pipe_descriptors) == 0 &&
		darling_windows_write(pipe_descriptors[1], pipe_payload, sizeof(pipe_payload) - 1) == 4;
	darling::windows_host::darling_pollfd poll_descriptor{pipe_descriptors[0], 0x0001, 0};
	const bool poll_ok = pipe_ok && darling_windows_poll(&poll_descriptor, 1, 0) == 1 &&
		(poll_descriptor.revents & 0x0001) != 0 && darling_windows_host_symbol("_poll") != 0;
	darling::windows_host::darling_pollfd ignored_poll_descriptors[] = {
		{-1, 0x0001, 0}, {pipe_descriptors[0], 0x0001, 0}
	};
	const bool ignored_poll_ok = pipe_ok && darling_windows_poll(ignored_poll_descriptors, 2, 0) == 1 &&
		ignored_poll_descriptors[0].revents == 0 &&
		(ignored_poll_descriptors[1].revents & 0x0001) != 0;
	darling_fd_set select_read_set{};
	select_read_set.fds_bits[pipe_descriptors[0] / 32] |=
		static_cast<std::int32_t>(1) << (pipe_descriptors[0] % 32);
	darling_timeval select_timeout{};
	const bool select_ok = pipe_ok && darling_windows_select(pipe_descriptors[0] + 1,
		&select_read_set, nullptr, nullptr, &select_timeout) == 1 &&
		(select_read_set.fds_bits[pipe_descriptors[0] / 32] &
			(static_cast<std::int32_t>(1) << (pipe_descriptors[0] % 32))) != 0 &&
		darling_windows_host_symbol("_select") != 0;
	darling_fd_set standard_write_set{};
	standard_write_set.fds_bits[1 / 32] |=
		static_cast<std::int32_t>(1) << (1 % 32);
	darling_timeval standard_timeout{0, 0};
	const bool standard_select_ok = darling_windows_select(2, nullptr,
		&standard_write_set, nullptr, &standard_timeout) == 1 &&
		(standard_write_set.fds_bits[1 / 32] &
			(static_cast<std::int32_t>(1) << (1 % 32))) != 0;
	darling_timeval invalid_select_timeout{0, 1'000'000};
	const bool invalid_select_timeout_ok = darling_windows_select(0, nullptr,
		nullptr, nullptr, &invalid_select_timeout) == -1;
	darling_fd_set pselect_write_set{};
	pselect_write_set.fds_bits[1 / 32] |=
		static_cast<std::int32_t>(1) << (1 % 32);
	const darling_timespec pselect_timeout{0, 0};
	const bool pselect_ok = darling_windows_pselect(2, nullptr,
		&pselect_write_set, nullptr, &pselect_timeout, nullptr) == 1 &&
		(pselect_write_set.fds_bits[1 / 32] &
			(static_cast<std::int32_t>(1) << (1 % 32))) != 0 &&
		darling_windows_host_symbol("_pselect") != 0;
	int socket_listener = darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int socket_client = -1;
	int socket_peer = -1;
	sockaddr_in socket_address{};
	socket_address.sin_family = AF_INET;
	socket_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	socket_address.sin_port = htons(0);
	bool socket_api_ok = socket_listener >= 0 &&
		darling_windows_bind(socket_listener, &socket_address, sizeof(socket_address)) == 0;
	int socket_address_length = sizeof(socket_address);
	socket_api_ok = socket_api_ok && darling_windows_getsockname(
		socket_listener, &socket_address, &socket_address_length) == 0 &&
		darling_windows_listen(socket_listener, 1) == 0;
	if (socket_api_ok) {
		socket_client = darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		socket_api_ok = socket_client >= 0 &&
			darling_windows_connect(socket_client, &socket_address, sizeof(socket_address)) == 0;
	}
	if (socket_api_ok)
		socket_peer = darling_windows_accept(socket_listener, nullptr, nullptr);
	sockaddr_in socket_peer_address{};
	int socket_peer_address_length = sizeof(socket_peer_address);
	const bool socket_peer_name_ok = socket_api_ok && socket_client >= 0 &&
		darling_windows_getpeername(socket_client, &socket_peer_address,
		&socket_peer_address_length) == 0 &&
		socket_peer_address.sin_family == AF_INET && socket_peer_address.sin_port != 0;
	int refused_listener = darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in refused_address{};
	refused_address.sin_family = AF_INET;
	refused_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	refused_address.sin_port = htons(0);
	int refused_address_length = sizeof(refused_address);
	const bool refused_bound = refused_listener >= 0 &&
		darling_windows_bind(refused_listener, &refused_address, sizeof(refused_address)) == 0 &&
		darling_windows_getsockname(refused_listener, &refused_address,
		&refused_address_length) == 0;
	if (refused_listener >= 0)
		darling_windows_close(refused_listener);
	int refused_client = refused_bound ?
		darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : -1;
	const bool connect_refused_ok = refused_bound && refused_client >= 0 &&
		darling_windows_connect(refused_client, &refused_address, sizeof(refused_address)) == -1 &&
		darling_windows_errno() != nullptr && *darling_windows_errno() == 61;
	if (refused_client >= 0)
		darling_windows_close(refused_client);
	darling_addrinfo* resolved_localhost = nullptr;
	const bool addrinfo_ok = darling_windows_getaddrinfo("localhost", nullptr, nullptr,
		&resolved_localhost) == 0 && resolved_localhost != nullptr &&
		resolved_localhost->ai_addr != nullptr && resolved_localhost->ai_addrlen != 0;
	darling_windows_freeaddrinfo(resolved_localhost);
	darling_addrinfo numeric_hints{};
	numeric_hints.ai_flags = AI_NUMERICHOST;
	darling_addrinfo* invalid_resolution = nullptr;
	const int invalid_resolution_error = darling_windows_getaddrinfo("not-an-ip-address",
		nullptr, &numeric_hints, &invalid_resolution);
	const bool addrinfo_error_ok = invalid_resolution_error == -2 && invalid_resolution == nullptr;
	darling_windows_freeaddrinfo(invalid_resolution);
	darling_addrinfo numeric_service_hints{};
	numeric_service_hints.ai_flags = 0x1000;
	numeric_service_hints.ai_family = AF_INET;
	numeric_service_hints.ai_socktype = SOCK_STREAM;
	darling_addrinfo* numeric_service_result = nullptr;
	const bool numeric_service_ok = darling_windows_getaddrinfo("127.0.0.1", "80",
		&numeric_service_hints, &numeric_service_result) == 0 &&
		numeric_service_result != nullptr &&
		numeric_service_result->ai_addr != nullptr;
	darling_windows_freeaddrinfo(numeric_service_result);
	darling_addrinfo* nonnumeric_service_result = nullptr;
	const bool nonnumeric_service_ok = darling_windows_getaddrinfo("127.0.0.1", "http",
		&numeric_service_hints, &nonnumeric_service_result) == -2 &&
		nonnumeric_service_result == nullptr;
	darling_windows_freeaddrinfo(nonnumeric_service_result);
	char peer_host[NI_MAXHOST]{};
	char peer_service[NI_MAXSERV]{};
	const bool nameinfo_ok = socket_peer_name_ok &&
		darling_windows_getnameinfo(&socket_peer_address, socket_peer_address_length,
			peer_host, sizeof(peer_host), peer_service, sizeof(peer_service),
			NI_NUMERICHOST | NI_NUMERICSERV) == 0 && peer_host[0] != '\0' && peer_service[0] != '\0';
	in_addr parsed_address{};
	char rendered_address[INET_ADDRSTRLEN]{};
	const bool inet_conversion_ok = darling_windows_inet_pton(AF_INET, "127.0.0.1",
		&parsed_address) == 1 && darling_windows_inet_ntop(AF_INET, &parsed_address,
		rendered_address, sizeof(rendered_address)) != nullptr &&
		std::strcmp(rendered_address, "127.0.0.1") == 0;
	int socket_keepalive = 1;
	int socket_keepalive_readback = 0;
	int socket_keepalive_length = sizeof(socket_keepalive_readback);
	const bool socket_options_ok = socket_api_ok && socket_client >= 0 &&
		darling_windows_setsockopt(socket_client, SOL_SOCKET, SO_KEEPALIVE,
		&socket_keepalive, sizeof(socket_keepalive)) == 0 &&
		darling_windows_getsockopt(socket_client, SOL_SOCKET, SO_KEEPALIVE,
		&socket_keepalive_readback, &socket_keepalive_length) == 0 &&
		socket_keepalive_readback != 0;
	constexpr int darwin_so_nosigpipe = 0x1022;
	int socket_nosigpipe = 1;
	int socket_nosigpipe_readback = 0;
	int socket_nosigpipe_length = sizeof(socket_nosigpipe_readback);
	const bool socket_nosigpipe_ok = socket_api_ok && socket_client >= 0 &&
		darling_windows_setsockopt(socket_client, SOL_SOCKET, darwin_so_nosigpipe,
		&socket_nosigpipe, sizeof(socket_nosigpipe)) == 0 &&
		darling_windows_getsockopt(socket_client, SOL_SOCKET, darwin_so_nosigpipe,
		&socket_nosigpipe_readback, &socket_nosigpipe_length) == 0 &&
		socket_nosigpipe_readback == 1 && socket_nosigpipe_length == sizeof(int);
	constexpr int darwin_so_reuseport = 0x0200;
	int socket_reuseport = 1;
	int socket_reuseport_readback = 0;
	int socket_reuseport_length = sizeof(socket_reuseport_readback);
	const bool socket_reuseport_ok = socket_api_ok && socket_client >= 0 &&
		darling_windows_setsockopt(socket_client, SOL_SOCKET, darwin_so_reuseport,
		&socket_reuseport, sizeof(socket_reuseport)) == 0 &&
		darling_windows_getsockopt(socket_client, SOL_SOCKET, darwin_so_reuseport,
		&socket_reuseport_readback, &socket_reuseport_length) == 0 &&
		socket_reuseport_readback == 1 && socket_reuseport_length == sizeof(int);
	const int socket_duplicate = socket_api_ok && socket_client >= 0 ?
		darling_windows_fcntl(socket_client, 0, 40) : -1;
	const bool socket_duplication_ok = socket_duplicate >= 40 &&
		darling_windows_fcntl(socket_duplicate, 4, 0x0004) == 0 &&
		(darling_windows_fcntl(socket_duplicate, 3) & 0x0004) != 0 &&
		darling_windows_fcntl(socket_duplicate, 2, 1) == 0 &&
		darling_windows_fcntl(socket_duplicate, 1) == 1;
	const char socket_payload[] = "SOCK";
	const bool socket_poll_ok = socket_api_ok && socket_peer >= 0 && socket_client >= 0 &&
		darling_windows_write(socket_client, socket_payload, sizeof(socket_payload) - 1) == 4;
	darling::windows_host::darling_pollfd socket_poll{socket_peer, 0x0001, 0};
	const bool socket_readiness_ok = socket_poll_ok && darling_windows_poll(&socket_poll, 1, 100) == 1 &&
		(socket_poll.revents & 0x0001) != 0;
	const char socket_exception_payload[] = "!";
	const bool socket_exception_send_ok = socket_api_ok && socket_peer >= 0 && socket_client >= 0 &&
		darling_windows_sendto(socket_client, socket_exception_payload, 1, MSG_OOB,
			nullptr, 0) == 1;
	darling_fd_set socket_exception_set{};
	if (socket_peer >= 0)
		socket_exception_set.fds_bits[socket_peer / 32] |=
			static_cast<std::int32_t>(1) << (socket_peer % 32);
	darling_timeval socket_exception_timeout{0, 500000};
	const bool socket_exception_ok = socket_exception_send_ok &&
		darling_windows_select(socket_peer + 1, nullptr, nullptr,
			&socket_exception_set, &socket_exception_timeout) == 1 &&
		(socket_exception_set.fds_bits[socket_peer / 32] &
			(static_cast<std::int32_t>(1) << (socket_peer % 32))) != 0;
	char socket_buffer[sizeof(socket_payload)]{};
	 socket_api_ok = socket_readiness_ok &&
		darling_windows_read(socket_peer, socket_buffer, sizeof(socket_payload) - 1) == 4 &&
		std::memcmp(socket_buffer, socket_payload, sizeof(socket_payload) - 1) == 0 &&
		socket_options_ok &&
		socket_nosigpipe_ok &&
		socket_reuseport_ok &&
		 socket_duplication_ok &&
	 socket_exception_ok &&
		darling_windows_host_symbol("socket") != 0 &&
		darling_windows_host_symbol("bind") != 0 &&
		darling_windows_host_symbol("connect") != 0 &&
		darling_windows_host_symbol("listen") != 0 &&
		darling_windows_host_symbol("accept") != 0 &&
		darling_windows_host_symbol("getsockname") != 0 &&
		darling_windows_host_symbol("getpeername") != 0 &&
		darling_windows_host_symbol("getaddrinfo") != 0 &&
		darling_windows_host_symbol("freeaddrinfo") != 0 &&
		darling_windows_host_symbol("getnameinfo") != 0 &&
		darling_windows_host_symbol("inet_pton") != 0 &&
		darling_windows_host_symbol("inet_ntop") != 0 &&
	 darling_windows_host_symbol("_socket") != 0 &&
		darling_windows_host_symbol("_bind") != 0 &&
		darling_windows_host_symbol("_connect") != 0 &&
		darling_windows_host_symbol("_listen") != 0 &&
		darling_windows_host_symbol("_accept") != 0 &&
		darling_windows_host_symbol("_getsockname") != 0 &&
		 socket_peer_name_ok &&
		 connect_refused_ok &&
		darling_windows_host_symbol("_getpeername") != 0 &&
		addrinfo_ok && darling_windows_host_symbol("_getaddrinfo") != 0 &&
		darling_windows_host_symbol("_freeaddrinfo") != 0 &&
		addrinfo_error_ok &&
		numeric_service_ok &&
		nonnumeric_service_ok &&
		nameinfo_ok && darling_windows_host_symbol("_getnameinfo") != 0 &&
		inet_conversion_ok && darling_windows_host_symbol("_inet_pton") != 0 &&
		darling_windows_host_symbol("_inet_ntop") != 0 &&
		darling_windows_host_symbol("_getsockopt") != 0 &&
		darling_windows_host_symbol("_setsockopt") != 0 &&
		darling_windows_host_symbol("_shutdown") != 0;
	std::cout << "DARWIN_NORMALIZED_SOCKET_SYMBOLS="
		<< ((darling_windows_host_symbol("socket") != 0 &&
			darling_windows_host_symbol("bind") != 0 &&
			darling_windows_host_symbol("connect") != 0 &&
			darling_windows_host_symbol("listen") != 0 &&
			darling_windows_host_symbol("accept") != 0 &&
			darling_windows_host_symbol("getaddrinfo") != 0 &&
			darling_windows_host_symbol("freeaddrinfo") != 0 &&
			darling_windows_host_symbol("inet_pton") != 0 &&
			 darling_windows_host_symbol("inet_ntop") != 0) ? "PASS" : "FAIL") << "\n";
	std::cout << "DARWIN_NORMALIZED_SOCKET_IO_SYMBOLS="
		<< ((darling_windows_host_symbol("poll") != 0 &&
			darling_windows_host_symbol("select") != 0 &&
			darling_windows_host_symbol("pselect") != 0 &&
			darling_windows_host_symbol("getsockopt") != 0 &&
			darling_windows_host_symbol("setsockopt") != 0 &&
			darling_windows_host_symbol("sendto") != 0 &&
			darling_windows_host_symbol("recvfrom") != 0 &&
			darling_windows_host_symbol("sendmsg") != 0 &&
			darling_windows_host_symbol("recvmsg") != 0 &&
			darling_windows_host_symbol("shutdown") != 0) ? "PASS" : "FAIL") << "\n";
	const bool normalized_path_symbols =
		darling_windows_host_symbol("unlink") != 0 &&
		darling_windows_host_symbol("rmdir") != 0 &&
		darling_windows_host_symbol("link") != 0 &&
		darling_windows_host_symbol("symlink") != 0 &&
		darling_windows_host_symbol("readlink") != 0 &&
		darling_windows_host_symbol("access") != 0 &&
		darling_windows_host_symbol("rename") != 0 &&
		darling_windows_host_symbol("chdir") != 0 &&
		darling_windows_host_symbol("getcwd") != 0 &&
		darling_windows_host_symbol("mkdir") != 0;
	std::cout << "DARWIN_NORMALIZED_PATH_SYMBOLS="
		<< (normalized_path_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_system_symbols =
		darling_windows_host_symbol("sysconf") != 0 &&
		darling_windows_host_symbol("uname") != 0 &&
		darling_windows_host_symbol("gethostname") != 0 &&
		darling_windows_host_symbol("clock_gettime") != 0 &&
		darling_windows_host_symbol("clock_getres") != 0 &&
		darling_windows_host_symbol("clock_nanosleep") != 0 &&
		darling_windows_host_symbol("clock_nanosleep_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_SYSTEM_SYMBOLS="
		<< (normalized_system_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_mach_time_symbols =
		darling_windows_host_symbol("mach_absolute_time") != 0 &&
		darling_windows_host_symbol("mach_continuous_time") != 0 &&
		darling_windows_host_symbol("mach_timebase_info") != 0;
	std::cout << "DARWIN_NORMALIZED_MACH_TIME_SYMBOLS="
		<< (normalized_mach_time_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_posix_time_symbols =
		darling_windows_host_symbol("nanosleep") != 0 &&
		darling_windows_host_symbol("gettimeofday") != 0 &&
		darling_windows_host_symbol("usleep") != 0 &&
		darling_windows_host_symbol("sleep") != 0 &&
		darling_windows_host_symbol("sleep_nocancel") != 0 &&
		darling_windows_host_symbol("__sleep") != 0 &&
		darling_windows_host_symbol("clock_gettime_nocancel") != 0 &&
		darling_windows_host_symbol("clock_getres_nocancel") != 0 &&
		darling_windows_host_symbol("nanosleep_nocancel") != 0 &&
		darling_windows_host_symbol("gettimeofday_nocancel") != 0 &&
		darling_windows_host_symbol("usleep_nocancel") != 0 &&
		darling_windows_host_symbol("__clock_gettime_nocancel") != 0 &&
		darling_windows_host_symbol("__nanosleep_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_POSIX_TIME_SYMBOLS="
		<< (normalized_posix_time_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_resource_symbols =
		darling_windows_host_symbol("getlogin") != 0 &&
		darling_windows_host_symbol("getlogin_r") != 0 &&
		darling_windows_host_symbol("getrusage") != 0 &&
		darling_windows_host_symbol("times") != 0 &&
		darling_windows_host_symbol("getpagesize") != 0 &&
		darling_windows_host_symbol("getpagesizes") != 0;
	std::cout << "DARWIN_NORMALIZED_RESOURCE_SYMBOLS="
	          << (normalized_resource_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_process_resource_symbols =
		darling_windows_host_symbol("confstr") != 0 &&
		darling_windows_host_symbol("pathconf") != 0 &&
		darling_windows_host_symbol("fpathconf") != 0 &&
		darling_windows_host_symbol("getdtablesize") != 0 &&
		darling_windows_host_symbol("getrlimit") != 0 &&
		darling_windows_host_symbol("setrlimit") != 0 &&
		darling_windows_host_symbol("getpriority") != 0 &&
		darling_windows_host_symbol("setpriority") != 0 &&
		darling_windows_host_symbol("umask") != 0 &&
		darling_windows_host_symbol("getloadavg") != 0;
	std::cout << "DARWIN_NORMALIZED_PROCESS_RESOURCE_SYMBOLS="
	          << (normalized_process_resource_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_nocancel_symbols =
		darling_windows_host_symbol("open_nocancel") != 0 &&
		darling_windows_host_symbol("read_nocancel") != 0 &&
		darling_windows_host_symbol("write_nocancel") != 0 &&
		darling_windows_host_symbol("pread_nocancel") != 0 &&
		darling_windows_host_symbol("pwrite_nocancel") != 0 &&
		darling_windows_host_symbol("close_nocancel") != 0 &&
		darling_windows_host_symbol("fcntl_nocancel") != 0 &&
		darling_windows_host_symbol("fsync_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_NOCANCEL_SYMBOLS="
	          << (normalized_nocancel_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_socket_nocancel_symbols =
		darling_windows_host_symbol("poll_nocancel") != 0 &&
		darling_windows_host_symbol("select_nocancel") != 0 &&
		darling_windows_host_symbol("connect_nocancel") != 0 &&
		darling_windows_host_symbol("accept_nocancel") != 0 &&
		darling_windows_host_symbol("sendto_nocancel") != 0 &&
		darling_windows_host_symbol("recvfrom_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_SOCKET_NOCANCEL_SYMBOLS="
	          << (normalized_socket_nocancel_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_process_nocancel_symbols =
		darling_windows_host_symbol("waitpid_nocancel") != 0 &&
		darling_windows_host_symbol("wait4_nocancel") != 0 &&
		darling_windows_host_symbol("waitid_nocancel") != 0 &&
		darling_windows_host_symbol("kill_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_PROCESS_NOCANCEL_SYMBOLS="
	          << (normalized_process_nocancel_symbols ? "PASS" : "FAIL") << "\n";
	const bool normalized_vector_nocancel_symbols =
		darling_windows_host_symbol("readv_nocancel") != 0 &&
		darling_windows_host_symbol("writev_nocancel") != 0 &&
		darling_windows_host_symbol("sendmsg_nocancel") != 0 &&
		darling_windows_host_symbol("recvmsg_nocancel") != 0 &&
		darling_windows_host_symbol("shutdown_nocancel") != 0;
	std::cout << "DARWIN_NORMALIZED_VECTOR_NOCANCEL_SYMBOLS="
	          << (normalized_vector_nocancel_symbols ? "PASS" : "FAIL") << "\n";
	char pthread_name[64]{};
	const auto pthread_self = darling_windows_pthread_self();
	const bool pthread_abi_ok =
		darling_windows_host_symbol("pthread_self") != 0 &&
		darling_windows_host_symbol("pthread_equal") != 0 &&
		darling_windows_host_symbol("pthread_setname_np") != 0 &&
		darling_windows_host_symbol("pthread_getname_np") != 0 &&
		pthread_self != 0 &&
		darling_windows_pthread_equal(pthread_self, pthread_self) == 1 &&
		darling_windows_pthread_equal(pthread_self, pthread_self + 1) == 0 &&
		darling_windows_pthread_setname_np("darling-smoke") == 0 &&
		darling_windows_pthread_getname_np(pthread_self, pthread_name,
		sizeof(pthread_name)) == 0 &&
		std::strcmp(pthread_name, "darling-smoke") == 0;
	char pthread_payload[] = "thread-result";
	std::uint64_t created_thread = 0;
	void* joined_result = nullptr;
	const bool pthread_lifecycle_ok =
		darling_windows_host_symbol("pthread_create") != 0 &&
		darling_windows_host_symbol("pthread_join") != 0 &&
		darling_windows_pthread_create(&created_thread, nullptr,
			&PthreadSmokeStart, pthread_payload) == 0 &&
		darling_windows_pthread_join(created_thread, &joined_result) == 0 &&
		joined_result == pthread_payload + 5;
	std::uint64_t cancellation_thread = 0;
	void* cancellation_result = nullptr;
	const bool pthread_cancellation_ok =
		darling_windows_host_symbol("pthread_cancel") != 0 &&
		darling_windows_host_symbol("pthread_setcancelstate") != 0 &&
		darling_windows_host_symbol("pthread_setcanceltype") != 0 &&
		darling_windows_host_symbol("pthread_testcancel") != 0 &&
		darling_windows_pthread_create(&cancellation_thread, nullptr,
		&PthreadCancellationStart, nullptr) == 0 &&
		([&] {
			for (int attempt = 0; attempt < 100 &&
				!cancellation_worker_started.load(std::memory_order_acquire); ++attempt)
				Sleep(1);
			return cancellation_worker_started.load(std::memory_order_acquire);
		}()) &&
		darling_windows_pthread_cancel(cancellation_thread) == 0 &&
		darling_windows_pthread_join(cancellation_thread, &cancellation_result) == 0 &&
		cancellation_result == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) &&
		cancellation_cleanup_calls.load(std::memory_order_relaxed) == 1 &&
		darling_windows_pthread_setcanceltype(1, nullptr) == 22;
	void* cancellation_mutex = nullptr;
	std::atomic_bool cancellation_lock_started{false};
	CancellationLockContext cancellation_lock_context{&cancellation_mutex,
		&cancellation_lock_started};
	std::uint64_t cancellation_lock_thread = 0;
	void* cancellation_lock_result = nullptr;
	const bool pthread_lock_cancellation_ok =
		darling_windows_pthread_mutex_init(&cancellation_mutex, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&cancellation_mutex) == 0 &&
		darling_windows_pthread_create(&cancellation_lock_thread, nullptr,
		&PthreadCancellationLockStart, &cancellation_lock_context) == 0 &&
		([&] {
			for (int attempt = 0; attempt < 100 &&
				!cancellation_lock_started.load(std::memory_order_acquire); ++attempt)
				Sleep(1);
			return cancellation_lock_started.load(std::memory_order_acquire);
		}()) &&
		darling_windows_pthread_cancel(cancellation_lock_thread) == 0 &&
		darling_windows_pthread_join(cancellation_lock_thread, &cancellation_lock_result) == 0 &&
		cancellation_lock_result == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) &&
		darling_windows_pthread_mutex_unlock(&cancellation_mutex) == 0 &&
		darling_windows_pthread_mutex_destroy(&cancellation_mutex) == 0;
	void* cancellation_condition = nullptr;
	void* cancellation_condition_mutex = nullptr;
	std::atomic_bool cancellation_condition_started{false};
	CancellationConditionContext cancellation_condition_context{&cancellation_condition,
		&cancellation_condition_mutex, &cancellation_condition_started};
	std::uint64_t cancellation_condition_thread = 0;
	void* cancellation_condition_result = nullptr;
	const bool pthread_condition_cancellation_ok =
		darling_windows_pthread_cond_init(&cancellation_condition, nullptr) == 0 &&
		darling_windows_pthread_mutex_init(&cancellation_condition_mutex, nullptr) == 0 &&
		darling_windows_pthread_create(&cancellation_condition_thread, nullptr,
		&PthreadCancellationConditionStart, &cancellation_condition_context) == 0 &&
		([&] {
			for (int attempt = 0; attempt < 100 &&
				!cancellation_condition_started.load(std::memory_order_acquire); ++attempt)
				Sleep(1);
			return cancellation_condition_started.load(std::memory_order_acquire);
		}()) &&
		darling_windows_pthread_cancel(cancellation_condition_thread) == 0 &&
		darling_windows_pthread_join(cancellation_condition_thread,
		&cancellation_condition_result) == 0 &&
		cancellation_condition_result == reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1)) &&
		darling_windows_pthread_mutex_destroy(&cancellation_condition_mutex) == 0 &&
		darling_windows_pthread_cond_destroy(&cancellation_condition) == 0;
	void* pthread_attributes = nullptr;
	int pthread_detach_state = 0;
	std::size_t pthread_stack_size = 0;
	std::size_t pthread_guard_size = 0;
	std::uint64_t attribute_detached_thread = 0;
	const bool pthread_attributes_ok =
		darling_windows_host_symbol("pthread_attr_init") != 0 &&
		darling_windows_host_symbol("pthread_attr_destroy") != 0 &&
		darling_windows_host_symbol("pthread_attr_setdetachstate") != 0 &&
		darling_windows_host_symbol("pthread_attr_getdetachstate") != 0 &&
		darling_windows_host_symbol("pthread_attr_setstacksize") != 0 &&
		darling_windows_host_symbol("pthread_attr_getstacksize") != 0 &&
		darling_windows_host_symbol("pthread_attr_setguardsize") != 0 &&
		darling_windows_host_symbol("pthread_attr_getguardsize") != 0 &&
		darling_windows_pthread_attr_init(&pthread_attributes) == 0 &&
		darling_windows_pthread_attr_getdetachstate(&pthread_attributes,
		&pthread_detach_state) == 0 && pthread_detach_state == 0 &&
		darling_windows_pthread_attr_setdetachstate(&pthread_attributes, 1) == 0 &&
		darling_windows_pthread_attr_getdetachstate(&pthread_attributes,
		&pthread_detach_state) == 0 && pthread_detach_state == 1 &&
		darling_windows_pthread_attr_setstacksize(&pthread_attributes,
		256 * 1024) == 0 &&
		darling_windows_pthread_attr_getstacksize(&pthread_attributes,
		&pthread_stack_size) == 0 && pthread_stack_size == 256 * 1024 &&
		darling_windows_pthread_attr_getguardsize(&pthread_attributes,
		&pthread_guard_size) == 0 && pthread_guard_size == 4096 &&
		darling_windows_pthread_attr_setguardsize(&pthread_attributes, 8192) == 0 &&
		darling_windows_pthread_attr_getguardsize(&pthread_attributes,
		&pthread_guard_size) == 0 && pthread_guard_size == 8192 &&
		darling_windows_pthread_create(&attribute_detached_thread,
		&pthread_attributes, &PthreadAttrDetachedStart, nullptr) == 0 &&
		darling_windows_pthread_attr_destroy(&pthread_attributes) == 0 &&
		(std::this_thread::sleep_for(std::chrono::milliseconds(10)),
		pthread_attr_detached_calls == 1) &&
		darling_windows_pthread_join(attribute_detached_thread, nullptr) == 3;
	std::uint64_t detached_thread = 0;
	const bool pthread_detach_ok =
		darling_windows_host_symbol("pthread_detach") != 0 &&
		darling_windows_pthread_create(&detached_thread, nullptr,
			&PthreadSmokeStart, pthread_payload) == 0 &&
		darling_windows_pthread_detach(detached_thread) == 0 &&
		darling_windows_pthread_join(detached_thread, nullptr) == 3;
	std::uint64_t thread_id = 0;
	const bool pthread_threadid_ok =
		darling_windows_host_symbol("pthread_threadid_np") != 0 &&
		darling_windows_pthread_threadid_np(0, &thread_id) == 0 &&
		thread_id == pthread_self &&
		darling_windows_pthread_threadid_np(pthread_self + 1, &thread_id) == 22;
	void* pthread_mutex_storage = nullptr;
	const bool pthread_mutex_ok =
		darling_windows_host_symbol("pthread_mutex_init") != 0 &&
		darling_windows_host_symbol("pthread_mutex_lock") != 0 &&
		darling_windows_host_symbol("pthread_mutex_trylock") != 0 &&
		darling_windows_host_symbol("pthread_mutex_unlock") != 0 &&
		darling_windows_host_symbol("pthread_mutex_destroy") != 0 &&
		darling_windows_pthread_mutex_init(&pthread_mutex_storage, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&pthread_mutex_storage) == 0 &&
		darling_windows_pthread_mutex_trylock(&pthread_mutex_storage) == 16 &&
		darling_windows_pthread_mutex_unlock(&pthread_mutex_storage) == 0 &&
		darling_windows_pthread_mutex_destroy(&pthread_mutex_storage) == 0;
	void* pthread_rwlock_storage = nullptr;
	const bool pthread_rwlock_ok =
		darling_windows_host_symbol("pthread_rwlock_init") != 0 &&
		darling_windows_host_symbol("pthread_rwlock_rdlock") != 0 &&
		darling_windows_host_symbol("pthread_rwlock_wrlock") != 0 &&
		darling_windows_host_symbol("pthread_rwlock_unlock") != 0 &&
		darling_windows_host_symbol("pthread_rwlock_destroy") != 0 &&
		darling_windows_pthread_rwlock_init(&pthread_rwlock_storage, nullptr) == 0 &&
		darling_windows_pthread_rwlock_rdlock(&pthread_rwlock_storage) == 0 &&
		darling_windows_pthread_rwlock_unlock(&pthread_rwlock_storage) == 0 &&
		darling_windows_pthread_rwlock_wrlock(&pthread_rwlock_storage) == 0 &&
		darling_windows_pthread_rwlock_unlock(&pthread_rwlock_storage) == 0 &&
		darling_windows_pthread_rwlock_destroy(&pthread_rwlock_storage) == 0;
	std::uint64_t pthread_key = 0;
	const char pthread_tls_value[] = "tls-value";
	const bool pthread_tls_ok =
		darling_windows_host_symbol("pthread_key_create") != 0 &&
		darling_windows_host_symbol("pthread_key_delete") != 0 &&
		darling_windows_host_symbol("pthread_setspecific") != 0 &&
		darling_windows_host_symbol("pthread_getspecific") != 0 &&
		darling_windows_pthread_key_create(&pthread_key, nullptr) == 0 &&
		darling_windows_pthread_setspecific(pthread_key, pthread_tls_value) == 0 &&
		std::strcmp(static_cast<const char*>(darling_windows_pthread_getspecific(pthread_key)),
			pthread_tls_value) == 0 &&
		darling_windows_pthread_key_delete(pthread_key) == 0 &&
		darling_windows_pthread_getspecific(pthread_key) == nullptr;
	std::uint64_t destructor_key = 0;
	const char destructor_value[] = "destructor-value";
	TlsThreadContext tls_thread_context{0, destructor_value};
	std::uint64_t tls_thread = 0;
	const bool pthread_tls_destructor_ok =
		darling_windows_pthread_key_create(&destructor_key, &PthreadTlsDestructor) == 0 &&
		(tls_thread_context.key = destructor_key, true) &&
		darling_windows_pthread_create(&tls_thread, nullptr,
			&PthreadTlsThreadStart, &tls_thread_context) == 0 &&
		darling_windows_pthread_join(tls_thread, nullptr) == 0 &&
		tls_destructor_calls == 1 &&
		darling_windows_pthread_key_delete(destructor_key) == 0;
	void* pthread_once_control = nullptr;
	const bool pthread_once_ok =
		darling_windows_host_symbol("pthread_once") != 0 &&
		darling_windows_pthread_once(&pthread_once_control, &PthreadOnceInitializer) == 0 &&
		darling_windows_pthread_once(&pthread_once_control, &PthreadOnceInitializer) == 0 &&
		pthread_once_calls == 1;
	void* pthread_condition_storage = nullptr;
	void* pthread_condition_mutex_storage = nullptr;
	volatile long condition_ready = 0;
	CondSmokeContext condition_context{&pthread_condition_storage,
		&pthread_condition_mutex_storage, &condition_ready};
	std::uint64_t condition_thread = 0;
	const bool pthread_condition_ok =
		darling_windows_host_symbol("pthread_cond_init") != 0 &&
		darling_windows_host_symbol("pthread_cond_wait") != 0 &&
		darling_windows_host_symbol("pthread_cond_signal") != 0 &&
		darling_windows_host_symbol("pthread_cond_broadcast") != 0 &&
		darling_windows_host_symbol("pthread_cond_destroy") != 0 &&
		darling_windows_pthread_cond_init(&pthread_condition_storage, nullptr) == 0 &&
		darling_windows_pthread_mutex_init(&pthread_condition_mutex_storage, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&pthread_condition_mutex_storage) == 0 &&
		darling_windows_pthread_create(&condition_thread, nullptr,
			&PthreadConditionSmokeStart, &condition_context) == 0 &&
		darling_windows_pthread_cond_wait(&pthread_condition_storage,
			&pthread_condition_mutex_storage) == 0 && condition_ready == 1 &&
		darling_windows_pthread_mutex_unlock(&pthread_condition_mutex_storage) == 0 &&
		darling_windows_pthread_join(condition_thread, nullptr) == 0 &&
		darling_windows_pthread_mutex_destroy(&pthread_condition_mutex_storage) == 0 &&
		darling_windows_pthread_cond_destroy(&pthread_condition_storage) == 0;
	void* timed_condition_storage = nullptr;
	void* timed_mutex_storage = nullptr;
	const auto timed_now = std::chrono::system_clock::now().time_since_epoch() +
		std::chrono::milliseconds(20);
	const auto timed_seconds = std::chrono::duration_cast<std::chrono::seconds>(timed_now);
	const auto timed_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
		timed_now - timed_seconds);
	darling_timespec timed_deadline{timed_seconds.count(), timed_nanos.count()};
	const bool pthread_timedwait_ok =
		darling_windows_host_symbol("pthread_cond_timedwait") != 0 &&
		darling_windows_pthread_cond_init(&timed_condition_storage, nullptr) == 0 &&
		darling_windows_pthread_mutex_init(&timed_mutex_storage, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&timed_mutex_storage) == 0 &&
		darling_windows_pthread_cond_timedwait(&timed_condition_storage,
			&timed_mutex_storage, &timed_deadline) == 110 &&
		darling_windows_pthread_mutex_unlock(&timed_mutex_storage) == 0 &&
		darling_windows_pthread_mutex_destroy(&timed_mutex_storage) == 0 &&
		darling_windows_pthread_cond_destroy(&timed_condition_storage) == 0;
	std::cout << "DARWIN_PTHREAD_SELF_NAME_EQUAL="
		          << (pthread_abi_ok && pthread_lifecycle_ok && pthread_cancellation_ok && pthread_lock_cancellation_ok && pthread_condition_cancellation_ok && pthread_attributes_ok && pthread_detach_ok &&
			pthread_threadid_ok && pthread_mutex_ok && pthread_rwlock_ok && pthread_tls_ok &&
			pthread_tls_destructor_ok && pthread_once_ok && pthread_condition_ok &&
			pthread_timedwait_ok ? "PASS" : "FAIL") << "\n";
	const bool normalized_terminal_environment_symbols =
		darling_windows_host_symbol("getenv") != 0 &&
		darling_windows_host_symbol("setenv") != 0 &&
		darling_windows_host_symbol("unsetenv") != 0 &&
		darling_windows_host_symbol("putenv") != 0 &&
		darling_windows_host_symbol("clearenv") != 0 &&
		darling_windows_host_symbol("isatty") != 0 &&
		darling_windows_host_symbol("ioctl") != 0 &&
		darling_windows_host_symbol("ctermid") != 0;
	std::cout << "DARWIN_NORMALIZED_TERMINAL_ENV_SYMBOLS="
	          << (normalized_terminal_environment_symbols ? "PASS" : "FAIL") << "\n";
	int datagram_server = darling_windows_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int datagram_client = -1;
	sockaddr_in datagram_address{};
	datagram_address.sin_family = AF_INET;
	datagram_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	datagram_address.sin_port = htons(0);
	bool datagram_ok = datagram_server >= 0 &&
		darling_windows_bind(datagram_server, &datagram_address, sizeof(datagram_address)) == 0;
	int datagram_address_length = sizeof(datagram_address);
	datagram_ok = datagram_ok && darling_windows_getsockname(
		datagram_server, &datagram_address, &datagram_address_length) == 0;
	if (datagram_ok) {
		datagram_client = darling_windows_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		datagram_ok = datagram_client >= 0;
	}
	const char datagram_payload[] = "DGRAM";
	char datagram_buffer[sizeof(datagram_payload)]{};
	sockaddr_in datagram_sender{};
	int datagram_sender_length = sizeof(datagram_sender);
	datagram_ok = datagram_ok && darling_windows_sendto(datagram_client, datagram_payload,
		sizeof(datagram_payload) - 1, 0, &datagram_address, sizeof(datagram_address)) == 5 &&
		darling_windows_recvfrom(datagram_server, datagram_buffer, sizeof(datagram_payload) - 1,
		0, &datagram_sender, &datagram_sender_length) == 5 &&
		std::memcmp(datagram_buffer, datagram_payload, sizeof(datagram_payload) - 1) == 0 &&
		darling_windows_host_symbol("_sendto") != 0 &&
		darling_windows_host_symbol("_recvfrom") != 0;
	if (datagram_client >= 0) darling_windows_close(datagram_client);
	if (datagram_server >= 0) darling_windows_close(datagram_server);
	int socket_pair[2]{-1, -1};
	const char socket_pair_payload[] = "PAIR";
	char socket_pair_buffer[sizeof(socket_pair_payload)]{};
	const bool socket_pair_ok = darling_windows_socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == 0 &&
		darling_windows_write(socket_pair[0], socket_pair_payload,
			sizeof(socket_pair_payload) - 1) == 4 &&
		darling_windows_read(socket_pair[1], socket_pair_buffer,
			sizeof(socket_pair_payload) - 1) == 4 &&
		std::memcmp(socket_pair_buffer, socket_pair_payload,
			sizeof(socket_pair_payload) - 1) == 0 &&
		darling_windows_host_symbol("_socketpair") != 0;
	char message_left[] = "MSG";
	char message_right[] = "!";
	darling_iovec message_send_vectors[] = {
		{message_left, sizeof(message_left) - 1}, {message_right, sizeof(message_right) - 1}};
	darling_msghdr message_send{};
	message_send.msg_iov = message_send_vectors;
	message_send.msg_iovlen = 2;
	char message_read_left[sizeof(message_left)]{};
	char message_read_right[sizeof(message_right)]{};
	darling_iovec message_read_vectors[] = {
		{message_read_left, sizeof(message_left) - 1},
		{message_read_right, sizeof(message_right) - 1}};
	darling_msghdr message_read{};
	message_read.msg_iov = message_read_vectors;
	message_read.msg_iovlen = 2;
	const bool message_ok = socket_pair_ok &&
		darling_windows_sendmsg(socket_pair[0], &message_send, 0) == 4 &&
		darling_windows_recvmsg(socket_pair[1], &message_read, 0) == 4 &&
		std::memcmp(message_read_left, message_left, sizeof(message_left) - 1) == 0 &&
		std::memcmp(message_read_right, message_right, sizeof(message_right) - 1) == 0 &&
		darling_windows_host_symbol("_sendmsg") != 0 &&
		darling_windows_host_symbol("_recvmsg") != 0;
	char dontwait_buffer[2]{};
	const bool dontwait_ok = socket_pair_ok &&
		darling_windows_recvfrom(socket_pair[1], dontwait_buffer, sizeof(dontwait_buffer),
		0x0080, nullptr, nullptr) == -1 && darling_windows_errno() != nullptr &&
		*darling_windows_errno() == 35;
	int rights_pipe[2]{-1, -1};
	const char rights_payload[] = "RIGHT";
	const bool rights_pipe_ok = darling_windows_pipe(rights_pipe) == 0 &&
		darling_windows_write(rights_pipe[1], rights_payload, sizeof(rights_payload) - 1) == 5;
	unsigned char rights_send_control[64]{};
	const darling_cmsghdr rights_send_header{
		static_cast<std::uint32_t>(sizeof(darling_cmsghdr) + sizeof(int)), 0xffff, 1};
	std::memcpy(rights_send_control, &rights_send_header, sizeof(rights_send_header));
	std::memcpy(rights_send_control + sizeof(rights_send_header), &rights_pipe[0], sizeof(int));
	char rights_message_value = 'R';
	darling_iovec rights_send_vector{&rights_message_value, 1};
	darling_msghdr rights_send_message{};
	rights_send_message.msg_iov = &rights_send_vector;
	rights_send_message.msg_iovlen = 1;
	rights_send_message.msg_control = rights_send_control;
	rights_send_message.msg_controllen = sizeof(rights_send_header) + sizeof(int);
	unsigned char rights_receive_control[64]{};
	char rights_receive_value = 0;
	darling_iovec rights_receive_vector{&rights_receive_value, 1};
	darling_msghdr rights_receive_message{};
	rights_receive_message.msg_iov = &rights_receive_vector;
	rights_receive_message.msg_iovlen = 1;
	rights_receive_message.msg_control = rights_receive_control;
	rights_receive_message.msg_controllen = sizeof(rights_receive_control);
	const bool rights_send_ok = rights_pipe_ok && socket_pair_ok && dontwait_ok &&
		darling_windows_sendmsg(socket_pair[0], &rights_send_message, 0) == 1;
	const auto* rights_receive_header = reinterpret_cast<const darling_cmsghdr*>(rights_receive_control);
	int received_right = -1;
	if (rights_send_ok && darling_windows_recvmsg(socket_pair[1], &rights_receive_message, 0) == 1 &&
		rights_receive_value == 'R' && rights_receive_header->cmsg_len >=
		sizeof(darling_cmsghdr) + sizeof(int)) {
		std::memcpy(&received_right, rights_receive_control + sizeof(darling_cmsghdr), sizeof(int));
	}
	char rights_readback[sizeof(rights_payload)]{};
	const bool rights_ok = rights_send_ok && received_right >= 0 &&
		darling_windows_read(received_right, rights_readback, sizeof(rights_payload) - 1) == 5 &&
		std::memcmp(rights_readback, rights_payload, sizeof(rights_payload) - 1) == 0;
	if (received_right >= 0) darling_windows_close(received_right);
	if (rights_pipe[0] >= 0) darling_windows_close(rights_pipe[0]);
	if (rights_pipe[1] >= 0) darling_windows_close(rights_pipe[1]);
	if (socket_pair[0] >= 0) darling_windows_close(socket_pair[0]);
	if (socket_pair[1] >= 0) darling_windows_close(socket_pair[1]);
	if (socket_duplicate >= 0) darling_windows_close(socket_duplicate);
	if (socket_peer >= 0) darling_windows_close(socket_peer);
	if (socket_client >= 0) darling_windows_close(socket_client);
	if (socket_listener >= 0) darling_windows_close(socket_listener);
	char pipe_buffer[sizeof(pipe_payload)]{};
	const bool pipe_read_ok = pipe_ok &&
		darling_windows_read(pipe_descriptors[0], pipe_buffer, sizeof(pipe_payload) - 1) == 4 &&
		std::memcmp(pipe_buffer, pipe_payload, sizeof(pipe_payload) - 1) == 0;
	const bool fcntl_ok = darling_windows_fcntl(pipe_descriptors[0], 1) == 0 &&
		darling_windows_fcntl(pipe_descriptors[0], 3) == 0 &&
		darling_windows_fcntl(pipe_descriptors[0], 4, 0) == 0 &&
		darling_windows_fcntl(pipe_descriptors[0], 4, 0x0004) == 0;
	const int fcntl_duplicate = darling_windows_fcntl(pipe_descriptors[0], 0, 20);
	const bool fcntl_duplicate_ok = fcntl_duplicate >= 20;
	if (fcntl_duplicate >= 0) darling_windows_close(fcntl_duplicate);
	if (pipe_descriptors[0] >= 0) darling_windows_close(pipe_descriptors[0]);
	if (pipe_descriptors[1] >= 0) darling_windows_close(pipe_descriptors[1]);
	int vector_pipe[2]{-1, -1};
	const char vector_left[] = "LEFT";
	const char vector_right[] = "RIGHT";
	const darling_iovec write_vectors[] = {
		{const_cast<char*>(vector_left), sizeof(vector_left) - 1},
		{const_cast<char*>(vector_right), sizeof(vector_right) - 1}};
	char vector_left_read[sizeof(vector_left)]{};
	char vector_right_read[sizeof(vector_right)]{};
	const darling_iovec read_vectors[] = {
		{vector_left_read, sizeof(vector_left) - 1},
		{vector_right_read, sizeof(vector_right) - 1}};
	const bool vector_ok = darling_windows_pipe(vector_pipe) == 0 &&
		darling_windows_writev(vector_pipe[1], write_vectors, 2) == 9 &&
		darling_windows_readv(vector_pipe[0], read_vectors, 2) == 9 &&
		std::memcmp(vector_left_read, vector_left, sizeof(vector_left) - 1) == 0 &&
		std::memcmp(vector_right_read, vector_right, sizeof(vector_right) - 1) == 0;
	if (vector_pipe[0] >= 0) darling_windows_close(vector_pipe[0]);
	if (vector_pipe[1] >= 0) darling_windows_close(vector_pipe[1]);
	int pipe2_descriptors[2]{-1, -1};
	const bool pipe2_ok = darling_windows_pipe2(pipe2_descriptors,
		0x0004 | 0x01000000) == 0 &&
		(darling_windows_fcntl(pipe2_descriptors[0], 3) & 0x0004) != 0 &&
		(darling_windows_fcntl(pipe2_descriptors[1], 3) & 0x0004) != 0 &&
		darling_windows_fcntl(pipe2_descriptors[0], 1) == 1 &&
		darling_windows_fcntl(pipe2_descriptors[1], 1) == 1 &&
		darling_windows_host_symbol("_pipe2") != 0;
	if (pipe2_descriptors[0] >= 0) darling_windows_close(pipe2_descriptors[0]);
	if (pipe2_descriptors[1] >= 0) darling_windows_close(pipe2_descriptors[1]);
	char cwd[4096]{};
	const bool cwd_ok = darling_windows_getcwd(cwd, sizeof(cwd)) != nullptr;
	char resolved_cwd[4096]{};
	const bool realpath_ok = cwd_ok && darling_windows_realpath(cwd, resolved_cwd) != nullptr &&
		std::strlen(resolved_cwd) != 0;
	const std::string directory_path = std::string(temporary_path) + "darling-host-api-dir";
	const bool directory_ok = darling_windows_mkdir(directory_path.c_str(), 0700) == 0 &&
		darling_windows_chdir(directory_path.c_str()) == 0 &&
		darling_windows_chdir(cwd) == 0 &&
		RemoveDirectoryA(directory_path.c_str()) != 0;
	SetEnvironmentVariableA("DARLING_HOST_API_CLEARENV", "present");
	const bool clearenv_ok = umask_ok && loadavg_ok && page_sizes_ok && process_group_ok && thread_local_session_ok && identity_triplet_ok && normalized_identity_symbols_ok && supplementary_groups_ok && identity_mutation_ok && darling_windows_host_symbol("_clearenv") != 0 &&
		darling_windows_clearenv() == 0 &&
		darling_windows_getenv("DARLING_HOST_API_CLEARENV") == nullptr;
	if (!pid_ok || !exit_symbol_ok || !parent_pid_ok || !waitpid_ok || !identity_ok || !login_ok || !executable_path_ok || !program_name_ok || !usage_ok || !times_ok || !dynamic_loader_ok || !dladdr_ok || !dyld_images_ok || !page_size_ok || !sysconf_ok || !confstr_ok || !pathconf_ok || !fpathconf_ok || !descriptor_limit_ok || !rlimit_ok || !priority_ok || !sysctl_ok || !sysctl_mib_ok || !scheduling_ok || !random_ok || !entropy_ok || !explicit_zero_ok || !kill_ok || !raise_ok || !hostname_ok || !domainname_ok || !terminal_names_ok || !directory_entries_ok || !uname_ok || !clock_ok || !mach_time_ok || !clock_resolution_ok || !sleep_ok || !wall_time_ok || !environment_ok || !environment_mutation_ok || !putenv_ok || !environ_ok || !nsget_environ_ok || !clearenv_ok || !errno_roundtrip_ok || !cwd_ok || !realpath_ok || !directory_ok || !pipe_read_ok || !poll_ok || !ignored_poll_ok || !select_ok || !standard_select_ok || !invalid_select_timeout_ok || !pselect_ok || !socket_api_ok || !datagram_ok || !socket_pair_ok || !message_ok || !rights_ok || !pipe2_ok || !mkstemp_ok || !mkstemps_ok || !mkdtemp_ok || !vector_ok || !fcntl_ok || !fcntl_duplicate_ok || !strerror_ok || !strerror_r_ok || !memory_overlap_ok || !bsd_memory_ok || !duplicate_string_ok || !calloc_ok || !realloc_ok || !parsing_ok || !floating_parsing_ok || !formatting_ok || !string_helpers_ok || !string_search_ok || !memory_search_ok || !case_insensitive_ok || !string_append_ok || !bounded_string_ok || !tokenize_ok || darling_windows_isatty(99) != 0 ||
		!allocation_ok || !string_ok || !file_ok) {
		darling_windows_free(allocation);
		if (!file_rename_ok)
			std::cerr << "FILE_RENAME_RESULT=" << rename_result <<
				" WIN32_ERROR=" << rename_error << "\n";
		std::cerr << "HOST_API_SMOKE_ERROR=" << (!pid_ok ? "pid" : !exit_symbol_ok ? "exit_symbol" : !parent_pid_ok ? "ppid" : !waitpid_ok ? "waitpid" : !identity_ok ? "identity" : !login_ok ? "login" : !executable_path_ok ? "NSGetExecutablePath" : !program_name_ok ? "progname" : !usage_ok ? "getrusage" : !times_ok ? "times" : !dynamic_loader_ok ? "dynamic_loader" : !dladdr_ok ? "dladdr" : !dyld_images_ok ? "dyld_images" : !page_size_ok ? "getpagesize" : !sysconf_ok ? "sysconf" : !confstr_ok ? "confstr" : !pathconf_ok ? "pathconf" : !fpathconf_ok ? "fpathconf" : !scheduling_ok ? "scheduling" : !random_ok ? "random" : !entropy_ok ? "getentropy" : !explicit_zero_ok ? "explicit_bzero" : !kill_ok ? "kill" : !raise_ok ? "raise" : !hostname_ok ? "hostname" : !uname_ok ? "uname" : !clock_resolution_ok ? "clock_getres" :
			!clock_ok ? "clock" : !mach_time_ok ? "mach_time" : !sleep_ok ? "nanosleep" : !wall_time_ok ? "gettimeofday" : !sysctl_ok ? "sysctlbyname" : !sysctl_mib_ok ? "sysctl" : !environment_ok ? "environment" :
			!environment_mutation_ok ? "environment_mutation" : !putenv_ok ? "putenv" : !environ_ok ? "environ" : !nsget_environ_ok ? "NSGetEnviron" : !executable_path_ok ? "NSGetExecutablePath" : !clearenv_ok ? "clearenv" : !realpath_ok ? "realpath" :
			!file_values_ok ? "file_values" : !file_access_ok ? "file_access" :
			!file_rename_ok ? "file_rename" : !file_remove_ok ? "file_remove" :
			!pipe_read_ok ? "pipe" : !pipe2_ok ? "pipe2" : !mkstemp_ok ? "mkstemp" : !mkstemps_ok ? "mkstemps" : !mkdtemp_ok ? "mkdtemp" :
			!vector_ok ? "vector_io" :
			!fcntl_ok ? "fcntl" :
			!fcntl_duplicate_ok ? "fcntl_dup" : !strerror_ok ? "strerror" : !strerror_r_ok ? "strerror_r" : !memory_overlap_ok ? "memmove" : !bsd_memory_ok ? "bsd_memory" : !duplicate_string_ok ? "strdup" : !calloc_ok ? "calloc" : !realloc_ok ? "realloc" : !parsing_ok ? "strto" : !floating_parsing_ok ? "strto_float" : !formatting_ok ? "snprintf" : !dynamic_format_ok ? "asprintf" : !string_helpers_ok ? "string_helpers" : !string_search_ok ? "string_search" : !memory_search_ok ? "memory_search" : !case_insensitive_ok ? "strcasecmp" : !string_append_ok ? "string_append" : !bounded_copy_ok ? "strlcpy" : !bounded_string_ok ? "strlcat" : !tokenize_ok ? "tokenize" :
			!allocation_ok ? "allocation" : !string_ok ? "string" : "file") << "\n";
		return 1;
	}
	darling_windows_free(allocation);
	std::cout << "DARWIN_HOST_API=PASS\n";
	return 0;
}
