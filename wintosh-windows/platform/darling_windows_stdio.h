/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <cstddef>
#include <cstdarg>
#include <cstdint>

#include "darling_windows_syscalls.h"
#include "darling_windows_signals.h"

struct darling_timespec final {
	std::int64_t tv_sec = 0;
	std::int64_t tv_nsec = 0;
};

struct darling_mach_timebase_info final {
	std::uint32_t numer = 0;
	std::uint32_t denom = 0;
};

struct darling_timeval final {
	std::int64_t tv_sec = 0;
	std::int64_t tv_usec = 0;
};

struct darling_rusage final {
	darling_timeval ru_utime{};
	darling_timeval ru_stime{};
	std::int64_t ru_maxrss = 0;
	std::int64_t ru_ixrss = 0;
	std::int64_t ru_idrss = 0;
	std::int64_t ru_isrss = 0;
	std::int64_t ru_minflt = 0;
	std::int64_t ru_majflt = 0;
	std::int64_t ru_nswap = 0;
	std::int64_t ru_inblock = 0;
	std::int64_t ru_oublock = 0;
	std::int64_t ru_msgsnd = 0;
	std::int64_t ru_msgrcv = 0;
	std::int64_t ru_nsignals = 0;
	std::int64_t ru_nvcsw = 0;
	std::int64_t ru_nivcsw = 0;
};

struct darling_rlimit final {
	std::uint64_t rlim_cur;
	std::uint64_t rlim_max;
};

struct darling_dl_info final {
	const char* dli_fname = nullptr;
	void* dli_fbase = nullptr;
	const char* dli_sname = nullptr;
	void* dli_saddr = nullptr;
};

struct darling_siginfo final {
	int si_signo = 0;
	int si_errno = 0;
	int si_code = 0;
	std::int32_t si_pid = 0;
	std::uint32_t si_uid = 0;
	int si_status = 0;
	std::uintptr_t si_addr = 0;
	union {
		int si_value_int;
		std::uintptr_t si_value_ptr;
	} si_value{};
	std::intptr_t si_band = 0;
	std::uintptr_t si_pad[7]{};
};

struct darling_tms final {
	std::int64_t tms_utime = 0;
	std::int64_t tms_stime = 0;
	std::int64_t tms_cutime = 0;
	std::int64_t tms_cstime = 0;
};

struct darling_utsname final {
	char sysname[256]{};
	char nodename[256]{};
	char release[256]{};
	char version[256]{};
	char machine[256]{};
};

struct darling_iovec final {
	void* iov_base = nullptr;
	std::size_t iov_len = 0;
};

struct darling_fd_set final {
	std::int32_t fds_bits[32]{};
};

struct darling_msghdr final {
	void* msg_name = nullptr;
	std::uint32_t msg_namelen = 0;
	darling_iovec* msg_iov = nullptr;
	int msg_iovlen = 0;
	void* msg_control = nullptr;
	std::uint32_t msg_controllen = 0;
	int msg_flags = 0;
};

struct darling_cmsghdr final {
	std::uint32_t cmsg_len = 0;
	int cmsg_level = 0;
	int cmsg_type = 0;
};

struct darling_addrinfo final {
	int ai_flags = 0;
	int ai_family = 0;
	int ai_socktype = 0;
	int ai_protocol = 0;
	std::uint32_t ai_addrlen = 0;
	char* ai_canonname = nullptr;
	void* ai_addr = nullptr;
	darling_addrinfo* ai_next = nullptr;
};

struct darling_darwin_stat final {
	std::uint64_t st_dev = 0;
	std::uint16_t st_mode = 0;
	std::uint16_t st_nlink = 0;
	std::uint64_t st_ino = 0;
	std::uint32_t st_uid = 0;
	std::uint32_t st_gid = 0;
	std::uint64_t st_rdev = 0;
	darling_timespec st_atimespec{};
	darling_timespec st_mtimespec{};
	darling_timespec st_ctimespec{};
	darling_timespec st_birthtimespec{};
	std::int64_t st_size = 0;
	std::int64_t st_blocks = 0;
	std::int32_t st_blksize = 4096;
	std::uint32_t st_flags = 0;
	std::uint32_t st_gen = 0;
	std::int32_t st_lspare = 0;
	std::int64_t st_qspare[2]{};
};

