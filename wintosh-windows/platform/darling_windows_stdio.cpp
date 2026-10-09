/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_stdio.h"
#include "darling_windows_objc.h"
#include "darling_windows_syscalls.h"
#include "darling_windows_errno.h"
#include "darling_windows_filesystem.h"
#include "darling_windows_paths.h"
#include "darling_windows_time.h"
#include "darling_windows_tlv.h"
#include "darling_windows_dyld.h"
#include "darling_windows_signals.h"
#include "darling_windows_memory.h"
#include "darling_windows_mach.h"
#include "darling_windows_foundation.h"

#include <windows.h>
#include <ws2tcpip.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cstdint>
#include <atomic>
#include <array>
#include <cstring>
#include <cstdarg>
#include <csignal>
#include <functional>
#include <limits>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <string>
#include <condition_variable>
#include <shared_mutex>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <new>

extern "C" char** darling_windows_environ = nullptr;

namespace {

struct DarwinWindowsReparseDataBuffer final {
	DWORD ReparseTag{};
	WORD ReparseDataLength{};
	WORD Reserved{};
	struct {
		WORD SubstituteNameOffset{};
		WORD SubstituteNameLength{};
		WORD PrintNameOffset{};
		WORD PrintNameLength{};
		DWORD Flags{};
		WCHAR PathBuffer[1]{};
	} SymbolicLinkReparseBuffer;
};

darling::windows_host::DarwinSyscalls global_syscalls;

struct DarwinFileMapping final {
	void* base = nullptr;
	HANDLE mapping = nullptr;
	std::size_t bytes = 0;
};

std::mutex darwin_file_mapping_mutex;
std::unordered_map<void*, DarwinFileMapping> darwin_file_mappings;

bool ReleaseOwnedFileMapping(void* user_address)
{
	std::lock_guard lock(darwin_file_mapping_mutex);
	const auto found = darwin_file_mappings.find(user_address);
	if (found == darwin_file_mappings.end()) {
		return false;
	}
	const auto mapping = found->second;
	if (!UnmapViewOfFile(mapping.base)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return false;
	}
	CloseHandle(mapping.mapping);
	darwin_file_mappings.erase(found);
	return true;
}

std::mutex child_resource_mutex;
// Serialize child selection and reaping across concurrent wait calls. This is
// recursive because waitid delegates its normal reap path to wait4.
std::recursive_mutex child_wait_mutex;
darling_rusage completed_child_resources{};
std::unordered_set<int> darling_child_processes;
std::unordered_map<int, int> darling_child_groups;
std::unordered_map<int, HANDLE> darling_child_handles;
std::unordered_map<int, HANDLE> darling_child_group_jobs;
std::unordered_map<int, int> darling_child_termination_signals;
std::mutex socket_rights_mutex;
std::unordered_map<int, std::vector<std::vector<int>>> pending_socket_rights;
std::mutex exit_handler_mutex;
struct DarlingExitHandler final {
	std::function<void()> function;
	void* dso_handle{};
};
std::vector<DarlingExitHandler> darling_exit_handlers;
std::mutex darwin_environment_mutex;
std::vector<std::string> darwin_environment_entries;
std::vector<char*> darwin_environment_pointers;
std::mutex program_name_mutex;
std::string darling_program_name;
std::mutex resource_limit_mutex;
std::array<darling_rlimit, 9> resource_limits = [] {
	std::array<darling_rlimit, 9> result{};
	const auto infinity = (std::numeric_limits<std::uint64_t>::max)();
	for (auto& limit : result) limit = {infinity, infinity};
	result[3] = {8ull * 1024ull * 1024ull, infinity};
	result[4] = {0, infinity};
	result[8] = {1024, 1024};
	return result;
}();
std::mutex umask_mutex;
int darling_umask_value = 022;

int DarwinResolverError(int error) noexcept
{
	if (error == 0) return 0;
	if (error == EAI_BADFLAGS) return -1;
	if (error == EAI_NONAME || error == WSAHOST_NOT_FOUND) return -2;
	if (error == EAI_AGAIN || error == WSATRY_AGAIN) return -3;
	if (error == EAI_FAIL || error == WSANO_RECOVERY) return -4;
	if (error == EAI_NODATA || error == WSANO_DATA) return -5;
	if (error == EAI_FAMILY) return -6;
	if (error == EAI_SOCKTYPE) return -7;
	if (error == EAI_SERVICE) return -8;
	#ifdef EAI_ADDRFAMILY
	if (error == EAI_ADDRFAMILY) return -9;
	#endif
	if (error == EAI_MEMORY) return -10;
	#ifdef EAI_SYSTEM
	if (error == EAI_SYSTEM) return -11;
	#endif
	#ifdef EAI_OVERFLOW
	if (error == EAI_OVERFLOW) return -12;
	#endif
	return -4;
}

bool DecodeSocketRights(const darling_msghdr& message, std::vector<int>& descriptors)
{
	if (message.msg_control == nullptr && message.msg_controllen == 0)
		return true;
	if (message.msg_control == nullptr || message.msg_controllen < sizeof(darling_cmsghdr))
		return false;
	constexpr int socket_level = 0xffff;
	constexpr int rights_type = 1;
	const auto* bytes = static_cast<const unsigned char*>(message.msg_control);
	std::size_t offset = 0;
	while (offset < message.msg_controllen) {
		if (message.msg_controllen - offset < sizeof(darling_cmsghdr))
			return false;
		const auto* header = reinterpret_cast<const darling_cmsghdr*>(bytes + offset);
		if (header->cmsg_len < sizeof(darling_cmsghdr) ||
			header->cmsg_len > message.msg_controllen - offset ||
			header->cmsg_level != socket_level || header->cmsg_type != rights_type)
			return false;
		const std::size_t payload = header->cmsg_len - sizeof(darling_cmsghdr);
		if (payload % sizeof(int) != 0)
			return false;
		const auto* values = reinterpret_cast<const int*>(bytes + offset + sizeof(darling_cmsghdr));
		descriptors.insert(descriptors.end(), values, values + payload / sizeof(int));
		const std::size_t step = (header->cmsg_len + 3u) & ~std::size_t(3u);
		if (step == 0 || step > message.msg_controllen - offset)
			return false;
		offset += step;
	}
	return true;
}

bool EncodeSocketRights(darling_msghdr& message, const std::vector<int>& descriptors)
{
	if (descriptors.empty()) {
		message.msg_controllen = 0;
		return true;
	}
	const std::size_t needed = sizeof(darling_cmsghdr) + descriptors.size() * sizeof(int);
	if (message.msg_control == nullptr || message.msg_controllen < needed)
		return false;
	const auto header = darling_cmsghdr{static_cast<std::uint32_t>(needed), 0xffff, 1};
	std::memcpy(message.msg_control, &header, sizeof(header));
	std::memcpy(static_cast<unsigned char*>(message.msg_control) + sizeof(header),
		descriptors.data(), descriptors.size() * sizeof(int));
	message.msg_controllen = static_cast<std::uint32_t>(needed);
	return true;
}

bool DarwinFdSetContains(const darling_fd_set* set, int descriptor)
{
	return set != nullptr && descriptor >= 0 && descriptor < 1024 &&
		(set->fds_bits[descriptor / 32] & (static_cast<std::int32_t>(1) << (descriptor % 32))) != 0;
}

void ClearDarwinFdSet(darling_fd_set* set)
{
	if (set != nullptr)
		std::memset(set, 0, sizeof(*set));
}

void SetDarwinFdSet(darling_fd_set* set, int descriptor)
{
	if (set != nullptr && descriptor >= 0 && descriptor < 1024)
		set->fds_bits[descriptor / 32] |= static_cast<std::int32_t>(1) << (descriptor % 32);
}

void CleanupDarlingChildren()
{
	std::lock_guard lock(child_resource_mutex);
	for (const auto& child : darling_child_handles) {
		if (child.second != nullptr)
			CloseHandle(child.second);
	}
	for (const auto& group : darling_child_group_jobs) {
		if (group.second != nullptr)
			CloseHandle(group.second);
	}
	darling_child_group_jobs.clear();
	darling_child_handles.clear();
	darling_child_termination_signals.clear();
	darling_child_groups.clear();
	darling_child_processes.clear();
}

struct DarlingChildCleanupRegistrar final {
	DarlingChildCleanupRegistrar()
	{
		std::atexit(&CleanupDarlingChildren);
	}
};

DarlingChildCleanupRegistrar darling_child_cleanup_registrar;

bool IsDarlingChild(int process_id)
{
	std::lock_guard lock(child_resource_mutex);
	return darling_child_processes.find(process_id) != darling_child_processes.end();
}

void ForgetDarlingChild(int process_id)
{
	std::lock_guard lock(child_resource_mutex);
	int group_id = 0;
	const auto group = darling_child_groups.find(process_id);
	if (group != darling_child_groups.end())
		group_id = group->second;
	darling_child_processes.erase(process_id);
	darling_child_groups.erase(process_id);
	darling_child_termination_signals.erase(process_id);
	const auto handle = darling_child_handles.find(process_id);
	if (handle != darling_child_handles.end()) {
		CloseHandle(handle->second);
		darling_child_handles.erase(handle);
	}
	if (group_id != 0) {
		bool group_still_used = false;
		for (const auto& child_group : darling_child_groups) {
			if (child_group.second == group_id) {
				group_still_used = true;
				break;
			}
		}
		if (!group_still_used) {
			const auto job = darling_child_group_jobs.find(group_id);
			if (job != darling_child_group_jobs.end()) {
				CloseHandle(job->second);
				darling_child_group_jobs.erase(job);
			}
		}
	}
}

HANDLE DuplicateDarlingChildHandle(int process_id)
{
	std::lock_guard lock(child_resource_mutex);
	const auto handle = darling_child_handles.find(process_id);
	if (handle == darling_child_handles.end())
		return nullptr;
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), handle->second,
		GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return nullptr;
	return duplicate;
}

int DarlingChildTerminationSignal(int process_id)
{
	std::lock_guard lock(child_resource_mutex);
	const auto found = darling_child_termination_signals.find(process_id);
	return found == darling_child_termination_signals.end() ? 0 : found->second;
}

bool MeasureWindowsRusage(HANDLE process, darling_rusage& result)
{
	std::memset(&result, 0, sizeof(result));
	FILETIME creation{};
	FILETIME exit{};
	FILETIME kernel{};
	FILETIME user{};
	if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
		return false;
	const auto to_microseconds = [](const FILETIME& value) -> std::int64_t {
		ULARGE_INTEGER ticks{};
		ticks.LowPart = value.dwLowDateTime;
		ticks.HighPart = value.dwHighDateTime;
		return static_cast<std::int64_t>(ticks.QuadPart / 10);
	};
	const auto assign_time = [](darling_timeval& target, std::int64_t microseconds) {
		target.tv_sec = microseconds / 1'000'000;
		target.tv_usec = microseconds % 1'000'000;
	};
	assign_time(result.ru_utime, to_microseconds(user));
	assign_time(result.ru_stime, to_microseconds(kernel));
	PROCESS_MEMORY_COUNTERS_EX memory{};
	if (GetProcessMemoryInfo(process,
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
		result.ru_maxrss = static_cast<std::int64_t>(memory.PeakWorkingSetSize / 1024);
		result.ru_minflt = static_cast<std::int64_t>(memory.PageFaultCount);
	}
	IO_COUNTERS io{};
	if (GetProcessIoCounters(process, &io)) {
		result.ru_inblock = static_cast<std::int64_t>(io.ReadOperationCount);
		result.ru_oublock = static_cast<std::int64_t>(io.WriteOperationCount);
	}
	return true;
}

void AccumulateChildResources(const darling_rusage& value)
{
	std::lock_guard lock(child_resource_mutex);
	const auto add_time = [](darling_timeval& target, const darling_timeval& value) {
		target.tv_sec += value.tv_sec;
		target.tv_usec += value.tv_usec;
		if (target.tv_usec >= 1'000'000) {
			target.tv_sec += target.tv_usec / 1'000'000;
			target.tv_usec %= 1'000'000;
		}
	};
	add_time(completed_child_resources.ru_utime, value.ru_utime);
	add_time(completed_child_resources.ru_stime, value.ru_stime);
	completed_child_resources.ru_maxrss = std::max(completed_child_resources.ru_maxrss,
		value.ru_maxrss);
	completed_child_resources.ru_ixrss = std::max(completed_child_resources.ru_ixrss,
		value.ru_ixrss);
	completed_child_resources.ru_idrss = std::max(completed_child_resources.ru_idrss,
		value.ru_idrss);
	completed_child_resources.ru_isrss = std::max(completed_child_resources.ru_isrss,
		value.ru_isrss);
	completed_child_resources.ru_minflt += value.ru_minflt;
	completed_child_resources.ru_majflt += value.ru_majflt;
	completed_child_resources.ru_nswap += value.ru_nswap;
	completed_child_resources.ru_inblock += value.ru_inblock;
	completed_child_resources.ru_oublock += value.ru_oublock;
	completed_child_resources.ru_msgsnd += value.ru_msgsnd;
	completed_child_resources.ru_msgrcv += value.ru_msgrcv;
	completed_child_resources.ru_nsignals += value.ru_nsignals;
	completed_child_resources.ru_nvcsw += value.ru_nvcsw;
	completed_child_resources.ru_nivcsw += value.ru_nivcsw;
}

bool OpenAnyDarlingChild(bool nohang, int process_group, int& process_id, HANDLE& process)
{
	std::vector<int> candidates;
	{
		std::lock_guard lock(child_resource_mutex);
		for (const int candidate : darling_child_processes) {
			const auto group = darling_child_groups.find(candidate);
			if (process_group < 0 ||
				(group != darling_child_groups.end() && group->second == process_group))
				candidates.push_back(candidate);
		}
	}
	if (candidates.empty())
		return false;
	if (candidates.size() > MAXIMUM_WAIT_OBJECTS)
		return false;
	std::vector<HANDLE> handles;
	std::vector<int> valid_ids;
	for (const int candidate : candidates) {
		HANDLE candidate_handle = DuplicateDarlingChildHandle(candidate);
		if (candidate_handle == nullptr) {
			candidate_handle = OpenProcess(
				PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
				static_cast<DWORD>(candidate));
		}
		if (candidate_handle != nullptr) {
			handles.push_back(candidate_handle);
			valid_ids.push_back(candidate);
		}
	}
	if (handles.empty())
		return false;
	const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
		handles.data(), FALSE, nohang ? 0 : INFINITE);
	if (result == WAIT_TIMEOUT) {
		for (const HANDLE handle : handles)
			CloseHandle(handle);
		process_id = 0;
		process = nullptr;
		return true;
	}
	if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + handles.size()) {
		for (const HANDLE handle : handles)
			CloseHandle(handle);
		return false;
	}
	const std::size_t selected = static_cast<std::size_t>(result - WAIT_OBJECT_0);
	for (std::size_t index = 0; index < handles.size(); ++index) {
		if (index != selected)
			CloseHandle(handles[index]);
	}
	process_id = valid_ids[selected];
	process = handles[selected];
	return true;
}

HANDLE StandardHandle(int descriptor)
{
	return GetStdHandle(descriptor == 0 ? STD_INPUT_HANDLE :
		descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
}

std::filesystem::path Utf8Path(const char* value)
{
	if (value == nullptr) {
		return {};
	}
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		value, -1, nullptr, 0);
	if (length <= 0) {
		return {};
	}
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
		wide.data(), length) <= 0) {
		return {};
	}
	wide.resize(static_cast<std::size_t>(length - 1));
	return std::filesystem::path(wide);
}

darling_timespec FileTimeSpec(const FILETIME& value)
{
	ULARGE_INTEGER ticks{};
	ticks.LowPart = value.dwLowDateTime;
	ticks.HighPart = value.dwHighDateTime;
	constexpr std::uint64_t epoch_offset = 11644473600ull * 10'000'000ull;
	if (ticks.QuadPart < epoch_offset) {
		return {};
	}
	const auto unix_ticks = ticks.QuadPart - epoch_offset;
	return {static_cast<std::int64_t>(unix_ticks / 10'000'000ull),
		static_cast<std::int64_t>((unix_ticks % 10'000'000ull) * 100ull)};
}

void FillDarwinStat(const BY_HANDLE_FILE_INFORMATION& information,
	darling_darwin_stat* result)
{
	const bool directory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	const bool reparse_point =
		(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	result->st_dev = information.dwVolumeSerialNumber;
	result->st_mode = static_cast<std::uint16_t>(reparse_point ? 0120777 :
		(directory ? 0040755 : 0100644));
	if ((information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
		result->st_mode = static_cast<std::uint16_t>(result->st_mode & ~0222u);
	}
	result->st_nlink = static_cast<std::uint16_t>(information.nNumberOfLinks);
	result->st_ino = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32) |
		information.nFileIndexLow;
	result->st_uid = 0;
	result->st_gid = 0;
	result->st_atimespec = FileTimeSpec(information.ftLastAccessTime);
	result->st_mtimespec = FileTimeSpec(information.ftLastWriteTime);
	result->st_ctimespec = FileTimeSpec(information.ftCreationTime);
	result->st_birthtimespec = result->st_ctimespec;
	result->st_size = (static_cast<std::int64_t>(information.nFileSizeHigh) << 32) |
		information.nFileSizeLow;
	result->st_blocks = (result->st_size + 511) / 512;
	result->st_blksize = 4096;
}

int FillPathStat(const std::filesystem::path& wide_path, darling_darwin_stat* result,
	bool no_follow)
{
	if (wide_path.empty() || result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	BY_HANDLE_FILE_INFORMATION information{};
	if (no_follow) {
		const HANDLE handle = CreateFileW(wide_path.c_str(), 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		const bool queried = GetFileInformationByHandle(handle, &information) != FALSE;
		const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (!queried) {
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
	} else {
		// A normal handle follows the final symbolic link.  Using
		// GetFileAttributesExW here is insufficient on Windows because it can
		// return the reparse-point metadata (including size zero) instead of the
		// target metadata.
		const HANDLE handle = CreateFileW(wide_path.c_str(), 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		const bool queried = GetFileInformationByHandle(handle, &information) != FALSE;
		const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (!queried) {
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
	}
	FillDarwinStat(information, result);
	return 0;
}

int FillPathStat(const std::filesystem::path& wide_path, darling_darwin_stat* result)
{
	return FillPathStat(wide_path, result, false);
}

int FillPathStat(const char* path, darling_darwin_stat* result)
{
	if (path == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	return FillPathStat(Utf8Path(path), result, false);
}

void CopyStatfsString(char* destination, std::size_t capacity, const std::string& value)
{
	if (capacity == 0) {
		return;
	}
	const auto count = (std::min)(capacity - 1, value.size());
	std::memcpy(destination, value.data(), count);
	destination[count] = '\0';
}

std::string Utf8String(const std::wstring& value);

int FillStatfs(const std::filesystem::path& path, darling_darwin_statfs* result)
{
	if (path.empty() || result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	wchar_t volume_path[MAX_PATH]{};
	if (!GetVolumePathNameW(path.c_str(), volume_path, MAX_PATH)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	ULARGE_INTEGER free_bytes_user{};
	ULARGE_INTEGER total_bytes{};
	ULARGE_INTEGER free_bytes_total{};
	if (!GetDiskFreeSpaceExW(volume_path, &free_bytes_user, &total_bytes,
		&free_bytes_total)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	DWORD serial = 0;
	DWORD maximum_component = 0;
	DWORD volume_flags = 0;
	wchar_t filesystem_name[MAX_PATH]{};
	if (!GetVolumeInformationW(volume_path, nullptr, 0, &serial,
		&maximum_component, &volume_flags, filesystem_name, MAX_PATH)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	constexpr std::uint64_t block_size = 4096;
	result->f_bsize = static_cast<std::int32_t>(block_size);
	result->f_iosize = static_cast<std::int32_t>(block_size);
	result->f_blocks = total_bytes.QuadPart / block_size;
	result->f_bfree = free_bytes_total.QuadPart / block_size;
	result->f_bavail = free_bytes_user.QuadPart / block_size;
	result->f_fsid = serial;
	result->f_owner = 0;
	result->f_type = 0;
	result->f_flags = volume_flags;
	result->f_fssubtype = 0;
	CopyStatfsString(result->f_fstypename, sizeof(result->f_fstypename),
		Utf8String(filesystem_name));
	CopyStatfsString(result->f_mntonname, sizeof(result->f_mntonname),
		Utf8String(volume_path));
	CopyStatfsString(result->f_mntfromname, sizeof(result->f_mntfromname),
		Utf8String(volume_path));
	(void)maximum_component;
	return 0;
}

std::vector<std::filesystem::path> WindowsVolumeRoots()
{
	const DWORD required = GetLogicalDriveStringsW(0, nullptr);
	if (required == 0) {
		return {};
	}
	std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
	if (GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data()) == 0) {
		return {};
	}
	std::vector<std::filesystem::path> roots;
	for (const wchar_t* cursor = buffer.data(); *cursor != L'\0';
		cursor += std::wcslen(cursor) + 1) {
		roots.emplace_back(cursor);
	}
	return roots;
}

std::string Utf8String(const std::wstring& value)
{
	if (value.empty()) {
		return {};
	}
	const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) {
		return {};
	}
	std::string result(static_cast<std::size_t>(length), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) <= 0) {
		return {};
	}
	return result;
}

void RefreshDarwinEnvironment()
{
	LPWCH block = GetEnvironmentStringsW();
	if (block == nullptr) {
		return;
	}
	std::vector<std::string> entries;
	for (const wchar_t* entry = block; *entry != L'\0';
		entry += std::wcslen(entry) + 1) {
		if (entry[0] == L'=') {
			continue;
		}
		const std::string converted = Utf8String(entry);
		if (!converted.empty()) {
			entries.push_back(converted);
		}
	}
	FreeEnvironmentStringsW(block);
	std::lock_guard lock(darwin_environment_mutex);
	darwin_environment_entries = std::move(entries);
	darwin_environment_pointers.clear();
	darwin_environment_pointers.reserve(darwin_environment_entries.size() + 1);
	for (std::string& entry : darwin_environment_entries) {
		darwin_environment_pointers.push_back(entry.data());
	}
	darwin_environment_pointers.push_back(nullptr);
	darling_windows_environ = darwin_environment_pointers.data();
}

struct DarwinEnvironmentRegistrar final {
	DarwinEnvironmentRegistrar()
	{
		RefreshDarwinEnvironment();
	}
};

DarwinEnvironmentRegistrar darwin_environment_registrar;

void InitializeProgramName()
{
	char path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(std::size(path)));
	std::string name = length == 0 || length >= std::size(path) ?
		"darling_windows" : std::string(path, length);
	const auto separator = name.find_last_of("\\/");
	if (separator != std::string::npos) {
		name.erase(0, separator + 1);
	}
	std::lock_guard lock(program_name_mutex);
	darling_program_name = std::move(name);
}

struct DarwinProgramNameRegistrar final {
	DarwinProgramNameRegistrar()
	{
		InitializeProgramName();
	}
};

DarwinProgramNameRegistrar darwin_program_name_registrar;

thread_local std::string dynamic_loader_error;
std::mutex dynamic_image_mutex;
std::unordered_map<void*, darling::windows_host::DarwinDynamicImage*> dynamic_images;
std::unordered_map<void*, std::size_t> dynamic_image_references;
std::unordered_map<std::wstring, void*> dynamic_image_paths;
struct DynamicImageLoadState final {
	std::condition_variable condition;
	bool complete = false;
	std::thread::id owner;
};
std::unordered_map<std::wstring, std::shared_ptr<DynamicImageLoadState>>
	dynamic_image_loads;

void* DarwinRtldDefaultHandle() noexcept
{
	return reinterpret_cast<void*>(static_cast<std::intptr_t>(-2));
}

void SetDynamicLoaderError(const char* operation, DWORD error = ERROR_SUCCESS)
{
	if (error == ERROR_SUCCESS) {
		error = GetLastError();
	}
	dynamic_loader_error = std::string(operation) + " failed (Windows error " +
		std::to_string(error) + ")";
}

std::vector<HMODULE> CurrentProcessModules()
{
	std::vector<HMODULE> modules(64);
	DWORD required = 0;
	for (;;) {
		if (!EnumProcessModules(GetCurrentProcess(), modules.data(),
			static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &required)) {
			return {};
		}
		if (required <= modules.size() * sizeof(HMODULE)) {
			modules.resize(required / sizeof(HMODULE));
			return modules;
		}
		modules.resize(modules.size() * 2);
	}
}

std::wstring Utf8Wide(const char* value)
{
	if (value == nullptr) {
		return {};
	}
	const auto path = Utf8Path(value);
	return path.empty() ? std::wstring{} : path.wstring();
}

std::wstring QuoteProcessArgument(const std::wstring& value)
{
	if (!value.empty() && value.find_first_of(L" \t\"") == std::wstring::npos) {
		return value;
	}
	std::wstring result = L"\"";
	std::size_t backslashes = 0;
	for (const wchar_t character : value) {
		if (character == L'\\') {
			++backslashes;
		} else if (character == L'\"') {
			result.append(backslashes * 2 + 1, L'\\');
			result.push_back(L'\"');
			backslashes = 0;
		} else {
			result.append(backslashes, L'\\');
			backslashes = 0;
			result.push_back(character);
		}
	}
	result.append(backslashes * 2, L'\\');
	result.push_back(L'\"');
	return result;
}

constexpr int DarwinSpawnFileActionOpen = 0;
constexpr int DarwinSpawnFileActionClose = 1;
constexpr int DarwinSpawnFileActionDup2 = 2;
constexpr int DarwinSpawnFileActionInherit = 3;
constexpr int DarwinSpawnFileActionFileportDup2 = 4;
constexpr int DarwinSpawnFileActionChdir = 5;
constexpr int DarwinSpawnFileActionFchdir = 6;
constexpr std::size_t DarwinSpawnPathMax = 1024;
constexpr short DarwinSpawnResetids = 0x0001;
constexpr short DarwinSpawnCloexecDefault = 0x4000;
constexpr short DarwinSpawnSetpgroup = 0x0002;
constexpr short DarwinSpawnStartSuspended = 0x0080;
constexpr short DarwinSpawnSetsid = 0x0400;

#pragma pack(push, 4)
struct DarwinSpawnFileAction final {
	int type;
	union {
		int file_descriptor;
		std::uint32_t fileport;
	};
	union {
		struct {
			int flags;
			int mode;
			char path[DarwinSpawnPathMax];
		} open;
		struct {
			int new_file_descriptor;
		} dup2;
		struct {
			char path[DarwinSpawnPathMax];
		} chdir;
	};
};

struct DarwinSpawnFileActions final {
	int allocated;
	int count;
	DarwinSpawnFileAction actions[1];
};
#pragma pack(pop)

[[nodiscard]] HANDLE OpenSpawnActionFile(const DarwinSpawnFileAction& action)
{
	const auto path = Utf8Path(action.open.path);
	if (path.empty())
		return INVALID_HANDLE_VALUE;
	constexpr int write_only = 0x0001;
	constexpr int read_write = 0x0002;
	constexpr int append = 0x0008;
	constexpr int create = 0x0200;
	constexpr int exclusive = 0x0800;
	constexpr int truncate = 0x0400;
	constexpr int directory_only = 0x00100000;
	const int access_mode = action.open.flags & 0x0003;
	const DWORD access = access_mode == read_write ? GENERIC_READ | GENERIC_WRITE :
		access_mode == write_only ? GENERIC_WRITE : GENERIC_READ;
	DWORD creation = OPEN_EXISTING;
	const bool existed_before = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	if ((action.open.flags & create) != 0) {
		creation = (action.open.flags & exclusive) != 0 ? CREATE_NEW :
			(action.open.flags & truncate) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
	} else if ((action.open.flags & truncate) != 0) {
		creation = TRUNCATE_EXISTING;
	}
	const HANDLE handle = CreateFileW(path.c_str(), access,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		creation, FILE_ATTRIBUTE_NORMAL |
			((action.open.flags & directory_only) != 0 ? FILE_FLAG_BACKUP_SEMANTICS : 0),
		nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;
	if ((action.open.flags & create) != 0 && !existed_before) {
		std::lock_guard lock(umask_mutex);
		(void)darling_windows_chmod(action.open.path, action.open.mode & ~darling_umask_value);
	}
	if ((action.open.flags & append) != 0) {
		LARGE_INTEGER zero{};
		if (!SetFilePointerEx(handle, zero, nullptr, FILE_END)) {
			CloseHandle(handle);
			return INVALID_HANDLE_VALUE;
		}
	}
	return handle;
}

[[nodiscard]] std::wstring SpawnActionDirectoryPath(HANDLE handle)
{
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
		return {};
	std::vector<wchar_t> buffer(512);
	for (;;) {
		const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
			static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
		if (length == 0)
			return {};
		if (length < buffer.size()) {
			std::wstring result(buffer.data(), length);
			if (result.rfind(L"\\\\?\\", 0) == 0)
				result.erase(0, 4);
			return result;
		}
		buffer.resize(static_cast<std::size_t>(length) + 1);
	}
}

int SpawnPosixProcess(int* process_id, const char* executable,
	const void* file_actions, const void* attributes,
	const char* const argv[], const char* const environment[], bool search_path)
{
	if (process_id == nullptr || executable == nullptr || executable[0] == '\0') {
		return 22;
	}
	short spawn_attributes = 0;
	int spawn_pgroup = 0;
	if (attributes != nullptr) {
		std::memcpy(&spawn_attributes, attributes, sizeof(spawn_attributes));
		if ((spawn_attributes & DarwinSpawnSetpgroup) != 0) {
			// Darwin stores the pgroup immediately after the flags word in the
			// opaque spawn-attribute object.  The registry below carries this
			// identity even though Windows has no POSIX pgid namespace.
			std::memcpy(&spawn_pgroup,
				static_cast<const std::uint8_t*>(attributes) + sizeof(short) * 2,
				sizeof(spawn_pgroup));
		}
		if ((spawn_attributes &
			~(DarwinSpawnResetids | DarwinSpawnCloexecDefault | DarwinSpawnSetpgroup |
				DarwinSpawnStartSuspended | DarwinSpawnSetsid)) != 0)
			return 95;
	}
	std::wstring executable_wide = Utf8Wide(executable);
	if (executable_wide.empty()) {
		return 2;
	}
	if (search_path && executable_wide.find_first_of(L"\\/") == std::wstring::npos) {
		wchar_t resolved[MAX_PATH]{};
		const DWORD length = SearchPathW(nullptr, executable_wide.c_str(), nullptr,
			MAX_PATH, resolved, nullptr);
		if (length == 0 || length >= MAX_PATH) {
			return 2;
		}
		executable_wide.assign(resolved, length);
	}
	std::wstring command_line = QuoteProcessArgument(executable_wide);
	if (argv != nullptr) {
		for (std::size_t index = 1; argv[index] != nullptr; ++index) {
			const auto argument = Utf8Wide(argv[index]);
			if (argument.empty() && argv[index][0] != '\0') {
				return 22;
			}
			command_line.push_back(L' ');
			command_line += QuoteProcessArgument(argument);
		}
	}
	std::vector<wchar_t> environment_block;
	if (environment != nullptr) {
		for (std::size_t index = 0; environment[index] != nullptr; ++index) {
			const auto entry = Utf8Wide(environment[index]);
			if (entry.empty() && environment[index][0] != '\0') {
				return 22;
			}
			environment_block.insert(environment_block.end(), entry.begin(), entry.end());
			environment_block.push_back(L'\0');
		}
		environment_block.push_back(L'\0');
	}
	std::array<HANDLE, 3> child_standard_handles{
		GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
		GetStdHandle(STD_ERROR_HANDLE)};
	std::array<std::wstring, 3> child_standard_directories{};
	std::map<int, HANDLE> opened_actions;
	std::wstring child_directory;
	if (file_actions != nullptr) {
		const auto* actions = static_cast<const DarwinSpawnFileActions*>(file_actions);
		if (actions->allocated < 0 || actions->count < 0 || actions->count > actions->allocated ||
			actions->count > 4096) {
			return 22;
		}
		for (int index = 0; index < actions->count; ++index) {
			const auto& action = actions->actions[index];
			const int descriptor = action.file_descriptor;
			if (descriptor < 0 || descriptor > 2) {
				for (const auto& opened : opened_actions) CloseHandle(opened.second);
				return 95;
			}
			switch (action.type) {
			case DarwinSpawnFileActionOpen: {
				const HANDLE handle = OpenSpawnActionFile(action);
				if (handle == INVALID_HANDLE_VALUE) {
					for (const auto& opened : opened_actions) CloseHandle(opened.second);
					return 2;
				}
				const auto previous = opened_actions.find(descriptor);
				if (previous != opened_actions.end()) {
					CloseHandle(previous->second);
					previous->second = handle;
				} else {
					opened_actions.emplace(descriptor, handle);
				}
				child_standard_handles[descriptor] = handle;
				child_standard_directories[descriptor] =
					(action.open.flags & 0x00100000) != 0 ? Utf8Path(action.open.path).wstring() :
					std::wstring{};
				break;
			}
			case DarwinSpawnFileActionClose: {
				const auto previous = opened_actions.find(descriptor);
				if (previous != opened_actions.end()) {
					CloseHandle(previous->second);
					opened_actions.erase(previous);
				}
				child_standard_handles[descriptor] = nullptr;
				child_standard_directories[descriptor].clear();
				break;
			}
			case DarwinSpawnFileActionDup2: {
				const int target = action.dup2.new_file_descriptor;
				if (target < 0 || target > 2) {
					for (const auto& opened : opened_actions) CloseHandle(opened.second);
					return 95;
				}
				child_standard_handles[target] = child_standard_handles[descriptor];
				child_standard_directories[target] = child_standard_directories[descriptor];
				break;
			}
			case DarwinSpawnFileActionChdir: {
				const auto path = Utf8Path(action.chdir.path);
				if (path.empty()) {
					for (const auto& opened : opened_actions) CloseHandle(opened.second);
					return 2;
				}
				child_directory = path.wstring();
				break;
			}
			case DarwinSpawnFileActionFchdir: {
				const auto path = !child_standard_directories[descriptor].empty() ?
					child_standard_directories[descriptor] :
					SpawnActionDirectoryPath(child_standard_handles[descriptor]);
				if (path.empty()) {
					for (const auto& opened : opened_actions) CloseHandle(opened.second);
					return 20;
				}
				child_directory = path;
				break;
			}
			case DarwinSpawnFileActionInherit:
				// Standard descriptors are inherited through the handle list already.
				break;
			case DarwinSpawnFileActionFileportDup2:
			default:
				for (const auto& opened : opened_actions) CloseHandle(opened.second);
				return 95;
			}
		}
	}
	std::vector<HANDLE> inherited_handles;
	std::array<HANDLE, 3> child_handle_copies{nullptr, nullptr, nullptr};
	for (std::size_t index = 0; index < child_standard_handles.size(); ++index) {
		if (child_standard_handles[index] == nullptr ||
			child_standard_handles[index] == INVALID_HANDLE_VALUE)
			continue;
		if (!DuplicateHandle(GetCurrentProcess(), child_standard_handles[index],
			GetCurrentProcess(), &child_handle_copies[index], 0, TRUE,
			DUPLICATE_SAME_ACCESS)) {
			for (const auto& handle : child_handle_copies) if (handle) CloseHandle(handle);
			for (const auto& opened : opened_actions) CloseHandle(opened.second);
			return 5;
		}
		inherited_handles.push_back(child_handle_copies[index]);
	}
	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = child_handle_copies[0];
	startup.StartupInfo.hStdOutput = child_handle_copies[1];
	startup.StartupInfo.hStdError = child_handle_copies[2];
	SIZE_T attribute_bytes = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
	std::vector<std::uint8_t> attribute_storage(attribute_bytes);
	startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
		attribute_storage.data());
	if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
		&attribute_bytes) || (!inherited_handles.empty() &&
		!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
			PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles.data(),
			inherited_handles.size() * sizeof(HANDLE), nullptr, nullptr))) {
		for (const auto& handle : child_handle_copies) if (handle) CloseHandle(handle);
		for (const auto& opened : opened_actions) CloseHandle(opened.second);
		return 5;
	}
	PROCESS_INFORMATION process{};
	const BOOL started = CreateProcessW(executable_wide.c_str(), command_line.data(),
		nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW |
		EXTENDED_STARTUPINFO_PRESENT |
		((spawn_attributes & DarwinSpawnStartSuspended) != 0 ? CREATE_SUSPENDED : 0) |
		((spawn_attributes & (DarwinSpawnSetpgroup | DarwinSpawnSetsid)) != 0 ?
			CREATE_NEW_PROCESS_GROUP : 0),
		environment == nullptr ? nullptr : environment_block.data(),
		child_directory.empty() ? nullptr : child_directory.c_str(),
		&startup.StartupInfo, &process);
	DeleteProcThreadAttributeList(startup.lpAttributeList);
	for (const auto& handle : child_handle_copies) if (handle) CloseHandle(handle);
	for (const auto& opened : opened_actions) CloseHandle(opened.second);
	if (!started) {
		const DWORD error = GetLastError();
		return error == ERROR_FILE_NOT_FOUND ? 2 : 1;
	}
	*process_id = static_cast<int>(process.dwProcessId);
	{
		std::lock_guard lock(child_resource_mutex);
		darling_child_processes.insert(*process_id);
		const int child_group = (spawn_attributes & DarwinSpawnSetsid) != 0 ?
			*process_id : ((spawn_attributes & DarwinSpawnSetpgroup) != 0 ?
				(spawn_pgroup != 0 ? spawn_pgroup : *process_id) : 0);
		if (child_group != 0) {
			HANDLE job = nullptr;
			const auto existing = darling_child_group_jobs.find(child_group);
			if (existing != darling_child_group_jobs.end()) {
				job = existing->second;
			} else {
				job = CreateJobObjectW(nullptr, nullptr);
				if (job != nullptr) {
					JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
					limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
					if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
						&limits, sizeof(limits))) {
						CloseHandle(job);
						job = nullptr;
					}
				}
				if (job != nullptr)
					darling_child_group_jobs.emplace(child_group, job);
			}
			if (job == nullptr || !AssignProcessToJobObject(job, process.hProcess)) {
				if (existing == darling_child_group_jobs.end() && job != nullptr) {
					darling_child_group_jobs.erase(child_group);
					CloseHandle(job);
				}
			}
		}
		darling_child_groups[*process_id] = child_group;
		darling_child_handles[*process_id] = process.hProcess;
	}
	CloseHandle(process.hThread);
	return 0;
}

bool CurrentDarwinIdentityId(int& result)
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}
	DWORD required = 0;
	(void)GetTokenInformation(token, TokenUser, nullptr, 0, &required);
	std::vector<std::uint8_t> data(required);
	const bool queried = required != 0 &&
		GetTokenInformation(token, TokenUser, data.data(), required, &required) != FALSE;
	if (!queried) {
		CloseHandle(token);
		return false;
	}
	const auto* token_user = reinterpret_cast<const TOKEN_USER*>(data.data());
	const PSID sid = token_user->User.Sid;
	if (!IsValidSid(sid) || *GetSidSubAuthorityCount(sid) == 0) {
		CloseHandle(token);
		return false;
	}
	result = static_cast<int>(*GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1));
	CloseHandle(token);
	return true;
}

} // namespace