struct darling_darwin_statfs final {
	std::int32_t f_bsize = 0;
	std::int32_t f_iosize = 0;
	std::uint64_t f_blocks = 0;
	std::uint64_t f_bfree = 0;
	std::uint64_t f_bavail = 0;
	std::uint64_t f_files = 0;
	std::uint64_t f_ffree = 0;
	std::uint64_t f_fsid = 0;
	std::uint32_t f_owner = 0;
	std::uint32_t f_type = 0;
	std::uint32_t f_flags = 0;
	std::uint32_t f_fssubtype = 0;
	char f_fstypename[16]{};
	char f_mntonname[1024]{};
	char f_mntfromname[1024]{};
	std::uint32_t f_reserved[8]{};
};

extern "C" int darling_windows_write_stdout(const void* data, std::size_t bytes);
extern "C" int darling_windows_read(int descriptor, void* data, std::size_t bytes);
extern "C" int darling_windows_write(int descriptor, const void* data, std::size_t bytes);
extern "C" std::int64_t darling_windows_pread(int descriptor, void* data,
	std::size_t bytes, std::int64_t offset);
extern "C" std::int64_t darling_windows_pwrite(int descriptor, const void* data,
	std::size_t bytes, std::int64_t offset);
extern "C" std::int64_t darling_windows_preadv(int descriptor,
	const darling_iovec* vectors, int count, std::int64_t offset);
extern "C" std::int64_t darling_windows_pwritev(int descriptor,
	const darling_iovec* vectors, int count, std::int64_t offset);
extern "C" int darling_windows_flock(int descriptor, int operation);
extern "C" int darling_windows_fsync(int descriptor);
extern "C" int darling_windows_fdatasync(int descriptor);
extern "C" std::int64_t darling_windows_readv(int descriptor, const darling_iovec* vectors, int count);
extern "C" std::int64_t darling_windows_writev(int descriptor, const darling_iovec* vectors, int count);
extern "C" int darling_windows_close(int descriptor);
extern "C" int darling_windows_fcntl(int descriptor, int command, ...);
extern "C" int darling_windows_dup(int descriptor);
extern "C" int darling_windows_dup2(int descriptor, int target);
extern "C" int darling_windows_pipe(int descriptors[2]);
extern "C" int darling_windows_pipe2(int descriptors[2], int flags);
extern "C" int darling_windows_poll(darling::windows_host::darling_pollfd* descriptors,
	std::size_t count, int timeout_ms);
extern "C" int darling_windows_socket(int domain, int type, int protocol);
extern "C" int darling_windows_socketpair(int domain, int type, int protocol, int descriptors[2]);
extern "C" int darling_windows_bind(int descriptor, const void* address, int address_length);
extern "C" int darling_windows_connect(int descriptor, const void* address, int address_length);
extern "C" int darling_windows_listen(int descriptor, int backlog);
extern "C" int darling_windows_accept(int descriptor, void* address, int* address_length);
extern "C" int darling_windows_getsockname(int descriptor, void* address, int* address_length);
extern "C" int darling_windows_getpeername(int descriptor, void* address, int* address_length);
extern "C" int darling_windows_getaddrinfo(const char* node, const char* service,
	const darling_addrinfo* hints, darling_addrinfo** result);
extern "C" void darling_windows_freeaddrinfo(darling_addrinfo* result);
extern "C" int darling_windows_getnameinfo(const void* address, int address_length,
	char* host, std::size_t host_length, char* service, std::size_t service_length, int flags);
extern "C" int darling_windows_inet_pton(int family, const char* source, void* destination);
extern "C" const char* darling_windows_inet_ntop(int family, const void* source,
	char* destination, std::size_t destination_length);
extern "C" int darling_windows_select(int nfds, darling_fd_set* read_set,
	darling_fd_set* write_set, darling_fd_set* exception_set, darling_timeval* timeout);
extern "C" int darling_windows_pselect(int nfds, darling_fd_set* read_set,
	darling_fd_set* write_set, darling_fd_set* exception_set,
	const darling_timespec* timeout, const darling_darwin_sigset* signal_mask);
extern "C" int darling_windows_getsockopt(int descriptor, int level, int option, void* value,
	int* value_length);
extern "C" int darling_windows_setsockopt(int descriptor, int level, int option,
	const void* value, int value_length);
extern "C" int darling_windows_sendto(int descriptor, const void* buffer, std::size_t bytes,
	int flags, const void* address, int address_length);
extern "C" int darling_windows_recvfrom(int descriptor, void* buffer, std::size_t bytes,
	int flags, void* address, int* address_length);
extern "C" std::int64_t darling_windows_sendmsg(int descriptor, const darling_msghdr* message,
	int flags);
extern "C" std::int64_t darling_windows_recvmsg(int descriptor, darling_msghdr* message,
	int flags);
extern "C" int darling_windows_shutdown(int descriptor, int how);
extern "C" int darling_windows_open(const char* path, int flags, int mode);
extern "C" int darling_windows_mkstemp(char* path_template);
extern "C" int darling_windows_mkstemps(char* path_template, int suffix_length);
extern "C" char* darling_windows_mkdtemp(char* path_template);
extern "C" int darling_windows_unlink(const char* path);
extern "C" int darling_windows_rmdir(const char* path);
extern "C" int darling_windows_link(const char* existing_path, const char* link_path);
extern "C" int darling_windows_symlink(const char* target, const char* link_path);
extern "C" std::int64_t darling_windows_readlink(const char* path, char* buffer,
	std::size_t size);
extern "C" int darling_windows_access(const char* path, int mode);
extern "C" int darling_windows_rename(const char* old_path, const char* new_path);
extern "C" std::int64_t darling_windows_lseek(int descriptor, std::int64_t offset, int whence);
extern "C" int darling_windows_ftruncate(int descriptor, std::int64_t length);
extern "C" int darling_windows_truncate(const char* path, std::int64_t length);
extern "C" int darling_windows_chmod(const char* path, int mode);
extern "C" int darling_windows_fchmod(int descriptor, int mode);
extern "C" int darling_windows_umask(int mode);
extern "C" int darling_windows_getloadavg(double* loads, int count);
extern "C" int darling_windows_utimensat(int directory_descriptor, const char* path,
	const darling_timespec times[2], int flags);
extern "C" int darling_windows_futimens(int descriptor, const darling_timespec times[2]);
extern "C" int darling_windows_chdir(const char* path);
extern "C" char* darling_windows_getcwd(char* buffer, std::size_t size);
extern "C" char* darling_windows_realpath(const char* path, char* resolved);
extern "C" int darling_windows_mkdir(const char* path, int mode);
struct darling_dirent final {
	std::uint64_t d_ino = 0;
	std::int64_t d_seekoff = 0;
	std::uint16_t d_reclen = 0;
	std::uint16_t d_namlen = 0;
	std::uint8_t d_type = 0;
	char d_name[256]{};
};
extern "C" std::int64_t darling_windows_getdirentries(int descriptor, char* buffer,
	std::size_t buffer_size, std::int64_t* base);
extern "C" std::int64_t darling_windows_getdirentries64(int descriptor, char* buffer,
	std::size_t buffer_size, std::int64_t* base);
extern "C" int darling_windows_stat(const char* path, darling_darwin_stat* result);
extern "C" int darling_windows_lstat(const char* path, darling_darwin_stat* result);
extern "C" int darling_windows_fstat(int descriptor, darling_darwin_stat* result);
extern "C" int darling_windows_fstatat(int directory_descriptor, const char* path,
	darling_darwin_stat* result, int flags);
extern "C" int darling_windows_statfs(const char* path, darling_darwin_statfs* result);
extern "C" int darling_windows_fstatfs(int descriptor, darling_darwin_statfs* result);
extern "C" int darling_windows_getfsstat(darling_darwin_statfs* buffer,
	int buffer_size, int flags);
extern "C" std::uintptr_t darling_windows_host_symbol(const char* name);
extern "C" int* darling_windows_errno();
extern "C" [[noreturn]] void darling_windows_exit(int status);
extern "C" [[noreturn]] void darling_windows__exit(int status);
extern "C" int darling_windows_atexit(void (*function)());
extern "C" int darling_windows_cxa_atexit(void (*function)(void*), void* argument,
	void* dso_handle);