extern "C" int darling_windows_read(int descriptor, void* data, std::size_t bytes)
{
	try {
		if (descriptor == 0) {
			if (bytes > static_cast<std::size_t>(UINT32_MAX)) {
				return -1;
			}
			DWORD transferred = 0;
			if (!ReadFile(StandardHandle(0), data, static_cast<DWORD>(bytes),
				&transferred, nullptr)) {
				return -1;
			}
			return static_cast<int>(transferred);
		}
		return static_cast<int>(global_syscalls.Read(descriptor, data, bytes));
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_write(int descriptor, const void* data, std::size_t bytes)
{
	try {
		if (descriptor == 1 || descriptor == 2) {
			if (bytes > static_cast<std::size_t>(UINT32_MAX)) {
				return -1;
			}
			DWORD written = 0;
			if (!WriteFile(StandardHandle(descriptor), data, static_cast<DWORD>(bytes),
				&written, nullptr)) {
				return -1;
			}
			return static_cast<int>(written);
		}
		return static_cast<int>(global_syscalls.Write(descriptor, data, bytes));
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_pread(int descriptor, void* data,
	std::size_t bytes, std::int64_t offset)
{
	try {
		return static_cast<std::int64_t>(global_syscalls.ReadAt(
			descriptor, data, bytes, offset));
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_pwrite(int descriptor, const void* data,
	std::size_t bytes, std::int64_t offset)
{
	try {
		return static_cast<std::int64_t>(global_syscalls.WriteAt(
			descriptor, data, bytes, offset));
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_preadv(int descriptor,
	const darling_iovec* vectors, int count, std::int64_t offset)
{
	if (vectors == nullptr || count < 0 || offset < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::int64_t total = 0;
	for (int index = 0; index < count; ++index) {
		const auto result = darling_windows_pread(descriptor, vectors[index].iov_base,
			vectors[index].iov_len, offset + total);
		if (result < 0)
			return total == 0 ? -1 : total;
		total += result;
		if (static_cast<std::size_t>(result) != vectors[index].iov_len)
			break;
	}
	return total;
}

extern "C" std::int64_t darling_windows_pwritev(int descriptor,
	const darling_iovec* vectors, int count, std::int64_t offset)
{
	if (vectors == nullptr || count < 0 || offset < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::int64_t total = 0;
	for (int index = 0; index < count; ++index) {
		const auto result = darling_windows_pwrite(descriptor, vectors[index].iov_base,
			vectors[index].iov_len, offset + total);
		if (result < 0)
			return total == 0 ? -1 : total;
		total += result;
		if (static_cast<std::size_t>(result) != vectors[index].iov_len)
			break;
	}
	return total;
}

extern "C" int darling_windows_flock(int descriptor, int operation)
{
	try {
		global_syscalls.Lock(descriptor, operation);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_readv(int descriptor,
	const darling_iovec* vectors, int count)
{
	if (vectors == nullptr || count < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::int64_t total = 0;
	for (int index = 0; index < count; ++index) {
		const int result = darling_windows_read(descriptor, vectors[index].iov_base,
			vectors[index].iov_len);
		if (result < 0) {
			return total == 0 ? -1 : total;
		}
		total += result;
		if (static_cast<std::size_t>(result) != vectors[index].iov_len) {
			break;
		}
	}
	return total;
}

extern "C" std::int64_t darling_windows_writev(int descriptor,
	const darling_iovec* vectors, int count)
{
	if (vectors == nullptr || count < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::int64_t total = 0;
	for (int index = 0; index < count; ++index) {
		const int result = darling_windows_write(descriptor, vectors[index].iov_base,
			vectors[index].iov_len);
		if (result < 0) {
			return total == 0 ? -1 : total;
		}
		total += result;
		if (static_cast<std::size_t>(result) != vectors[index].iov_len) {
			break;
		}
	}
	return total;
}

extern "C" int darling_windows_close(int descriptor)
{
	try {
		if (descriptor >= 0 && descriptor <= 2) {
			return 0;
		}
		global_syscalls.Close(descriptor);
		{
			std::lock_guard lock(socket_rights_mutex);
			pending_socket_rights.erase(descriptor);
		}
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fcntl(int descriptor, int command, ...)
{
	constexpr int get_fd = 1;
	constexpr int set_fd = 2;
	constexpr int get_flags = 3;
	constexpr int set_flags = 4;
	constexpr int duplicate_fd = 0;
	constexpr int darwin_duplicate_fd_cloexec = 67;
	constexpr int duplicate_fd_cloexec = 1030;
	try {
		if (descriptor < 0) {
			darling::windows_host::DarwinErrno::Set(9);
			return -1;
		}
		va_list arguments;
		va_start(arguments, command);
		if (command == duplicate_fd || command == darwin_duplicate_fd_cloexec ||
			command == duplicate_fd_cloexec) {
			const int minimum = va_arg(arguments, int);
			va_end(arguments);
			const int duplicate = global_syscalls.DuplicateAtLeast(descriptor, minimum);
			if (command == darwin_duplicate_fd_cloexec || command == duplicate_fd_cloexec)
				global_syscalls.SetDescriptorFdFlags(duplicate, 1);
			return duplicate;
		}
		if (command == get_fd) {
			va_end(arguments);
			return global_syscalls.GetDescriptorFdFlags(descriptor);
		}
		if (command == get_flags) {
			va_end(arguments);
			return global_syscalls.GetDescriptorFlags(descriptor);
		}
		if (command == set_fd) {
			const int flags = va_arg(arguments, int);
			va_end(arguments);
			global_syscalls.SetDescriptorFdFlags(descriptor, flags);
			return 0;
		}
		if (command == set_flags) {
			const int flags = va_arg(arguments, int);
			va_end(arguments);
			global_syscalls.SetDescriptorFlags(descriptor, flags);
			return 0;
		}
		va_end(arguments);
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_dup(int descriptor)
{
	try {
		return global_syscalls.Duplicate(descriptor);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_dup2(int descriptor, int target)
{
	try {
		if (descriptor == target) {
			return target;
		}
		return global_syscalls.Duplicate(descriptor, target);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_pipe(int descriptors[2])
{
	try {
		global_syscalls.CreatePipe(descriptors);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_pipe2(int descriptors[2], int flags)
{
	constexpr int non_blocking = 0x0004;
	constexpr int close_on_exec = 0x01000000;
	if ((flags & ~(non_blocking | close_on_exec)) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (darling_windows_pipe(descriptors) != 0) {
		return -1;
	}
	const int descriptor_flags = (flags & non_blocking) != 0 ? non_blocking : 0;
	const int descriptor_fd_flags = (flags & close_on_exec) != 0 ? 1 : 0;
	if (darling_windows_fcntl(descriptors[0], 4, descriptor_flags) != 0 ||
		darling_windows_fcntl(descriptors[1], 4, descriptor_flags) != 0 ||
		darling_windows_fcntl(descriptors[0], 2, descriptor_fd_flags) != 0 ||
		darling_windows_fcntl(descriptors[1], 2, descriptor_fd_flags) != 0) {
		darling_windows_close(descriptors[0]);
		darling_windows_close(descriptors[1]);
		descriptors[0] = -1;
		descriptors[1] = -1;
		return -1;
	}
	return 0;
}

extern "C" int darling_windows_poll(
	darling::windows_host::darling_pollfd* descriptors, std::size_t count, int timeout_ms)
{
	try {
		return global_syscalls.Poll(descriptors, count, timeout_ms);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_socket(int domain, int type, int protocol)
{
	try {
		return global_syscalls.Socket(domain, type, protocol);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_socketpair(int domain, int type, int protocol, int descriptors[2])
{
	try {
		global_syscalls.SocketPair(domain, type, protocol, descriptors);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_bind(int descriptor, const void* address, int address_length)
{
	try {
		global_syscalls.BindSocket(descriptor, reinterpret_cast<const sockaddr*>(address), address_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_connect(int descriptor, const void* address, int address_length)
{
	try {
		global_syscalls.ConnectSocket(descriptor, reinterpret_cast<const sockaddr*>(address), address_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_listen(int descriptor, int backlog)
{
	try {
		global_syscalls.ListenSocket(descriptor, backlog);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_accept(int descriptor, void* address, int* address_length)
{
	try {
		return global_syscalls.AcceptSocket(descriptor, reinterpret_cast<sockaddr*>(address),
			address_length);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_getsockname(int descriptor, void* address, int* address_length)
{
	try {
		global_syscalls.GetSocketName(descriptor, reinterpret_cast<sockaddr*>(address),
			address_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_getpeername(int descriptor, void* address, int* address_length)
{
	try {
		global_syscalls.GetPeerSocketName(descriptor, reinterpret_cast<sockaddr*>(address),
			address_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_getaddrinfo(const char* node, const char* service,
	const darling_addrinfo* hints, darling_addrinfo** result)
{
	if (result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	*result = nullptr;
	static std::once_flag winsock_once;
	try {
		std::call_once(winsock_once, [] {
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
				throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup");
		});
		ADDRINFOA native_hints{};
		ADDRINFOA* native_hints_pointer = nullptr;
		if (hints != nullptr) {
			constexpr int darwin_ai_numericserv = 0x1000;
			native_hints.ai_flags = hints->ai_flags & ~darwin_ai_numericserv;
			if ((hints->ai_flags & darwin_ai_numericserv) != 0)
				native_hints.ai_flags |= AI_NUMERICSERV;
			native_hints.ai_family = hints->ai_family;
			native_hints.ai_socktype = hints->ai_socktype;
			native_hints.ai_protocol = hints->ai_protocol;
			native_hints_pointer = &native_hints;
		}
		ADDRINFOA* native_result = nullptr;
		const int error = ::getaddrinfo(node, service, native_hints_pointer, &native_result);
		if (error != 0)
			return DarwinResolverError(error);
		darling_addrinfo* head = nullptr;
		darling_addrinfo* tail = nullptr;
		try {
			for (ADDRINFOA* current = native_result; current != nullptr; current = current->ai_next) {
				auto* converted = static_cast<darling_addrinfo*>(std::calloc(1, sizeof(darling_addrinfo)));
				if (converted == nullptr)
					throw std::bad_alloc();
				constexpr int darwin_ai_numericserv = 0x1000;
				converted->ai_flags = current->ai_flags & ~AI_NUMERICSERV;
				if ((current->ai_flags & AI_NUMERICSERV) != 0)
					converted->ai_flags |= darwin_ai_numericserv;
				converted->ai_family = current->ai_family;
				converted->ai_socktype = current->ai_socktype;
				converted->ai_protocol = current->ai_protocol;
				converted->ai_addrlen = static_cast<std::uint32_t>(current->ai_addrlen);
				if (current->ai_addr != nullptr && current->ai_addrlen != 0) {
					converted->ai_addr = std::malloc(current->ai_addrlen);
					if (converted->ai_addr == nullptr) {
						std::free(converted);
						throw std::bad_alloc();
					}
					std::memcpy(converted->ai_addr, current->ai_addr, current->ai_addrlen);
				}
				if (current->ai_canonname != nullptr) {
					const auto length = std::strlen(current->ai_canonname) + 1;
					converted->ai_canonname = static_cast<char*>(std::malloc(length));
					if (converted->ai_canonname == nullptr) {
						std::free(converted->ai_addr);
						std::free(converted);
						throw std::bad_alloc();
					}
					std::memcpy(converted->ai_canonname, current->ai_canonname, length);
				}
				if (tail == nullptr) head = converted;
				else tail->ai_next = converted;
				tail = converted;
			}
		} catch (...) {
			darling_windows_freeaddrinfo(head);
			::freeaddrinfo(native_result);
			return -10;
		}
		::freeaddrinfo(native_result);
		*result = head;
		return 0;
	} catch (...) {
		return -4;
	}
}

extern "C" void darling_windows_freeaddrinfo(darling_addrinfo* result)
{
	while (result != nullptr) {
		darling_addrinfo* next = result->ai_next;
		std::free(result->ai_canonname);
		std::free(result->ai_addr);
		std::free(result);
		result = next;
	}
}

extern "C" int darling_windows_getnameinfo(const void* address, int address_length,
	char* host, std::size_t host_length, char* service, std::size_t service_length, int flags)
{
	if (address == nullptr || address_length < 0 ||
		(host == nullptr && host_length != 0) || (service == nullptr && service_length != 0) ||
		host_length > std::numeric_limits<DWORD>::max() ||
		service_length > std::numeric_limits<DWORD>::max()) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	static std::once_flag winsock_once;
	try {
		std::call_once(winsock_once, [] {
			WSADATA data{};
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
				throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup");
		});
		return DarwinResolverError(::getnameinfo(static_cast<const sockaddr*>(address), address_length,
			host, static_cast<DWORD>(host_length), service, static_cast<DWORD>(service_length), flags));
	} catch (...) {
		return -4;
	}
}

extern "C" int darling_windows_inet_pton(int family, const char* source, void* destination)
{
	if (source == nullptr || destination == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const INT result = InetPtonA(family, source, destination);
	if (result == 0)
		darling::windows_host::DarwinErrno::Set(22);
	else if (result < 0)
		darling::windows_host::DarwinErrno::SetFromWin32(WSAGetLastError());
	return result;
}

extern "C" const char* darling_windows_inet_ntop(int family, const void* source,
	char* destination, std::size_t destination_length)
{
	if (source == nullptr || destination == nullptr ||
		destination_length > std::numeric_limits<DWORD>::max()) {
		darling::windows_host::DarwinErrno::Set(22);
		return nullptr;
	}
	const char* result = InetNtopA(family, source, destination,
		static_cast<DWORD>(destination_length));
	if (result == nullptr)
		darling::windows_host::DarwinErrno::SetFromWin32(WSAGetLastError());
	return result;
}

extern "C" int darling_windows_select(int nfds, darling_fd_set* read_set,
	darling_fd_set* write_set, darling_fd_set* exception_set, darling_timeval* timeout)
{
	if (nfds < 0 || nfds > 1024 || (timeout != nullptr &&
		(timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1'000'000))) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::vector<darling::windows_host::darling_pollfd> descriptors;
	descriptors.reserve(static_cast<std::size_t>(nfds));
	const ULONGLONG select_start = GetTickCount64();
	const auto update_timeout = [&]() {
		if (timeout == nullptr)
			return;
		const ULONGLONG elapsed_ms = GetTickCount64() - select_start;
		const std::uint64_t requested_us = static_cast<std::uint64_t>(timeout->tv_sec) *
			1'000'000ull + static_cast<std::uint64_t>(timeout->tv_usec);
		const std::uint64_t elapsed_us = elapsed_ms > (UINT64_MAX / 1000ull) ?
			UINT64_MAX : static_cast<std::uint64_t>(elapsed_ms) * 1000ull;
		const std::uint64_t remaining_us = elapsed_us >= requested_us ? 0 :
			requested_us - elapsed_us;
		timeout->tv_sec = static_cast<std::int64_t>(remaining_us / 1'000'000ull);
		timeout->tv_usec = static_cast<std::int32_t>(remaining_us % 1'000'000ull);
	};
	for (int descriptor = 0; descriptor < nfds; ++descriptor) {
		short events = 0;
		if (DarwinFdSetContains(read_set, descriptor)) events |= 0x0001;
		if (DarwinFdSetContains(write_set, descriptor)) events |= 0x0004;
		if (DarwinFdSetContains(exception_set, descriptor)) events |= 0x0002;
		if (events != 0)
			descriptors.push_back({descriptor, events, 0});
	}
	int timeout_ms = -1;
	if (timeout != nullptr) {
		const auto milliseconds = timeout->tv_sec > (std::numeric_limits<std::int64_t>::max() / 1000) ?
			std::numeric_limits<std::int64_t>::max() : timeout->tv_sec * 1000 +
			(timeout->tv_usec + 999) / 1000;
		timeout_ms = milliseconds > std::numeric_limits<int>::max() ?
			std::numeric_limits<int>::max() : static_cast<int>(milliseconds);
	}
	ClearDarwinFdSet(read_set);
	ClearDarwinFdSet(write_set);
	ClearDarwinFdSet(exception_set);
	if (descriptors.empty()) {
		if (timeout_ms > 0)
			Sleep(static_cast<DWORD>(timeout_ms));
		update_timeout();
		return 0;
	}
	const int ready = darling_windows_poll(descriptors.data(), descriptors.size(), timeout_ms);
	if (ready < 0)
		return -1;
	int result = 0;
	for (const auto& descriptor : descriptors) {
		bool counted = false;
		if ((descriptor.revents & (0x0001 | 0x0010)) != 0) {
			SetDarwinFdSet(read_set, descriptor.fd);
			++result;
			counted = true;
		}
		if ((descriptor.revents & 0x0004) != 0) {
			SetDarwinFdSet(write_set, descriptor.fd);
			if (!counted) ++result;
			counted = true;
		}
		if ((descriptor.revents & (0x0002 | 0x0008 | 0x0020)) != 0) {
			SetDarwinFdSet(exception_set, descriptor.fd);
			if (!counted) ++result;
		}
	}
	update_timeout();
	return result;
}

extern "C" int darling_windows_pselect(int nfds, darling_fd_set* read_set,
	darling_fd_set* write_set, darling_fd_set* exception_set,
	const darling_timespec* timeout, const darling_darwin_sigset* signal_mask)
{
	if (timeout != nullptr && (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
		timeout->tv_nsec >= 1'000'000'000)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	darling_darwin_sigset previous_mask{};
	if (signal_mask != nullptr && darling_windows_sigprocmask(3, signal_mask,
		&previous_mask) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	darling_timeval select_timeout{};
	darling_timeval* select_timeout_pointer = nullptr;
	if (timeout != nullptr) {
		select_timeout.tv_sec = timeout->tv_sec;
		select_timeout.tv_usec = (timeout->tv_nsec + 999) / 1000;
		if (select_timeout.tv_usec == 1'000'000) {
			++select_timeout.tv_sec;
			select_timeout.tv_usec = 0;
		}
		select_timeout_pointer = &select_timeout;
	}
	const int result = darling_windows_select(nfds, read_set, write_set,
		exception_set, select_timeout_pointer);
	if (signal_mask != nullptr)
		(void)darling_windows_sigprocmask(3, &previous_mask, nullptr);
	return result;
}

extern "C" int darling_windows_getsockopt(int descriptor, int level, int option, void* value,
	int* value_length)
{
	try {
		global_syscalls.GetSocketOption(descriptor, level, option, value, value_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_setsockopt(int descriptor, int level, int option,
	const void* value, int value_length)
{
	try {
		global_syscalls.SetSocketOption(descriptor, level, option, value, value_length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_sendto(int descriptor, const void* buffer, std::size_t bytes,
	int flags, const void* address, int address_length)
{
	try {
		return static_cast<int>(global_syscalls.SendToSocket(descriptor, buffer, bytes, flags,
			reinterpret_cast<const sockaddr*>(address), address_length));
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_recvfrom(int descriptor, void* buffer, std::size_t bytes,
	int flags, void* address, int* address_length)
{
	try {
		return static_cast<int>(global_syscalls.ReceiveFromSocket(descriptor, buffer, bytes, flags,
			reinterpret_cast<sockaddr*>(address), address_length));
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_sendmsg(int descriptor,
	const darling_msghdr* message, int flags)
{
	if (message == nullptr || message->msg_iov == nullptr || message->msg_iovlen < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	try {
		std::vector<int> rights;
		if (!DecodeSocketRights(*message, rights)) {
			darling::windows_host::DarwinErrno::Set(95);
			return -1;
		}
		if (!rights.empty() && global_syscalls.SocketPeerDescriptor(descriptor) < 0) {
			darling::windows_host::DarwinErrno::Set(95);
			return -1;
		}
		std::vector<WSABUF> buffers;
		buffers.reserve(static_cast<std::size_t>(message->msg_iovlen));
		for (int index = 0; index < message->msg_iovlen; ++index) {
			if (message->msg_iov[index].iov_len > std::numeric_limits<ULONG>::max()) {
				darling::windows_host::DarwinErrno::Set(22);
				return -1;
			}
			buffers.push_back(WSABUF{static_cast<ULONG>(message->msg_iov[index].iov_len),
				static_cast<char*>(message->msg_iov[index].iov_base)});
		}
		const auto result = static_cast<std::int64_t>(global_syscalls.SendMessage(descriptor, buffers.data(),
			static_cast<DWORD>(buffers.size()), flags,
			reinterpret_cast<const sockaddr*>(message->msg_name),
			static_cast<int>(message->msg_namelen)));
		if (!rights.empty()) {
			std::lock_guard lock(socket_rights_mutex);
			pending_socket_rights[global_syscalls.SocketPeerDescriptor(descriptor)].push_back(
				std::move(rights));
		}
		return result;
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_recvmsg(int descriptor, darling_msghdr* message,
	int flags)
{
	if (message == nullptr || message->msg_iov == nullptr || message->msg_iovlen < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	try {
		std::vector<WSABUF> buffers;
		buffers.reserve(static_cast<std::size_t>(message->msg_iovlen));
		for (int index = 0; index < message->msg_iovlen; ++index) {
			if (message->msg_iov[index].iov_len > std::numeric_limits<ULONG>::max()) {
				darling::windows_host::DarwinErrno::Set(22);
				return -1;
			}
			buffers.push_back(WSABUF{static_cast<ULONG>(message->msg_iov[index].iov_len),
				static_cast<char*>(message->msg_iov[index].iov_base)});
		}
		int native_flags = flags;
		int address_length = static_cast<int>(message->msg_namelen);
		const auto result = global_syscalls.ReceiveMessage(descriptor, buffers.data(),
			static_cast<DWORD>(buffers.size()), &native_flags,
			reinterpret_cast<sockaddr*>(message->msg_name),
			message->msg_name == nullptr ? nullptr : &address_length);
		if (message->msg_name != nullptr)
			message->msg_namelen = static_cast<std::uint32_t>(address_length);
		message->msg_flags = native_flags;
		std::vector<int> rights;
		{
			std::lock_guard lock(socket_rights_mutex);
			const auto pending = pending_socket_rights.find(descriptor);
			if (pending != pending_socket_rights.end() && !pending->second.empty())
				rights = pending->second.front();
		}
		if (!rights.empty()) {
			std::vector<int> duplicates;
			for (const int source : rights) {
				const int duplicate = global_syscalls.Duplicate(source);
				if (duplicate < 0) {
					for (const int value : duplicates) {
						try { global_syscalls.Close(value); } catch (...) {}
					}
					return -1;
				}
				duplicates.push_back(duplicate);
			}
			const std::size_t rights_bytes = sizeof(darling_cmsghdr) +
				rights.size() * sizeof(int);
			if (message->msg_control == nullptr ||
				message->msg_controllen < rights_bytes) {
				message->msg_flags |= 0x0008;
				for (const int value : duplicates) {
					try { global_syscalls.Close(value); } catch (...) {}
				}
				std::lock_guard lock(socket_rights_mutex);
				const auto pending = pending_socket_rights.find(descriptor);
				if (pending != pending_socket_rights.end() && !pending->second.empty()) {
					pending->second.erase(pending->second.begin());
					if (pending->second.empty()) pending_socket_rights.erase(pending);
				}
				return static_cast<std::int64_t>(result);
			}
			if (!EncodeSocketRights(*message, duplicates)) {
				message->msg_flags |= 0x0008;
				for (const int value : duplicates) {
					try { global_syscalls.Close(value); } catch (...) {}
				}
				std::lock_guard lock(socket_rights_mutex);
				const auto pending = pending_socket_rights.find(descriptor);
				if (pending != pending_socket_rights.end() && !pending->second.empty()) {
					pending->second.erase(pending->second.begin());
					if (pending->second.empty()) pending_socket_rights.erase(pending);
				}
			} else {
				std::lock_guard lock(socket_rights_mutex);
				const auto pending = pending_socket_rights.find(descriptor);
				if (pending != pending_socket_rights.end() && !pending->second.empty()) {
					pending->second.erase(pending->second.begin());
					if (pending->second.empty()) pending_socket_rights.erase(pending);
				}
			}
		}
		return static_cast<std::int64_t>(result);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_shutdown(int descriptor, int how)
{
	try {
		global_syscalls.ShutdownSocket(descriptor, how);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_open(const char* path, int flags, int mode)
{
	try {
		const auto wide_path = Utf8Path(path);
		if (wide_path.empty()) {
			return -1;
		}
		const int descriptor = global_syscalls.Open(wide_path, flags);
		if (descriptor >= 0 && (flags & 0x0200) != 0) { // Darwin O_CREAT
			std::lock_guard lock(umask_mutex);
			(void)darling_windows_chmod(path, mode & ~darling_umask_value);
		}
		return descriptor;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_mkstemp(char* path_template)
{
	try {
		if (path_template == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const std::string original(path_template);
		if (original.size() < 6 || original.compare(original.size() - 6, 6, "XXXXXX") != 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto requested = Utf8Path(path_template);
		const auto directory = requested.has_parent_path() ? requested.parent_path() : std::filesystem::path(L".");
		const auto filename = requested.filename().wstring();
		const auto prefix = filename.substr(0, (std::min<std::size_t>)(3, filename.size()));
		wchar_t temporary_name[MAX_PATH]{};
		if (GetTempFileNameW(directory.c_str(), prefix.c_str(), 0, temporary_name) == 0) {
			const DWORD error = GetLastError();
			// App-container temp providers can reject GetTempFileNameW even
			// though CREATE_NEW is permitted.  Fall back to an atomic unique
			// CREATE_NEW candidate in the requested directory.
			for (unsigned int attempt = 0; attempt < 128; ++attempt) {
				const unsigned long long nonce =
					static_cast<unsigned long long>(GetTickCount64()) +
					static_cast<unsigned long long>(GetCurrentProcessId()) + attempt;
				std::wstring candidate_filename = filename;
				wchar_t nonce_text[7]{};
				_swprintf_p(nonce_text, std::size(nonce_text), L"%06llX", nonce & 0xFFFFFFULL);
				candidate_filename.replace(candidate_filename.size() - 6, 6, nonce_text);
				const auto candidate = directory / candidate_filename;
				const HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
					FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle != INVALID_HANDLE_VALUE) {
					CloseHandle(handle);
					const auto candidate_utf8 = Utf8String(candidate.wstring());
					if (!candidate_utf8.empty() && candidate_utf8.size() <= original.size()) {
						std::memcpy(path_template, candidate_utf8.c_str(), candidate_utf8.size() + 1);
						return global_syscalls.Open(candidate, 2);
					}
					DeleteFileW(candidate.c_str());
				}
			}
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
		const std::filesystem::path generated(temporary_name);
		const auto generated_name = generated.filename().wstring();
		if (generated_name.size() < 6) {
			DeleteFileW(generated.c_str());
			darling::windows_host::DarwinErrno::Set(5);
			return -1;
		}
		std::wstring replacement = filename;
		replacement.replace(replacement.size() - 6, 6,
			generated_name.substr(generated_name.size() - 6));
		const auto final_path = directory / replacement;
		if (!MoveFileExW(generated.c_str(), final_path.c_str(), MOVEFILE_COPY_ALLOWED)) {
			const DWORD error = GetLastError();
			// Some sandboxed Windows temp providers allow creation but deny a
			// rename in the same directory.  The generated name is already
			// unique and therefore remains a valid mkstemp result.
			if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) {
				const HANDLE replacement_handle = CreateFileW(final_path.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (replacement_handle != INVALID_HANDLE_VALUE) {
					CloseHandle(replacement_handle);
					DeleteFileW(generated.c_str());
					const auto final_utf8 = Utf8String(final_path.wstring());
					std::memcpy(path_template, final_utf8.c_str(), final_utf8.size() + 1);
					return global_syscalls.Open(final_path, 2);
				}
				DeleteFileW(generated.c_str());
				darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
				return -1;
			}
			DeleteFileW(generated.c_str());
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
		const auto final_utf8 = Utf8String(final_path.wstring());
		if (final_utf8.empty() || final_utf8.size() > original.size()) {
			DeleteFileW(final_path.c_str());
			darling::windows_host::DarwinErrno::Set(34);
			return -1;
		}
		std::memcpy(path_template, final_utf8.c_str(), final_utf8.size() + 1);
		const int descriptor = global_syscalls.Open(final_path, 2);
		return descriptor;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_mkstemps(char* path_template, int suffix_length)
{
	try {
		if (path_template == nullptr || suffix_length < 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const std::string original(path_template);
		const auto suffix = static_cast<std::size_t>(suffix_length);
		if (original.size() < suffix + 6 ||
			original.compare(original.size() - suffix - 6, 6, "XXXXXX") != 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		std::string stem_template = original.substr(0, original.size() - suffix);
		stem_template.push_back('\0');
		const int descriptor = darling_windows_mkstemp(stem_template.data());
		if (descriptor < 0) {
			return -1;
		}
		if (darling_windows_close(descriptor) != 0) {
			return -1;
		}
		const std::string generated = stem_template.data();
		const std::string final_path = generated + original.substr(original.size() - suffix);
		const auto generated_wide = Utf8Path(generated.c_str());
		const auto final_wide = Utf8Path(final_path.c_str());
		if (generated_wide.empty() || final_wide.empty() ||
			!MoveFileExW(generated_wide.c_str(), final_wide.c_str(), MOVEFILE_COPY_ALLOWED)) {
			const DWORD error = GetLastError();
			if ((error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) &&
				!generated_wide.empty() && !final_wide.empty()) {
				const HANDLE replacement_handle = CreateFileW(final_wide.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (replacement_handle != INVALID_HANDLE_VALUE) {
					CloseHandle(replacement_handle);
					DeleteFileW(generated_wide.c_str());
					std::memcpy(path_template, final_path.c_str(), final_path.size() + 1);
					return global_syscalls.Open(final_wide, 2);
				}
			}
			return -1;
		}
		std::memcpy(path_template, final_path.c_str(), final_path.size() + 1);
		return global_syscalls.Open(final_wide, 2);
	} catch (...) {
		return -1;
	}
}

extern "C" char* darling_windows_mkdtemp(char* path_template)
{
	try {
		if (path_template == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return nullptr;
		}
		std::string file_template(path_template);
		if (file_template.size() < 6 ||
			file_template.compare(file_template.size() - 6, 6, "XXXXXX") != 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return nullptr;
		}
		const int descriptor = darling_windows_mkstemp(file_template.data());
		if (descriptor < 0 || darling_windows_close(descriptor) != 0) {
			return nullptr;
		}
		const auto file_path = Utf8Path(file_template.c_str());
		if (file_path.empty() || !DeleteFileW(file_path.c_str()) ||
			!CreateDirectoryW(file_path.c_str(), nullptr)) {
			return nullptr;
		}
		std::memcpy(path_template, file_template.c_str(), file_template.size() + 1);
		return path_template;
	} catch (...) {
		return nullptr;
	}
}

extern "C" int darling_windows_unlink(const char* path)
{
	try {
		const auto wide_path = Utf8Path(path);
		return !wide_path.empty() && DeleteFileW(wide_path.c_str()) ? 0 : -1;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_rmdir(const char* path)
{
	try {
		const auto wide_path = Utf8Path(path);
		return !wide_path.empty() && RemoveDirectoryW(wide_path.c_str()) ? 0 : -1;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_link(const char* existing_path, const char* link_path)
{
	try {
		if (existing_path == nullptr || link_path == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto existing = Utf8Path(existing_path);
		const auto link = Utf8Path(link_path);
		if (existing.empty() || link.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		if (CreateHardLinkW(link.c_str(), existing.c_str(), nullptr))
			return 0;
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_symlink(const char* target, const char* link_path)
{
	try {
		const auto target_path = Utf8Path(target);
		const auto link = Utf8Path(link_path);
		if (target_path.empty() || link.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const DWORD attributes = GetFileAttributesW(target_path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		DWORD flags = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ?
			SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
		flags |= 0x2; // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
		if (!CreateSymbolicLinkW(link.c_str(), target_path.c_str(), flags) &&
			!CreateSymbolicLinkW(link.c_str(), target_path.c_str(),
				flags & ~0x2u)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_readlink(const char* path, char* buffer,
	std::size_t size)
{
	try {
		const auto wide_path = Utf8Path(path);
		if (wide_path.empty() || (buffer == nullptr && size != 0)) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const HANDLE handle = CreateFileW(wide_path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		std::array<std::uint8_t, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> data{};
		DWORD returned = 0;
		const BOOL queried = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT,
			nullptr, 0, data.data(), static_cast<DWORD>(data.size()), &returned, nullptr);
		const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (!queried) {
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
		auto* reparse = reinterpret_cast<const DarwinWindowsReparseDataBuffer*>(data.data());
		if (reparse->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
			darling::windows_host::DarwinErrno::Set(95);
			return -1;
		}
		const auto& link = reparse->SymbolicLinkReparseBuffer;
		const std::wstring target(link.PathBuffer + link.SubstituteNameOffset / sizeof(wchar_t),
			link.SubstituteNameLength / sizeof(wchar_t));
		const std::wstring normalized = target.rfind(L"\\??\\", 0) == 0 ?
			target.substr(4) : target;
		const auto utf8 = Utf8String(normalized);
		if (size != 0) {
			const auto copied = (std::min)(size, utf8.size());
			std::memcpy(buffer, utf8.data(), copied);
		}
		return static_cast<std::int64_t>(utf8.size());
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_access(const char* path, int mode)
{
	try {
		const auto wide_path = Utf8Path(path);
		if (wide_path.empty() || (mode & ~7) != 0 ||
			GetFileAttributesW(wide_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			return -1;
		}
		if ((mode & 2) != 0) {
			WIN32_FILE_ATTRIBUTE_DATA attributes{};
			if (!GetFileAttributesExW(wide_path.c_str(), GetFileExInfoStandard, &attributes) ||
				(attributes.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
				return -1;
			}
		}
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_rename(const char* old_path, const char* new_path)
{
	try {
		if (old_path == nullptr || new_path == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto old_wide_path = Utf8Path(old_path);
		const auto new_wide_path = Utf8Path(new_path);
		if (old_wide_path.empty() || new_wide_path.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		if (MoveFileExW(old_wide_path.c_str(), new_wide_path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
			return 0;
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_lseek(int descriptor, std::int64_t offset, int whence)
{
	try {
		return global_syscalls.Seek(descriptor, offset, whence);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_ftruncate(int descriptor, std::int64_t length)
{
	try {
		global_syscalls.Truncate(descriptor, length);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_truncate(const char* path, std::int64_t length)
{
	try {
		if (path == nullptr || length < 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto wide_path = Utf8Path(path);
		if (wide_path.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const HANDLE handle = CreateFileW(wide_path.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		LARGE_INTEGER position{};
		position.QuadPart = length;
		const bool positioned = SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) != FALSE;
		const bool resized = positioned && SetEndOfFile(handle) != FALSE;
		const DWORD error = (positioned && resized) ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (!resized) {
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_chmod(const char* path, int mode)
{
	try {
		if (path == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto wide_path = Utf8Path(path);
		const DWORD attributes = GetFileAttributesW(wide_path.c_str());
		if (wide_path.empty() || attributes == INVALID_FILE_ATTRIBUTES) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		// Windows has no POSIX mode bits. Map the aggregate Darwin write bits
		// to the NTFS readonly attribute and preserve all other attributes.
		const DWORD updated = (mode & 0222) == 0 ?
			(attributes | FILE_ATTRIBUTE_READONLY) :
			(attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
		return SetFileAttributesW(wide_path.c_str(), updated) ? 0 : -1;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fchmod(int descriptor, int mode)
{
	try {
		const auto path = global_syscalls.PathForDescriptor(descriptor);
		return path.empty() ? -1 : darling_windows_chmod(Utf8String(path.wstring()).c_str(), mode);
	} catch (...) {
		return -1;
	}
}

namespace {
thread_local std::string darling_pthread_name;
void RunPthreadTlsDestructors();

struct DarlingPthreadAttributes final {
	bool detached = false;
	std::size_t stack_size = 0;
	std::size_t guard_size = 4096;
};

DarlingPthreadAttributes* PthreadAttributesFromStorage(const void* storage)
{
	return storage == nullptr ? nullptr : *reinterpret_cast<DarlingPthreadAttributes* const*>(storage);
}

struct DarlingPthreadStart final {
	void* (*function)(void*) = nullptr;
	void* argument = nullptr;
	void* result = nullptr;
	std::atomic_bool detached = false;
	std::atomic_bool cancel_requested = false;
	std::atomic_bool cancel_enabled = true;
	std::atomic_bool cancel_deferred = true;
};

struct DarlingPthreadCancelled final {};
thread_local DarlingPthreadStart* current_pthread_start = nullptr;
thread_local darling_pthread_cleanup_record* current_pthread_cleanup = nullptr;

DWORD WINAPI DarlingPthreadThunk(void* raw)
{
	auto* start = static_cast<DarlingPthreadStart*>(raw);
	current_pthread_start = start;
	try {
		start->result = start->function(start->argument);
	} catch (const DarlingPthreadCancelled&) {
		start->result = reinterpret_cast<void*>(static_cast<std::uintptr_t>(-1));
	} catch (...) {
		start->result = nullptr;
	}
	current_pthread_start = nullptr;
	RunPthreadTlsDestructors();
	if (start->detached.load(std::memory_order_acquire))
		delete start;
	return 0;
}

std::mutex darling_pthread_mutex;
std::unordered_map<std::uint64_t, std::unique_ptr<DarlingPthreadStart>> darling_pthreads;
}

extern "C" std::uint64_t darling_windows_pthread_self()
{
	return static_cast<std::uint64_t>(GetCurrentThreadId());
}

extern "C" int darling_windows_pthread_equal(std::uint64_t left, std::uint64_t right)
{
	return left == right ? 1 : 0;
}

extern "C" int darling_windows_pthread_cancel(std::uint64_t thread)
{
	std::lock_guard lock(darling_pthread_mutex);
	auto found = darling_pthreads.find(thread);
	if (found == darling_pthreads.end()) return 3;
	found->second->cancel_requested.store(true, std::memory_order_release);
	return 0;
}

extern "C" void darling_windows_pthread_cleanup_push(
	darling_pthread_cleanup_record* record, void (*routine)(void*), void* argument)
{
	if (record == nullptr) return;
	record->routine = routine;
	record->argument = argument;
	record->next = current_pthread_cleanup;
	current_pthread_cleanup = record;
}

extern "C" void darling_windows_pthread_cleanup_pop(
	darling_pthread_cleanup_record* record, int execute)
{
	if (record == nullptr) return;
	if (current_pthread_cleanup == record)
		current_pthread_cleanup = record->next;
	if (execute != 0 && record->routine != nullptr)
		record->routine(record->argument);
}

void RunPthreadCleanupHandlers() noexcept
{
	while (current_pthread_cleanup != nullptr) {
		auto* record = current_pthread_cleanup;
		current_pthread_cleanup = record->next;
		if (record->routine != nullptr)
			record->routine(record->argument);
	}
}

extern "C" int darling_windows_pthread_setcancelstate(int state, int* old_state)
{
	if (current_pthread_start == nullptr || (state != 0 && state != 1)) return 22;
	const bool was_enabled = current_pthread_start->cancel_enabled.exchange(
		state == 0, std::memory_order_acq_rel);
	if (old_state != nullptr) *old_state = was_enabled ? 0 : 1;
	if (state == 0 && current_pthread_start->cancel_deferred.load(std::memory_order_acquire) &&
		current_pthread_start->cancel_requested.load(std::memory_order_acquire)) {
		RunPthreadCleanupHandlers();
		RunPthreadTlsDestructors();
		throw DarlingPthreadCancelled{};
	}
	return 0;
}

extern "C" int darling_windows_pthread_setcanceltype(int type, int* old_type)
{
	if (current_pthread_start == nullptr || (type != 0 && type != 1)) return 22;
	if (type == 1) return 95;
	const bool was_deferred = current_pthread_start->cancel_deferred.exchange(
		true, std::memory_order_acq_rel);
	if (old_type != nullptr) *old_type = was_deferred ? 0 : 1;
	return 0;
}

extern "C" int darling_windows_pthread_testcancel()
{
	if (current_pthread_start != nullptr &&
		current_pthread_start->cancel_enabled.load(std::memory_order_acquire) &&
		current_pthread_start->cancel_deferred.load(std::memory_order_acquire) &&
		current_pthread_start->cancel_requested.load(std::memory_order_acquire)) {
		RunPthreadTlsDestructors();
		throw DarlingPthreadCancelled{};
	}
	return 0;
}

extern "C" int darling_windows_pthread_setname_np(const char* name)
{
	if (name == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	darling_pthread_name.assign(name);
	return 0;
}

extern "C" int darling_windows_pthread_getname_np(std::uint64_t thread,
	char* buffer, std::size_t size)
{
	if (buffer == nullptr || size == 0 || thread != darling_windows_pthread_self()) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	const std::size_t copied = (std::min)(size - 1, darling_pthread_name.size());
	std::memcpy(buffer, darling_pthread_name.data(), copied);
	buffer[copied] = '\0';
	return 0;
}

extern "C" int darling_windows_pthread_attr_init(void* attributes)
{
	if (attributes == nullptr) return 22;
	*reinterpret_cast<DarlingPthreadAttributes**>(attributes) =
		new (std::nothrow) DarlingPthreadAttributes();
	return *reinterpret_cast<DarlingPthreadAttributes**>(attributes) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_attr_destroy(void* attributes)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr) return 22;
	delete value;
	*reinterpret_cast<DarlingPthreadAttributes**>(attributes) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_attr_setdetachstate(void* attributes, int state)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || (state != 0 && state != 1)) return 22;
	value->detached = state == 1;
	return 0;
}

extern "C" int darling_windows_pthread_attr_getdetachstate(const void* attributes, int* state)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || state == nullptr) return 22;
	*state = value->detached ? 1 : 0;
	return 0;
}

extern "C" int darling_windows_pthread_attr_setstacksize(void* attributes, std::size_t size)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || size < 64 * 1024) return 22;
	value->stack_size = size;
	return 0;
}

extern "C" int darling_windows_pthread_attr_getstacksize(const void* attributes, std::size_t* size)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || size == nullptr) return 22;
	*size = value->stack_size == 0 ? 1024 * 1024 : value->stack_size;
	return 0;
}

extern "C" int darling_windows_pthread_attr_setguardsize(void* attributes, std::size_t size)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || (size != 0 && size < 4096)) return 22;
	value->guard_size = size;
	return 0;
}

extern "C" int darling_windows_pthread_attr_getguardsize(const void* attributes, std::size_t* size)
{
	auto* value = PthreadAttributesFromStorage(attributes);
	if (value == nullptr || size == nullptr) return 22;
	*size = value->guard_size;
	return 0;
}

extern "C" int darling_windows_pthread_create(std::uint64_t* thread,
	const void* attributes, void* (*start)(void*), void* argument)
{
	if (thread == nullptr || start == nullptr) return 22;
	auto context = std::make_unique<DarlingPthreadStart>();
	context->function = start;
	context->argument = argument;
	const auto* attr = PthreadAttributesFromStorage(attributes);
	if (attributes != nullptr && attr == nullptr) return 22;
	context->detached.store(attr != nullptr && attr->detached, std::memory_order_release);
	const SIZE_T stack_size = attr == nullptr ? 0 : static_cast<SIZE_T>(attr->stack_size);
	HANDLE handle = CreateThread(nullptr, stack_size, DarlingPthreadThunk, context.get(), 0, nullptr);
	if (handle == nullptr) return 11;
	const auto token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
	{
		std::lock_guard lock(darling_pthread_mutex);
		darling_pthreads.emplace(token, std::move(context));
	}
	*thread = token;
	if (attr != nullptr && attr->detached) {
		// The context remains owned by the map until the thunk observes the flag;
		// remove the ownership here and let the thunk reclaim it after execution.
		std::unique_ptr<DarlingPthreadStart> detached_context;
		{
			std::lock_guard lock(darling_pthread_mutex);
			detached_context = std::move(darling_pthreads[token]);
			darling_pthreads.erase(token);
		}
		detached_context.release();
	}
	return 0;
}

extern "C" int darling_windows_pthread_join(std::uint64_t thread, void** result)
{
	std::unique_ptr<DarlingPthreadStart>* context = nullptr;
	HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(thread));
	{
		std::lock_guard lock(darling_pthread_mutex);
		auto found = darling_pthreads.find(thread);
		if (found == darling_pthreads.end()) return 3;
		context = &found->second;
	}
	darling_windows_pthread_testcancel();
	for (;;) {
		const auto wait_result = WaitForSingleObject(handle, 10);
		if (wait_result == WAIT_OBJECT_0) break;
		if (wait_result == WAIT_FAILED) return 4;
		darling_windows_pthread_testcancel();
	}
	if (result != nullptr) *result = (*context)->result;
	CloseHandle(handle);
	{
		std::lock_guard lock(darling_pthread_mutex);
		darling_pthreads.erase(thread);
	}
	return 0;
}

extern "C" int darling_windows_pthread_detach(std::uint64_t thread)
{
	std::unique_ptr<DarlingPthreadStart> context;
	HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(thread));
	{
		std::lock_guard lock(darling_pthread_mutex);
		auto found = darling_pthreads.find(thread);
		if (found == darling_pthreads.end()) return 3;
		context = std::move(found->second);
		darling_pthreads.erase(found);
	}
	context->detached.store(true, std::memory_order_release);
	context.release();
	CloseHandle(handle);
	return 0;
}

extern "C" int darling_windows_pthread_threadid_np(std::uint64_t thread,
	std::uint64_t* thread_id)
{
	if (thread_id == nullptr || (thread != 0 && thread != darling_windows_pthread_self())) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	*thread_id = darling_windows_pthread_self();
	return 0;
}

namespace {
struct DarlingPthreadMutex final {
	explicit DarlingPthreadMutex(int requested_type)
		: type(requested_type), recursive(requested_type == 1) {}

	int lock_result()
	{
		if (type == 2 && owner.load(std::memory_order_acquire) == GetCurrentThreadId())
			return 35;
		if (recursive) recursive_mutex.lock(); else mutex.lock();
		if (type == 2) owner.store(GetCurrentThreadId(), std::memory_order_release);
		return 0;
	}
	int try_lock_result()
	{
		if (type == 2 && owner.load(std::memory_order_acquire) == GetCurrentThreadId())
			return 16;
		const bool acquired = recursive ? recursive_mutex.try_lock() : mutex.try_lock();
		if (!acquired) return 16;
		if (type == 2) owner.store(GetCurrentThreadId(), std::memory_order_release);
		return 0;
	}
	bool try_lock() { return try_lock_result() == 0; }
	void lock() { (void)lock_result(); }
	int unlock_result()
	{
		if (type == 2 && owner.load(std::memory_order_acquire) != GetCurrentThreadId())
			return 1;
		if (recursive) recursive_mutex.unlock(); else mutex.unlock();
		if (type == 2) owner.store(0, std::memory_order_release);
		return 0;
	}
	void unlock() { (void)unlock_result(); }

	int type = 0;
	bool recursive = false;
	std::atomic<DWORD> owner{0};
	std::mutex mutex;
	std::recursive_mutex recursive_mutex;
};

DarlingPthreadMutex* PthreadMutexFromStorage(void* storage)
{
	if (storage == nullptr) return nullptr;
	return *reinterpret_cast<DarlingPthreadMutex**>(storage);
}

struct DarlingPthreadMutexAttributes final {
	int type = 0;
	int pshared = 0;
	int protocol = 0;
	int robust = 0;
};
struct DarlingPthreadConditionAttributes final {
	int pshared = 0;
	int clock_id = 0;
};

DarlingPthreadMutexAttributes* PthreadMutexAttributesFromStorage(const void* storage)
{
	return storage == nullptr ? nullptr :
		*reinterpret_cast<DarlingPthreadMutexAttributes* const*>(storage);
}
}

extern "C" int darling_windows_pthread_mutexattr_init(void* attributes)
{
	if (attributes == nullptr) return 22;
	*reinterpret_cast<DarlingPthreadMutexAttributes**>(attributes) =
		new (std::nothrow) DarlingPthreadMutexAttributes();
	return *reinterpret_cast<DarlingPthreadMutexAttributes**>(attributes) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_mutexattr_destroy(void* attributes)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr) return 22;
	delete value;
	*reinterpret_cast<DarlingPthreadMutexAttributes**>(attributes) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_mutexattr_settype(void* attributes, int type)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || (type != 0 && type != 1 && type != 2)) return 22;
	value->type = type;
	return 0;
}

extern "C" int darling_windows_pthread_mutexattr_gettype(const void* attributes, int* type)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || type == nullptr) return 22;
	*type = value->type;
	return 0;
}

extern "C" int darling_windows_pthread_mutexattr_setpshared(void* attributes, int shared)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || (shared != 0 && shared != 1)) return 22;
	value->pshared = shared;
	return shared == 0 ? 0 : 95;
}

extern "C" int darling_windows_pthread_mutexattr_getpshared(const void* attributes, int* shared)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || shared == nullptr) return 22;
	*shared = value->pshared;
	return 0;
}

extern "C" int darling_windows_pthread_mutexattr_setprotocol(void* attributes, int protocol)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || (protocol != 0 && protocol != 1 && protocol != 2)) return 22;
	value->protocol = protocol;
	return protocol == 0 ? 0 : 95;
}

extern "C" int darling_windows_pthread_mutexattr_getprotocol(const void* attributes, int* protocol)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || protocol == nullptr) return 22;
	*protocol = value->protocol;
	return 0;
}

extern "C" int darling_windows_pthread_mutexattr_setrobust(void* attributes, int robust)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || (robust != 0 && robust != 1)) return 22;
	value->robust = robust;
	return robust == 0 ? 0 : 95;
}

extern "C" int darling_windows_pthread_mutexattr_getrobust(const void* attributes, int* robust)
{
	auto* value = PthreadMutexAttributesFromStorage(attributes);
	if (value == nullptr || robust == nullptr) return 22;
	*robust = value->robust;
	return 0;
}

extern "C" int darling_windows_pthread_mutex_init(void* mutex, const void* attributes)
{
	if (mutex == nullptr) return 22;
	const auto* attr = PthreadMutexAttributesFromStorage(attributes);
	if (attributes != nullptr && attr == nullptr) return 22;
	if (attr != nullptr && (attr->pshared != 0 || attr->protocol != 0 || attr->robust != 0)) return 95;
	const int type = attr == nullptr ? 0 : attr->type;
	*reinterpret_cast<DarlingPthreadMutex**>(mutex) =
		new (std::nothrow) DarlingPthreadMutex(type);
	return *reinterpret_cast<DarlingPthreadMutex**>(mutex) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_mutex_destroy(void* mutex)
{
	auto* value = PthreadMutexFromStorage(mutex);
	if (value == nullptr) return 22;
	delete value;
	*reinterpret_cast<DarlingPthreadMutex**>(mutex) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_mutex_lock(void* mutex)
{
	auto* value = PthreadMutexFromStorage(mutex);
	if (value == nullptr) return 22;
	darling_windows_pthread_testcancel();
	if (value->type == 2) {
		const auto result = value->lock_result();
		if (result != 0) return result;
		return 0;
	}
	while (!value->try_lock()) {
		Sleep(1);
		darling_windows_pthread_testcancel();
	}
	return 0;
}

extern "C" int darling_windows_pthread_mutex_trylock(void* mutex)
{
	auto* value = PthreadMutexFromStorage(mutex);
	if (value == nullptr) return 22;
	return value->try_lock_result();
}

extern "C" int darling_windows_pthread_mutex_unlock(void* mutex)
{
	auto* value = PthreadMutexFromStorage(mutex);
	if (value == nullptr) return 22;
	return value->unlock_result();
}

namespace {
std::condition_variable_any* PthreadConditionFromStorage(void* storage)
{
	if (storage == nullptr) return nullptr;
	return *reinterpret_cast<std::condition_variable_any**>(storage);
}
}

extern "C" int darling_windows_pthread_cond_init(void* condition, const void* attributes)
{
	if (condition == nullptr) return 22;
	const auto* attr = attributes == nullptr ? nullptr :
		*reinterpret_cast<const DarlingPthreadConditionAttributes* const*>(attributes);
	if (attributes != nullptr && attr == nullptr) return 22;
	if (attr != nullptr && attr->pshared != 0) return 95;
	*reinterpret_cast<std::condition_variable_any**>(condition) =
		new (std::nothrow) std::condition_variable_any();
	return *reinterpret_cast<std::condition_variable_any**>(condition) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_condattr_init(void* attributes)
{
	if (attributes == nullptr) return 22;
	*reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes) =
		new (std::nothrow) DarlingPthreadConditionAttributes();
	return *reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_condattr_destroy(void* attributes)
{
	auto* value = attributes == nullptr ? nullptr :
		*reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes);
	if (value == nullptr) return 22;
	delete value;
	*reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_condattr_setpshared(void* attributes, int shared)
{
	auto* value = attributes == nullptr ? nullptr :
		*reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes);
	if (value == nullptr || (shared != 0 && shared != 1)) return 22;
	value->pshared = shared;
	return shared == 0 ? 0 : 95;
}

extern "C" int darling_windows_pthread_condattr_getpshared(const void* attributes, int* shared)
{
	auto* value = attributes == nullptr ? nullptr :
		*reinterpret_cast<const DarlingPthreadConditionAttributes* const*>(attributes);
	if (value == nullptr || shared == nullptr) return 22;
	*shared = value->pshared;
	return 0;
}

extern "C" int darling_windows_pthread_condattr_setclock(void* attributes, int clock_id)
{
	auto* value = attributes == nullptr ? nullptr :
		*reinterpret_cast<DarlingPthreadConditionAttributes**>(attributes);
	if (value == nullptr || (clock_id != 0 && clock_id != 1)) return 22;
	value->clock_id = clock_id;
	return clock_id == 0 ? 0 : 95;
}

extern "C" int darling_windows_pthread_condattr_getclock(const void* attributes, int* clock_id)
{
	auto* value = attributes == nullptr ? nullptr :
		*reinterpret_cast<const DarlingPthreadConditionAttributes* const*>(attributes);
	if (value == nullptr || clock_id == nullptr) return 22;
	*clock_id = value->clock_id;
	return 0;
}

extern "C" int darling_windows_pthread_cond_destroy(void* condition)
{
	auto* value = PthreadConditionFromStorage(condition);
	if (value == nullptr) return 22;
	delete value;
	*reinterpret_cast<std::condition_variable_any**>(condition) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_cond_wait(void* condition, void* mutex)
{
	auto* condition_value = PthreadConditionFromStorage(condition);
	auto* mutex_value = PthreadMutexFromStorage(mutex);
	if (condition_value == nullptr || mutex_value == nullptr) return 22;
	std::unique_lock lock(*mutex_value, std::adopt_lock);
	try {
		darling_windows_pthread_testcancel();
		while (condition_value->wait_for(lock, std::chrono::milliseconds(10)) ==
			std::cv_status::timeout)
			darling_windows_pthread_testcancel();
	} catch (const DarlingPthreadCancelled&) {
		/* POSIX cancellation of cond_wait reacquires the mutex first. */
		if (!lock.owns_lock()) lock.lock();
		lock.release();
		throw;
	}
	lock.release();
	return 0;
}

extern "C" int darling_windows_pthread_cond_timedwait(void* condition, void* mutex,
	const darling_timespec* deadline)
{
	auto* condition_value = PthreadConditionFromStorage(condition);
	auto* mutex_value = PthreadMutexFromStorage(mutex);
	if (condition_value == nullptr || mutex_value == nullptr || deadline == nullptr ||
		deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000) return 22;
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	const auto deadline_duration = std::chrono::seconds(deadline->tv_sec) +
		std::chrono::nanoseconds(deadline->tv_nsec);
	const auto remaining = deadline_duration - now;
	std::unique_lock lock(*mutex_value, std::adopt_lock);
	try {
		darling_windows_pthread_testcancel();
		if (remaining <= std::chrono::nanoseconds::zero()) {
			lock.release();
			return 110;
		}
		const auto status = condition_value->wait_for(lock,
			(remaining > std::chrono::milliseconds(10) ?
				std::chrono::milliseconds(10) : remaining));
		if (status == std::cv_status::timeout && remaining > std::chrono::milliseconds(10)) {
			lock.release();
			return darling_windows_pthread_cond_timedwait(condition, mutex, deadline);
		}
		darling_windows_pthread_testcancel();
		lock.release();
		return status == std::cv_status::timeout ? 110 : 0;
	} catch (const DarlingPthreadCancelled&) {
		if (!lock.owns_lock()) lock.lock();
		lock.release();
		throw;
	}
}

extern "C" int darling_windows_pthread_cond_signal(void* condition)
{
	auto* value = PthreadConditionFromStorage(condition);
	if (value == nullptr) return 22;
	value->notify_one();
	return 0;
}

extern "C" int darling_windows_pthread_cond_broadcast(void* condition)
{
	auto* value = PthreadConditionFromStorage(condition);
	if (value == nullptr) return 22;
	value->notify_all();
	return 0;
}

namespace {
std::shared_mutex* PthreadRwlockFromStorage(void* storage)
{
	if (storage == nullptr) return nullptr;
	return *reinterpret_cast<std::shared_mutex**>(storage);
}
thread_local std::unordered_map<void*, bool> darling_rwlock_read_modes;
std::atomic<std::uint64_t> darling_next_pthread_key{1};
std::mutex darling_pthread_key_mutex;
std::unordered_map<std::uint64_t, void (*)(void*)> darling_pthread_key_destructors;
thread_local std::unordered_map<std::uint64_t, const void*> darling_pthread_values;

void RunPthreadTlsDestructors()
{
	for (int pass = 0; pass < 4; ++pass) {
		bool called = false;
		for (auto& [key, value] : darling_pthread_values) {
			if (value == nullptr) continue;
			void (*destructor)(void*) = nullptr;
			{
				std::lock_guard lock(darling_pthread_key_mutex);
				const auto found = darling_pthread_key_destructors.find(key);
				if (found != darling_pthread_key_destructors.end()) destructor = found->second;
			}
			if (destructor != nullptr) {
				const void* destructor_value = value;
				value = nullptr;
				destructor(const_cast<void*>(destructor_value));
				called = true;
			}
		}
		if (!called) break;
	}
	darling_pthread_values.clear();
}
}

extern "C" int darling_windows_pthread_rwlock_init(void* lock, const void* attributes)
{
	(void)attributes;
	if (lock == nullptr) return 22;
	*reinterpret_cast<std::shared_mutex**>(lock) = new (std::nothrow) std::shared_mutex();
	return *reinterpret_cast<std::shared_mutex**>(lock) == nullptr ? 12 : 0;
}

extern "C" int darling_windows_pthread_rwlock_destroy(void* lock)
{
	auto* value = PthreadRwlockFromStorage(lock);
	if (value == nullptr) return 22;
	darling_rwlock_read_modes.erase(lock);
	delete value;
	*reinterpret_cast<std::shared_mutex**>(lock) = nullptr;
	return 0;
}

extern "C" int darling_windows_pthread_rwlock_rdlock(void* lock)
{
	auto* value = PthreadRwlockFromStorage(lock);
	if (value == nullptr) return 22;
	darling_windows_pthread_testcancel();
	while (!value->try_lock_shared()) {
		Sleep(1);
		darling_windows_pthread_testcancel();
	}
	darling_rwlock_read_modes[lock] = true;
	return 0;
}

extern "C" int darling_windows_pthread_rwlock_wrlock(void* lock)
{
	auto* value = PthreadRwlockFromStorage(lock);
	if (value == nullptr) return 22;
	darling_windows_pthread_testcancel();
	while (!value->try_lock()) {
		Sleep(1);
		darling_windows_pthread_testcancel();
	}
	darling_rwlock_read_modes[lock] = false;
	return 0;
}

extern "C" int darling_windows_pthread_rwlock_unlock(void* lock)
{
	auto* value = PthreadRwlockFromStorage(lock);
	if (value == nullptr) return 22;
	const auto found = darling_rwlock_read_modes.find(lock);
	if (found != darling_rwlock_read_modes.end() && found->second)
		value->unlock_shared();
	else
		value->unlock();
	darling_rwlock_read_modes.erase(lock);
	return 0;
}

extern "C" int darling_windows_pthread_key_create(std::uint64_t* key,
	void (*destructor)(void*))
{
	if (key == nullptr) return 22;
	const auto value = darling_next_pthread_key.fetch_add(1, std::memory_order_relaxed);
	if (value == 0) return 11;
	{
		std::lock_guard lock(darling_pthread_key_mutex);
		darling_pthread_key_destructors.emplace(value, destructor);
	}
	*key = value;
	return 0;
}

extern "C" int darling_windows_pthread_key_delete(std::uint64_t key)
{
	std::lock_guard lock(darling_pthread_key_mutex);
	if (darling_pthread_key_destructors.erase(key) == 0) return 22;
	darling_pthread_values.erase(key);
	return 0;
}

extern "C" int darling_windows_pthread_setspecific(std::uint64_t key, const void* value)
{
	{
		std::lock_guard lock(darling_pthread_key_mutex);
		if (darling_pthread_key_destructors.find(key) == darling_pthread_key_destructors.end())
			return 22;
	}
	darling_pthread_values[key] = value;
	return 0;
}

extern "C" const void* darling_windows_pthread_getspecific(std::uint64_t key)
{
	{
		std::lock_guard lock(darling_pthread_key_mutex);
		if (darling_pthread_key_destructors.find(key) == darling_pthread_key_destructors.end())
			return nullptr;
	}
	const auto found = darling_pthread_values.find(key);
	return found == darling_pthread_values.end() ? nullptr : found->second;
}

extern "C" int darling_windows_pthread_once(void* control, void (*initializer)(void))
{
	if (control == nullptr || initializer == nullptr) return 22;
	auto** flag = reinterpret_cast<std::once_flag**>(control);
	if (*flag == nullptr) *flag = new (std::nothrow) std::once_flag();
	if (*flag == nullptr) return 12;
	try {
		std::call_once(**flag, initializer);
		return 0;
	} catch (...) {
		return 22;
	}
}

extern "C" int darling_windows_umask(int mode)
{
	if ((mode & ~0777) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::lock_guard lock(umask_mutex);
	const int previous = darling_umask_value;
	darling_umask_value = mode & 0777;
	return previous;
}

extern "C" int darling_windows_getloadavg(double* loads, int count)
{
	if (loads == nullptr || count <= 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	FILETIME idle{}, kernel{}, user{};
	if (!GetSystemTimes(&idle, &kernel, &user)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	ULARGE_INTEGER idle_ticks{};
	ULARGE_INTEGER kernel_ticks{};
	ULARGE_INTEGER user_ticks{};
	idle_ticks.LowPart = idle.dwLowDateTime; idle_ticks.HighPart = idle.dwHighDateTime;
	kernel_ticks.LowPart = kernel.dwLowDateTime; kernel_ticks.HighPart = kernel.dwHighDateTime;
	user_ticks.LowPart = user.dwLowDateTime; user_ticks.HighPart = user.dwHighDateTime;
	const auto total = kernel_ticks.QuadPart + user_ticks.QuadPart;
	const auto active = total > idle_ticks.QuadPart ? total - idle_ticks.QuadPart : 0;
	const double load = total == 0 ? 0.0 : static_cast<double>(active) /
		static_cast<double>(total) * static_cast<double>(darling_windows_sysconf(57));
	for (int index = 0; index < count; ++index) loads[index] = load;
	return count;
}

extern "C" int darling_windows_fsync(int descriptor)
{
	try {
		global_syscalls.Flush(descriptor);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fdatasync(int descriptor)
{
	return darling_windows_fsync(descriptor);
}

extern "C" int darling_windows_utimensat(int directory_descriptor, const char* path,
	const darling_timespec times[2], int flags)
{
	try {
		constexpr int at_fdcwd = -2;
		constexpr int at_symlink_nofollow = 0x20;
		constexpr std::int64_t utime_now = 0x3fffffff;
		constexpr std::int64_t utime_omit = 0x3ffffffe;
		if (path == nullptr || (flags & ~at_symlink_nofollow) != 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		auto requested_path = Utf8Path(path);
		if (requested_path.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		if (!requested_path.is_absolute() && directory_descriptor != at_fdcwd) {
			requested_path = global_syscalls.PathForDescriptor(directory_descriptor) /
				requested_path;
		}
		const DWORD disposition = (flags & at_symlink_nofollow) != 0 ?
			(FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS) :
			FILE_FLAG_BACKUP_SEMANTICS;
		const HANDLE handle = CreateFileW(requested_path.c_str(), FILE_WRITE_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, disposition, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		FILETIME access_time{}, write_time{};
		FILETIME current_time{};
		GetSystemTimeAsFileTime(&current_time);
		const auto convert = [&](const darling_timespec& value, FILETIME& output,
			bool& keep) -> bool {
			if (value.tv_sec == utime_omit) {
				keep = true;
				return true;
			}
			if (value.tv_sec == utime_now) {
				output = current_time;
				return true;
			}
			if (value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1'000'000'000) {
				return false;
			}
			const auto ticks = static_cast<std::uint64_t>(value.tv_sec) * 10'000'000ull +
				static_cast<std::uint64_t>(value.tv_nsec / 100);
			constexpr std::uint64_t epoch_offset = 11644473600ull * 10'000'000ull;
			if (ticks > (std::numeric_limits<std::uint64_t>::max)() - epoch_offset) {
				return false;
			}
			const auto windows_ticks = ticks + epoch_offset;
			output.dwLowDateTime = static_cast<DWORD>(windows_ticks);
			output.dwHighDateTime = static_cast<DWORD>(windows_ticks >> 32);
			return true;
		};
		bool keep_access = false;
		bool keep_write = false;
		const bool valid = times == nullptr ? true :
			convert(times[0], access_time, keep_access) && convert(times[1], write_time, keep_write);
		const FILETIME* access_pointer = times == nullptr ? &current_time :
			(keep_access ? nullptr : &access_time);
		const FILETIME* write_pointer = times == nullptr ? &current_time :
			(keep_write ? nullptr : &write_time);
		const bool updated = valid && SetFileTime(handle, nullptr, access_pointer, write_pointer) != FALSE;
		const DWORD error = updated ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (!updated) {
			if (!valid) {
				darling::windows_host::DarwinErrno::Set(22);
			} else {
				darling::windows_host::DarwinErrno::SetFromWin32(error);
			}
			return -1;
		}
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_futimens(int descriptor, const darling_timespec times[2])
{
	try {
		const auto path = global_syscalls.PathForDescriptor(descriptor);
		return path.empty() ? -1 : darling_windows_utimensat(-2,
			Utf8String(path.wstring()).c_str(), times, 0);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_chdir(const char* path)
{
	try {
		darling::windows_host::DarwinPaths::ChangeWorkingDirectory(Utf8Path(path));
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" char* darling_windows_getcwd(char* buffer, std::size_t size)
{
	try {
		if (buffer == nullptr || size == 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return nullptr;
		}
		const auto value = Utf8String(
			darling::windows_host::DarwinPaths::CurrentWorkingDirectory().wstring());
		if (value.empty() || value.size() + 1 > size) {
			darling::windows_host::DarwinErrno::Set(34);
			return nullptr;
		}
		std::memcpy(buffer, value.data(), value.size());
		buffer[value.size()] = '\0';
		return buffer;
	} catch (...) {
		return nullptr;
	}
}

extern "C" char* darling_windows_realpath(const char* path, char* resolved)
{
	try {
		if (path == nullptr || resolved == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return nullptr;
		}
		const auto wide_path = Utf8Path(path);
		if (wide_path.empty()) {
			return nullptr;
		}
		auto fallback_absolute = [&]() -> char* {
			const auto absolute = std::filesystem::absolute(wide_path).lexically_normal();
			const auto utf8_result = Utf8String(absolute.wstring());
			if (utf8_result.empty())
				return static_cast<char*>(nullptr);
			std::memcpy(resolved, utf8_result.c_str(), utf8_result.size() + 1);
			return resolved;
		};
		const HANDLE handle = CreateFileW(wide_path.c_str(), 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			// Some restricted Windows sandboxes deny a zero-access directory
			// handle even though the path is readable.  Preserve realpath's
			// useful absolute-path behavior in that case; symlink resolution
			// still uses the handle-based path below whenever available.
			return fallback_absolute();
		}
		std::vector<wchar_t> wide_result(32768);
		const DWORD length = GetFinalPathNameByHandleW(handle, wide_result.data(),
			static_cast<DWORD>(wide_result.size()), FILE_NAME_NORMALIZED);
		const DWORD error = length == 0 ? GetLastError() : ERROR_SUCCESS;
		CloseHandle(handle);
		if (length == 0 && error != ERROR_SUCCESS)
			return fallback_absolute();
		if (length == 0 || length >= wide_result.size()) {
			return fallback_absolute();
		}
		std::wstring normalized(wide_result.data(), length);
		if (normalized.rfind(L"\\\\?\\UNC\\", 0) == 0) {
			normalized.erase(0, 8);
			normalized.insert(0, L"\\\\");
		} else if (normalized.rfind(L"\\\\?\\", 0) == 0) {
			normalized.erase(0, 4);
		}
		const auto utf8_result = Utf8String(normalized);
		if (utf8_result.empty()) {
			return nullptr;
		}
		std::memcpy(resolved, utf8_result.c_str(), utf8_result.size() + 1);
		return resolved;
	} catch (...) {
		return nullptr;
	}
}

extern "C" int darling_windows_mkdir(const char* path, int)
{
	try {
		darling::windows_host::DarwinFilesystem::MakeDirectory(Utf8Path(path));
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_getdirentries(int descriptor, char* buffer,
	std::size_t buffer_size, std::int64_t* base)
{
	if (buffer == nullptr || base == nullptr || buffer_size < sizeof(darling_dirent)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	try {
		const auto path = global_syscalls.PathForDescriptor(descriptor);
		const auto entries = darling::windows_host::DarwinFilesystem::ListDirectory(path);
		const std::size_t start = *base < 0 ? 0 : static_cast<std::size_t>(*base);
		const std::size_t capacity = buffer_size / sizeof(darling_dirent);
		std::size_t written = 0;
		for (std::size_t index = start; index < entries.size() && written < capacity; ++index) {
			darling_dirent record{};
			const std::string name = Utf8String(entries[index]);
			if (name.size() >= sizeof(record.d_name))
				continue;
			std::memcpy(record.d_name, name.c_str(), name.size() + 1);
			record.d_namlen = static_cast<std::uint16_t>(name.size());
			record.d_reclen = static_cast<std::uint16_t>(sizeof(record));
			record.d_seekoff = static_cast<std::int64_t>(index + 1);
			record.d_ino = static_cast<std::uint64_t>(std::hash<std::string>{}(name));
			record.d_type = std::filesystem::is_directory(path / entries[index]) ? 4 : 8;
			std::memcpy(buffer + written * sizeof(record), &record, sizeof(record));
			++written;
		}
		*base = static_cast<std::int64_t>(start + written);
		return static_cast<std::int64_t>(written * sizeof(darling_dirent));
	} catch (const std::system_error&) {
		return -1;
	}
}

extern "C" std::int64_t darling_windows_getdirentries64(int descriptor, char* buffer,
	std::size_t buffer_size, std::int64_t* base)
{
	return darling_windows_getdirentries(descriptor, buffer, buffer_size, base);
}

extern "C" int darling_windows_stat(const char* path, darling_darwin_stat* result)
{
	try {
		// stat(2) follows the final symbolic link; lstat(2) below is the
		// explicit no-follow variant for the reparse point itself.
		return FillPathStat(Utf8Path(path), result, false);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_lstat(const char* path, darling_darwin_stat* result)
{
	try {
		// lstat(2) must inspect the reparse point itself rather than its target.
		if (path == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		return FillPathStat(Utf8Path(path), result, true);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fstat(int descriptor, darling_darwin_stat* result)
{
	try {
		if (result == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		BY_HANDLE_FILE_INFORMATION information{};
		global_syscalls.GetFileInformation(descriptor, &information);
		FillDarwinStat(information, result);
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fstatat(int directory_descriptor, const char* path,
	darling_darwin_stat* result, int flags)
{
	constexpr int at_fdcwd = -2;
	constexpr int at_symlink_nofollow = 0x20;
	try {
		if ((flags & ~at_symlink_nofollow) != 0) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		if (path == nullptr || result == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		auto requested_path = Utf8Path(path);
		if (requested_path.empty()) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		if (!requested_path.is_absolute() && directory_descriptor != at_fdcwd) {
			requested_path = global_syscalls.PathForDescriptor(directory_descriptor) /
				requested_path;
		}
		// The current Windows path bridge does not yet have no-follow reparse
		// point metadata; keep the accepted flag explicit while preserving the
		// same documented behavior as lstat.
		return FillPathStat(requested_path, result, (flags & at_symlink_nofollow) != 0);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_statfs(const char* path, darling_darwin_statfs* result)
{
	try {
		if (path == nullptr) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		return FillStatfs(Utf8Path(path), result);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_fstatfs(int descriptor, darling_darwin_statfs* result)
{
	try {
		return FillStatfs(global_syscalls.PathForDescriptor(descriptor), result);
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_getfsstat(darling_darwin_statfs* buffer,
	int buffer_size, int)
{
	try {
		if (buffer_size < 0 || (buffer == nullptr && buffer_size != 0)) {
			darling::windows_host::DarwinErrno::Set(22);
			return -1;
		}
		const auto roots = WindowsVolumeRoots();
		const int entry_count = static_cast<int>(roots.size());
		if (entry_count == 0) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		if (buffer == nullptr || buffer_size == 0) {
			return entry_count;
		}
		const auto capacity = static_cast<std::size_t>(buffer_size) /
			sizeof(darling_darwin_statfs);
		const auto copied = (std::min)(capacity, roots.size());
		for (std::size_t index = 0; index < copied; ++index) {
			if (FillStatfs(roots[index], buffer + index) != 0) {
				return index == 0 ? -1 : static_cast<int>(index);
			}
		}
		return static_cast<int>(copied);
	} catch (...) {
		return -1;
	}
}

extern "C" void* darling_windows_mmap(void* address, std::size_t length,
	int protection, int flags, int descriptor, std::int64_t offset)
{
	constexpr int map_shared = 0x0001;
	constexpr int map_private = 0x0002;
	constexpr int map_fixed = 0x0010;
	constexpr int map_anon = 0x1000;
	if (length == 0 || (protection & ~0x0007) != 0 || offset < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return reinterpret_cast<void*>(-1);
	}
	if ((flags & map_anon) != 0) {
		return darling_windows_mmap_anonymous(address, length, protection, flags,
			descriptor, offset);
	}
	if (descriptor < 0 || (flags & (map_shared | map_private)) == 0 ||
		((flags & map_shared) != 0 && (flags & map_private) != 0)) {
		darling::windows_host::DarwinErrno::Set(22);
		return reinterpret_cast<void*>(-1);
	}
	SYSTEM_INFO system_info{};
	GetSystemInfo(&system_info);
	const std::uint64_t granularity = system_info.dwAllocationGranularity == 0 ?
		65536ull : system_info.dwAllocationGranularity;
	const auto unsigned_offset = static_cast<std::uint64_t>(offset);
	const auto aligned_offset = unsigned_offset - (unsigned_offset % granularity);
	const auto delta = static_cast<std::size_t>(unsigned_offset - aligned_offset);
	if (delta > (std::numeric_limits<std::size_t>::max)() - length ||
		aligned_offset > (std::numeric_limits<std::uint64_t>::max)() - delta - length) {
		darling::windows_host::DarwinErrno::Set(75);
		return reinterpret_cast<void*>(-1);
	}
	const auto map_bytes = delta + length;
	const auto mapping_end = aligned_offset + map_bytes;
	const std::uint64_t page_size = system_info.dwPageSize == 0 ? 4096ull :
		system_info.dwPageSize;
	if (static_cast<std::uint64_t>(map_bytes) >
		(std::numeric_limits<std::uint64_t>::max)() - (page_size - 1)) {
		darling::windows_host::DarwinErrno::Set(75);
		return reinterpret_cast<void*>(-1);
	}
	const auto rounded_view_bytes = static_cast<std::uint64_t>(
		(map_bytes + page_size - 1) / page_size * page_size);
	if (rounded_view_bytes > (std::numeric_limits<std::size_t>::max)()) {
		darling::windows_host::DarwinErrno::Set(75);
		return reinterpret_cast<void*>(-1);
	}
	const auto view_bytes = static_cast<std::size_t>(rounded_view_bytes);
	if (aligned_offset > (std::numeric_limits<std::uint64_t>::max)() - rounded_view_bytes) {
		darling::windows_host::DarwinErrno::Set(75);
		return reinterpret_cast<void*>(-1);
	}
	const auto mapping_size = (std::max)(mapping_end,
		aligned_offset + rounded_view_bytes);
	const bool write = (protection & 2) != 0;
	const bool execute = (protection & 4) != 0;
	const bool private_mapping = (flags & map_private) != 0;
	DWORD page_protection = PAGE_READONLY;
	if (execute) {
		page_protection = private_mapping ?
			(write ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READ) :
			(write ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ);
	} else if (private_mapping && write) {
		// Windows implements the per-view copy through FILE_MAP_COPY; the
		// mapping object itself must remain read/write for this view to be
		// readable on all supported Windows configurations.
		page_protection = PAGE_READWRITE;
	} else if (write) {
		page_protection = PAGE_READWRITE;
	}
	DWORD desired_access = private_mapping ? FILE_MAP_COPY : FILE_MAP_READ;
	if (execute) desired_access |= FILE_MAP_EXECUTE;
	if (private_mapping) {
		// FILE_MAP_COPY itself grants read/write copy access.
	} else if (write) {
		desired_access |= FILE_MAP_WRITE;
	}
	void* requested_base = nullptr;
	if ((flags & map_fixed) != 0) {
		if (address == nullptr || reinterpret_cast<std::uintptr_t>(address) < delta) {
			darling::windows_host::DarwinErrno::Set(22);
			return reinterpret_cast<void*>(-1);
		}
		requested_base = static_cast<std::uint8_t*>(address) - delta;
		if ((reinterpret_cast<std::uintptr_t>(requested_base) % granularity) != 0) {
			darling::windows_host::DarwinErrno::Set(95);
			return reinterpret_cast<void*>(-1);
		}
	}
	const auto high = static_cast<DWORD>(mapping_size >> 32);
	const auto low = static_cast<DWORD>(mapping_size & 0xffffffffu);
	HANDLE mapping = CreateFileMappingW(global_syscalls.MappingHandle(descriptor),
		nullptr, page_protection, high, low, nullptr);
	if (mapping == nullptr) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return reinterpret_cast<void*>(-1);
	}
	const auto view_offset_high = static_cast<DWORD>(aligned_offset >> 32);
	const auto view_offset_low = static_cast<DWORD>(aligned_offset & 0xffffffffu);
	void* base = (flags & map_fixed) != 0 ?
		MapViewOfFileEx(mapping, desired_access, view_offset_high, view_offset_low,
			view_bytes, requested_base) :
		MapViewOfFile(mapping, desired_access, view_offset_high, view_offset_low, view_bytes);
	if (base == nullptr) {
		if ((flags & map_fixed) != 0) {
			(void)ReleaseOwnedFileMapping(address);
			base = (flags & map_fixed) != 0 ?
				MapViewOfFileEx(mapping, desired_access, view_offset_high, view_offset_low,
					view_bytes, requested_base) : nullptr;
		}
	}
	if (base == nullptr) {
		const DWORD error = GetLastError();
		CloseHandle(mapping);
		darling::windows_host::DarwinErrno::SetFromWin32(error);
		return reinterpret_cast<void*>(-1);
	}
	void* result = static_cast<std::uint8_t*>(base) + delta;
	{
		std::lock_guard lock(darwin_file_mapping_mutex);
		darwin_file_mappings.emplace(result, DarwinFileMapping{base, mapping, view_bytes});
	}
	return result;
}

extern "C" int darling_windows_munmap(void* address, std::size_t length)
{
	if (address == nullptr || address == reinterpret_cast<void*>(-1) || length == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	DarwinFileMapping mapping{};
	{
		std::lock_guard lock(darwin_file_mapping_mutex);
		const auto found = darwin_file_mappings.find(address);
		if (found != darwin_file_mappings.end()) {
			mapping = found->second;
			darwin_file_mappings.erase(found);
		}
	}
	if (mapping.base != nullptr) {
		(void)FlushViewOfFile(mapping.base, mapping.bytes);
		if (!UnmapViewOfFile(mapping.base)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			CloseHandle(mapping.mapping);
			return -1;
		}
		CloseHandle(mapping.mapping);
		return 0;
	}
	return darling_windows_munmap_anonymous(address, length);
}

extern "C" int darling_windows_madvise(void* address, std::size_t length,
	int advice)
{
	// Darwin advice values are hints, not access-control operations. Windows
	// has no portable equivalent for every hint, so valid hints are accepted
	// without changing protection or contents; invalid values fail closed.
	constexpr int madv_normal = 0;
	constexpr int madv_random = 1;
	constexpr int madv_sequential = 2;
	constexpr int madv_willneed = 3;
	constexpr int madv_dontneed = 4;
	constexpr int madv_free = 5;
	constexpr int madv_free_reusable = 7;
	constexpr int madv_can_reuse = 8;
	if (address == nullptr || address == reinterpret_cast<void*>(-1) || length == 0 ||
		(advice != madv_normal && advice != madv_random &&
		 advice != madv_sequential && advice != madv_willneed &&
		 advice != madv_dontneed && advice != madv_free &&
		 advice != madv_free_reusable && advice != madv_can_reuse)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	return 0;
}

extern "C" int darling_windows_msync(void* address, std::size_t length,
	int flags)
{
	// Darwin: MS_ASYNC=1, MS_INVALIDATE=2, MS_SYNC=16. Windows has no
	// invalidate operation with equivalent semantics; flushing the mapped view
	// is the conservative behavior for both synchronous and asynchronous calls.
	constexpr int ms_async = 1;
	constexpr int ms_invalidate = 2;
	constexpr int ms_sync = 16;
	if (address == nullptr || address == reinterpret_cast<void*>(-1) || length == 0 ||
		(flags & ~(ms_async | ms_invalidate | ms_sync)) != 0 ||
		((flags & ms_async) != 0 && (flags & ms_sync) != 0)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	MEMORY_BASIC_INFORMATION memory_info{};
	if (VirtualQuery(address, &memory_info, sizeof(memory_info)) == 0 ||
		memory_info.State != MEM_COMMIT) {
		darling::windows_host::DarwinErrno::Set(14); // EFAULT
		return -1;
	}
	DarwinFileMapping mapping{};
	std::uintptr_t mapping_user = 0;
	{
		std::lock_guard lock(darwin_file_mapping_mutex);
		const auto requested = reinterpret_cast<std::uintptr_t>(address);
		for (const auto& [user_address, candidate] : darwin_file_mappings) {
			const auto start = reinterpret_cast<std::uintptr_t>(user_address);
			if (requested >= start && requested - start < candidate.bytes) {
				mapping_user = start;
				mapping = candidate;
				break;
			}
		}
	}
	if (mapping.base != nullptr) {
		const auto delta = reinterpret_cast<std::uintptr_t>(address) - mapping_user;
		const auto flush_length = (std::min)(length, mapping.bytes - delta);
		void* flush_address = static_cast<std::uint8_t*>(mapping.base) + delta;
		if (flush_length == 0 || !FlushViewOfFile(flush_address, flush_length)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
	}
	return 0;
}

extern "C" std::uintptr_t darling_windows_host_symbol(const char* name)
{
	if (name == nullptr) {
		return 0;
	}
	if (std::strcmp(name, "_errno") == 0 ||
		std::strcmp(name, "___error") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_errno);
	}
	if (std::strcmp(name, "_tlv_atexit") == 0) {
		return reinterpret_cast<std::uintptr_t>(
			&darling::windows_host::darling_windows_tlv_atexit);
	}
	if (std::strcmp(name, "___cxa_thread_atexit") == 0) {
		return reinterpret_cast<std::uintptr_t>(
			&darling::windows_host::darling_windows_cxa_thread_atexit);
	}
	if (std::strcmp(name, "___cxa_atexit") == 0 ||
		std::strcmp(name, "_cxa_atexit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_cxa_atexit);
	}
	if (std::strcmp(name, "___cxa_finalize") == 0 ||
		std::strcmp(name, "_cxa_finalize") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_cxa_finalize);
	}
	if (std::strcmp(name, "_sel_registerName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&sel_registerName);
	}
	if (std::strcmp(name, "_sel_getName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&sel_getName);
	}
	if (std::strcmp(name, "_objc_getClass") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_getClass);
	}
	if (std::strcmp(name, "_class_getInstanceMethod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getInstanceMethod);
	}
	if (std::strcmp(name, "_class_getInstanceSize") == 0 ||
		std::strcmp(name, "class_getInstanceSize") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getInstanceSize);
	}
	if (std::strcmp(name, "_class_addIvar") == 0 || std::strcmp(name, "class_addIvar") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_addIvar);
	}
	if (std::strcmp(name, "_class_replaceMethod") == 0 ||
		std::strcmp(name, "class_replaceMethod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_replaceMethod);
	}
	if (std::strcmp(name, "_class_getClassMethod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getClassMethod);
	}
	if (std::strcmp(name, "_class_respondsToSelector") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_respondsToSelector);
	}
	if (std::strcmp(name, "_object_getClassName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&object_getClassName);
	}
	if (std::strcmp(name, "_object_getIvar") == 0 || std::strcmp(name, "object_getIvar") == 0) {
		return reinterpret_cast<std::uintptr_t>(&object_getIvar);
	}
	if (std::strcmp(name, "_object_setIvar") == 0 || std::strcmp(name, "object_setIvar") == 0) {
		return reinterpret_cast<std::uintptr_t>(&object_setIvar);
	}
	if (std::strcmp(name, "_object_isClass") == 0) {
		return reinterpret_cast<std::uintptr_t>(&object_isClass);
	}
	if (std::strcmp(name, "_class_copyMethodList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_copyMethodList);
	}
	if (std::strcmp(name, "_method_getName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&method_getName);
	}
	if (std::strcmp(name, "_method_getImplementation") == 0) {
		return reinterpret_cast<std::uintptr_t>(&method_getImplementation);
	}
	if (std::strcmp(name, "_method_setImplementation") == 0 ||
		std::strcmp(name, "method_setImplementation") == 0) {
		return reinterpret_cast<std::uintptr_t>(&method_setImplementation);
	}
	if (std::strcmp(name, "_method_getTypeEncoding") == 0) {
		return reinterpret_cast<std::uintptr_t>(&method_getTypeEncoding);
	}
	if (std::strcmp(name, "_class_getInstanceVariable") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getInstanceVariable);
	}
	if (std::strcmp(name, "_ivar_getName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&ivar_getName);
	}
	if (std::strcmp(name, "_ivar_getTypeEncoding") == 0) {
		return reinterpret_cast<std::uintptr_t>(&ivar_getTypeEncoding);
	}
	if (std::strcmp(name, "_ivar_getOffset") == 0) {
		return reinterpret_cast<std::uintptr_t>(&ivar_getOffset);
	}
	if (std::strcmp(name, "_class_copyIvarList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_copyIvarList);
	}
	if (std::strcmp(name, "_class_getProperty") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getProperty);
	}
	if (std::strcmp(name, "_property_getName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&property_getName);
	}
	if (std::strcmp(name, "_property_getAttributes") == 0) {
		return reinterpret_cast<std::uintptr_t>(&property_getAttributes);
	}
	if (std::strcmp(name, "_class_copyPropertyList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_copyPropertyList);
	}
	if (std::strcmp(name, "_protocol_getName") == 0) {
		return reinterpret_cast<std::uintptr_t>(&protocol_getName);
	}
	if (std::strcmp(name, "_protocol_getMethodDescription") == 0) {
		return reinterpret_cast<std::uintptr_t>(&protocol_getMethodDescription);
	}
	if (std::strcmp(name, "_objc_getProtocolList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_getProtocolList);
	}
	if (std::strcmp(name, "_class_getProtocolList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_getProtocolList);
	}
	if (std::strcmp(name, "_class_copyProtocolList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_copyProtocolList);
	}
	if (std::strcmp(name, "_objc_msgSend") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_msgSend);
	}
	if (std::strcmp(name, "_objc_retain") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_retain);
	}
	if (std::strcmp(name, "_objc_storeStrong") == 0 ||
		std::strcmp(name, "objc_storeStrong") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_storeStrong);
	}
	if (std::strcmp(name, "_objc_loadWeakRetained") == 0 ||
		std::strcmp(name, "objc_loadWeakRetained") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_loadWeakRetained);
	}
	if (std::strcmp(name, "_objc_release") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_release);
	}
	if (std::strcmp(name, "_objc_retainBlock") == 0 ||
		std::strcmp(name, "_Block_copy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_retainBlock);
	}
	if (std::strcmp(name, "_objc_releaseBlock") == 0 ||
		std::strcmp(name, "_Block_release") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_releaseBlock);
	}
	if (std::strcmp(name, "_Block_object_assign") == 0) {
		return reinterpret_cast<std::uintptr_t>(&_Block_object_assign);
	}
	if (std::strcmp(name, "_Block_object_dispose") == 0) {
		return reinterpret_cast<std::uintptr_t>(&_Block_object_dispose);
	}
	if (std::strcmp(name, "_sigaction") == 0 || std::strcmp(name, "sigaction") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigaction);
	}
	if (std::strcmp(name, "_sigemptyset") == 0 || std::strcmp(name, "__sigemptyset") == 0 || std::strcmp(name, "sigemptyset") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigemptyset);
	if (std::strcmp(name, "_sigfillset") == 0 || std::strcmp(name, "__sigfillset") == 0 || std::strcmp(name, "sigfillset") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigfillset);
	if (std::strcmp(name, "_sigaddset") == 0 || std::strcmp(name, "__sigaddset") == 0 || std::strcmp(name, "sigaddset") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigaddset);
	if (std::strcmp(name, "_sigdelset") == 0 || std::strcmp(name, "__sigdelset") == 0 || std::strcmp(name, "sigdelset") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigdelset);
	if (std::strcmp(name, "_sigismember") == 0 || std::strcmp(name, "__sigismember") == 0 || std::strcmp(name, "sigismember") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigismember);
	if (std::strcmp(name, "__sigaction") == 0 ||
		std::strcmp(name, "_sigaction_nocancel") == 0 ||
		std::strcmp(name, "__sigaction_nocancel") == 0 ||
		std::strcmp(name, "sigaction_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigaction);
	}
	if (std::strcmp(name, "_sigprocmask") == 0 || std::strcmp(name, "sigprocmask") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigprocmask);
	}
	if (std::strcmp(name, "_pthread_sigmask") == 0 ||
		std::strcmp(name, "__pthread_sigmask") == 0 ||
		std::strcmp(name, "pthread_sigmask") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigprocmask);
	}
	if (std::strcmp(name, "__sigprocmask") == 0 ||
		std::strcmp(name, "_sigprocmask_nocancel") == 0 ||
		std::strcmp(name, "__sigprocmask_nocancel") == 0 ||
		std::strcmp(name, "sigprocmask_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigprocmask);
	}
	if (std::strcmp(name, "_sigpending") == 0 || std::strcmp(name, "sigpending") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigpending);
	}
	if (std::strcmp(name, "__sigpending") == 0 ||
		std::strcmp(name, "_sigpending_nocancel") == 0 ||
		std::strcmp(name, "__sigpending_nocancel") == 0 ||
		std::strcmp(name, "sigpending_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigpending);
	}
	if (std::strcmp(name, "_sigwait") == 0 || std::strcmp(name, "sigwait") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigwait);
	}
	if (std::strcmp(name, "_sigwait_nocancel") == 0 ||
		std::strcmp(name, "__sigwait_nocancel") == 0 ||
		std::strcmp(name, "sigwait_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigwait);
	}
	if (std::strcmp(name, "_sigtimedwait") == 0 || std::strcmp(name, "sigtimedwait") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigtimedwait);
	}
	if (std::strcmp(name, "_sigwaitinfo") == 0 || std::strcmp(name, "sigwaitinfo") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigwaitinfo);
	}
	if (std::strcmp(name, "_sigwaitinfo_nocancel") == 0 ||
		std::strcmp(name, "__sigwaitinfo_nocancel") == 0 ||
		std::strcmp(name, "sigwaitinfo_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigwaitinfo);
	}
	if (std::strcmp(name, "_sigqueue") == 0 || std::strcmp(name, "sigqueue") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigqueue);
	}
	if (std::strcmp(name, "__sigqueue") == 0 ||
		std::strcmp(name, "_sigqueue_nocancel") == 0 ||
		std::strcmp(name, "__sigqueue_nocancel") == 0 ||
		std::strcmp(name, "sigqueue_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sigqueue);
	}
	if (std::strcmp(name, "_objc_autorelease") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_autorelease);
	}
	if (std::strcmp(name, "_objc_getProtocol") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_getProtocol);
	}
	if (std::strcmp(name, "_class_conformsToProtocol") == 0) {
		return reinterpret_cast<std::uintptr_t>(&class_conformsToProtocol);
	}
	if (std::strcmp(name, "_objc_getClassList") == 0) {
		return reinterpret_cast<std::uintptr_t>(&objc_getClassList);
	}
	if (std::strcmp(name, "_read") == 0 || std::strcmp(name, "read") == 0 ||
		std::strcmp(name, "_read_nocancel") == 0 ||
		std::strcmp(name, "read_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_read);
	}
	if (std::strcmp(name, "_write") == 0 || std::strcmp(name, "write") == 0 ||
		std::strcmp(name, "_write_nocancel") == 0 ||
		std::strcmp(name, "write_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_write);
	}
	if (std::strcmp(name, "_pread") == 0 || std::strcmp(name, "pread") == 0 ||
		std::strcmp(name, "_pread_nocancel") == 0 ||
		std::strcmp(name, "pread_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pread);
	}
	if (std::strcmp(name, "_pwrite") == 0 || std::strcmp(name, "pwrite") == 0 ||
		std::strcmp(name, "_pwrite_nocancel") == 0 ||
		std::strcmp(name, "pwrite_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pwrite);
	}
	if (std::strcmp(name, "_preadv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_preadv);
	}
	if (std::strcmp(name, "_pwritev") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pwritev);
	}
	if (std::strcmp(name, "_flock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_flock);
	}
	if (std::strcmp(name, "_readv") == 0 || std::strcmp(name, "readv") == 0 ||
		std::strcmp(name, "_readv_nocancel") == 0 ||
		std::strcmp(name, "__readv_nocancel") == 0 ||
		std::strcmp(name, "readv_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_readv);
	}
	if (std::strcmp(name, "_writev") == 0 || std::strcmp(name, "writev") == 0 ||
		std::strcmp(name, "_writev_nocancel") == 0 ||
		std::strcmp(name, "__writev_nocancel") == 0 ||
		std::strcmp(name, "writev_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_writev);
	}
	if (std::strcmp(name, "_close") == 0 || std::strcmp(name, "close") == 0 ||
		std::strcmp(name, "_close_nocancel") == 0 ||
		std::strcmp(name, "close_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_close);
	}
	if (std::strcmp(name, "_fcntl") == 0 || std::strcmp(name, "fcntl") == 0 ||
		std::strcmp(name, "_fcntl_nocancel") == 0 ||
		std::strcmp(name, "fcntl_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fcntl);
	}
	if (std::strcmp(name, "_dup") == 0 || std::strcmp(name, "dup") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dup);
	}
	if (std::strcmp(name, "_dup2") == 0 || std::strcmp(name, "dup2") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dup2);
	}
	if (std::strcmp(name, "_pipe") == 0 || std::strcmp(name, "pipe") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pipe);
	}
	if (std::strcmp(name, "_pipe2") == 0 || std::strcmp(name, "pipe2") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pipe2);
	}
	if (std::strcmp(name, "_poll") == 0 || std::strcmp(name, "poll") == 0 ||
		std::strcmp(name, "_poll_nocancel") == 0 ||
		std::strcmp(name, "__poll_nocancel") == 0 ||
		std::strcmp(name, "poll_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_poll);
	}
	if (std::strcmp(name, "_socket") == 0 || std::strcmp(name, "socket") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_socket);
	}
	if (std::strcmp(name, "_socketpair") == 0 || std::strcmp(name, "socketpair") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_socketpair);
	}
	if (std::strcmp(name, "_bind") == 0 || std::strcmp(name, "bind") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_bind);
	}
	if (std::strcmp(name, "_connect") == 0 || std::strcmp(name, "connect") == 0 ||
		std::strcmp(name, "_connect_nocancel") == 0 ||
		std::strcmp(name, "__connect_nocancel") == 0 ||
		std::strcmp(name, "connect_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_connect);
	}
	if (std::strcmp(name, "_listen") == 0 || std::strcmp(name, "listen") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_listen);
	}
	if (std::strcmp(name, "_accept") == 0 || std::strcmp(name, "accept") == 0 ||
		std::strcmp(name, "_accept_nocancel") == 0 ||
		std::strcmp(name, "__accept_nocancel") == 0 ||
		std::strcmp(name, "accept_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_accept);
	}
	if (std::strcmp(name, "_getsockname") == 0 || std::strcmp(name, "getsockname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getsockname);
	}
	if (std::strcmp(name, "_getpeername") == 0 || std::strcmp(name, "getpeername") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpeername);
	}
	if (std::strcmp(name, "_getaddrinfo") == 0 || std::strcmp(name, "getaddrinfo") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getaddrinfo);
	}
	if (std::strcmp(name, "_freeaddrinfo") == 0 || std::strcmp(name, "freeaddrinfo") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_freeaddrinfo);
	}
	if (std::strcmp(name, "_getnameinfo") == 0 || std::strcmp(name, "getnameinfo") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getnameinfo);
	}
	if (std::strcmp(name, "_inet_pton") == 0 || std::strcmp(name, "inet_pton") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_inet_pton);
	}
	if (std::strcmp(name, "_inet_ntop") == 0 || std::strcmp(name, "inet_ntop") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_inet_ntop);
	}
	if (std::strcmp(name, "_select") == 0 || std::strcmp(name, "select") == 0 ||
		std::strcmp(name, "_select_nocancel") == 0 ||
		std::strcmp(name, "__select_nocancel") == 0 ||
		std::strcmp(name, "select_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_select);
	}
	if (std::strcmp(name, "_pselect") == 0 || std::strcmp(name, "pselect") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pselect);
	}
	if (std::strcmp(name, "_getsockopt") == 0 || std::strcmp(name, "getsockopt") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getsockopt);
	}
	if (std::strcmp(name, "_setsockopt") == 0 || std::strcmp(name, "setsockopt") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setsockopt);
	}
	if (std::strcmp(name, "_sendto") == 0 || std::strcmp(name, "sendto") == 0 ||
		std::strcmp(name, "_sendto_nocancel") == 0 ||
		std::strcmp(name, "__sendto_nocancel") == 0 ||
		std::strcmp(name, "sendto_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sendto);
	}
	if (std::strcmp(name, "_recvfrom") == 0 || std::strcmp(name, "recvfrom") == 0 ||
		std::strcmp(name, "_recvfrom_nocancel") == 0 ||
		std::strcmp(name, "__recvfrom_nocancel") == 0 ||
		std::strcmp(name, "recvfrom_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_recvfrom);
	}
	if (std::strcmp(name, "_sendmsg") == 0 || std::strcmp(name, "sendmsg") == 0 ||
		std::strcmp(name, "_sendmsg_nocancel") == 0 ||
		std::strcmp(name, "__sendmsg_nocancel") == 0 ||
		std::strcmp(name, "sendmsg_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sendmsg);
	}
	if (std::strcmp(name, "_recvmsg") == 0 || std::strcmp(name, "recvmsg") == 0 ||
		std::strcmp(name, "_recvmsg_nocancel") == 0 ||
		std::strcmp(name, "__recvmsg_nocancel") == 0 ||
		std::strcmp(name, "recvmsg_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_recvmsg);
	}
	if (std::strcmp(name, "_shutdown") == 0 || std::strcmp(name, "shutdown") == 0 ||
		std::strcmp(name, "_shutdown_nocancel") == 0 ||
		std::strcmp(name, "__shutdown_nocancel") == 0 ||
		std::strcmp(name, "shutdown_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_shutdown);
	}
	if (std::strcmp(name, "_open") == 0 || std::strcmp(name, "open") == 0 ||
		std::strcmp(name, "_open_nocancel") == 0 ||
		std::strcmp(name, "open_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_open);
	}
	if (std::strcmp(name, "_mkstemp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mkstemp);
	}
	if (std::strcmp(name, "_mkstemps") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mkstemps);
	}
	if (std::strcmp(name, "_mkdtemp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mkdtemp);
	}
	if (std::strcmp(name, "_unlink") == 0 || std::strcmp(name, "unlink") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_unlink);
	}
	if (std::strcmp(name, "_rmdir") == 0 || std::strcmp(name, "rmdir") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_rmdir);
	}
	if (std::strcmp(name, "_link") == 0 || std::strcmp(name, "link") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_link);
	}
	if (std::strcmp(name, "_symlink") == 0 || std::strcmp(name, "symlink") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_symlink);
	}
	if (std::strcmp(name, "_readlink") == 0 || std::strcmp(name, "readlink") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_readlink);
	}
	if (std::strcmp(name, "_access") == 0 || std::strcmp(name, "access") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_access);
	}
	if (std::strcmp(name, "_rename") == 0 || std::strcmp(name, "rename") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_rename);
	}
	if (std::strcmp(name, "_lseek") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_lseek);
	}
	if (std::strcmp(name, "_ftruncate") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_ftruncate);
	}
	if (std::strcmp(name, "_truncate") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_truncate);
	}
	if (std::strcmp(name, "_chmod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_chmod);
	}
	if (std::strcmp(name, "_fchmod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fchmod);
	}
	if (std::strcmp(name, "_fsync") == 0 ||
		std::strcmp(name, "fsync") == 0 ||
		std::strcmp(name, "_fsync_nocancel") == 0 ||
		std::strcmp(name, "fsync_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fsync);
	}
	if (std::strcmp(name, "_fdatasync") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fdatasync);
	}
	if (std::strcmp(name, "_utimensat") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_utimensat);
	}
	if (std::strcmp(name, "_futimens") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_futimens);
	}
	if (std::strcmp(name, "_chdir") == 0 || std::strcmp(name, "chdir") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_chdir);
	}
	if (std::strcmp(name, "_getcwd") == 0 || std::strcmp(name, "getcwd") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getcwd);
	}
	if (std::strcmp(name, "_realpath") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_realpath);
	}
	if (std::strcmp(name, "_mkdir") == 0 || std::strcmp(name, "mkdir") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mkdir);
	}
	if (std::strcmp(name, "_getdirentries") == 0 || std::strcmp(name, "getdirentries") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getdirentries);
	}
	if (std::strcmp(name, "_getdirentries64") == 0 || std::strcmp(name, "getdirentries64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getdirentries64);
	}
	if (std::strcmp(name, "_stat") == 0 || std::strcmp(name, "_stat64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_stat);
	}
	if (std::strcmp(name, "_lstat") == 0 || std::strcmp(name, "_lstat64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_lstat);
	}
	if (std::strcmp(name, "_fstat") == 0 || std::strcmp(name, "_fstat64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fstat);
	}
	if (std::strcmp(name, "_fstatat") == 0 || std::strcmp(name, "_fstatat64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fstatat);
	}
	if (std::strcmp(name, "_statfs") == 0 || std::strcmp(name, "_statfs64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_statfs);
	}
	if (std::strcmp(name, "_fstatfs") == 0 || std::strcmp(name, "_fstatfs64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fstatfs);
	}
	if (std::strcmp(name, "_getfsstat") == 0 || std::strcmp(name, "_getfsstat64") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getfsstat);
	}
	if (std::strcmp(name, "_getpid") == 0 || std::strcmp(name, "getpid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpid);
	}
	if (std::strcmp(name, "_getpgrp") == 0 || std::strcmp(name, "getpgrp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpgrp);
	}
	if (std::strcmp(name, "_getpgid") == 0 || std::strcmp(name, "getpgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpgid);
	}
	if (std::strcmp(name, "_getsid") == 0 || std::strcmp(name, "getsid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getsid);
	}
	if (std::strcmp(name, "_setpgid") == 0 || std::strcmp(name, "setpgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setpgid);
	}
	if (std::strcmp(name, "_setsid") == 0 || std::strcmp(name, "setsid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setsid);
	}
	if (std::strcmp(name, "_atexit") == 0 || std::strcmp(name, "atexit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_atexit);
	}
	if (std::strcmp(name, "__exit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows__exit);
	}
	if (std::strcmp(name, "_exit") == 0 || std::strcmp(name, "exit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_exit);
	}
	if (std::strcmp(name, "_getppid") == 0 || std::strcmp(name, "getppid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getppid);
	}
	if (std::strcmp(name, "_waitpid") == 0 || std::strcmp(name, "waitpid") == 0 ||
		std::strcmp(name, "_waitpid_nocancel") == 0 ||
		std::strcmp(name, "__waitpid_nocancel") == 0 ||
		std::strcmp(name, "waitpid_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_waitpid);
	}
	if (std::strcmp(name, "_wait4") == 0 || std::strcmp(name, "wait4") == 0 ||
		std::strcmp(name, "_wait4_nocancel") == 0 ||
		std::strcmp(name, "__wait4_nocancel") == 0 ||
		std::strcmp(name, "wait4_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_wait4);
	}
	if (std::strcmp(name, "_waitid") == 0 || std::strcmp(name, "waitid") == 0 ||
		std::strcmp(name, "_waitid_nocancel") == 0 ||
		std::strcmp(name, "__waitid_nocancel") == 0 ||
		std::strcmp(name, "waitid_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_waitid);
	}
	if (std::strcmp(name, "_posix_spawn") == 0 || std::strcmp(name, "posix_spawn") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_posix_spawn);
	}
	if (std::strcmp(name, "_posix_spawnp") == 0 || std::strcmp(name, "posix_spawnp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_posix_spawnp);
	}
	if (std::strcmp(name, "_getuid") == 0 || std::strcmp(name, "getuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getuid);
	}
	if (std::strcmp(name, "_geteuid") == 0 || std::strcmp(name, "geteuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_geteuid);
	}
	if (std::strcmp(name, "_getgid") == 0 || std::strcmp(name, "getgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getgid);
	}
	if (std::strcmp(name, "_getegid") == 0 || std::strcmp(name, "getegid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getegid);
	}
	if (std::strcmp(name, "_getresuid") == 0 || std::strcmp(name, "getresuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getresuid);
	}
	if (std::strcmp(name, "_getresgid") == 0 || std::strcmp(name, "getresgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getresgid);
	}
	if (std::strcmp(name, "_getgroups") == 0 || std::strcmp(name, "getgroups") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getgroups);
	}
	if (std::strcmp(name, "_setgroups") == 0 || std::strcmp(name, "setgroups") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setgroups);
	}
	if (std::strcmp(name, "_setuid") == 0 || std::strcmp(name, "setuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setuid);
	}
	if (std::strcmp(name, "_seteuid") == 0 || std::strcmp(name, "seteuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_seteuid);
	}
	if (std::strcmp(name, "_setgid") == 0 || std::strcmp(name, "setgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setgid);
	}
	if (std::strcmp(name, "_setegid") == 0 || std::strcmp(name, "setegid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setegid);
	}
	if (std::strcmp(name, "_setreuid") == 0 || std::strcmp(name, "setreuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setreuid);
	}
	if (std::strcmp(name, "_setregid") == 0 || std::strcmp(name, "setregid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setregid);
	}
	if (std::strcmp(name, "_setresuid") == 0 || std::strcmp(name, "setresuid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setresuid);
	}
	if (std::strcmp(name, "_setresgid") == 0 || std::strcmp(name, "setresgid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setresgid);
	}
	if (std::strcmp(name, "_getlogin") == 0 || std::strcmp(name, "getlogin") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getlogin);
	}
	if (std::strcmp(name, "_getlogin_r") == 0 || std::strcmp(name, "getlogin_r") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getlogin_r);
	}
	if (std::strcmp(name, "_setlogin") == 0 || std::strcmp(name, "setlogin") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setlogin);
	}
	if (std::strcmp(name, "_getrusage") == 0 || std::strcmp(name, "getrusage") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getrusage);
	}
	if (std::strcmp(name, "_times") == 0 || std::strcmp(name, "times") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_times);
	}
	if (std::strcmp(name, "_dlopen") == 0 || std::strcmp(name, "dlopen") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dlopen);
	}
	if (std::strcmp(name, "_dlsym") == 0 || std::strcmp(name, "dlsym") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dlsym);
	}
	if (std::strcmp(name, "_dlclose") == 0 || std::strcmp(name, "dlclose") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dlclose);
	}
	if (std::strcmp(name, "_dlerror") == 0 || std::strcmp(name, "dlerror") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dlerror);
	}
	if (std::strcmp(name, "_dladdr") == 0 || std::strcmp(name, "dladdr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dladdr);
	}
	if (std::strcmp(name, "_dyld_image_count") == 0 ||
		std::strcmp(name, "__dyld_image_count") == 0 ||
		std::strcmp(name, "___dyld_image_count") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dyld_image_count);
	}
	if (std::strcmp(name, "_dyld_get_image_name") == 0 ||
		std::strcmp(name, "__dyld_get_image_name") == 0 ||
		std::strcmp(name, "___dyld_get_image_name") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dyld_get_image_name);
	}
	if (std::strcmp(name, "_dyld_get_image_header") == 0 ||
		std::strcmp(name, "__dyld_get_image_header") == 0 ||
		std::strcmp(name, "___dyld_get_image_header") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dyld_get_image_header);
	}
	if (std::strcmp(name, "_dyld_get_image_vmaddr_slide") == 0 ||
		std::strcmp(name, "__dyld_get_image_vmaddr_slide") == 0 ||
		std::strcmp(name, "___dyld_get_image_vmaddr_slide") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_dyld_get_image_vmaddr_slide);
	}
	if (std::strcmp(name, "_getprogname") == 0 || std::strcmp(name, "getprogname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getprogname);
	}
	if (std::strcmp(name, "_setprogname") == 0 || std::strcmp(name, "setprogname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setprogname);
	}
	if (std::strcmp(name, "_getpagesize") == 0 || std::strcmp(name, "getpagesize") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpagesize);
	}
	if (std::strcmp(name, "_getpagesizes") == 0 || std::strcmp(name, "getpagesizes") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpagesizes);
	}
	if (std::strcmp(name, "_mmap") == 0 || std::strcmp(name, "mmap") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mmap);
	}
	if (std::strcmp(name, "_mprotect") == 0 || std::strcmp(name, "mprotect") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mprotect);
	}
	if (std::strcmp(name, "_madvise") == 0 || std::strcmp(name, "madvise") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_madvise);
	}
	if (std::strcmp(name, "_msync") == 0 || std::strcmp(name, "msync") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_msync);
	}
	if (std::strcmp(name, "_munmap") == 0 || std::strcmp(name, "munmap") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_munmap);
	}
	if (std::strcmp(name, "_sysconf") == 0 || std::strcmp(name, "sysconf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sysconf);
	}
	if (std::strcmp(name, "_confstr") == 0 || std::strcmp(name, "confstr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_confstr);
	}
	if (std::strcmp(name, "_pathconf") == 0 || std::strcmp(name, "pathconf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pathconf);
	}
	if (std::strcmp(name, "_fpathconf") == 0 || std::strcmp(name, "fpathconf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_fpathconf);
	}
	if (std::strcmp(name, "_getdtablesize") == 0 || std::strcmp(name, "getdtablesize") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getdtablesize);
	}
	if (std::strcmp(name, "_pthread_self") == 0 || std::strcmp(name, "pthread_self") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_self);
	}
	if (std::strcmp(name, "_pthread_equal") == 0 || std::strcmp(name, "pthread_equal") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_equal);
	}
	if (std::strcmp(name, "_pthread_setname_np") == 0 ||
		std::strcmp(name, "pthread_setname_np") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_setname_np);
	}
	if (std::strcmp(name, "_pthread_getname_np") == 0 ||
		std::strcmp(name, "pthread_getname_np") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_getname_np);
	}
	if (std::strcmp(name, "_pthread_create") == 0 || std::strcmp(name, "pthread_create") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_create);
	}
	if (std::strcmp(name, "_pthread_join") == 0 || std::strcmp(name, "pthread_join") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_join);
	}
	if (std::strcmp(name, "_pthread_detach") == 0 || std::strcmp(name, "pthread_detach") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_detach);
	}
	if (std::strcmp(name, "_pthread_cancel") == 0 || std::strcmp(name, "pthread_cancel") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cancel);
	if (std::strcmp(name, "_pthread_setcancelstate") == 0 || std::strcmp(name, "pthread_setcancelstate") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_setcancelstate);
	if (std::strcmp(name, "_pthread_setcanceltype") == 0 || std::strcmp(name, "pthread_setcanceltype") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_setcanceltype);
	if (std::strcmp(name, "_pthread_testcancel") == 0 || std::strcmp(name, "pthread_testcancel") == 0)
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_testcancel);
	if (std::strcmp(name, "_pthread_threadid_np") == 0 ||
		std::strcmp(name, "pthread_threadid_np") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_threadid_np);
	}
	if (std::strcmp(name, "_pthread_mutex_init") == 0 || std::strcmp(name, "pthread_mutex_init") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutex_init);
	}
	if (std::strcmp(name, "_pthread_mutex_destroy") == 0 || std::strcmp(name, "pthread_mutex_destroy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutex_destroy);
	}
	if (std::strcmp(name, "_pthread_mutex_lock") == 0 || std::strcmp(name, "pthread_mutex_lock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutex_lock);
	}
	if (std::strcmp(name, "_pthread_mutex_trylock") == 0 || std::strcmp(name, "pthread_mutex_trylock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutex_trylock);
	}
	if (std::strcmp(name, "_pthread_mutex_unlock") == 0 || std::strcmp(name, "pthread_mutex_unlock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutex_unlock);
	}
	if (std::strcmp(name, "_pthread_mutexattr_init") == 0 || std::strcmp(name, "pthread_mutexattr_init") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_init);
	if (std::strcmp(name, "_pthread_mutexattr_destroy") == 0 || std::strcmp(name, "pthread_mutexattr_destroy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_destroy);
	if (std::strcmp(name, "_pthread_mutexattr_settype") == 0 || std::strcmp(name, "pthread_mutexattr_settype") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_settype);
	if (std::strcmp(name, "_pthread_mutexattr_gettype") == 0 || std::strcmp(name, "pthread_mutexattr_gettype") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_gettype);
	if (std::strcmp(name, "_pthread_mutexattr_setpshared") == 0 || std::strcmp(name, "pthread_mutexattr_setpshared") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_setpshared);
	if (std::strcmp(name, "_pthread_mutexattr_getpshared") == 0 || std::strcmp(name, "pthread_mutexattr_getpshared") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_getpshared);
	if (std::strcmp(name, "_pthread_mutexattr_setprotocol") == 0 || std::strcmp(name, "pthread_mutexattr_setprotocol") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_setprotocol);
	if (std::strcmp(name, "_pthread_mutexattr_getprotocol") == 0 || std::strcmp(name, "pthread_mutexattr_getprotocol") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_getprotocol);
	if (std::strcmp(name, "_pthread_mutexattr_setrobust") == 0 || std::strcmp(name, "pthread_mutexattr_setrobust") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_setrobust);
	if (std::strcmp(name, "_pthread_mutexattr_getrobust") == 0 || std::strcmp(name, "pthread_mutexattr_getrobust") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_mutexattr_getrobust);
	if (std::strcmp(name, "_pthread_cond_init") == 0 || std::strcmp(name, "pthread_cond_init") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_init);
	}
	if (std::strcmp(name, "_pthread_condattr_init") == 0 || std::strcmp(name, "pthread_condattr_init") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_init);
	if (std::strcmp(name, "_pthread_condattr_destroy") == 0 || std::strcmp(name, "pthread_condattr_destroy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_destroy);
	if (std::strcmp(name, "_pthread_condattr_setpshared") == 0 || std::strcmp(name, "pthread_condattr_setpshared") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_setpshared);
	if (std::strcmp(name, "_pthread_condattr_getpshared") == 0 || std::strcmp(name, "pthread_condattr_getpshared") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_getpshared);
	if (std::strcmp(name, "_pthread_condattr_setclock") == 0 || std::strcmp(name, "pthread_condattr_setclock") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_setclock);
	if (std::strcmp(name, "_pthread_condattr_getclock") == 0 || std::strcmp(name, "pthread_condattr_getclock") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_condattr_getclock);
	if (std::strcmp(name, "_pthread_cond_destroy") == 0 || std::strcmp(name, "pthread_cond_destroy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_destroy);
	}
	if (std::strcmp(name, "_pthread_cond_wait") == 0 || std::strcmp(name, "pthread_cond_wait") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_wait);
	}
	if (std::strcmp(name, "_pthread_cond_timedwait") == 0 ||
		std::strcmp(name, "pthread_cond_timedwait") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_timedwait);
	}
	if (std::strcmp(name, "_pthread_cond_signal") == 0 || std::strcmp(name, "pthread_cond_signal") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_signal);
	}
	if (std::strcmp(name, "_pthread_cond_broadcast") == 0 || std::strcmp(name, "pthread_cond_broadcast") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_cond_broadcast);
	}
	if (std::strcmp(name, "_pthread_rwlock_init") == 0 || std::strcmp(name, "pthread_rwlock_init") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_rwlock_init);
	}
	if (std::strcmp(name, "_pthread_rwlock_destroy") == 0 || std::strcmp(name, "pthread_rwlock_destroy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_rwlock_destroy);
	}
	if (std::strcmp(name, "_pthread_rwlock_rdlock") == 0 || std::strcmp(name, "pthread_rwlock_rdlock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_rwlock_rdlock);
	}
	if (std::strcmp(name, "_pthread_rwlock_wrlock") == 0 || std::strcmp(name, "pthread_rwlock_wrlock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_rwlock_wrlock);
	}
	if (std::strcmp(name, "_pthread_rwlock_unlock") == 0 || std::strcmp(name, "pthread_rwlock_unlock") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_rwlock_unlock);
	}
	if (std::strcmp(name, "_pthread_key_create") == 0 || std::strcmp(name, "pthread_key_create") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_key_create);
	}
	if (std::strcmp(name, "_pthread_key_delete") == 0 || std::strcmp(name, "pthread_key_delete") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_key_delete);
	}
	if (std::strcmp(name, "_pthread_setspecific") == 0 || std::strcmp(name, "pthread_setspecific") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_setspecific);
	}
	if (std::strcmp(name, "_pthread_getspecific") == 0 || std::strcmp(name, "pthread_getspecific") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_getspecific);
	}
	if (std::strcmp(name, "_pthread_once") == 0 || std::strcmp(name, "pthread_once") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_once);
	}
	if (std::strcmp(name, "pthread_attr_init") == 0 || std::strcmp(name, "_pthread_attr_init") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_init);
	if (std::strcmp(name, "pthread_attr_destroy") == 0 || std::strcmp(name, "_pthread_attr_destroy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_destroy);
	if (std::strcmp(name, "pthread_attr_setdetachstate") == 0 || std::strcmp(name, "_pthread_attr_setdetachstate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_setdetachstate);
	if (std::strcmp(name, "pthread_attr_getdetachstate") == 0 || std::strcmp(name, "_pthread_attr_getdetachstate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_getdetachstate);
	if (std::strcmp(name, "pthread_attr_setstacksize") == 0 || std::strcmp(name, "_pthread_attr_setstacksize") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_setstacksize);
	if (std::strcmp(name, "pthread_attr_getstacksize") == 0 || std::strcmp(name, "_pthread_attr_getstacksize") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_getstacksize);
	if (std::strcmp(name, "pthread_attr_setguardsize") == 0 || std::strcmp(name, "_pthread_attr_setguardsize") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_setguardsize);
	if (std::strcmp(name, "pthread_attr_getguardsize") == 0 || std::strcmp(name, "_pthread_attr_getguardsize") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_pthread_attr_getguardsize);
	if (std::strcmp(name, "mach_task_self") == 0 || std::strcmp(name, "_mach_task_self") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_task_self);
	if (std::strcmp(name, "mach_thread_self") == 0 || std::strcmp(name, "_mach_thread_self") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_thread_self);
	if (std::strcmp(name, "mach_host_self") == 0 || std::strcmp(name, "_mach_host_self") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_host_self);
	if (std::strcmp(name, "host_page_size") == 0 || std::strcmp(name, "_host_page_size") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_host_page_size);
	if (std::strcmp(name, "mach_vm_allocate") == 0 || std::strcmp(name, "_mach_vm_allocate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_allocate);
	if (std::strcmp(name, "mach_vm_deallocate") == 0 || std::strcmp(name, "_mach_vm_deallocate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_deallocate);
	if (std::strcmp(name, "mach_vm_protect") == 0 || std::strcmp(name, "_mach_vm_protect") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_protect);
	if (std::strcmp(name, "mach_vm_read_overwrite") == 0 || std::strcmp(name, "_mach_vm_read_overwrite") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_read_overwrite);
	if (std::strcmp(name, "mach_vm_read") == 0 || std::strcmp(name, "_mach_vm_read") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_read);
	if (std::strcmp(name, "mach_vm_write") == 0 || std::strcmp(name, "_mach_vm_write") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_write);
	if (std::strcmp(name, "mach_vm_copy") == 0 || std::strcmp(name, "_mach_vm_copy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_copy);
	if (std::strcmp(name, "mach_vm_region") == 0 || std::strcmp(name, "_mach_vm_region") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_region);
	if (std::strcmp(name, "mach_vm_region_recurse") == 0 || std::strcmp(name, "_mach_vm_region_recurse") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_vm_region_recurse);
	if (std::strcmp(name, "mach_port_deallocate") == 0 || std::strcmp(name, "_mach_port_deallocate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_deallocate);
	if (std::strcmp(name, "mach_port_destroy") == 0 || std::strcmp(name, "_mach_port_destroy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_destroy);
	if (std::strcmp(name, "mach_port_allocate") == 0 || std::strcmp(name, "_mach_port_allocate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_allocate);
	if (std::strcmp(name, "mach_port_insert_right") == 0 || std::strcmp(name, "_mach_port_insert_right") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_insert_right);
	if (std::strcmp(name, "mach_port_extract_right") == 0 || std::strcmp(name, "_mach_port_extract_right") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_extract_right);
	if (std::strcmp(name, "mach_port_mod_refs") == 0 || std::strcmp(name, "_mach_port_mod_refs") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_mod_refs);
	if (std::strcmp(name, "mach_port_get_refs") == 0 || std::strcmp(name, "_mach_port_get_refs") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_get_refs);
	if (std::strcmp(name, "mach_port_type") == 0 || std::strcmp(name, "_mach_port_type") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_type);
	if (std::strcmp(name, "mach_port_set_allocate") == 0 || std::strcmp(name, "_mach_port_set_allocate") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_set_allocate);
	if (std::strcmp(name, "mach_port_move_member") == 0 || std::strcmp(name, "_mach_port_move_member") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_move_member);
	if (std::strcmp(name, "mach_port_remove_member") == 0 || std::strcmp(name, "_mach_port_remove_member") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_remove_member);
	if (std::strcmp(name, "mach_port_set_destroy") == 0 || std::strcmp(name, "_mach_port_set_destroy") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_set_destroy);
	if (std::strcmp(name, "mach_port_set_receive") == 0 || std::strcmp(name, "_mach_port_set_receive") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_port_set_receive);
	if (std::strcmp(name, "mach_msg") == 0 || std::strcmp(name, "_mach_msg") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_msg);
	if (std::strcmp(name, "NSGetSizeAndAlignment") == 0 || std::strcmp(name, "_NSGetSizeAndAlignment") == 0) return reinterpret_cast<std::uintptr_t>(&darling_windows_NSGetSizeAndAlignment);
	if (std::strcmp(name, "_getrlimit") == 0 || std::strcmp(name, "getrlimit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getrlimit);
	}
	if (std::strcmp(name, "_setrlimit") == 0 || std::strcmp(name, "setrlimit") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setrlimit);
	}
	if (std::strcmp(name, "_getpriority") == 0 || std::strcmp(name, "getpriority") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getpriority);
	}
	if (std::strcmp(name, "_setpriority") == 0 || std::strcmp(name, "setpriority") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setpriority);
	}
	if (std::strcmp(name, "_umask") == 0 || std::strcmp(name, "umask") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_umask);
	}
	if (std::strcmp(name, "_getloadavg") == 0 || std::strcmp(name, "getloadavg") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getloadavg);
	}
	if (std::strcmp(name, "___sysctlbyname") == 0 ||
		std::strcmp(name, "__sysctlbyname") == 0 ||
		std::strcmp(name, "_sysctlbyname") == 0 ||
		std::strcmp(name, "sysctlbyname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sysctlbyname);
	}
	if (std::strcmp(name, "___sysctl") == 0 || std::strcmp(name, "__sysctl") == 0 ||
		std::strcmp(name, "_sysctl") == 0 || std::strcmp(name, "sysctl") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sysctl);
	}
	if (std::strcmp(name, "_sched_yield") == 0 || std::strcmp(name, "sched_yield") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sched_yield);
	}
	if (std::strcmp(name, "_sleep") == 0 || std::strcmp(name, "sleep") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sleep);
	}
	if (std::strcmp(name, "__sleep") == 0 ||
		std::strcmp(name, "_sleep_nocancel") == 0 ||
		std::strcmp(name, "__sleep_nocancel") == 0 ||
		std::strcmp(name, "sleep_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_sleep);
	}
	if (std::strcmp(name, "_arc4random") == 0 || std::strcmp(name, "arc4random") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_arc4random);
	}
	if (std::strcmp(name, "_arc4random_buf") == 0 || std::strcmp(name, "arc4random_buf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_arc4random_buf);
	}
	if (std::strcmp(name, "_getentropy") == 0 || std::strcmp(name, "getentropy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getentropy);
	}
	if (std::strcmp(name, "_kill") == 0 || std::strcmp(name, "kill") == 0 ||
		std::strcmp(name, "_kill_nocancel") == 0 ||
		std::strcmp(name, "__kill_nocancel") == 0 ||
		std::strcmp(name, "kill_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_kill);
	}
	if (std::strcmp(name, "_raise") == 0 || std::strcmp(name, "raise") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_raise);
	}
	if (std::strcmp(name, "_gethostname") == 0 || std::strcmp(name, "gethostname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_gethostname);
	}
	if (std::strcmp(name, "_getdomainname") == 0 || std::strcmp(name, "getdomainname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getdomainname);
	}
	if (std::strcmp(name, "_uname") == 0 || std::strcmp(name, "uname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_uname);
	}
	if (std::strcmp(name, "___NSGetExecutablePath") == 0 ||
		std::strcmp(name, "__NSGetExecutablePath") == 0 ||
		std::strcmp(name, "_NSGetExecutablePath") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_NSGetExecutablePath);
	}
	if (std::strcmp(name, "_clock_gettime") == 0 || std::strcmp(name, "clock_gettime") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clock_gettime);
	}
	if (std::strcmp(name, "_clock_gettime_nocancel") == 0 ||
		std::strcmp(name, "__clock_gettime_nocancel") == 0 ||
		std::strcmp(name, "clock_gettime_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clock_gettime);
	}
	if (std::strcmp(name, "_clock_getres") == 0 || std::strcmp(name, "clock_getres") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clock_getres);
	}
	if (std::strcmp(name, "_clock_getres_nocancel") == 0 ||
		std::strcmp(name, "__clock_getres_nocancel") == 0 ||
		std::strcmp(name, "clock_getres_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clock_getres);
	}
	if (std::strcmp(name, "_clock_nanosleep") == 0 ||
		std::strcmp(name, "__clock_nanosleep") == 0 ||
		std::strcmp(name, "clock_nanosleep") == 0 ||
		std::strcmp(name, "_clock_nanosleep_nocancel") == 0 ||
		std::strcmp(name, "__clock_nanosleep_nocancel") == 0 ||
		std::strcmp(name, "clock_nanosleep_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clock_nanosleep);
	}
	if (std::strcmp(name, "_mach_absolute_time") == 0 ||
		std::strcmp(name, "__mach_absolute_time") == 0 ||
		std::strcmp(name, "mach_absolute_time") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_absolute_time);
	}
	if (std::strcmp(name, "_mach_continuous_time") == 0 ||
		std::strcmp(name, "__mach_continuous_time") == 0 ||
		std::strcmp(name, "mach_continuous_time") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_absolute_time);
	}
	if (std::strcmp(name, "_mach_timebase_info") == 0 ||
		std::strcmp(name, "__mach_timebase_info") == 0 ||
		std::strcmp(name, "mach_timebase_info") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_mach_timebase_info);
	}
	if (std::strcmp(name, "_nanosleep") == 0 ||
		std::strcmp(name, "nanosleep") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_nanosleep);
	}
	if (std::strcmp(name, "_nanosleep_nocancel") == 0 ||
		std::strcmp(name, "__nanosleep_nocancel") == 0 ||
		std::strcmp(name, "nanosleep_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_nanosleep);
	}
	if (std::strcmp(name, "_gettimeofday") == 0 ||
		std::strcmp(name, "gettimeofday") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_gettimeofday);
	}
	if (std::strcmp(name, "_gettimeofday_nocancel") == 0 ||
		std::strcmp(name, "__gettimeofday_nocancel") == 0 ||
		std::strcmp(name, "gettimeofday_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_gettimeofday);
	}
	if (std::strcmp(name, "_usleep") == 0 ||
		std::strcmp(name, "usleep") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_usleep);
	}
	if (std::strcmp(name, "_usleep_nocancel") == 0 ||
		std::strcmp(name, "__usleep_nocancel") == 0 ||
		std::strcmp(name, "usleep_nocancel") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_usleep);
	}
	if (std::strcmp(name, "_getenv") == 0 || std::strcmp(name, "getenv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_getenv);
	}
	if (std::strcmp(name, "_setenv") == 0 || std::strcmp(name, "setenv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_setenv);
	}
	if (std::strcmp(name, "_unsetenv") == 0 || std::strcmp(name, "unsetenv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_unsetenv);
	}
	if (std::strcmp(name, "_putenv") == 0 || std::strcmp(name, "putenv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_putenv);
	}
	if (std::strcmp(name, "_clearenv") == 0 || std::strcmp(name, "clearenv") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_clearenv);
	}
	if (std::strcmp(name, "_environ") == 0 || std::strcmp(name, "environ") == 0 ||
		std::strcmp(name, "__environ") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_environ);
	}
	if (std::strcmp(name, "___NSGetEnviron") == 0 ||
		std::strcmp(name, "__NSGetEnviron") == 0 ||
		std::strcmp(name, "_NSGetEnviron") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_NSGetEnviron);
	}
	if (std::strcmp(name, "_isatty") == 0 || std::strcmp(name, "isatty") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_isatty);
	}
	if (std::strcmp(name, "_ioctl") == 0 || std::strcmp(name, "ioctl") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_ioctl);
	}
	if (std::strcmp(name, "_ctermid") == 0 || std::strcmp(name, "ctermid") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_ctermid);
	}
	if (std::strcmp(name, "_ttyname_r") == 0 || std::strcmp(name, "ttyname_r") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_ttyname_r);
	}
	if (std::strcmp(name, "_ttyname") == 0 || std::strcmp(name, "ttyname") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_ttyname);
	}
	if (std::strcmp(name, "_malloc") == 0 || std::strcmp(name, "malloc") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_malloc);
	}
	if (std::strcmp(name, "_calloc") == 0 || std::strcmp(name, "calloc") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_calloc);
	}
	if (std::strcmp(name, "_realloc") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_realloc);
	}
	if (std::strcmp(name, "_free") == 0 || std::strcmp(name, "free") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_free);
	}
	if (std::strcmp(name, "_memcpy") == 0 || std::strcmp(name, "memcpy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memcpy);
	}
	if (std::strcmp(name, "_memset") == 0 || std::strcmp(name, "memset") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memset);
	}
	if (std::strcmp(name, "_memmove") == 0 || std::strcmp(name, "memmove") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memmove);
	}
	if (std::strcmp(name, "_memcmp") == 0 || std::strcmp(name, "memcmp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memcmp);
	}
	if (std::strcmp(name, "_bzero") == 0 || std::strcmp(name, "bzero") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_bzero);
	}
	if (std::strcmp(name, "_explicit_bzero") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_explicit_bzero);
	}
	if (std::strcmp(name, "_bcopy") == 0 || std::strcmp(name, "bcopy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_bcopy);
	}
	if (std::strcmp(name, "_memccpy") == 0 || std::strcmp(name, "memccpy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memccpy);
	}
	if (std::strcmp(name, "_strdup") == 0 || std::strcmp(name, "strdup") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strdup);
	}
	if (std::strcmp(name, "_strndup") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strndup);
	}
	if (std::strcmp(name, "_strtol") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtol);
	}
	if (std::strcmp(name, "_strtoll") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtoll);
	}
	if (std::strcmp(name, "_strtoul") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtoul);
	}
	if (std::strcmp(name, "_strtod") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtod);
	}
	if (std::strcmp(name, "_strtof") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtof);
	}
	if (std::strcmp(name, "_strtold") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtold);
	}
	if (std::strcmp(name, "_snprintf") == 0 || std::strcmp(name, "snprintf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_snprintf);
	}
	if (std::strcmp(name, "_vsnprintf") == 0 || std::strcmp(name, "vsnprintf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_vsnprintf);
	}
	if (std::strcmp(name, "_vasprintf") == 0 || std::strcmp(name, "vasprintf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_vasprintf);
	}
	if (std::strcmp(name, "_asprintf") == 0 || std::strcmp(name, "asprintf") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_asprintf);
	}
	if (std::strcmp(name, "_strlen") == 0 || std::strcmp(name, "strlen") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strlen);
	}
	if (std::strcmp(name, "_strnlen") == 0 || std::strcmp(name, "strnlen") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strnlen);
	}
	if (std::strcmp(name, "_memchr") == 0 || std::strcmp(name, "memchr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memchr);
	}
	if (std::strcmp(name, "_memmem") == 0 || std::strcmp(name, "memmem") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_memmem);
	}
	if (std::strcmp(name, "_strcmp") == 0 || std::strcmp(name, "strcmp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcmp);
	}
	if (std::strcmp(name, "_strcasecmp") == 0 || std::strcmp(name, "strcasecmp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcasecmp);
	}
	if (std::strcmp(name, "_strncasecmp") == 0 || std::strcmp(name, "strncasecmp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strncasecmp);
	}
	if (std::strcmp(name, "_strcpy") == 0 || std::strcmp(name, "strcpy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcpy);
	}
	if (std::strcmp(name, "_strncpy") == 0 || std::strcmp(name, "strncpy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strncpy);
	}
	if (std::strcmp(name, "_strcat") == 0 || std::strcmp(name, "strcat") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcat);
	}
	if (std::strcmp(name, "_strncat") == 0 || std::strcmp(name, "strncat") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strncat);
	}
	if (std::strcmp(name, "_strlcpy") == 0 || std::strcmp(name, "strlcpy") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strlcpy);
	}
	if (std::strcmp(name, "_strlcat") == 0 || std::strcmp(name, "strlcat") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strlcat);
	}
	if (std::strcmp(name, "_strchr") == 0 || std::strcmp(name, "strchr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strchr);
	}
	if (std::strcmp(name, "_strrchr") == 0 || std::strcmp(name, "strrchr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strrchr);
	}
	if (std::strcmp(name, "_strstr") == 0 || std::strcmp(name, "strstr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strstr);
	}
	if (std::strcmp(name, "_strcasestr") == 0 || std::strcmp(name, "strcasestr") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcasestr);
	}
	if (std::strcmp(name, "_strspn") == 0 || std::strcmp(name, "strspn") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strspn);
	}
	if (std::strcmp(name, "_strcspn") == 0 || std::strcmp(name, "strcspn") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strcspn);
	}
	if (std::strcmp(name, "_strpbrk") == 0 || std::strcmp(name, "strpbrk") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strpbrk);
	}
	if (std::strcmp(name, "_strtok_r") == 0 || std::strcmp(name, "strtok_r") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtok_r);
	}
	if (std::strcmp(name, "_strtok") == 0 || std::strcmp(name, "strtok") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strtok);
	}
	if (std::strcmp(name, "_strerror") == 0 || std::strcmp(name, "strerror") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strerror);
	}
	if (std::strcmp(name, "_strerror_r") == 0 || std::strcmp(name, "strerror_r") == 0) {
		return reinterpret_cast<std::uintptr_t>(&darling_windows_strerror_r);
	}
	return 0;
}

extern "C" [[noreturn]] void darling_windows_exit(int status)
{
	for (;;) {
		std::function<void()> handler;
		{
			std::lock_guard lock(exit_handler_mutex);
			if (darling_exit_handlers.empty())
				break;
			handler = std::move(darling_exit_handlers.back().function);
			darling_exit_handlers.pop_back();
		}
		if (handler != nullptr)
			handler();
	}
	ExitProcess(static_cast<UINT>(status));
	std::abort();
}

extern "C" [[noreturn]] void darling_windows__exit(int status)
{
	ExitProcess(static_cast<UINT>(status));
	std::abort();
}

extern "C" int darling_windows_atexit(void (*function)())
{
	if (function == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	try {
		std::lock_guard lock(exit_handler_mutex);
		darling_exit_handlers.push_back({[function]() { function(); }, nullptr});
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_cxa_atexit(void (*function)(void*), void* argument,
	void* dso_handle)
{
	(void)dso_handle;
	if (function == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	try {
		std::lock_guard lock(exit_handler_mutex);
		darling_exit_handlers.push_back({[function, argument]() {
			function(argument);
		}, dso_handle});
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" void darling_windows_cxa_finalize(void* dso_handle)
{
	std::vector<std::function<void()>> pending;
	{
		std::lock_guard lock(exit_handler_mutex);
		for (std::size_t index = darling_exit_handlers.size(); index != 0; --index) {
			const auto position = index - 1;
			if (dso_handle != nullptr && darling_exit_handlers[position].dso_handle != dso_handle)
				continue;
			pending.push_back(std::move(darling_exit_handlers[position].function));
			darling_exit_handlers.erase(darling_exit_handlers.begin() +
				static_cast<std::ptrdiff_t>(position));
		}
	}
	for (auto& function : pending) {
		if (function)
			function();
	}
}

extern "C" int darling_windows_getpid()
{
	return static_cast<int>(GetCurrentProcessId());
}

extern "C" int darling_windows_getpgrp()
{
	return static_cast<int>(GetCurrentProcessId());
}

extern "C" int darling_windows_getpgid(int process_id)
{
	const int current = darling_windows_getpid();
	if (process_id == 0 || process_id == current)
		return current;
	{
		std::lock_guard lock(child_resource_mutex);
		const auto child = darling_child_groups.find(process_id);
		if (child != darling_child_groups.end() && child->second != 0)
			return child->second;
	}
	darling::windows_host::DarwinErrno::Set(3);
	return -1;
}

extern "C" int darling_windows_getsid(int process_id)
{
	const int current = darling_windows_getpid();
	if (process_id == 0 || process_id == current)
		return current;
	{
		std::lock_guard lock(child_resource_mutex);
		const auto child = darling_child_groups.find(process_id);
		if (child != darling_child_groups.end() && child->second != 0)
			return child->second;
	}
	darling::windows_host::DarwinErrno::Set(3);
	return -1;
}

extern "C" int darling_windows_setpgid(int process_id, int group_id)
{
	const int current = darling_windows_getpid();
	if ((process_id == 0 || process_id == current) &&
		(group_id == 0 || group_id == current))
		return 0;
	const int target_pid = process_id == 0 ? current : process_id;
	std::lock_guard lock(child_resource_mutex);
	const auto child = darling_child_groups.find(target_pid);
	if (child != darling_child_groups.end()) {
		const int target_group = group_id == 0 ? target_pid : group_id;
		if (target_group == target_pid || darling_child_groups.find(target_group) != darling_child_groups.end()) {
			child->second = target_group;
			return 0;
		}
	}
	darling::windows_host::DarwinErrno::Set(1);
	return -1;
}

extern "C" int darling_windows_setsid()
{
	// Windows process boundaries already provide the isolated session boundary
	// used by this bridge; subsequent calls match POSIX EPERM semantics.
	static thread_local bool session_created = false;
	if (session_created) {
		darling::windows_host::DarwinErrno::Set(1);
		return -1;
	}
	session_created = true;
	return darling_windows_getpid();
}

extern "C" int darling_windows_getppid()
{
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return -1;
	}
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	const DWORD current_pid = GetCurrentProcessId();
	DWORD parent_pid = 0;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (entry.th32ProcessID == current_pid) {
				parent_pid = entry.th32ParentProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return parent_pid == 0 ? -1 : static_cast<int>(parent_pid);
}

extern "C" int darling_windows_waitpid(int process_id, int* status, int options)
{
	std::lock_guard wait_lock(child_wait_mutex);
	constexpr int wait_nohang = 1;
	if ((options & ~wait_nohang) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	HANDLE process = nullptr;
	const bool nohang = (options & wait_nohang) != 0;
	const bool any_child = process_id == -1;
	const bool same_group = process_id == 0;
	const bool group_child = same_group || process_id < -1;
	if (any_child || group_child) {
		const int process_group = any_child ? -1 : (same_group ? 0 : -process_id);
		if (!OpenAnyDarlingChild(nohang, process_group, process_id, process)) {
			darling::windows_host::DarwinErrno::Set(10);
			return -1;
		}
		if (process_id == 0)
			return 0;
	}
	else {
		process = DuplicateDarlingChildHandle(process_id);
		if (process == nullptr) {
			process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
				FALSE, static_cast<DWORD>(process_id));
		}
		if (process == nullptr) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
	}
	const DWORD wait_result = (any_child || group_child) ? WAIT_OBJECT_0 : WaitForSingleObject(process,
		nohang ? 0 : INFINITE);
	if (wait_result == WAIT_TIMEOUT) {
		CloseHandle(process);
		return 0;
	}
	if (wait_result != WAIT_OBJECT_0) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		CloseHandle(process);
		return -1;
	}
	DWORD exit_code = 0;
	const bool queried = GetExitCodeProcess(process, &exit_code) != FALSE;
	const bool darling_child = IsDarlingChild(process_id);
	if (queried && darling_child) {
		darling_rusage measured_usage{};
		if (MeasureWindowsRusage(process, measured_usage))
			AccumulateChildResources(measured_usage);
		ForgetDarlingChild(process_id);
	}
	CloseHandle(process);
	if (!queried) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	if (status != nullptr) {
		*status = exit_code <= 0xffu ? static_cast<int>(exit_code << 8) :
			static_cast<int>(exit_code);
	}
	return process_id;
}

extern "C" int darling_windows_wait4(int process_id, int* status, int options,
	darling_rusage* usage)
{
	std::lock_guard wait_lock(child_wait_mutex);
	if (usage != nullptr)
		std::memset(usage, 0, sizeof(*usage));
	if ((options & ~1) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	HANDLE process = nullptr;
	const bool any_child = process_id == -1;
	const bool same_group = process_id == 0;
	const bool group_child = same_group || process_id < -1;
	if (any_child || group_child) {
		const int process_group = any_child ? -1 : (same_group ? 0 : -process_id);
		if (!OpenAnyDarlingChild((options & 1) != 0, process_group, process_id, process)) {
			darling::windows_host::DarwinErrno::Set(10);
			return -1;
		}
		if (process_id == 0)
			return 0;
	}
	else {
		process = DuplicateDarlingChildHandle(process_id);
		if (process == nullptr) {
			process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
				FALSE, static_cast<DWORD>(process_id));
		}
		if (process == nullptr) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
	}
	const DWORD wait_result = (any_child || group_child) ? WAIT_OBJECT_0 : WaitForSingleObject(process,
		(options & 1) != 0 ? 0 : INFINITE);
	if (wait_result == WAIT_TIMEOUT) {
		CloseHandle(process);
		return 0;
	}
	if (wait_result != WAIT_OBJECT_0) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		CloseHandle(process);
		return -1;
	}
	const bool darling_child = IsDarlingChild(process_id);
	DWORD exit_code = 0;
	if (!GetExitCodeProcess(process, &exit_code)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		CloseHandle(process);
		return -1;
	}
	if (status != nullptr) {
		*status = exit_code <= 0xffu ? static_cast<int>(exit_code << 8) :
			static_cast<int>(exit_code);
	}
	darling_rusage measured_usage{};
	(void)MeasureWindowsRusage(process, measured_usage);
	if (usage != nullptr)
		*usage = measured_usage;
	if (darling_child) {
		AccumulateChildResources(measured_usage);
		ForgetDarlingChild(process_id);
	}
	CloseHandle(process);
	return process_id;
}

extern "C" int darling_windows_waitid(int id_type, int id,
	darling_siginfo* info, int options)
{
	std::lock_guard wait_lock(child_wait_mutex);
	constexpr int darwin_p_all = 0;
	constexpr int darwin_p_pid = 1;
	constexpr int darwin_p_pgid = 2;
	constexpr int wait_nohang = 1;
	constexpr int wait_exited = 4;
	constexpr int wait_nowait = 0x20;
	constexpr int supported_options = wait_nohang | wait_exited | wait_nowait;
	constexpr int sigchld = 20;
	constexpr int child_exited = 1;
	if ((options & wait_exited) == 0 || (options & ~supported_options) != 0 ||
		(id_type != darwin_p_all && id_type != darwin_p_pid && id_type != darwin_p_pgid) ||
		(id_type == darwin_p_pid && id <= 0) ||
		(id_type == darwin_p_pgid && id <= 0)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const bool nohang = (options & wait_nohang) != 0;
	const bool nowait = (options & wait_nowait) != 0;
	int selected_id = id_type == darwin_p_all ? -1 :
		(id_type == darwin_p_pid ? id : -id);
	const int requested_termination_signal = id_type == darwin_p_pid ?
		DarlingChildTerminationSignal(selected_id) : 0;
	if (nowait) {
		HANDLE process = nullptr;
		const bool any_child = selected_id == -1;
		const bool group_child = selected_id < -1;
		const bool selected_group = selected_id == 0;
		bool opened = false;
		if (any_child || group_child || selected_group) {
			const int process_group = any_child ? -1 :
				(selected_group ? 0 : -selected_id);
			opened = OpenAnyDarlingChild(nohang, process_group, selected_id, process);
		} else {
			process = DuplicateDarlingChildHandle(selected_id);
			opened = process != nullptr;
		}
		if (!opened) {
			darling::windows_host::DarwinErrno::Set(10);
			return -1;
		}
		if (selected_id == 0)
			return 0;
		const DWORD wait_result = (any_child || group_child || selected_group) ?
			WAIT_OBJECT_0 : WaitForSingleObject(process, nohang ? 0 : INFINITE);
		if (wait_result == WAIT_TIMEOUT) {
			CloseHandle(process);
			if (info != nullptr)
				std::memset(info, 0, sizeof(*info));
			return 0;
		}
		if (wait_result != WAIT_OBJECT_0) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			CloseHandle(process);
			return -1;
		}
		DWORD exit_code = 0;
		if (!GetExitCodeProcess(process, &exit_code)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			CloseHandle(process);
			return -1;
		}
		const DWORD observed_pid = GetProcessId(process);
		const int observed_termination_signal = observed_pid == 0 ? 0 :
			DarlingChildTerminationSignal(static_cast<int>(observed_pid));
		if (info != nullptr) {
			std::memset(info, 0, sizeof(*info));
			info->si_signo = sigchld;
			info->si_errno = 0;
			info->si_code = (observed_termination_signal != 0 ?
				observed_termination_signal : requested_termination_signal) != 0 ? 2 : child_exited;
			info->si_pid = observed_pid != 0 ? static_cast<int>(observed_pid) : selected_id;
			info->si_uid = static_cast<std::uint32_t>(darling_windows_getuid());
			const int termination_signal = observed_termination_signal != 0 ?
				observed_termination_signal : requested_termination_signal;
			info->si_status = termination_signal != 0 ? termination_signal :
				(exit_code <= 0xffu ? static_cast<int>(exit_code) :
				static_cast<int>((exit_code >> 8) & 0xffu));
		}
		CloseHandle(process);
		return 0;
	}
	int status = 0;
	int observed_termination_signal = requested_termination_signal;
	if (selected_id < 0) {
		HANDLE observed_process = nullptr;
		int observed_pid = selected_id;
		if (OpenAnyDarlingChild(nohang, selected_id == -1 ? -1 : -selected_id,
			observed_pid, observed_process) && observed_process != nullptr) {
			observed_termination_signal = DarlingChildTerminationSignal(observed_pid);
			CloseHandle(observed_process);
		}
	}
	const int result = darling_windows_wait4(selected_id, &status,
		nohang ? wait_nohang : 0, nullptr);
	if (result < 0)
		return -1;
	if (result == 0) {
		if (info != nullptr)
			std::memset(info, 0, sizeof(*info));
		return 0;
	}
	if (info != nullptr) {
		std::memset(info, 0, sizeof(*info));
		info->si_signo = sigchld;
		info->si_errno = 0;
		info->si_code = observed_termination_signal != 0 ? 2 : child_exited;
		info->si_pid = result;
		info->si_uid = static_cast<std::uint32_t>(darling_windows_getuid());
		info->si_status = observed_termination_signal != 0 ?
			observed_termination_signal : (status >> 8) & 0xff;
	}
	return 0;
}

extern "C" int darling_windows_posix_spawn(int* process_id, const char* path,
	const void* file_actions, const void* attributes,
	const char* const argv[], const char* const environment[])
{
	return SpawnPosixProcess(process_id, path, file_actions, attributes,
		argv, environment, false);
}

extern "C" int darling_windows_posix_spawnp(int* process_id, const char* file,
	const void* file_actions, const void* attributes,
	const char* const argv[], const char* const environment[])
{
	return SpawnPosixProcess(process_id, file, file_actions, attributes,
		argv, environment, true);
}

extern "C" int darling_windows_getuid()
{
	int identity = 0;
	if (!CurrentDarwinIdentityId(identity)) {
		darling::windows_host::DarwinErrno::Set(1);
		return -1;
	}
	return identity;
}

extern "C" int darling_windows_geteuid()
{
	return darling_windows_getuid();
}

extern "C" int darling_windows_getgid()
{
	return darling_windows_getuid();
}

extern "C" int darling_windows_getegid()
{
	return darling_windows_getuid();
}

extern "C" int darling_windows_getresuid(std::uint32_t* real_uid,
	std::uint32_t* effective_uid, std::uint32_t* saved_uid)
{
	if (real_uid == nullptr || effective_uid == nullptr || saved_uid == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto uid = static_cast<std::uint32_t>(darling_windows_getuid());
	*real_uid = uid; *effective_uid = uid; *saved_uid = uid;
	return 0;
}

extern "C" int darling_windows_getresgid(std::uint32_t* real_gid,
	std::uint32_t* effective_gid, std::uint32_t* saved_gid)
{
	if (real_gid == nullptr || effective_gid == nullptr || saved_gid == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto gid = static_cast<std::uint32_t>(darling_windows_getgid());
	*real_gid = gid; *effective_gid = gid; *saved_gid = gid;
	return 0;
}

extern "C" int darling_windows_getgroups(int group_count, std::uint32_t* groups)
{
	if (group_count < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (group_count == 0) {
		return 1;
	}
	if (groups == nullptr) {
		darling::windows_host::DarwinErrno::Set(14);
		return -1;
	}
	groups[0] = static_cast<std::uint32_t>(darling_windows_getgid());
	return 1;
}

extern "C" int darling_windows_setgroups(int group_count, const std::uint32_t* groups)
{
	if (group_count < 0 || (group_count > 0 && groups == nullptr)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	darling::windows_host::DarwinErrno::Set(1);
	return -1;
}

static int SetDarwinIdentity(int requested, int current)
{
	if (requested < 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (requested == current) {
		return 0;
	}
	darling::windows_host::DarwinErrno::Set(1);
	return -1;
}

extern "C" int darling_windows_setuid(int uid)
{
	return SetDarwinIdentity(uid, darling_windows_getuid());
}

extern "C" int darling_windows_seteuid(int uid)
{
	return SetDarwinIdentity(uid, darling_windows_geteuid());
}

extern "C" int darling_windows_setgid(int gid)
{
	return SetDarwinIdentity(gid, darling_windows_getgid());
}

extern "C" int darling_windows_setegid(int gid)
{
	return SetDarwinIdentity(gid, darling_windows_getegid());
}

extern "C" int darling_windows_setreuid(int real_uid, int effective_uid)
{
	return SetDarwinIdentity(real_uid, darling_windows_getuid()) == 0 ?
		SetDarwinIdentity(effective_uid, darling_windows_geteuid()) : -1;
}

extern "C" int darling_windows_setregid(int real_gid, int effective_gid)
{
	return SetDarwinIdentity(real_gid, darling_windows_getgid()) == 0 ?
		SetDarwinIdentity(effective_gid, darling_windows_getegid()) : -1;
}

extern "C" int darling_windows_setresuid(int real_uid, int effective_uid, int saved_uid)
{
	return SetDarwinIdentity(real_uid, darling_windows_getuid()) == 0 &&
		SetDarwinIdentity(effective_uid, darling_windows_geteuid()) == 0 ?
		SetDarwinIdentity(saved_uid, darling_windows_getuid()) : -1;
}

extern "C" int darling_windows_setresgid(int real_gid, int effective_gid, int saved_gid)
{
	return SetDarwinIdentity(real_gid, darling_windows_getgid()) == 0 &&
		SetDarwinIdentity(effective_gid, darling_windows_getegid()) == 0 ?
		SetDarwinIdentity(saved_gid, darling_windows_getgid()) : -1;
}

extern "C" int darling_windows_getlogin_r(char* buffer, std::size_t size)
{
	if (buffer == nullptr || size == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	DWORD length = static_cast<DWORD>(size);
	if (!GetUserNameA(buffer, &length)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			darling::windows_host::DarwinErrno::Set(34);
			return 34;
		}
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return 1;
	}
	return 0;
}

extern "C" const char* darling_windows_getlogin()
{
	thread_local char login[256]{};
	if (darling_windows_getlogin_r(login, sizeof(login)) != 0) {
		return nullptr;
	}
	return login;
}

extern "C" int darling_windows_setlogin(const char* name)
{
	if (name == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const char* current = darling_windows_getlogin();
	if (current != nullptr && std::strcmp(name, current) == 0) {
		return 0;
	}
	darling::windows_host::DarwinErrno::Set(1);
	return -1;
}

extern "C" int darling_windows_getrusage(int who, darling_rusage* result)
{
	if (result == nullptr || (who != 0 && who != -1)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::memset(result, 0, sizeof(*result));
	if (who == -1) {
		std::lock_guard lock(child_resource_mutex);
		*result = completed_child_resources;
		return 0;
	}
	FILETIME creation{}, exit_time{}, kernel{}, user{};
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	const auto to_timeval = [](const FILETIME& value) {
		ULARGE_INTEGER ticks{};
		ticks.LowPart = value.dwLowDateTime;
		ticks.HighPart = value.dwHighDateTime;
		const auto microseconds = ticks.QuadPart / 10ull;
		darling_timeval result_time{};
		result_time.tv_sec = static_cast<std::int64_t>(microseconds / 1'000'000ull);
		result_time.tv_usec = static_cast<std::int64_t>(microseconds % 1'000'000ull);
		return result_time;
	};
	result->ru_stime = to_timeval(kernel);
	result->ru_utime = to_timeval(user);
	PROCESS_MEMORY_COUNTERS_EX memory{};
	if (GetProcessMemoryInfo(GetCurrentProcess(),
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
		result->ru_maxrss = static_cast<std::int64_t>(memory.PeakWorkingSetSize / 1024);
		result->ru_minflt = static_cast<std::int64_t>(memory.PageFaultCount);
	}
	IO_COUNTERS io{};
	if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
		result->ru_inblock = static_cast<std::int64_t>(io.ReadOperationCount);
		result->ru_oublock = static_cast<std::int64_t>(io.WriteOperationCount);
	}
	return 0;
}

extern "C" std::int64_t darling_windows_times(darling_tms* result)
{
	if (result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::memset(result, 0, sizeof(*result));
	FILETIME creation{}, exit_time{}, kernel{}, user{};
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	const auto to_ticks = [](const FILETIME& value) {
		ULARGE_INTEGER ticks{};
		ticks.LowPart = value.dwLowDateTime;
		ticks.HighPart = value.dwHighDateTime;
		return static_cast<std::int64_t>(ticks.QuadPart / 100'000ull);
	};
	result->tms_stime = to_ticks(kernel);
	result->tms_utime = to_ticks(user);
	return static_cast<std::int64_t>(GetTickCount64() / 10ull);
}

extern "C" void* darling_windows_dlopen(const char* path, int)
{
	if (path == nullptr) {
		dynamic_loader_error.clear();
		return DarwinRtldDefaultHandle();
	}
	if (path[0] == '\0') {
		dynamic_loader_error = "dlopen: unsupported or empty Darwin library path";
		return nullptr;
	}
	const auto wide_path = Utf8Path(path);
	if (wide_path.empty()) {
		dynamic_loader_error = "dlopen: invalid UTF-8 library path";
		return nullptr;
	}
	try {
		const auto normalized_path = std::filesystem::absolute(wide_path).lexically_normal();
		const auto key = normalized_path.wstring();
		std::shared_ptr<DynamicImageLoadState> load_state;
		for (;;) {
			std::unique_lock lock(dynamic_image_mutex);
			const auto found = dynamic_image_paths.find(key);
			if (found != dynamic_image_paths.end()) {
				++dynamic_image_references[found->second];
				dynamic_loader_error.clear();
				return found->second;
			}
			const auto loading = dynamic_image_loads.find(key);
			if (loading != dynamic_image_loads.end()) {
				// A same-thread recursive load can occur from an initializer;
				// allow that path to fail or resolve independently rather than
				// deadlocking on our own condition variable.
				if (loading->second->owner == std::this_thread::get_id()) {
					load_state = loading->second;
					break;
				}
				loading->second->condition.wait(lock,
					[&loading]() { return loading->second->complete; });
				continue;
			}
			load_state = std::make_shared<DynamicImageLoadState>();
			load_state->owner = std::this_thread::get_id();
			dynamic_image_loads.emplace(key, load_state);
			break;
		}
		auto* image = darling::windows_host::OpenDynamicImage(wide_path);
		darling::windows_host::DarwinDynamicImage* duplicate_image = nullptr;
		void* duplicate_handle = nullptr;
		{
			std::lock_guard lock(dynamic_image_mutex);
			const auto found = dynamic_image_paths.find(key);
			if (found != dynamic_image_paths.end()) {
				++dynamic_image_references[found->second];
				duplicate_image = image;
				duplicate_handle = found->second;
			} else {
				dynamic_images.emplace(image, image);
				dynamic_image_references.emplace(image, 1);
				dynamic_image_paths.emplace(key, image);
			}
			const auto loading = dynamic_image_loads.find(key);
			if (loading != dynamic_image_loads.end() &&
				loading->second == load_state) {
				load_state->complete = true;
				dynamic_image_loads.erase(loading);
				load_state->condition.notify_all();
			}
		}
		if (duplicate_image != nullptr) {
			// Teardown is deliberately outside the loader mutex.
			darling::windows_host::CloseDynamicImage(duplicate_image);
			dynamic_loader_error.clear();
			return duplicate_handle;
		}
		dynamic_loader_error.clear();
		return image;
	} catch (...) {
		try {
			const auto key = std::filesystem::absolute(wide_path).lexically_normal().wstring();
			std::lock_guard lock(dynamic_image_mutex);
			const auto loading = dynamic_image_loads.find(key);
			if (loading != dynamic_image_loads.end()) {
				loading->second->complete = true;
				auto state = loading->second;
				dynamic_image_loads.erase(loading);
				state->condition.notify_all();
			}
		} catch (...) {
		}
	}
	const HMODULE module = LoadLibraryW(wide_path.c_str());
	if (module == nullptr) {
		SetDynamicLoaderError("dlopen");
		return nullptr;
	}
	dynamic_loader_error.clear();
	return module;
}

extern "C" void* darling_windows_dlsym(void* handle, const char* symbol)
{
	const auto rtld_default = DarwinRtldDefaultHandle();
	if ((handle == nullptr && handle != rtld_default) ||
		symbol == nullptr || symbol[0] == '\0') {
		dynamic_loader_error = "dlsym: invalid handle or symbol";
		return nullptr;
	}
	if (handle == rtld_default) {
		std::lock_guard lock(dynamic_image_mutex);
		for (const auto& entry : dynamic_images) {
			const auto address = darling::windows_host::DynamicImageSymbol(
				*entry.second, symbol);
			if (address != 0) {
				dynamic_loader_error.clear();
				return reinterpret_cast<void*>(address);
			}
		}
		const auto host_address = darling_windows_host_symbol(symbol);
		if (host_address != 0) {
			dynamic_loader_error.clear();
			return reinterpret_cast<void*>(host_address);
		}
		dynamic_loader_error = "dlsym: RTLD_DEFAULT symbol not found";
		return nullptr;
	}
	{
		std::lock_guard lock(dynamic_image_mutex);
		const auto found = dynamic_images.find(handle);
		if (found != dynamic_images.end()) {
			const auto address = darling::windows_host::DynamicImageSymbol(
				*found->second, symbol);
			if (address == 0) {
				dynamic_loader_error = "dlsym: Mach-O symbol not found";
				return nullptr;
			}
			dynamic_loader_error.clear();
			return reinterpret_cast<void*>(address);
		}
	}
	FARPROC address = GetProcAddress(static_cast<HMODULE>(handle), symbol);
	if (address == nullptr && symbol[0] == '_') {
		address = GetProcAddress(static_cast<HMODULE>(handle), symbol + 1);
	}
	if (address == nullptr) {
		SetDynamicLoaderError("dlsym");
		return nullptr;
	}
	dynamic_loader_error.clear();
	return reinterpret_cast<void*>(address);
}

extern "C" int darling_windows_dlclose(void* handle)
{
	if (handle == nullptr) {
		SetDynamicLoaderError("dlclose", ERROR_INVALID_HANDLE);
		return -1;
	}
	if (handle == DarwinRtldDefaultHandle()) {
		dynamic_loader_error.clear();
		return 0;
	}
	darling::windows_host::DarwinDynamicImage* dynamic_image = nullptr;
	{
		std::lock_guard lock(dynamic_image_mutex);
		const auto found = dynamic_images.find(handle);
		if (found != dynamic_images.end()) {
			auto reference = dynamic_image_references.find(handle);
			if (reference != dynamic_image_references.end() && reference->second > 1) {
				--reference->second;
				dynamic_loader_error.clear();
				return 0;
			}
			dynamic_image = found->second;
			dynamic_images.erase(found);
			dynamic_image_references.erase(handle);
			for (auto iterator = dynamic_image_paths.begin();
				iterator != dynamic_image_paths.end(); ++iterator) {
				if (iterator->second == handle) {
					dynamic_image_paths.erase(iterator);
					break;
				}
			}
		}
	}
	if (dynamic_image != nullptr) {
		darling::windows_host::CloseDynamicImage(dynamic_image);
		dynamic_loader_error.clear();
		return 0;
	}
	if (!FreeLibrary(static_cast<HMODULE>(handle))) {
		SetDynamicLoaderError("dlclose", handle == nullptr ? ERROR_INVALID_HANDLE : GetLastError());
		return -1;
	}
	dynamic_loader_error.clear();
	return 0;
}

extern "C" const char* darling_windows_dlerror()
{
	if (dynamic_loader_error.empty()) {
		return nullptr;
	}
	thread_local std::string pending_error;
	pending_error = std::move(dynamic_loader_error);
	dynamic_loader_error.clear();
	return pending_error.c_str();
}

extern "C" int darling_windows_dladdr(const void* address, darling_dl_info* info)
{
	if (address == nullptr || info == nullptr) {
		return 0;
	}
	MEMORY_BASIC_INFORMATION memory{};
	if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
		memory.AllocationBase == nullptr) {
		return 0;
	}
	static thread_local std::string image_name;
	char path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase),
		path, static_cast<DWORD>(std::size(path)));
	if (length == 0 || length >= std::size(path)) {
		return 0;
	}
	image_name.assign(path, length);
	info->dli_fname = image_name.c_str();
	info->dli_fbase = memory.AllocationBase;
	info->dli_sname = nullptr;
	info->dli_saddr = nullptr;
	return 1;
}

extern "C" std::uint32_t darling_windows_dyld_image_count()
{
	const auto modules = CurrentProcessModules();
	return static_cast<std::uint32_t>(modules.size());
}

extern "C" const char* darling_windows_dyld_get_image_name(std::uint32_t index)
{
	const auto modules = CurrentProcessModules();
	if (index >= modules.size()) {
		return nullptr;
	}
	static thread_local std::string image_name;
	char path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(modules[index], path,
		static_cast<DWORD>(std::size(path)));
	if (length == 0 || length >= std::size(path)) {
		return nullptr;
	}
	image_name.assign(path, length);
	return image_name.c_str();
}

extern "C" const void* darling_windows_dyld_get_image_header(std::uint32_t index)
{
	const auto modules = CurrentProcessModules();
	return index < modules.size() ? static_cast<const void*>(modules[index]) : nullptr;
}

extern "C" std::intptr_t darling_windows_dyld_get_image_vmaddr_slide(std::uint32_t index)
{
	(void)index;
	return 0;
}

extern "C" int darling_windows_getpagesize()
{
	SYSTEM_INFO system_info{};
	GetSystemInfo(&system_info);
	if (system_info.dwPageSize == 0 || system_info.dwPageSize > static_cast<DWORD>((std::numeric_limits<int>::max)())) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	return static_cast<int>(system_info.dwPageSize);
}

extern "C" int darling_windows_getpagesizes(std::size_t* pagesizes, int count)
{
	if (count < 0 || (count > 0 && pagesizes == nullptr)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (count == 0) return 1;
	pagesizes[0] = static_cast<std::size_t>(darling_windows_getpagesize());
	return 1;
}

extern "C" std::int64_t darling_windows_sysconf(int name)
{
	// Darwin's public sysconf numbering for the values handled here.
	constexpr int sc_pagesize = 29;
	constexpr int sc_clk_tck = 3;
	constexpr int sc_nprocessors_conf = 57;
	constexpr int sc_nprocessors_onln = 58;
	constexpr int sc_phys_pages = 200;
	constexpr int sc_avphys_pages = 201;
	SYSTEM_INFO system_info{};
	GetSystemInfo(&system_info);
	switch (name) {
	case sc_clk_tck:
		return 100;
	case sc_pagesize:
		return darling_windows_getpagesize();
	case 4: // _SC_OPEN_MAX
		return darling_windows_getdtablesize();
	case sc_nprocessors_conf:
	case sc_nprocessors_onln:
		return system_info.dwNumberOfProcessors == 0 ? 1 :
			static_cast<std::int64_t>(system_info.dwNumberOfProcessors);
	case sc_phys_pages:
	case sc_avphys_pages: {
		MEMORYSTATUSEX memory{};
		memory.dwLength = sizeof(memory);
		if (!GlobalMemoryStatusEx(&memory)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		const auto page_size = static_cast<std::uint64_t>(darling_windows_getpagesize());
		const auto bytes = name == sc_phys_pages ? memory.ullTotalPhys : memory.ullAvailPhys;
		return static_cast<std::int64_t>(bytes / page_size);
	}
	default:
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
}

extern "C" std::size_t darling_windows_confstr(int name, char* buffer, std::size_t length)
{
	std::string value;
	switch (name) {
	case 1: // _CS_PATH
		value = "/usr/bin:/bin:/usr/sbin:/sbin";
		break;
	case 65536: // _CS_DARWIN_USER_DIR
	case 65537: // _CS_DARWIN_USER_TEMP_DIR
	case 65538: // _CS_DARWIN_USER_CACHE_DIR: use the Windows per-user equivalents
	{
		const char* variable = name == 65536 ? "USERPROFILE" :
			(name == 65537 ? "TEMP" : "LOCALAPPDATA");
		char environment[4096]{};
		const DWORD written = GetEnvironmentVariableA(variable, environment,
			static_cast<DWORD>(std::size(environment)));
		if (written == 0 || written >= std::size(environment)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return 0;
		}
		value.assign(environment, written);
		break;
	}
	default:
		darling::windows_host::DarwinErrno::Set(22);
		return 0;
	}
	const std::size_t required = value.size() + 1;
	if (buffer != nullptr && length != 0) {
		const std::size_t copied = (std::min)(length - 1, value.size());
		std::memcpy(buffer, value.data(), copied);
		buffer[copied] = '\0';
	}
	return required;
}

namespace {

std::int64_t DarwinPathconfValue(int name)
{
	switch (name) {
	case 1: return 1024;       // _PC_LINK_MAX
	case 2: return 255;        // _PC_MAX_CANON
	case 3: return 255;        // _PC_MAX_INPUT
	case 4: return 255;        // _PC_NAME_MAX
	case 5: return 4096;       // _PC_PATH_MAX
	case 6: return 4096;       // _PC_PIPE_BUF
	case 7: return 1;          // _PC_CHOWN_RESTRICTED
	case 8: return 1;          // _PC_NO_TRUNC
	case 9: return 0;          // _PC_VDISABLE
	case 10: return 255;       // _PC_NAME_CHARS_MAX
	case 11: return 0;         // _PC_CASE_SENSITIVE (default Windows volume)
	case 12: return 1;         // _PC_CASE_PRESERVING
	case 13: return 1;         // _PC_EXTENDED_SECURITY_NP
	case 14: return 0;         // _PC_AUTH_OPAQUE_NP
	case 15: return 1;         // _PC_2_SYMLINKS
	case 16: return 4096;      // _PC_ALLOC_SIZE_MIN
	case 17: return 0;         // _PC_ASYNC_IO
	case 18: return 64;        // _PC_FILESIZEBITS
	case 19: return 0;         // _PC_PRIO_IO
	case 20: return 4096;      // _PC_REC_INCR_XFER_SIZE
	case 21: return 1 << 20;   // _PC_REC_MAX_XFER_SIZE
	case 22: return 4096;      // _PC_REC_MIN_XFER_SIZE
	case 23: return 4096;      // _PC_REC_XFER_ALIGN
	case 24: return 1024;      // _PC_SYMLINK_MAX
	case 25: return 1;         // _PC_SYNC_IO
	case 26: return 0;         // _PC_XATTR_SIZE_BITS (not exposed by this bridge)
	case 27: return 4096;      // _PC_MIN_HOLE_SIZE
	default:
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
}

bool DarwinPathExists(const std::filesystem::path& path)
{
	if (path.empty()) return false;
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES) return true;
	darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
	return false;
}

}

extern "C" std::int64_t darling_windows_pathconf(const char* path, int name)
{
	if (path == nullptr || *path == '\0') {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto wide_path = Utf8Path(path);
	if (!DarwinPathExists(wide_path)) return -1;
	return DarwinPathconfValue(name);
}

extern "C" std::int64_t darling_windows_fpathconf(int descriptor, int name)
{
	try {
		const auto path = global_syscalls.PathForDescriptor(descriptor);
		if (!DarwinPathExists(path)) {
			if (path.empty()) darling::windows_host::DarwinErrno::Set(9);
			return -1;
		}
		return DarwinPathconfValue(name);
	} catch (...) {
		darling::windows_host::DarwinErrno::Set(9);
		return -1;
	}
}

extern "C" int darling_windows_getdtablesize()
{
	// The bridge's descriptor table intentionally exposes the Darwin soft
	// limit used by fd_set and poll-compatible callers.
	return 1024;
}

extern "C" int darling_windows_getrlimit(int resource, darling_rlimit* limit)
{
	if (limit == nullptr || resource < 0 || resource >= 9) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::lock_guard lock(resource_limit_mutex);
	*limit = resource_limits[static_cast<std::size_t>(resource)];
	return 0;
}

extern "C" int darling_windows_setrlimit(int resource, const darling_rlimit* limit)
{
	if (limit == nullptr || resource < 0 || resource >= 9 ||
		limit->rlim_cur > limit->rlim_max) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::lock_guard lock(resource_limit_mutex);
	if (limit->rlim_max > resource_limits[static_cast<std::size_t>(resource)].rlim_max) {
		darling::windows_host::DarwinErrno::Set(1);
		return -1;
	}
	resource_limits[static_cast<std::size_t>(resource)] = *limit;
	return 0;
}

namespace {

int DarwinPriorityFromClass(DWORD priority_class)
{
	switch (priority_class) {
	case IDLE_PRIORITY_CLASS: return 10;
	case BELOW_NORMAL_PRIORITY_CLASS: return 5;
	case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
	case HIGH_PRIORITY_CLASS: return -10;
	case REALTIME_PRIORITY_CLASS: return -20;
	default: return 0;
	}
}

DWORD WindowsClassFromDarwinPriority(int priority)
{
	if (priority >= 10) return IDLE_PRIORITY_CLASS;
	if (priority >= 5) return BELOW_NORMAL_PRIORITY_CLASS;
	if (priority <= -20) return REALTIME_PRIORITY_CLASS;
	if (priority <= -10) return HIGH_PRIORITY_CLASS;
	if (priority < 0) return ABOVE_NORMAL_PRIORITY_CLASS;
	return NORMAL_PRIORITY_CLASS;
}

HANDLE DarwinPriorityProcess(int which, std::uint32_t who, DWORD access)
{
	if (which != 0) return nullptr;
	const DWORD pid = who == 0 ? GetCurrentProcessId() : who;
	if (pid == GetCurrentProcessId()) return GetCurrentProcess();
	return OpenProcess(access, FALSE, pid);
}

}

extern "C" int darling_windows_getpriority(int which, std::uint32_t who)
{
	const HANDLE process = DarwinPriorityProcess(which, who, PROCESS_QUERY_LIMITED_INFORMATION);
	if (process == nullptr) {
		darling::windows_host::DarwinErrno::Set(3);
		return -1;
	}
	const DWORD priority_class = GetPriorityClass(process);
	if (process != GetCurrentProcess()) CloseHandle(process);
	if (priority_class == 0) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	return DarwinPriorityFromClass(priority_class);
}

extern "C" int darling_windows_setpriority(int which, std::uint32_t who, int priority)
{
	if (which != 0 || priority < -20 || priority > 20) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const HANDLE process = DarwinPriorityProcess(which, who, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION);
	if (process == nullptr) {
		darling::windows_host::DarwinErrno::Set(3);
		return -1;
	}
	const BOOL updated = SetPriorityClass(process, WindowsClassFromDarwinPriority(priority));
	if (process != GetCurrentProcess()) CloseHandle(process);
	if (!updated) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return -1;
	}
	return 0;
}

extern "C" int darling_windows_sysctlbyname(const char* name, void* old_value,
	std::size_t* old_length, const void* new_value, std::size_t new_length)
{
	if (name == nullptr || old_length == nullptr || (new_value != nullptr && new_length != 0)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto copy_value = [&](const void* value, std::size_t value_length) {
		if (old_value == nullptr) {
			*old_length = value_length;
			return 0;
		}
		if (*old_length < value_length) {
			*old_length = value_length;
			darling::windows_host::DarwinErrno::Set(12);
			return -1;
		}
		std::memcpy(old_value, value, value_length);
		*old_length = value_length;
		return 0;
	};
	if (std::strcmp(name, "hw.ncpu") == 0 || std::strcmp(name, "hw.logicalcpu") == 0 ||
		std::strcmp(name, "hw.activecpu") == 0 || std::strcmp(name, "hw.physicalcpu") == 0) {
		const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		const int value = count == 0 ? 1 : static_cast<int>(count);
		return copy_value(&value, sizeof(value));
	}
	if (std::strcmp(name, "hw.optional.arm64") == 0) {
#if defined(_M_ARM64) || defined(__aarch64__)
		const int value = 1;
#else
		const int value = 0;
#endif
		return copy_value(&value, sizeof(value));
	}
	if (std::strcmp(name, "hw.memsize") == 0) {
		MEMORYSTATUSEX memory{};
		memory.dwLength = sizeof(memory);
		if (!GlobalMemoryStatusEx(&memory)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		return copy_value(&memory.ullTotalPhys, sizeof(memory.ullTotalPhys));
	}
	if (std::strcmp(name, "hw.machine") == 0 || std::strcmp(name, "hw.model") == 0 ||
		std::strcmp(name, "kern.ostype") == 0 || std::strcmp(name, "kern.osrelease") == 0 ||
		std::strcmp(name, "kern.osversion") == 0) {
		const char* value = nullptr;
		if (std::strcmp(name, "hw.machine") == 0) {
#if defined(_WIN64)
			value = "x86_64";
#else
			value = "i386";
#endif
		} else if (std::strcmp(name, "hw.model") == 0) {
			value = "Darling Windows Host";
		} else if (std::strcmp(name, "kern.ostype") == 0) {
			value = "Darwin";
		} else if (std::strcmp(name, "kern.osrelease") == 0) {
			value = "Windows";
		} else {
			value = "Darling Windows port";
		}
		return copy_value(value, std::strlen(value) + 1);
	}
	darling::windows_host::DarwinErrno::Set(2);
	return -1;
}

extern "C" int darling_windows_sysctl(const int* mib, std::uint32_t mib_length,
	void* old_value, std::size_t* old_length, const void* new_value,
	std::size_t new_length)
{
	if (mib == nullptr || mib_length != 2 || old_length == nullptr ||
		(new_value != nullptr && new_length != 0)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (mib[0] != 6) {
		darling::windows_host::DarwinErrno::Set(2);
		return -1;
	}
	const auto copy_value = [&](const void* value, std::size_t value_length) {
		if (old_value == nullptr) {
			*old_length = value_length;
			return 0;
		}
		if (*old_length < value_length) {
			*old_length = value_length;
			darling::windows_host::DarwinErrno::Set(12);
			return -1;
		}
		std::memcpy(old_value, value, value_length);
		*old_length = value_length;
		return 0;
	};
	const DWORD processor_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	const int processor_value = processor_count == 0 ? 1 : static_cast<int>(processor_count);
	const int page_size = darling_windows_getpagesize();
	const char* machine = nullptr;
#if defined(_WIN64)
	const char* machine_name = "x86_64";
#else
	const char* machine_name = "i386";
#endif
	switch (mib[1]) {
	case 1: // HW_MACHINE
	case 12: // HW_MACHINE_ARCH
		machine = machine_name;
		return copy_value(machine, std::strlen(machine) + 1);
	case 2: { // HW_MODEL
		machine = "Darling Windows Host";
		return copy_value(machine, std::strlen(machine) + 1);
	}
	case 3: // HW_NCPU
	case 25: // HW_AVAILCPU
		return copy_value(&processor_value, sizeof(processor_value));
	case 4: { // HW_BYTEORDER
		const int byte_order = 1234;
		return copy_value(&byte_order, sizeof(byte_order));
	}
	case 7: // HW_PAGESIZE
		return copy_value(&page_size, sizeof(page_size));
	case 24: { // HW_MEMSIZE
		MEMORYSTATUSEX memory{};
		memory.dwLength = sizeof(memory);
		if (!GlobalMemoryStatusEx(&memory)) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		return copy_value(&memory.ullTotalPhys, sizeof(memory.ullTotalPhys));
	}
	default:
		darling::windows_host::DarwinErrno::Set(2);
		return -1;
	}
}

extern "C" int darling_windows_sched_yield()
{
	(void)SwitchToThread();
	return 0;
}

extern "C" unsigned int darling_windows_sleep(unsigned int seconds)
{
	const auto milliseconds = static_cast<std::uint64_t>(seconds) * 1000ull;
	if (milliseconds > static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())) {
		const auto maximum_seconds = (std::numeric_limits<DWORD>::max)() / 1000u;
		Sleep((std::numeric_limits<DWORD>::max)());
		return seconds - maximum_seconds;
	}
	Sleep(static_cast<DWORD>(milliseconds));
	return 0;
}

extern "C" std::uint32_t darling_windows_arc4random()
{
	std::uint32_t value = 0;
	darling_windows_arc4random_buf(&value, sizeof(value));
	return value;
}

extern "C" void darling_windows_arc4random_buf(void* buffer, std::size_t bytes)
{
	if (buffer == nullptr || bytes == 0) {
		return;
	}
	using BCryptGenRandomFn = LONG (WINAPI*)(void*, PUCHAR, ULONG, ULONG);
	const HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
	if (bcrypt == nullptr) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		std::memset(buffer, 0, bytes);
		return;
	}
	const auto generate = reinterpret_cast<BCryptGenRandomFn>(GetProcAddress(bcrypt, "BCryptGenRandom"));
	const bool valid_size = bytes <= static_cast<std::size_t>((std::numeric_limits<ULONG>::max)());
	const LONG status = generate != nullptr && valid_size ?
		generate(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(bytes), 0x00000002u) : -1;
	FreeLibrary(bcrypt);
	if (status != 0) {
		darling::windows_host::DarwinErrno::Set(5);
		std::memset(buffer, 0, bytes);
	}
}

extern "C" int darling_windows_getentropy(void* buffer, std::size_t bytes)
{
	if (buffer == nullptr || bytes > 256) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (bytes == 0) {
		return 0;
	}
	darling_windows_arc4random_buf(buffer, bytes);
	return 0;
}

extern "C" int darling_windows_kill(int process_id, int signal_number)
{
	constexpr int darwin_sigkill = 9;
	constexpr int darwin_sigterm = 15;
	if (process_id == 0 || signal_number < 0) {
		darling::windows_host::DarwinErrno::Set(3);
		return -1;
	}
	if (process_id < 0) {
		if (signal_number == 0) {
			const int group_id = -process_id;
			std::lock_guard lock(child_resource_mutex);
			for (const int candidate : darling_child_processes) {
				const auto found = darling_child_groups.find(candidate);
				if (found != darling_child_groups.end() && found->second == group_id)
					return 0;
			}
			darling::windows_host::DarwinErrno::Set(3);
			return -1;
		}
		if (signal_number != darwin_sigkill && signal_number != darwin_sigterm) {
			darling::windows_host::DarwinErrno::Set(1);
			return -1;
		}
		const int group_id = -process_id;
		std::vector<int> members;
		{
			std::lock_guard lock(child_resource_mutex);
			for (const int candidate : darling_child_processes) {
				const auto found = darling_child_groups.find(candidate);
				if (found != darling_child_groups.end() && found->second == group_id)
					members.push_back(candidate);
			}
		}
		if (members.empty()) {
			darling::windows_host::DarwinErrno::Set(3);
			return -1;
		}
		const UINT exit_code = signal_number == darwin_sigkill ? 137u : 143u;
		bool failed = false;
		for (const int member : members) {
			const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE,
				static_cast<DWORD>(member));
			if (process == nullptr || !TerminateProcess(process, exit_code)) {
				failed = true;
				if (process != nullptr) CloseHandle(process);
				continue;
			}
			CloseHandle(process);
			std::lock_guard lock(child_resource_mutex);
			darling_child_termination_signals[member] = signal_number;
		}
		if (failed) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		return 0;
	}
	const DWORD target_pid = static_cast<DWORD>(process_id);
	if (signal_number == 0) {
		const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target_pid);
		if (process == nullptr) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		CloseHandle(process);
		return 0;
	}
	if (signal_number == darwin_sigkill || signal_number == darwin_sigterm) {
		const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, target_pid);
		if (process == nullptr) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return -1;
		}
		const UINT exit_code = signal_number == darwin_sigkill ? 137u : 143u;
		const BOOL terminated = TerminateProcess(process, exit_code);
		const DWORD error = terminated ? ERROR_SUCCESS : GetLastError();
		CloseHandle(process);
		if (!terminated) {
			darling::windows_host::DarwinErrno::SetFromWin32(error);
			return -1;
		}
		{
			std::lock_guard lock(child_resource_mutex);
			if (darling_child_processes.find(process_id) != darling_child_processes.end())
				darling_child_termination_signals[process_id] = signal_number;
		}
		return 0;
	}
	if (target_pid == GetCurrentProcessId() &&
		darling::windows_host::DarwinSignals::Raise(signal_number)) {
		return 0;
	}
	darling::windows_host::DarwinErrno::Set(1);
	return -1;
}

extern "C" int darling_windows_raise(int signal_number)
{
	if (signal_number <= 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	return darling::windows_host::DarwinSignals::Raise(signal_number) ? 0 : -1;
}

extern "C" int darling_windows_gethostname(char* buffer, std::size_t size)
{
	if (buffer == nullptr || size == 0 || size > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	DWORD length = static_cast<DWORD>(size);
	if (!GetComputerNameExA(ComputerNamePhysicalDnsHostname, buffer, &length)) {
		if (GetLastError() == ERROR_MORE_DATA) {
			darling::windows_host::DarwinErrno::Set(40);
		} else {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		}
		return -1;
	}
	return 0;
}

extern "C" int darling_windows_getdomainname(char* buffer, std::size_t size)
{
	if (buffer == nullptr || size == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	char domain[256]{};
	DWORD length = static_cast<DWORD>(sizeof(domain));
	if (!GetComputerNameExA(ComputerNameDnsDomain, domain, &length) || length == 0) {
		const char fallback[] = "local";
		length = static_cast<DWORD>(sizeof(fallback) - 1);
		if (size <= length) {
			darling::windows_host::DarwinErrno::Set(34);
			return -1;
		}
		std::memcpy(buffer, fallback, length + 1);
		return 0;
	}
	if (size <= static_cast<std::size_t>(length)) {
		darling::windows_host::DarwinErrno::Set(34);
		return -1;
	}
	std::memcpy(buffer, domain, static_cast<std::size_t>(length) + 1);
	return 0;
}

extern "C" int darling_windows_uname(darling_utsname* result)
{
	if (result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::memset(result, 0, sizeof(*result));
	const auto copy_field = [](char* destination, const char* value) {
		std::strncpy(destination, value, 255);
		destination[255] = '\0';
	};
	copy_field(result->sysname, "Darwin");
	char host_name[256]{};
	DWORD host_length = static_cast<DWORD>(sizeof(host_name));
	if (GetComputerNameExA(ComputerNamePhysicalDnsHostname, host_name, &host_length)) {
		copy_field(result->nodename, host_name);
	} else {
		copy_field(result->nodename, "localhost");
	}
	copy_field(result->release, "Darling-Windows");
	copy_field(result->version, "Darling native Windows backend");
	copy_field(result->machine, "x86_64");
	return 0;
}

extern "C" int darling_windows_NSGetExecutablePath(char* buffer, std::uint32_t* size)
{
	if (size == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	wchar_t path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
	if (length == 0 || length >= std::size(path)) {
		*size = 0;
		return -1;
	}
	const std::string utf8_path = Utf8String(std::wstring(path, length));
	const std::size_t required = utf8_path.size() + 1;
	if (required > std::numeric_limits<std::uint32_t>::max()) {
		*size = 0;
		return -1;
	}
	const auto required_u32 = static_cast<std::uint32_t>(required);
	if (buffer == nullptr || *size < required_u32) {
		*size = required_u32;
		return -1;
	}
	std::memcpy(buffer, utf8_path.c_str(), required);
	*size = required_u32;
	return 0;
}

extern "C" const char* darling_windows_getprogname()
{
	static thread_local std::string value;
	std::lock_guard lock(program_name_mutex);
	value = darling_program_name;
	return value.c_str();
}

extern "C" void darling_windows_setprogname(const char* name)
{
	if (name == nullptr) {
		return;
	}
	std::lock_guard lock(program_name_mutex);
	darling_program_name = name;
}

extern "C" int darling_windows_clock_gettime(int clock_id, darling_timespec* result)
{
	if (result == nullptr || (clock_id != 0 && clock_id != 6)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto nanoseconds = clock_id == 0 ?
		darling::windows_host::DarwinClock::WallClockNanoseconds() :
		darling::windows_host::DarwinClock::MonotonicNanoseconds();
	result->tv_sec = static_cast<std::int64_t>(nanoseconds / 1'000'000'000ull);
	result->tv_nsec = static_cast<std::int64_t>(nanoseconds % 1'000'000'000ull);
	return 0;
}

extern "C" int darling_windows_clock_getres(int clock_id, darling_timespec* result)
{
	if (result == nullptr || (clock_id != 0 && clock_id != 6)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	result->tv_sec = 0;
	result->tv_nsec = clock_id == 0 ? 100 : 1;
	return 0;
}

extern "C" int darling_windows_clock_nanosleep(int clock_id, int flags,
	const darling_timespec* request, darling_timespec* remaining)
{
	if (request == nullptr || (clock_id != 0 && clock_id != 6) ||
		(flags & ~1) != 0 || request->tv_sec < 0 || request->tv_nsec < 0 ||
		request->tv_nsec >= 1'000'000'000) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	if ((flags & 1) == 0) {
		return darling_windows_nanosleep(request, remaining) == 0 ? 0 : 22;
	}
	darling_timespec now{};
	if (darling_windows_clock_gettime(clock_id, &now) != 0) {
		return 22;
	}
	const auto target = static_cast<std::uint64_t>(request->tv_sec) * 1'000'000'000ull +
		static_cast<std::uint64_t>(request->tv_nsec);
	const auto current = static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
		static_cast<std::uint64_t>(now.tv_nsec);
	if (target <= current) {
		if (remaining != nullptr) {
			remaining->tv_sec = 0;
			remaining->tv_nsec = 0;
		}
		return 0;
	}
	const auto delta = target - current;
	darling_timespec relative{
		static_cast<std::int64_t>(delta / 1'000'000'000ull),
		static_cast<std::int64_t>(delta % 1'000'000'000ull)};
	return darling_windows_nanosleep(&relative, remaining) == 0 ? 0 : 22;
}

extern "C" std::uint64_t darling_windows_mach_absolute_time()
{
	LARGE_INTEGER counter{};
	if (!QueryPerformanceCounter(&counter)) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return 0;
	}
	return static_cast<std::uint64_t>(counter.QuadPart);
}

extern "C" int darling_windows_mach_timebase_info(darling_mach_timebase_info* result)
{
	if (result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	LARGE_INTEGER frequency{};
	if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return 5;
	}
	std::uint64_t numerator = 1'000'000'000ull;
	std::uint64_t denominator = static_cast<std::uint64_t>(frequency.QuadPart);
	while (denominator > (std::numeric_limits<std::uint32_t>::max)()) {
		const auto scale = (denominator + (std::numeric_limits<std::uint32_t>::max)() - 1) /
			(std::numeric_limits<std::uint32_t>::max)();
		numerator = (numerator / scale) == 0 ? 1 : numerator / scale;
		denominator /= scale;
	}
	result->numer = static_cast<std::uint32_t>(numerator);
	result->denom = static_cast<std::uint32_t>(denominator);
	return 0;
}

extern "C" int darling_windows_nanosleep(const darling_timespec* request,
	darling_timespec* remaining)
{
	if (request == nullptr || request->tv_sec < 0 || request->tv_nsec < 0 ||
		request->tv_nsec >= 1'000'000'000) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (remaining != nullptr) {
		remaining->tv_sec = 0;
		remaining->tv_nsec = 0;
	}
	const auto milliseconds = static_cast<std::uint64_t>(request->tv_sec) * 1000ull +
		(static_cast<std::uint64_t>(request->tv_nsec) + 999'999ull) / 1'000'000ull;
	if (milliseconds > static_cast<std::uint64_t>(INFINITE - 1)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	Sleep(static_cast<DWORD>(milliseconds));
	return 0;
}

extern "C" int darling_windows_gettimeofday(darling_timeval* result, void*)
{
	if (result == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto nanoseconds = darling::windows_host::DarwinClock::WallClockNanoseconds();
	result->tv_sec = static_cast<std::int64_t>(nanoseconds / 1'000'000'000ull);
	result->tv_usec = static_cast<std::int64_t>((nanoseconds / 1'000ull) % 1'000'000ull);
	return 0;
}

extern "C" int darling_windows_usleep(unsigned int microseconds)
{
	darling_timespec request{};
	request.tv_sec = static_cast<std::int64_t>(microseconds / 1'000'000u);
	request.tv_nsec = static_cast<std::int64_t>(microseconds % 1'000'000u) * 1'000;
	return darling_windows_nanosleep(&request, nullptr);
}

extern "C" const char* darling_windows_getenv(const char* name)
{
	static thread_local std::string value;
	if (name == nullptr || *name == '\0') {
		return nullptr;
	}
	const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
	if (required == 0) {
		return nullptr;
	}
	value.resize(required);
	const DWORD written = GetEnvironmentVariableA(name, value.data(), required);
	if (written == 0 || written >= required) {
		return nullptr;
	}
	value.resize(written);
	return value.c_str();
}

extern "C" char*** darling_windows_NSGetEnviron()
{
	return &darling_windows_environ;
}

extern "C" int darling_windows_setenv(const char* name, const char* value, int overwrite)
{
	if (name == nullptr || value == nullptr || *name == '\0' || std::strchr(name, '=') != nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (!overwrite && GetEnvironmentVariableA(name, nullptr, 0) != 0) {
		return 0;
	}
	if (!SetEnvironmentVariableA(name, value)) {
		return -1;
	}
	RefreshDarwinEnvironment();
	return 0;
}

extern "C" int darling_windows_unsetenv(const char* name)
{
	if (name == nullptr || *name == '\0' || std::strchr(name, '=') != nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (SetEnvironmentVariableA(name, nullptr)) {
		RefreshDarwinEnvironment();
		return 0;
	}
	if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
		RefreshDarwinEnvironment();
		return 0;
	}
	return -1;
}

extern "C" int darling_windows_putenv(char* assignment)
{
	if (assignment == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const char* separator = std::strchr(assignment, '=');
	if (separator == nullptr || separator == assignment) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const std::string name(assignment,
		static_cast<std::size_t>(separator - assignment));
	if (!SetEnvironmentVariableA(name.c_str(), separator + 1)) {
		return -1;
	}
	RefreshDarwinEnvironment();
	return 0;
}

extern "C" int darling_windows_clearenv()
{
	LPWCH block = GetEnvironmentStringsW();
	if (block == nullptr) {
		return -1;
	}
	bool success = true;
	for (const wchar_t* entry = block; *entry != L'\0';
		entry += std::wcslen(entry) + 1) {
		// Windows keeps drive-current-directory entries such as =C:, which are
		// not ordinary environment variables and cannot be removed this way.
		if (entry[0] == L'=')
			continue;
		const wchar_t* separator = std::wcschr(entry, L'=');
		if (separator == nullptr || separator == entry)
			continue;
		const std::wstring name(entry, separator);
		if (!SetEnvironmentVariableW(name.c_str(), nullptr) &&
			GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			success = false;
		}
	}
	FreeEnvironmentStringsW(block);
	if (success) {
		RefreshDarwinEnvironment();
	}
	return success ? 0 : -1;
}

extern "C" int darling_windows_ioctl(int descriptor, unsigned long request,
	void* argument)
{
	// Darwin's _IOR/_IOW encodings for TIOCGWINSZ/TIOCSWINSZ.
	constexpr unsigned long tiocgwinsz = 0x40087468ul;
	constexpr unsigned long tiocswinsz = 0x80087467ul;
	constexpr unsigned long fionread = 0x4004667ful;
	if (argument == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	if (request == tiocgwinsz) {
		try {
			const HANDLE handle = global_syscalls.MappingHandle(descriptor);
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!GetConsoleScreenBufferInfo(handle, &info)) {
				darling::windows_host::DarwinErrno::Set(25);
				return -1;
			}
			auto* size = static_cast<darling_winsize*>(argument);
			const auto columns = static_cast<std::uint32_t>(info.srWindow.Right) -
				static_cast<std::uint32_t>(info.srWindow.Left) + 1u;
			const auto rows = static_cast<std::uint32_t>(info.srWindow.Bottom) -
				static_cast<std::uint32_t>(info.srWindow.Top) + 1u;
			size->ws_col = static_cast<std::uint16_t>((std::min)(columns, 65535u));
			size->ws_row = static_cast<std::uint16_t>((std::min)(rows, 65535u));
			size->ws_xpixel = 0;
			size->ws_ypixel = 0;
			return 0;
		} catch (...) {
			return -1;
		}
	}
	if (request == fionread) {
		try {
			const HANDLE handle = global_syscalls.MappingHandle(descriptor);
			unsigned long available = 0;
			if (GetFileType(handle) == FILE_TYPE_PIPE) {
				if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
					darling::windows_host::DarwinErrno::Set(25);
					return -1;
				}
			} else {
				DWORD mode = 0;
				DWORD events = 0;
				if (!GetConsoleMode(handle, &mode) ||
					!GetNumberOfConsoleInputEvents(handle, &events)) {
					darling::windows_host::DarwinErrno::Set(25);
					return -1;
				}
				available = events;
			}
			*static_cast<unsigned long*>(argument) = available;
			return 0;
		} catch (...) {
			return -1;
		}
	}
	if (request == tiocswinsz) {
		darling::windows_host::DarwinErrno::Set(95);
		return -1;
	}
	darling::windows_host::DarwinErrno::Set(25);
	return -1;
}

extern "C" int darling_windows_isatty(int descriptor)
{
	if (descriptor < 0 || descriptor > 2) {
		return 0;
	}
	const HANDLE handle = StandardHandle(descriptor);
	DWORD mode = 0;
	return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
		GetConsoleMode(handle, &mode) ? 1 : 0;
}

extern "C" char* darling_windows_ctermid(char* buffer)
{
	static constexpr char terminal_name[] = "CONIN$";
	static thread_local char fallback[16]{};
	if (buffer == nullptr) {
		std::memcpy(fallback, terminal_name, sizeof(terminal_name));
		return fallback;
	}
	std::memcpy(buffer, terminal_name, sizeof(terminal_name));
	return buffer;
}

extern "C" int darling_windows_ttyname_r(int descriptor, char* buffer, std::size_t size)
{
	static constexpr char input_name[] = "CONIN$";
	static constexpr char output_name[] = "CONOUT$";
	if (buffer == nullptr || size == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	if (!darling_windows_isatty(descriptor)) {
		darling::windows_host::DarwinErrno::Set(25);
		return 25;
	}
	const char* name = descriptor == 0 ? input_name : output_name;
	const std::size_t required = std::strlen(name) + 1;
	if (size < required) {
		darling::windows_host::DarwinErrno::Set(34);
		return 34;
	}
	std::memcpy(buffer, name, required);
	return 0;
}

extern "C" char* darling_windows_ttyname(int descriptor)
{
	static thread_local char name[16]{};
	return darling_windows_ttyname_r(descriptor, name, sizeof(name)) == 0 ? name : nullptr;
}

extern "C" void* darling_windows_malloc(std::size_t bytes)
{
	if (bytes == 0) {
		bytes = 1;
	}
	void* address = HeapAlloc(GetProcessHeap(), 0, bytes);
	if (address == nullptr) {
		darling::windows_host::DarwinErrno::Set(12);
	}
	return address;
}

extern "C" void* darling_windows_calloc(std::size_t count, std::size_t bytes)
{
	if (count != 0 && bytes > (std::numeric_limits<std::size_t>::max)() / count) {
		darling::windows_host::DarwinErrno::Set(12);
		return nullptr;
	}
	const auto total = count * bytes;
	void* address = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total == 0 ? 1 : total);
	if (address == nullptr) darling::windows_host::DarwinErrno::Set(12);
	return address;
}

extern "C" void* darling_windows_realloc(void* address, std::size_t bytes)
{
	if (address == nullptr) return darling_windows_malloc(bytes);
	if (bytes == 0) {
		HeapFree(GetProcessHeap(), 0, address);
		return nullptr;
	}
	void* resized = HeapReAlloc(GetProcessHeap(), 0, address, bytes);
	if (resized == nullptr) darling::windows_host::DarwinErrno::Set(12);
	return resized;
}

extern "C" void darling_windows_free(void* address)
{
	if (address != nullptr) {
		HeapFree(GetProcessHeap(), 0, address);
	}
}

extern "C" void* darling_windows_memcpy(void* destination, const void* source,
	std::size_t bytes)
{
	return std::memcpy(destination, source, bytes);
}

extern "C" void* darling_windows_memset(void* destination, int value,
	std::size_t bytes)
{
	return std::memset(destination, value, bytes);
}

extern "C" void* darling_windows_memmove(void* destination, const void* source,
	std::size_t bytes)
{
	return std::memmove(destination, source, bytes);
}

extern "C" int darling_windows_memcmp(const void* left, const void* right,
	std::size_t bytes)
{
	return std::memcmp(left, right, bytes);
}

extern "C" void darling_windows_bzero(void* destination, std::size_t bytes)
{
	std::memset(destination, 0, bytes);
}

extern "C" void darling_windows_explicit_bzero(void* destination, std::size_t bytes)
{
	if (destination != nullptr && bytes != 0) {
		SecureZeroMemory(destination, bytes);
	}
}

extern "C" void darling_windows_bcopy(const void* source, void* destination,
	std::size_t bytes)
{
	std::memmove(destination, source, bytes);
}

extern "C" void* darling_windows_memccpy(void* destination, const void* source,
	int byte, std::size_t bytes)
{
	const auto* source_bytes = static_cast<const unsigned char*>(source);
	char* destination_bytes = static_cast<char*>(destination);
	for (std::size_t index = 0; index < bytes; ++index) {
		destination_bytes[index] = static_cast<char>(source_bytes[index]);
		if (source_bytes[index] == static_cast<unsigned char>(byte)) {
			return destination_bytes + index + 1;
		}
	}
	return nullptr;
}

extern "C" char* darling_windows_strdup(const char* value)
{
	if (value == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return nullptr;
	}
	const auto length = std::strlen(value);
	char* result = static_cast<char*>(darling_windows_malloc(length + 1));
	if (result != nullptr) {
		std::memcpy(result, value, length + 1);
	}
	return result;
}

extern "C" char* darling_windows_strndup(const char* value, std::size_t bytes)
{
	if (value == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return nullptr;
	}
	const auto length = (std::min)(std::strlen(value), bytes);
	char* result = static_cast<char*>(darling_windows_malloc(length + 1));
	if (result != nullptr) {
		std::memcpy(result, value, length);
		result[length] = '\0';
	}
	return result;
}

extern "C" long darling_windows_strtol(const char* value, char** end, int base)
{
	return std::strtol(value, end, base);
}

extern "C" long long darling_windows_strtoll(const char* value, char** end, int base)
{
	return std::strtoll(value, end, base);
}

extern "C" unsigned long darling_windows_strtoul(const char* value, char** end, int base)
{
	return std::strtoul(value, end, base);
}

extern "C" double darling_windows_strtod(const char* value, char** end)
{
	return std::strtod(value, end);
}

extern "C" float darling_windows_strtof(const char* value, char** end)
{
	return std::strtof(value, end);
}

extern "C" long double darling_windows_strtold(const char* value, char** end)
{
	return std::strtold(value, end);
}

extern "C" int darling_windows_vsnprintf(char* buffer, std::size_t size,
	const char* format, std::va_list arguments)
{
	return std::vsnprintf(buffer, size, format, arguments);
}

extern "C" int darling_windows_snprintf(char* buffer, std::size_t size,
	const char* format, ...)
{
	std::va_list arguments;
	va_start(arguments, format);
	const int result = darling_windows_vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return result;
}

extern "C" int darling_windows_vasprintf(char** result, const char* format,
	std::va_list arguments)
{
	if (result == nullptr || format == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	*result = nullptr;
	std::va_list sizing_arguments;
	va_copy(sizing_arguments, arguments);
	const int length = std::vsnprintf(nullptr, 0, format, sizing_arguments);
	va_end(sizing_arguments);
	if (length < 0) return -1;
	char* buffer = static_cast<char*>(darling_windows_malloc(static_cast<std::size_t>(length) + 1));
	if (buffer == nullptr) return -1;
	std::va_list writing_arguments;
	va_copy(writing_arguments, arguments);
	const int written = std::vsnprintf(buffer, static_cast<std::size_t>(length) + 1,
		format, writing_arguments);
	va_end(writing_arguments);
	if (written < 0) {
		darling_windows_free(buffer);
		return -1;
	}
	*result = buffer;
	return written;
}

extern "C" int darling_windows_asprintf(char** result, const char* format, ...)
{
	std::va_list arguments;
	va_start(arguments, format);
	const int written = darling_windows_vasprintf(result, format, arguments);
	va_end(arguments);
	return written;
}

extern "C" std::size_t darling_windows_strlen(const char* value)
{
	return value == nullptr ? 0 : std::strlen(value);
}

extern "C" std::size_t darling_windows_strnlen(const char* value, std::size_t limit)
{
	if (value == nullptr) return 0;
	std::size_t length = 0;
	while (length < limit && value[length] != '\0') ++length;
	return length;
}

extern "C" const void* darling_windows_memchr(const void* value, int byte, std::size_t bytes)
{
	return std::memchr(value, byte, bytes);
}

extern "C" const void* darling_windows_memmem(const void* haystack,
	std::size_t haystack_bytes, const void* needle, std::size_t needle_bytes)
{
	if (needle_bytes == 0) return haystack;
	if (haystack == nullptr || needle == nullptr || needle_bytes > haystack_bytes) return nullptr;
	const auto* source = static_cast<const unsigned char*>(haystack);
	const auto* target = static_cast<const unsigned char*>(needle);
	for (std::size_t offset = 0; offset + needle_bytes <= haystack_bytes; ++offset) {
		if (std::memcmp(source + offset, target, needle_bytes) == 0) return source + offset;
	}
	return nullptr;
}

extern "C" int darling_windows_strcmp(const char* left, const char* right)
{
	if (left == nullptr || right == nullptr) {
		return left == right ? 0 : (left == nullptr ? -1 : 1);
	}
	return std::strcmp(left, right);
}

extern "C" int darling_windows_strcasecmp(const char* left, const char* right)
{
	while (*left != '\0' && *right != '\0') {
		const int left_value = std::tolower(static_cast<unsigned char>(*left));
		const int right_value = std::tolower(static_cast<unsigned char>(*right));
		if (left_value != right_value) return left_value - right_value;
		++left;
		++right;
	}
	return std::tolower(static_cast<unsigned char>(*left)) -
		std::tolower(static_cast<unsigned char>(*right));
}

extern "C" int darling_windows_strncasecmp(const char* left, const char* right,
	std::size_t bytes)
{
	for (std::size_t index = 0; index < bytes; ++index) {
		const int left_value = std::tolower(static_cast<unsigned char>(left[index]));
		const int right_value = std::tolower(static_cast<unsigned char>(right[index]));
		if (left_value != right_value || left[index] == '\0' || right[index] == '\0') {
			return left_value - right_value;
		}
	}
	return 0;
}

extern "C" char* darling_windows_strcpy(char* destination, const char* source)
{
	return std::strcpy(destination, source);
}

extern "C" char* darling_windows_strncpy(char* destination, const char* source,
	std::size_t bytes)
{
	return std::strncpy(destination, source, bytes);
}

extern "C" char* darling_windows_strcat(char* destination, const char* source)
{
	return std::strcat(destination, source);
}

extern "C" char* darling_windows_strncat(char* destination, const char* source,
	std::size_t bytes)
{
	return std::strncat(destination, source, bytes);
}

extern "C" std::size_t darling_windows_strlcpy(char* destination, const char* source,
	std::size_t size)
{
	const auto source_length = std::strlen(source);
	if (size != 0) {
		const auto copy_length = (std::min)(source_length, size - 1);
		std::memcpy(destination, source, copy_length);
		destination[copy_length] = '\0';
	}
	return source_length;
}

extern "C" std::size_t darling_windows_strlcat(char* destination, const char* source,
	std::size_t size)
{
	const auto destination_length = std::strlen(destination);
	const auto source_length = std::strlen(source);
	if (destination_length >= size) {
		return size + source_length;
	}
	const auto available = size - destination_length - 1;
	const auto copy_length = (std::min)(source_length, available);
	std::memcpy(destination + destination_length, source, copy_length);
	destination[destination_length + copy_length] = '\0';
	return destination_length + source_length;
}

extern "C" const char* darling_windows_strchr(const char* value, int character)
{
	return std::strchr(value, character);
}

extern "C" const char* darling_windows_strrchr(const char* value, int character)
{
	return std::strrchr(value, character);
}

extern "C" const char* darling_windows_strstr(const char* value, const char* search)
{
	return std::strstr(value, search);
}

extern "C" const char* darling_windows_strcasestr(const char* value, const char* search)
{
	if (*search == '\0') return value;
	for (; *value != '\0'; ++value) {
		const char* left = value;
		const char* right = search;
		while (*left != '\0' && *right != '\0' &&
			std::tolower(static_cast<unsigned char>(*left)) ==
			std::tolower(static_cast<unsigned char>(*right))) {
			++left;
			++right;
		}
		if (*right == '\0') return value;
	}
	return nullptr;
}

extern "C" std::size_t darling_windows_strspn(const char* value, const char* accepted)
{
	return std::strspn(value, accepted);
}

extern "C" std::size_t darling_windows_strcspn(const char* value, const char* rejected)
{
	return std::strcspn(value, rejected);
}

extern "C" const char* darling_windows_strpbrk(const char* value, const char* accepted)
{
	return std::strpbrk(value, accepted);
}

extern "C" char* darling_windows_strtok_r(char* value, const char* delimiters, char** state)
{
	if (delimiters == nullptr || state == nullptr) {
		return nullptr;
	}
	char* cursor = value != nullptr ? value : *state;
	if (cursor == nullptr) {
		return nullptr;
	}
	cursor += std::strspn(cursor, delimiters);
	if (*cursor == '\0') {
		*state = cursor;
		return nullptr;
	}
	char* end = cursor + std::strcspn(cursor, delimiters);
	if (*end != '\0') {
		*end = '\0';
		*state = end + 1;
	} else {
		*state = end;
	}
	return cursor;
}

extern "C" char* darling_windows_strtok(char* value, const char* delimiters)
{
	static thread_local char* state = nullptr;
	return darling_windows_strtok_r(value, delimiters, &state);
}

extern "C" const char* darling_windows_strerror(int error_number)
{
	static thread_local std::string message;
	const char* known = nullptr;
	switch (error_number) {
	case 2: known = "No such file or directory"; break;
	case 4: known = "Interrupted system call"; break;
	case 9: known = "Bad file descriptor"; break;
	case 12: known = "Cannot allocate memory"; break;
	case 22: known = "Invalid argument"; break;
	case 34: known = "Result too large"; break;
	case 35: known = "Resource temporarily unavailable"; break;
	case 40: known = "Message too long"; break;
	case 61: known = "Connection refused"; break;
	default: break;
	}
	if (known != nullptr) {
		message = known;
	} else {
		message = "Darwin error " + std::to_string(error_number);
	}
	return message.c_str();
}

extern "C" int darling_windows_strerror_r(int error_number, char* buffer,
	std::size_t size)
{
	if (buffer == nullptr || size == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return 22;
	}
	const auto* message = darling_windows_strerror(error_number);
	const auto length = std::strlen(message);
	const auto copied = std::min(length, size - 1);
	std::memcpy(buffer, message, copied);
	buffer[copied] = '\0';
	if (copied != length) {
		darling::windows_host::DarwinErrno::Set(34);
		return 34;
	}
	return 0;
}

extern "C" int darling_windows_write_stdout(const void* data, std::size_t bytes)
{
	const auto written = darling_windows_write(1, data, bytes);
	return written == static_cast<int>(bytes) ? written : -1;
}

extern "C" int darling_windows_host_entry(int argc, char**, char**)
{
	const char* message = argc == 3 ? "DARWIN_HOST_STDOUT=PASS argc=3\n" :
		"DARWIN_HOST_STDOUT=PASS argc=other\n";
	const std::size_t length = std::strlen(message);
	if (darling_windows_write_stdout(message, length) != static_cast<int>(length)) {
		return -1;
	}
	return argc;
}