extern "C" void darling_windows_cxa_finalize(void* dso_handle);
extern "C" int darling_windows_getpid();
extern "C" std::uint64_t darling_windows_pthread_self();
extern "C" int darling_windows_pthread_equal(std::uint64_t left, std::uint64_t right);
extern "C" int darling_windows_pthread_setname_np(const char* name);
extern "C" int darling_windows_pthread_getname_np(std::uint64_t thread,
	char* buffer, std::size_t size);
extern "C" int darling_windows_pthread_create(std::uint64_t* thread, const void* attributes,
	void* (*start)(void*), void* argument);
extern "C" int darling_windows_pthread_join(std::uint64_t thread, void** result);
extern "C" int darling_windows_pthread_detach(std::uint64_t thread);
extern "C" int darling_windows_pthread_cancel(std::uint64_t thread);
extern "C" int darling_windows_pthread_setcancelstate(int state, int* old_state);
extern "C" int darling_windows_pthread_setcanceltype(int type, int* old_type);
extern "C" int darling_windows_pthread_testcancel();

struct darling_pthread_cleanup_record final {
	void (*routine)(void*) = nullptr;
	void* argument = nullptr;
	darling_pthread_cleanup_record* next = nullptr;
};
extern "C" void darling_windows_pthread_cleanup_push(
	darling_pthread_cleanup_record* record, void (*routine)(void*), void* argument);
extern "C" void darling_windows_pthread_cleanup_pop(
	darling_pthread_cleanup_record* record, int execute);

/* PureDarwin exposes these as matched macros. Keep this compatibility layer
 * opt-in so the adapter does not override a consumer's pthread header. */
#if defined(DARLING_WINDOWS_PTHREAD_COMPAT_MACROS) && \
	!defined(pthread_cleanup_push) && !defined(pthread_cleanup_pop)
#define pthread_cleanup_push(func, val) \
	{ \
		darling_pthread_cleanup_record __darling_cleanup_record{}; \
		darling_windows_pthread_cleanup_push(&__darling_cleanup_record, (func), (val));
#define pthread_cleanup_pop(execute) \
		darling_windows_pthread_cleanup_pop(&__darling_cleanup_record, (execute)); \
	}
#endif
extern "C" int darling_windows_pthread_threadid_np(std::uint64_t thread,
	std::uint64_t* thread_id);
extern "C" int darling_windows_pthread_attr_init(void* attributes);
extern "C" int darling_windows_pthread_attr_destroy(void* attributes);
extern "C" int darling_windows_pthread_attr_setdetachstate(void* attributes, int state);
extern "C" int darling_windows_pthread_attr_getdetachstate(const void* attributes, int* state);
extern "C" int darling_windows_pthread_attr_setinheritsched(void* attributes, int state);
extern "C" int darling_windows_pthread_attr_getinheritsched(const void* attributes, int* state);
extern "C" int darling_windows_pthread_attr_setscope(void* attributes, int scope);
extern "C" int darling_windows_pthread_attr_getscope(const void* attributes, int* scope);
extern "C" int darling_windows_pthread_attr_setstackaddr(void* attributes, void* stack);
extern "C" int darling_windows_pthread_attr_getstackaddr(const void* attributes, void** stack);
extern "C" int darling_windows_pthread_attr_setstacksize(void* attributes, std::size_t size);
extern "C" int darling_windows_pthread_attr_getstacksize(const void* attributes, std::size_t* size);
extern "C" int darling_windows_pthread_attr_setguardsize(void* attributes, std::size_t size);
extern "C" int darling_windows_pthread_attr_getguardsize(const void* attributes, std::size_t* size);
extern "C" int darling_windows_pthread_mutex_init(void* mutex, const void* attributes);
extern "C" int darling_windows_pthread_mutex_destroy(void* mutex);
extern "C" int darling_windows_pthread_mutex_lock(void* mutex);
extern "C" int darling_windows_pthread_mutex_trylock(void* mutex);
extern "C" int darling_windows_pthread_mutex_timedlock(void* mutex, const darling_timespec* deadline);
extern "C" int darling_windows_pthread_mutex_unlock(void* mutex);
extern "C" int darling_windows_pthread_mutex_getprioceiling(void* mutex, int* ceiling);
extern "C" int darling_windows_pthread_mutex_setprioceiling(void* mutex, int ceiling);
extern "C" int darling_windows_pthread_spin_init(void* lock, int process_shared);
extern "C" int darling_windows_pthread_spin_destroy(void* lock);
extern "C" int darling_windows_pthread_spin_lock(void* lock);
extern "C" int darling_windows_pthread_spin_trylock(void* lock);
extern "C" int darling_windows_pthread_spin_unlock(void* lock);
extern "C" int darling_windows_pthread_barrier_init(void* barrier, const void* attributes, unsigned count);
extern "C" int darling_windows_pthread_barrier_destroy(void* barrier);
extern "C" int darling_windows_pthread_barrier_wait(void* barrier);
extern "C" int darling_windows_pthread_barrierattr_init(void* attributes);
extern "C" int darling_windows_pthread_barrierattr_destroy(void* attributes);
extern "C" int darling_windows_pthread_barrierattr_getpshared(const void* attributes, int* shared);
extern "C" int darling_windows_pthread_barrierattr_setpshared(void* attributes, int shared);
extern "C" int darling_windows_pthread_mutexattr_init(void* attributes);
extern "C" int darling_windows_pthread_mutexattr_destroy(void* attributes);
extern "C" int darling_windows_pthread_mutexattr_settype(void* attributes, int type);
extern "C" int darling_windows_pthread_mutexattr_gettype(const void* attributes, int* type);
extern "C" int darling_windows_pthread_mutexattr_setpshared(void* attributes, int shared);
extern "C" int darling_windows_pthread_mutexattr_getpshared(const void* attributes, int* shared);
extern "C" int darling_windows_pthread_mutexattr_setprotocol(void* attributes, int protocol);
extern "C" int darling_windows_pthread_mutexattr_getprotocol(const void* attributes, int* protocol);
extern "C" int darling_windows_pthread_mutexattr_setrobust(void* attributes, int robust);
extern "C" int darling_windows_pthread_mutexattr_getrobust(const void* attributes, int* robust);
extern "C" int darling_windows_pthread_mutexattr_getprioceiling(const void* attributes, int* ceiling);
extern "C" int darling_windows_pthread_mutexattr_setprioceiling(void* attributes, int ceiling);
extern "C" int darling_windows_pthread_cond_init(void* condition, const void* attributes);
extern "C" int darling_windows_pthread_cond_destroy(void* condition);
extern "C" int darling_windows_pthread_condattr_init(void* attributes);
extern "C" int darling_windows_pthread_condattr_destroy(void* attributes);
extern "C" int darling_windows_pthread_condattr_setpshared(void* attributes, int shared);
extern "C" int darling_windows_pthread_condattr_getpshared(const void* attributes, int* shared);
extern "C" int darling_windows_pthread_condattr_setclock(void* attributes, int clock_id);
extern "C" int darling_windows_pthread_condattr_getclock(const void* attributes, int* clock_id);
extern "C" int darling_windows_pthread_cond_wait(void* condition, void* mutex);
extern "C" int darling_windows_pthread_cond_timedwait(void* condition, void* mutex,
	const darling_timespec* deadline);
extern "C" int darling_windows_pthread_cond_signal(void* condition);
extern "C" int darling_windows_pthread_cond_broadcast(void* condition);
extern "C" int darling_windows_pthread_rwlock_init(void* lock, const void* attributes);
extern "C" int darling_windows_pthread_rwlock_destroy(void* lock);
extern "C" int darling_windows_pthread_rwlockattr_init(void* attributes);
extern "C" int darling_windows_pthread_rwlockattr_destroy(void* attributes);
extern "C" int darling_windows_pthread_rwlockattr_setpshared(void* attributes, int shared);
extern "C" int darling_windows_pthread_rwlockattr_getpshared(const void* attributes, int* shared);
extern "C" int darling_windows_pthread_rwlock_rdlock(void* lock);
extern "C" int darling_windows_pthread_rwlock_wrlock(void* lock);
extern "C" int darling_windows_pthread_rwlock_tryrdlock(void* lock);
extern "C" int darling_windows_pthread_rwlock_trywrlock(void* lock);
extern "C" int darling_windows_pthread_rwlock_timedrdlock(void* lock, const darling_timespec* deadline);
extern "C" int darling_windows_pthread_rwlock_timedwrlock(void* lock, const darling_timespec* deadline);
extern "C" int darling_windows_pthread_rwlock_unlock(void* lock);
extern "C" int darling_windows_pthread_key_create(std::uint64_t* key,
	void (*destructor)(void*));
extern "C" int darling_windows_pthread_key_delete(std::uint64_t key);
extern "C" int darling_windows_pthread_setspecific(std::uint64_t key, const void* value);
extern "C" const void* darling_windows_pthread_getspecific(std::uint64_t key);
extern "C" int darling_windows_pthread_once(void* control, void (*initializer)(void));
extern "C" int darling_windows_getpgrp();
extern "C" int darling_windows_getpgid(int process_id);
extern "C" int darling_windows_getsid(int process_id);
extern "C" int darling_windows_setpgid(int process_id, int group_id);
extern "C" int darling_windows_setsid();
extern "C" int darling_windows_getppid();
extern "C" int darling_windows_waitpid(int process_id, int* status, int options);
extern "C" int darling_windows_wait4(int process_id, int* status, int options,
	darling_rusage* usage);
extern "C" int darling_windows_waitid(int id_type, int id,
	darling_siginfo* info, int options);
extern "C" int darling_windows_posix_spawn(int* process_id, const char* path,
	const void* file_actions, const void* attributes,
	const char* const argv[], const char* const environment[]);
extern "C" int darling_windows_posix_spawnp(int* process_id, const char* file,
	const void* file_actions, const void* attributes,
	const char* const argv[], const char* const environment[]);
extern "C" int darling_windows_getuid();
extern "C" int darling_windows_geteuid();
extern "C" int darling_windows_getgid();
extern "C" int darling_windows_getegid();
extern "C" int darling_windows_getresuid(std::uint32_t* real_uid, std::uint32_t* effective_uid, std::uint32_t* saved_uid);
extern "C" int darling_windows_getresgid(std::uint32_t* real_gid, std::uint32_t* effective_gid, std::uint32_t* saved_gid);
extern "C" int darling_windows_getgroups(int group_count, std::uint32_t* groups);
extern "C" int darling_windows_setgroups(int group_count, const std::uint32_t* groups);
extern "C" int darling_windows_setuid(int uid);
extern "C" int darling_windows_seteuid(int uid);
extern "C" int darling_windows_setgid(int gid);
extern "C" int darling_windows_setegid(int gid);
extern "C" int darling_windows_setreuid(int real_uid, int effective_uid);
extern "C" int darling_windows_setregid(int real_gid, int effective_gid);
extern "C" int darling_windows_setresuid(int real_uid, int effective_uid, int saved_uid);
extern "C" int darling_windows_setresgid(int real_gid, int effective_gid, int saved_gid);
extern "C" const char* darling_windows_getlogin();
extern "C" int darling_windows_getlogin_r(char* buffer, std::size_t size);
extern "C" int darling_windows_setlogin(const char* name);
extern "C" int darling_windows_getrusage(int who, darling_rusage* result);
extern "C" int darling_windows_getrlimit(int resource, darling_rlimit* limit);
extern "C" int darling_windows_setrlimit(int resource, const darling_rlimit* limit);
extern "C" int darling_windows_getpriority(int which, std::uint32_t who);
extern "C" int darling_windows_setpriority(int which, std::uint32_t who, int priority);
extern "C" std::int64_t darling_windows_times(darling_tms* result);
extern "C" void* darling_windows_dlopen(const char* path, int mode);
extern "C" void* darling_windows_dlsym(void* handle, const char* symbol);
extern "C" int darling_windows_dlclose(void* handle);
extern "C" const char* darling_windows_dlerror();
extern "C" int darling_windows_dladdr(const void* address, darling_dl_info* info);
extern "C" std::uint32_t darling_windows_dyld_image_count();
extern "C" const char* darling_windows_dyld_get_image_name(std::uint32_t index);
extern "C" const void* darling_windows_dyld_get_image_header(std::uint32_t index);
extern "C" std::intptr_t darling_windows_dyld_get_image_vmaddr_slide(std::uint32_t index);
extern "C" int darling_windows_getpagesize();
extern "C" int darling_windows_getpagesizes(std::size_t* pagesizes, int count);
extern "C" void* darling_windows_mmap(void* address, std::size_t length,
	int protection, int flags, int descriptor, std::int64_t offset);
extern "C" int darling_windows_mprotect(void* address, std::size_t length,
	int protection);
extern "C" int darling_windows_madvise(void* address, std::size_t length,
	int advice);
extern "C" int darling_windows_msync(void* address, std::size_t length,
	int flags);
extern "C" int darling_windows_munmap(void* address, std::size_t length);
extern "C" std::int64_t darling_windows_sysconf(int name);
extern "C" std::size_t darling_windows_confstr(int name, char* buffer, std::size_t length);
extern "C" std::int64_t darling_windows_pathconf(const char* path, int name);
extern "C" std::int64_t darling_windows_fpathconf(int descriptor, int name);
extern "C" int darling_windows_getdtablesize();
extern "C" int darling_windows_sysctlbyname(const char* name, void* old_value,
	std::size_t* old_length, const void* new_value, std::size_t new_length);
extern "C" int darling_windows_sysctl(const int* mib, std::uint32_t mib_length,
	void* old_value, std::size_t* old_length, const void* new_value,
	std::size_t new_length);
extern "C" int darling_windows_sched_yield();
extern "C" unsigned int darling_windows_sleep(unsigned int seconds);
extern "C" std::uint32_t darling_windows_arc4random();
extern "C" void darling_windows_arc4random_buf(void* buffer, std::size_t bytes);
extern "C" int darling_windows_getentropy(void* buffer, std::size_t bytes);
extern "C" int darling_windows_kill(int process_id, int signal_number);
extern "C" int darling_windows_raise(int signal_number);
extern "C" int darling_windows_gethostname(char* buffer, std::size_t size);
extern "C" int darling_windows_getdomainname(char* buffer, std::size_t size);
extern "C" int darling_windows_uname(darling_utsname* result);
extern "C" int darling_windows_NSGetExecutablePath(char* buffer, std::uint32_t* size);
extern "C" const char* darling_windows_getprogname();
extern "C" void darling_windows_setprogname(const char* name);
extern "C" int darling_windows_clock_gettime(int clock_id, darling_timespec* result);
extern "C" int darling_windows_clock_getres(int clock_id, darling_timespec* result);
extern "C" int darling_windows_clock_nanosleep(int clock_id, int flags,
	const darling_timespec* request, darling_timespec* remaining);
extern "C" std::uint64_t darling_windows_mach_absolute_time();
extern "C" int darling_windows_mach_timebase_info(darling_mach_timebase_info* result);
extern "C" int darling_windows_nanosleep(const darling_timespec* request, darling_timespec* remaining);
extern "C" int darling_windows_gettimeofday(darling_timeval* result, void* timezone);
extern "C" int darling_windows_usleep(unsigned int microseconds);
extern "C" const char* darling_windows_getenv(const char* name);
extern "C" char** darling_windows_environ;
extern "C" char*** darling_windows_NSGetEnviron();
extern "C" int darling_windows_setenv(const char* name, const char* value, int overwrite);
extern "C" int darling_windows_unsetenv(const char* name);
extern "C" int darling_windows_putenv(char* assignment);
extern "C" int darling_windows_clearenv();
extern "C" int darling_windows_isatty(int descriptor);
struct darling_winsize final {
	std::uint16_t ws_row = 0;
	std::uint16_t ws_col = 0;
	std::uint16_t ws_xpixel = 0;
	std::uint16_t ws_ypixel = 0;
};
extern "C" int darling_windows_ioctl(int descriptor, unsigned long request,
	void* argument);
extern "C" char* darling_windows_ctermid(char* buffer);
extern "C" int darling_windows_ttyname_r(int descriptor, char* buffer, std::size_t size);
extern "C" char* darling_windows_ttyname(int descriptor);
extern "C" void* darling_windows_malloc(std::size_t bytes);
extern "C" void* darling_windows_calloc(std::size_t count, std::size_t bytes);
extern "C" void* darling_windows_realloc(void* address, std::size_t bytes);
extern "C" void darling_windows_free(void* address);
extern "C" void* darling_windows_memcpy(void* destination, const void* source, std::size_t bytes);
extern "C" void* darling_windows_memset(void* destination, int value, std::size_t bytes);
extern "C" void* darling_windows_memmove(void* destination, const void* source, std::size_t bytes);
extern "C" int darling_windows_memcmp(const void* left, const void* right, std::size_t bytes);
extern "C" void darling_windows_bzero(void* destination, std::size_t bytes);
extern "C" void darling_windows_explicit_bzero(void* destination, std::size_t bytes);
extern "C" void darling_windows_bcopy(const void* source, void* destination, std::size_t bytes);
extern "C" void* darling_windows_memccpy(void* destination, const void* source, int byte, std::size_t bytes);
extern "C" char* darling_windows_strdup(const char* value);
extern "C" char* darling_windows_strndup(const char* value, std::size_t bytes);
extern "C" long darling_windows_strtol(const char* value, char** end, int base);
extern "C" long long darling_windows_strtoll(const char* value, char** end, int base);
extern "C" unsigned long darling_windows_strtoul(const char* value, char** end, int base);
extern "C" double darling_windows_strtod(const char* value, char** end);
extern "C" float darling_windows_strtof(const char* value, char** end);
extern "C" long double darling_windows_strtold(const char* value, char** end);
extern "C" int darling_windows_snprintf(char* buffer, std::size_t size, const char* format, ...);
extern "C" int darling_windows_vsnprintf(char* buffer, std::size_t size, const char* format, std::va_list arguments);
extern "C" int darling_windows_vasprintf(char** result, const char* format, std::va_list arguments);
extern "C" int darling_windows_asprintf(char** result, const char* format, ...);
extern "C" std::size_t darling_windows_strlen(const char* value);
extern "C" std::size_t darling_windows_strnlen(const char* value, std::size_t limit);
extern "C" const void* darling_windows_memchr(const void* value, int byte, std::size_t bytes);
extern "C" const void* darling_windows_memmem(const void* haystack, std::size_t haystack_bytes, const void* needle, std::size_t needle_bytes);
extern "C" int darling_windows_strcmp(const char* left, const char* right);
extern "C" int darling_windows_strcasecmp(const char* left, const char* right);
extern "C" int darling_windows_strncasecmp(const char* left, const char* right, std::size_t bytes);
extern "C" char* darling_windows_strcpy(char* destination, const char* source);
extern "C" char* darling_windows_strncpy(char* destination, const char* source, std::size_t bytes);
extern "C" char* darling_windows_strcat(char* destination, const char* source);
extern "C" char* darling_windows_strncat(char* destination, const char* source, std::size_t bytes);
extern "C" std::size_t darling_windows_strlcpy(char* destination, const char* source, std::size_t size);
extern "C" std::size_t darling_windows_strlcat(char* destination, const char* source, std::size_t size);
extern "C" const char* darling_windows_strchr(const char* value, int character);
extern "C" const char* darling_windows_strrchr(const char* value, int character);
extern "C" const char* darling_windows_strstr(const char* value, const char* search);
extern "C" const char* darling_windows_strcasestr(const char* value, const char* search);
extern "C" std::size_t darling_windows_strspn(const char* value, const char* accepted);
extern "C" std::size_t darling_windows_strcspn(const char* value, const char* rejected);
extern "C" const char* darling_windows_strpbrk(const char* value, const char* accepted);
extern "C" char* darling_windows_strtok_r(char* value, const char* delimiters, char** state);
extern "C" char* darling_windows_strtok(char* value, const char* delimiters);
extern "C" const char* darling_windows_strerror(int error_number);
extern "C" int darling_windows_strerror_r(int error_number, char* buffer, std::size_t size);
extern "C" int darling_windows_host_entry(int argc, char** argv, char** envp);
