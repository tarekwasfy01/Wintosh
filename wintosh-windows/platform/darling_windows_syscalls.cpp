/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_syscalls.h"
#include "darling_windows_errno.h"

#include <limits>
#include <cstdint>
#include <climits>
#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	DarwinErrno::SetFromWin32(GetLastError());
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

[[nodiscard]] DWORD CheckedLength(std::size_t bytes)
{
	if (bytes > std::numeric_limits<DWORD>::max()) {
		throw std::overflow_error("Darwin syscall I/O size exceeds DWORD range");
	}
	return static_cast<DWORD>(bytes);
}

[[nodiscard]] int DarwinSocketFlags(int flags)
{
	constexpr int darwin_msg_oob = 0x0001;
	constexpr int darwin_msg_peek = 0x0002;
	constexpr int darwin_msg_dontroute = 0x0004;
	constexpr int darwin_msg_waitall = 0x0040;
	constexpr int darwin_msg_dontwait = 0x0080;
	constexpr int darwin_msg_nosignal = 0x0200;
	const int supported = darwin_msg_oob | darwin_msg_peek | darwin_msg_dontroute |
		darwin_msg_waitall | darwin_msg_dontwait | darwin_msg_nosignal;
	if ((flags & ~supported) != 0) {
		DarwinErrno::Set(95);
		throw std::invalid_argument("unsupported Darwin socket message flags");
	}
	int native = flags & (darwin_msg_oob | darwin_msg_peek | darwin_msg_dontroute);
	if ((flags & darwin_msg_waitall) != 0)
		native |= MSG_WAITALL;
	return native;
}

[[nodiscard]] int DarwinSocketResultFlags(int flags)
{
	constexpr int darwin_msg_oob = 0x0001;
	constexpr int darwin_msg_peek = 0x0002;
	constexpr int darwin_msg_trunc = 0x0010;
	int darwin = 0;
	if ((flags & MSG_OOB) != 0) darwin |= darwin_msg_oob;
	if ((flags & MSG_PEEK) != 0) darwin |= darwin_msg_peek;
#ifdef MSG_PARTIAL
	if ((flags & MSG_PARTIAL) != 0) darwin |= darwin_msg_trunc;
#endif
	return darwin;
}

class TemporarySocketNonblocking final {
public:
	TemporarySocketNonblocking(SOCKET socket, bool requested, bool already_nonblocking)
		: m_socket(socket), m_changed(requested && !already_nonblocking)
	{
		if (!m_changed)
			return;
		u_long mode = 1;
		if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
			const int error = WSAGetLastError();
			DarwinErrno::Set(error);
			throw std::system_error(error, std::system_category(), "ioctlsocket(MSG_DONTWAIT)");
		}
	}

	~TemporarySocketNonblocking() noexcept
	{
		if (m_changed) {
			u_long mode = 0;
			(void)ioctlsocket(m_socket, FIONBIO, &mode);
		}
	}

	TemporarySocketNonblocking(const TemporarySocketNonblocking&) = delete;
	TemporarySocketNonblocking& operator=(const TemporarySocketNonblocking&) = delete;

private:
	SOCKET m_socket;
	bool m_changed;
};

} // namespace

DarwinSyscalls::DarwinSyscalls() : m_handles(3, nullptr)
{
}

DarwinSyscalls::~DarwinSyscalls() noexcept
{
	std::scoped_lock lock(m_mutex);
	for (int descriptor = 3; descriptor < static_cast<int>(m_handles.size()); ++descriptor) {
		CloseUnlocked(descriptor);
	}
}

int DarwinSyscalls::OpenRead(const std::filesystem::path& path)
{
	return Open(path, 0);
}

int DarwinSyscalls::OpenWrite(const std::filesystem::path& path)
{
	return Open(path, 1 | 0x0200 | 0x0400);
}

int DarwinSyscalls::OpenReadWrite(const std::filesystem::path& path)
{
	return Open(path, 2 | 0x0200);
}

int DarwinSyscalls::Open(const std::filesystem::path& path, int flags)
{
	constexpr int write_only = 0x0001;
	constexpr int read_write = 0x0002;
	constexpr int append = 0x0008;
	constexpr int create = 0x0200;
	constexpr int exclusive = 0x0800;
	constexpr int truncate = 0x0400;
	constexpr int no_follow = 0x0100;
	constexpr int directory_only = 0x00100000;
	constexpr int non_blocking = 0x0004;
	constexpr int close_on_exec = 0x01000000;
	const int access_mode = flags & 0x0003;
	const DWORD access = access_mode == read_write ? GENERIC_READ | GENERIC_WRITE :
		access_mode == write_only ? GENERIC_WRITE : GENERIC_READ;
	DWORD creation = OPEN_EXISTING;
	if ((flags & create) != 0) {
		creation = (flags & exclusive) != 0 ? CREATE_NEW :
			(flags & truncate) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
	} else if ((flags & truncate) != 0) {
		creation = TRUNCATE_EXISTING;
	}
	const HANDLE handle = CreateFileW(path.c_str(), access,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		creation, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS |
			((flags & no_follow) != 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0), nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		ThrowLastError("CreateFileW(open)");
	}
	BY_HANDLE_FILE_INFORMATION information{};
	if (!GetFileInformationByHandle(handle, &information)) {
		ThrowLastError("GetFileInformationByHandle(open)");
	}
	if ((flags & directory_only) != 0 &&
		(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		CloseHandle(handle);
		DarwinErrno::Set(20);
		throw std::invalid_argument("Darwin O_DIRECTORY requires a directory");
	}
	const bool handle_is_pipe = GetFileType(handle) == FILE_TYPE_PIPE;
	if ((flags & append) != 0) {
		LARGE_INTEGER zero{};
		if (!SetFilePointerEx(handle, zero, nullptr, FILE_END)) {
			CloseHandle(handle);
			ThrowLastError("SetFilePointerEx(append)");
		}
	}
	std::scoped_lock lock(m_mutex);
	for (int descriptor = 3; descriptor < static_cast<int>(m_handles.size()); ++descriptor) {
		if (m_handles[descriptor] == nullptr) {
			m_handles[descriptor] = handle;
			m_paths[descriptor] = path;
			if ((flags & append) != 0) {
				m_append_descriptors.insert(descriptor);
			}
			if ((flags & non_blocking) != 0 && !handle_is_pipe) {
				m_nonblocking_descriptors.insert(descriptor);
			}
			if ((flags & close_on_exec) != 0) {
				m_cloexec_descriptors.insert(descriptor);
			}
			return descriptor;
		}
	}
	m_handles.push_back(handle);
	m_paths.emplace(static_cast<int>(m_handles.size() - 1), path);
	if ((flags & append) != 0) {
		m_append_descriptors.insert(static_cast<int>(m_handles.size() - 1));
	}
	if ((flags & non_blocking) != 0 && !handle_is_pipe) {
		m_nonblocking_descriptors.insert(static_cast<int>(m_handles.size() - 1));
	}
	if ((flags & close_on_exec) != 0) {
		m_cloexec_descriptors.insert(static_cast<int>(m_handles.size() - 1));
	}
	return static_cast<int>(m_handles.size() - 1);
}

HANDLE DarwinSyscalls::GetHandle(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	if (m_sockets.contains(descriptor)) {
		throw std::invalid_argument("Darwin descriptor is a socket");
	}
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr) {
		throw std::invalid_argument("invalid Darwin file descriptor");
	}
	return m_handles[descriptor];
}

SOCKET DarwinSyscalls::GetSocket(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	const auto socket = m_sockets.find(descriptor);
	if (socket == m_sockets.end())
		throw std::invalid_argument("Darwin descriptor is not a socket");
	return socket->second;
}

std::size_t DarwinSyscalls::Read(int descriptor, void* buffer, std::size_t bytes)
{
	if (bytes == 0)
		return 0;
	{
		std::scoped_lock lock(m_mutex);
		const auto socket = m_sockets.find(descriptor);
		if (socket != m_sockets.end()) {
			const int result = recv(socket->second, static_cast<char*>(buffer),
				static_cast<int>((std::min)(bytes, static_cast<std::size_t>(INT_MAX))), 0);
			if (result == SOCKET_ERROR) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "recv");
			}
			return static_cast<std::size_t>(result);
		}
	}
	const HANDLE handle = GetHandle(descriptor);
	bool nonblocking = false;
	{
		std::scoped_lock lock(m_mutex);
		nonblocking = m_nonblocking_descriptors.contains(descriptor);
	}
	if (nonblocking && GetFileType(handle) == FILE_TYPE_PIPE) {
		DWORD available = 0;
		if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) == FALSE) {
			if (GetLastError() != ERROR_BROKEN_PIPE)
				ThrowLastError("PeekNamedPipe");
		} else if (available == 0) {
			DarwinErrno::Set(35); // EAGAIN / EWOULDBLOCK
			throw std::invalid_argument("nonblocking Darwin pipe has no data");
		} else {
			bytes = (std::min)(bytes, static_cast<std::size_t>(available));
		}
	}
	DWORD transferred = 0;
	if (!ReadFile(handle, buffer, CheckedLength(bytes), &transferred, nullptr)) {
		ThrowLastError("ReadFile");
	}
	return transferred;
}

std::size_t DarwinSyscalls::Write(int descriptor, const void* buffer, std::size_t bytes)
{
	if (bytes == 0)
		return 0;
	{
		std::scoped_lock lock(m_mutex);
		const auto socket = m_sockets.find(descriptor);
		if (socket != m_sockets.end()) {
			const int result = send(socket->second, static_cast<const char*>(buffer),
				static_cast<int>((std::min)(bytes, static_cast<std::size_t>(INT_MAX))), 0);
			if (result == SOCKET_ERROR) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "send");
			}
			return static_cast<std::size_t>(result);
		}
	}
	bool append = false;
	{
		std::scoped_lock lock(m_mutex);
		append = m_append_descriptors.contains(descriptor);
	}
	const HANDLE handle = GetHandle(descriptor);
	if (append) {
		LARGE_INTEGER zero{};
		if (!SetFilePointerEx(handle, zero, nullptr, FILE_END)) {
			ThrowLastError("SetFilePointerEx(append-write)");
		}
	}
	DWORD transferred = 0;
	if (!WriteFile(handle, buffer, CheckedLength(bytes), &transferred, nullptr)) {
		ThrowLastError("WriteFile");
	}
	return transferred;
}

HANDLE DarwinSyscalls::MappingHandle(int descriptor) const
{
	return GetHandle(descriptor);
}

std::size_t DarwinSyscalls::ReadAt(int descriptor, void* buffer, std::size_t bytes,
	std::int64_t offset)
{
	if (offset < 0)
		throw std::invalid_argument("negative Darwin pread offset");
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	const HANDLE handle = m_handles[descriptor];
	LARGE_INTEGER origin{};
	LARGE_INTEGER position{};
	position.QuadPart = offset;
	if (!SetFilePointerEx(handle, {}, &origin, FILE_CURRENT) ||
		!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
		ThrowLastError("SetFilePointerEx(pread)");
	}
	DWORD transferred = 0;
	const BOOL read = ReadFile(handle, buffer, CheckedLength(bytes), &transferred, nullptr);
	const DWORD read_error = read ? ERROR_SUCCESS : GetLastError();
	const BOOL restored = SetFilePointerEx(handle, origin, nullptr, FILE_BEGIN);
	if (!read) {
		if (!restored) SetLastError(read_error);
		ThrowLastError("ReadFile(pread)");
	}
	if (!restored)
		ThrowLastError("SetFilePointerEx(pread restore)");
	return transferred;
}

std::size_t DarwinSyscalls::WriteAt(int descriptor, const void* buffer, std::size_t bytes,
	std::int64_t offset)
{
	if (offset < 0)
		throw std::invalid_argument("negative Darwin pwrite offset");
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	const HANDLE handle = m_handles[descriptor];
	LARGE_INTEGER origin{};
	LARGE_INTEGER position{};
	position.QuadPart = offset;
	if (!SetFilePointerEx(handle, {}, &origin, FILE_CURRENT) ||
		!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
		ThrowLastError("SetFilePointerEx(pwrite)");
	}
	DWORD transferred = 0;
	const BOOL written = WriteFile(handle, buffer, CheckedLength(bytes),
		&transferred, nullptr);
	const DWORD write_error = written ? ERROR_SUCCESS : GetLastError();
	const BOOL restored = SetFilePointerEx(handle, origin, nullptr, FILE_BEGIN);
	if (!written) {
		if (!restored) SetLastError(write_error);
		ThrowLastError("WriteFile(pwrite)");
	}
	if (!restored)
		ThrowLastError("SetFilePointerEx(pwrite restore)");
	return transferred;
}

void DarwinSyscalls::Lock(int descriptor, int operation)
{
	constexpr int lock_shared = 1;
	constexpr int lock_exclusive = 2;
	constexpr int lock_nonblocking = 4;
	constexpr int lock_unlock = 8;
	if ((operation & ~(lock_shared | lock_exclusive | lock_nonblocking | lock_unlock)) != 0 ||
		((operation & lock_shared) != 0 && (operation & lock_exclusive) != 0) ||
		((operation & lock_unlock) != 0 &&
			(operation & (lock_shared | lock_exclusive)) != 0)) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("invalid Darwin flock operation");
	}
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	const HANDLE handle = m_handles[descriptor];
	OVERLAPPED overlapped{};
	if ((operation & lock_unlock) != 0) {
		if (!UnlockFileEx(handle, 0, 0xffffffffu, 0x7fffffffu, &overlapped))
			ThrowLastError("UnlockFileEx(flock)");
		return;
	}
	if ((operation & (lock_shared | lock_exclusive)) == 0) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("flock requires a lock mode");
	}
	DWORD flags = (operation & lock_exclusive) != 0 ? LOCKFILE_EXCLUSIVE_LOCK : 0;
	if ((operation & lock_nonblocking) != 0)
		flags |= LOCKFILE_FAIL_IMMEDIATELY;
	if (!LockFileEx(handle, flags, 0, 0xffffffffu, 0x7fffffffu, &overlapped))
		ThrowLastError("LockFileEx(flock)");
}

void DarwinSyscalls::GetFileInformation(
	int descriptor, BY_HANDLE_FILE_INFORMATION* information) const
{
	if (information == nullptr) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("null Darwin file information output");
	}
	if (!GetFileInformationByHandle(GetHandle(descriptor), information)) {
		ThrowLastError("GetFileInformationByHandle");
	}
}

void DarwinSyscalls::Flush(int descriptor)
{
	if (!FlushFileBuffers(GetHandle(descriptor))) {
		ThrowLastError("FlushFileBuffers");
	}
}

std::filesystem::path DarwinSyscalls::PathForDescriptor(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	const auto path = m_paths.find(descriptor);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr || path == m_paths.end() || path->second.empty()) {
		DarwinErrno::Set(9);
		throw std::invalid_argument("Darwin descriptor has no filesystem path");
	}
	return path->second;
}

int DarwinSyscalls::GetDescriptorFlags(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	int flags = 0;
	if (m_append_descriptors.contains(descriptor)) {
		flags |= 0x0008; // O_APPEND
	}
	if (m_nonblocking_descriptors.contains(descriptor)) {
		flags |= 0x0004; // O_NONBLOCK
	}
	return flags;
}

void DarwinSyscalls::SetDescriptorFlags(int descriptor, int flags)
{
	constexpr int nonblocking = 0x0004;
	constexpr int append = 0x0008;
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	const auto socket = m_sockets.find(descriptor);
	if (socket != m_sockets.end()) {
		u_long mode = (flags & nonblocking) != 0 ? 1UL : 0UL;
		if (ioctlsocket(socket->second, FIONBIO, &mode) == SOCKET_ERROR) {
			const int error = WSAGetLastError();
			DarwinErrno::Set(error);
			throw std::system_error(error, std::system_category(), "ioctlsocket(FIONBIO)");
		}
	}
	if ((flags & append) != 0) {
		m_append_descriptors.insert(descriptor);
	} else {
		m_append_descriptors.erase(descriptor);
	}
	if ((flags & nonblocking) != 0) {
		m_nonblocking_descriptors.insert(descriptor);
	} else {
		m_nonblocking_descriptors.erase(descriptor);
	}
}

int DarwinSyscalls::GetDescriptorFdFlags(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	return m_cloexec_descriptors.contains(descriptor) ? 1 : 0;
}

void DarwinSyscalls::SetDescriptorFdFlags(int descriptor, int flags)
{
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr)
		throw std::invalid_argument("invalid Darwin file descriptor");
	if (!m_sockets.contains(descriptor)) {
		const DWORD mask = HANDLE_FLAG_INHERIT;
		const DWORD value = (flags & 1) == 0 ? HANDLE_FLAG_INHERIT : 0;
		if (!SetHandleInformation(m_handles[descriptor], mask, value))
			ThrowLastError("SetHandleInformation(FD_CLOEXEC)");
	}
	if ((flags & 1) != 0) {
		m_cloexec_descriptors.insert(descriptor);
	} else {
		m_cloexec_descriptors.erase(descriptor);
	}
}

std::int64_t DarwinSyscalls::Seek(int descriptor, std::int64_t offset, int whence)
{
	LARGE_INTEGER distance{};
	distance.QuadPart = offset;
	DWORD method = FILE_BEGIN;
	if (whence == 1) {
		method = FILE_CURRENT;
	} else if (whence == 2) {
		method = FILE_END;
	} else if (whence != 0) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("invalid Darwin seek origin");
	}
	LARGE_INTEGER position{};
	if (!SetFilePointerEx(GetHandle(descriptor), distance, &position, method)) {
		ThrowLastError("SetFilePointerEx");
	}
	return position.QuadPart;
}

int DarwinSyscalls::Duplicate(int descriptor, int target)
{
	{
		std::scoped_lock lock(m_mutex);
		const auto source_socket = m_sockets.find(descriptor);
		if (source_socket != m_sockets.end()) {
			int result = target;
			if (result < 0) {
				for (result = 3; result < static_cast<int>(m_handles.size()); ++result)
					if (m_handles[result] == nullptr) break;
				if (result == static_cast<int>(m_handles.size())) m_handles.push_back(nullptr);
			} else {
				if (result < 3) {
					DarwinErrno::Set(9);
					throw std::invalid_argument("standard descriptor replacement is unsupported");
				}
				if (static_cast<std::size_t>(result) >= m_handles.size())
					m_handles.resize(static_cast<std::size_t>(result) + 1, nullptr);
				if (m_handles[result] != nullptr) CloseUnlocked(result);
			}
			WSAPROTOCOL_INFO protocol{};
			if (WSADuplicateSocket(source_socket->second, GetCurrentProcessId(), &protocol) != 0) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "WSADuplicateSocket");
			}
			const SOCKET duplicate = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
				FROM_PROTOCOL_INFO, &protocol, 0, WSA_FLAG_OVERLAPPED);
			if (duplicate == INVALID_SOCKET) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "WSASocket(duplicate)");
			}
			m_handles[result] = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));
			m_sockets[result] = duplicate;
			if (m_nonblocking_descriptors.contains(descriptor)) m_nonblocking_descriptors.insert(result);
			else m_nonblocking_descriptors.erase(result);
			if (m_cloexec_descriptors.contains(descriptor)) m_cloexec_descriptors.insert(result);
			else m_cloexec_descriptors.erase(result);
			if (m_nosigpipe_descriptors.contains(descriptor)) m_nosigpipe_descriptors.insert(result);
			else m_nosigpipe_descriptors.erase(result);
			if (m_reuseport_descriptors.contains(descriptor)) m_reuseport_descriptors.insert(result);
			else m_reuseport_descriptors.erase(result);
			m_append_descriptors.erase(result);
			return result;
		}
	}
	std::scoped_lock lock(m_mutex);
	HANDLE source = nullptr;
	if (descriptor >= 0 && descriptor <= 2) {
		source = GetStdHandle(descriptor == 0 ? STD_INPUT_HANDLE :
			descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
	} else if (descriptor >= 0 && static_cast<std::size_t>(descriptor) < m_handles.size()) {
		source = m_handles[descriptor];
	}
	if (source == nullptr || source == INVALID_HANDLE_VALUE) {
		DarwinErrno::Set(9);
		throw std::invalid_argument("invalid Darwin file descriptor");
	}
	int result = target;
	if (result < 0) {
		for (result = 3; result < static_cast<int>(m_handles.size()); ++result) {
			if (m_handles[result] == nullptr) {
				break;
			}
		}
		if (result == static_cast<int>(m_handles.size())) {
			m_handles.push_back(nullptr);
		}
	} else {
		if (result < 3) {
			DarwinErrno::Set(9);
			throw std::invalid_argument("standard descriptor replacement is unsupported");
		}
		if (static_cast<std::size_t>(result) >= m_handles.size()) {
			m_handles.resize(static_cast<std::size_t>(result) + 1, nullptr);
		}
		if (m_handles[result] != nullptr) {
			CloseHandle(m_handles[result]);
			m_handles[result] = nullptr;
		}
	}
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate,
		0, FALSE, DUPLICATE_SAME_ACCESS)) {
		ThrowLastError("DuplicateHandle");
	}
	m_handles[result] = duplicate;
	if (descriptor >= 3) {
		const auto path = m_paths.find(descriptor);
		if (path != m_paths.end()) {
			m_paths[result] = path->second;
		} else {
			m_paths.erase(result);
		}
	}
	if (m_append_descriptors.contains(descriptor)) {
		m_append_descriptors.insert(result);
	} else {
		m_append_descriptors.erase(result);
	}
	if (m_nonblocking_descriptors.contains(descriptor)) {
		m_nonblocking_descriptors.insert(result);
	} else {
		m_nonblocking_descriptors.erase(result);
	}
	m_cloexec_descriptors.erase(result);
	return result;
}

int DarwinSyscalls::DuplicateAtLeast(int descriptor, int minimum)
{
	if (minimum < 0) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("negative Darwin descriptor minimum");
	}
	{
		std::scoped_lock lock(m_mutex);
		const auto source_socket = m_sockets.find(descriptor);
		if (source_socket != m_sockets.end()) {
			int result = (std::max)(3, minimum);
			if (static_cast<std::size_t>(result) < m_handles.size()) {
				while (result < static_cast<int>(m_handles.size()) && m_handles[result] != nullptr) ++result;
			}
			if (result >= static_cast<int>(m_handles.size()))
				m_handles.resize(static_cast<std::size_t>(result) + 1, nullptr);
			WSAPROTOCOL_INFO protocol{};
			if (WSADuplicateSocket(source_socket->second, GetCurrentProcessId(), &protocol) != 0) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "WSADuplicateSocket");
			}
			const SOCKET duplicate = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
				FROM_PROTOCOL_INFO, &protocol, 0, WSA_FLAG_OVERLAPPED);
			if (duplicate == INVALID_SOCKET) {
				const int error = WSAGetLastError();
				DarwinErrno::Set(error);
				throw std::system_error(error, std::system_category(), "WSASocket(duplicate)");
			}
			m_handles[result] = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));
			m_sockets[result] = duplicate;
			if (m_nonblocking_descriptors.contains(descriptor)) m_nonblocking_descriptors.insert(result);
			else m_nonblocking_descriptors.erase(result);
			if (m_cloexec_descriptors.contains(descriptor)) m_cloexec_descriptors.insert(result);
			else m_cloexec_descriptors.erase(result);
			if (m_nosigpipe_descriptors.contains(descriptor)) m_nosigpipe_descriptors.insert(result);
			else m_nosigpipe_descriptors.erase(result);
			if (m_reuseport_descriptors.contains(descriptor)) m_reuseport_descriptors.insert(result);
			else m_reuseport_descriptors.erase(result);
			m_append_descriptors.erase(result);
			return result;
		}
	}
	std::scoped_lock lock(m_mutex);
	HANDLE source = nullptr;
	if (descriptor >= 0 && descriptor <= 2) {
		source = GetStdHandle(descriptor == 0 ? STD_INPUT_HANDLE :
			descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
	} else if (descriptor >= 0 && static_cast<std::size_t>(descriptor) < m_handles.size()) {
		source = m_handles[descriptor];
	}
	if (source == nullptr || source == INVALID_HANDLE_VALUE) {
		DarwinErrno::Set(9);
		throw std::invalid_argument("invalid Darwin file descriptor");
	}
	int result = (std::max)(3, minimum);
	if (static_cast<std::size_t>(result) < m_handles.size()) {
		while (result < static_cast<int>(m_handles.size()) && m_handles[result] != nullptr) {
			++result;
		}
	}
	if (result >= static_cast<int>(m_handles.size())) {
		m_handles.resize(static_cast<std::size_t>(result) + 1, nullptr);
	}
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate,
		0, FALSE, DUPLICATE_SAME_ACCESS)) {
		ThrowLastError("DuplicateHandle");
	}
	m_handles[result] = duplicate;
	if (descriptor >= 3) {
		const auto path = m_paths.find(descriptor);
		if (path != m_paths.end()) {
			m_paths[result] = path->second;
		} else {
			m_paths.erase(result);
		}
	}
	if (m_append_descriptors.contains(descriptor)) {
		m_append_descriptors.insert(result);
	} else {
		m_append_descriptors.erase(result);
	}
	if (m_nonblocking_descriptors.contains(descriptor)) {
		m_nonblocking_descriptors.insert(result);
	} else {
		m_nonblocking_descriptors.erase(result);
	}
	m_cloexec_descriptors.erase(result);
	return result;
}

void DarwinSyscalls::CreatePipe(int descriptors[2])
{
	if (descriptors == nullptr) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("null Darwin pipe descriptor array");
	}
	HANDLE read_handle = nullptr;
	HANDLE write_handle = nullptr;
	if (!::CreatePipe(&read_handle, &write_handle, nullptr, 0)) {
		ThrowLastError("CreatePipe");
	}
	std::scoped_lock lock(m_mutex);
	auto allocate = [&]() {
		for (int descriptor = 3; descriptor < static_cast<int>(m_handles.size()); ++descriptor) {
			if (m_handles[descriptor] == nullptr) {
				return descriptor;
			}
		}
		m_handles.push_back(nullptr);
		return static_cast<int>(m_handles.size() - 1);
	};
	descriptors[0] = allocate();
	m_handles[descriptors[0]] = read_handle;
	descriptors[1] = allocate();
	m_handles[descriptors[1]] = write_handle;
}

int DarwinSyscalls::Poll(darling_pollfd* descriptors, std::size_t count, int timeout_ms)
{
	constexpr short poll_in = 0x0001;
	constexpr short poll_pri = 0x0002;
	constexpr short poll_out = 0x0004;
	constexpr short poll_hup = 0x0010;
	constexpr short poll_nval = 0x0020;
	if (descriptors == nullptr || timeout_ms < -1) {
		DarwinErrno::Set(22);
		return -1;
	}
	const auto ready_once = [&]() {
		int ready = 0;
		for (std::size_t index = 0; index < count; ++index) {
			auto& descriptor = descriptors[index];
			descriptor.revents = 0;
			if (descriptor.fd < 0)
				continue;
			if (descriptor.fd < 3) {
				const DWORD standard_id = descriptor.fd == 0 ? STD_INPUT_HANDLE :
					descriptor.fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
				const HANDLE standard = GetStdHandle(standard_id);
				if (standard == nullptr || standard == INVALID_HANDLE_VALUE) {
					descriptor.revents = poll_nval;
					++ready;
					continue;
				}
				if ((descriptor.events & poll_out) != 0 && descriptor.fd != 0)
					descriptor.revents |= poll_out;
				if ((descriptor.events & poll_in) != 0 && descriptor.fd == 0) {
					const DWORD file_type = GetFileType(standard);
					if (file_type == FILE_TYPE_PIPE) {
						DWORD available = 0;
						if (PeekNamedPipe(standard, nullptr, 0, nullptr, &available, nullptr)) {
							if (available != 0)
								descriptor.revents |= poll_in;
						} else {
							descriptor.revents |= poll_hup;
						}
					} else if (file_type == FILE_TYPE_CHAR) {
						DWORD events = 0;
						if (GetNumberOfConsoleInputEvents(standard, &events) && events != 0)
							descriptor.revents |= poll_in;
					}
				}
				if (descriptor.revents != 0)
					++ready;
				continue;
			}
			{
				std::scoped_lock lock(m_mutex);
				const auto socket = m_sockets.find(descriptor.fd);
				if (socket != m_sockets.end()) {
					WSAPOLLFD native{};
					native.fd = socket->second;
					native.events = ((descriptor.events & poll_in) != 0 ? POLLRDNORM : 0) |
						((descriptor.events & poll_pri) != 0 ? POLLRDBAND : 0) |
						((descriptor.events & poll_out) != 0 ? POLLWRNORM : 0);
					if (WSAPoll(&native, 1, 0) == SOCKET_ERROR) {
						descriptor.revents = poll_nval;
					} else {
						if ((native.revents & (POLLRDNORM | POLLRDBAND)) != 0)
							descriptor.revents |= poll_in;
						if ((native.revents & POLLRDBAND) != 0)
							descriptor.revents |= poll_pri;
						if ((native.revents & POLLWRNORM) != 0)
							descriptor.revents |= poll_out;
						if ((native.revents & (POLLHUP | POLLERR)) != 0)
							descriptor.revents |= poll_hup;
					}
					if (descriptor.revents != 0)
						++ready;
					continue;
				}
			}
			HANDLE handle = nullptr;
			try {
				handle = GetHandle(descriptor.fd);
			} catch (...) {
				descriptor.revents = poll_nval;
				++ready;
				continue;
			}
			if (GetFileType(handle) == FILE_TYPE_PIPE) {
				DWORD available = 0;
				if ((descriptor.events & poll_in) != 0 &&
					PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
					if (available != 0)
						descriptor.revents |= poll_in;
				} else if ((descriptor.events & poll_in) != 0) {
					descriptor.revents |= poll_hup;
				}
				if ((descriptor.events & poll_out) != 0)
					descriptor.revents |= poll_out;
			} else {
				if ((descriptor.events & poll_in) != 0)
					descriptor.revents |= poll_in;
				if ((descriptor.events & poll_out) != 0)
					descriptor.revents |= poll_out;
			}
			if (descriptor.revents != 0)
				++ready;
		}
		return ready;
	};
	const int initial = ready_once();
	if (initial != 0 || timeout_ms == 0)
		return initial;
	const ULONGLONG start = GetTickCount64();
	while (timeout_ms < 0 || GetTickCount64() - start < static_cast<ULONGLONG>(timeout_ms)) {
		Sleep(1);
		const int ready = ready_once();
		if (ready != 0)
			return ready;
	}
	return 0;
}

bool DarwinSyscalls::IsNonblocking(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	return m_nonblocking_descriptors.contains(descriptor);
}

int DarwinSyscalls::Socket(int domain, int type, int protocol)
{
	// Darwin exposes these as type-word modifiers; Winsock expects the base
	// socket kind only and has no equivalent close-on-exec socket flag.
	constexpr int darwin_sock_nonblock = 0x20000000;
	constexpr int darwin_sock_cloexec = 0x10000000;
	const bool nonblocking = (type & darwin_sock_nonblock) != 0;
	const bool close_on_exec = (type & darwin_sock_cloexec) != 0;
	type &= ~(darwin_sock_nonblock | darwin_sock_cloexec);
	static std::once_flag winsock_once;
	std::call_once(winsock_once, [] {
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup");
	});
	const SOCKET socket = ::socket(domain, type, protocol);
	if (socket == INVALID_SOCKET) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "socket");
	}
	if (nonblocking) {
		u_long mode = 1;
		if (ioctlsocket(socket, FIONBIO, &mode) != 0) {
			const int error = WSAGetLastError();
			closesocket(socket);
			DarwinErrno::Set(error);
			throw std::system_error(error, std::system_category(), "ioctlsocket(SOCK_NONBLOCK)");
		}
	}
	std::scoped_lock lock(m_mutex);
	for (int descriptor = 3; descriptor < static_cast<int>(m_handles.size()); ++descriptor) {
		if (m_handles[descriptor] == nullptr) {
			m_handles[descriptor] = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));
			m_sockets.emplace(descriptor, socket);
			if (nonblocking) m_nonblocking_descriptors.insert(descriptor);
			if (close_on_exec) m_cloexec_descriptors.insert(descriptor);
			return descriptor;
		}
	}
	m_handles.push_back(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
	const int descriptor = static_cast<int>(m_handles.size() - 1);
	m_sockets.emplace(descriptor, socket);
	if (nonblocking) m_nonblocking_descriptors.insert(descriptor);
	if (close_on_exec) m_cloexec_descriptors.insert(descriptor);
	return descriptor;
}

void DarwinSyscalls::SocketPair(int domain, int type, int protocol, int descriptors[2])
{
	constexpr int darwin_sock_nonblock = 0x20000000;
	constexpr int darwin_sock_cloexec = 0x10000000;
	const bool nonblocking = (type & darwin_sock_nonblock) != 0;
	const bool close_on_exec = (type & darwin_sock_cloexec) != 0;
	if (descriptors == nullptr || (type & 0x0f) != SOCK_STREAM ||
		(domain != AF_UNIX && domain != AF_INET) || (protocol != 0 && protocol != IPPROTO_TCP)) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("unsupported Darwin socketpair parameters");
	}
	descriptors[0] = -1;
	descriptors[1] = -1;
	int listener = -1;
	try {
		listener = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(0);
		BindSocket(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		int address_length = sizeof(address);
		if (getsockname(GetSocket(listener), reinterpret_cast<sockaddr*>(&address),
			&address_length) == SOCKET_ERROR)
			throw std::system_error(WSAGetLastError(), std::system_category(), "socketpair getsockname");
		ListenSocket(listener, 1);
		descriptors[0] = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		ConnectSocket(descriptors[0], reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		descriptors[1] = AcceptSocket(listener, nullptr, nullptr);
		Close(listener);
		{
			std::scoped_lock lock(m_mutex);
			m_socket_peers[descriptors[0]] = descriptors[1];
			m_socket_peers[descriptors[1]] = descriptors[0];
		}
		if (nonblocking) {
			SetDescriptorFlags(descriptors[0], 0x0004);
			SetDescriptorFlags(descriptors[1], 0x0004);
		}
		if (close_on_exec) {
			SetDescriptorFdFlags(descriptors[0], 1);
			SetDescriptorFdFlags(descriptors[1], 1);
		}
	} catch (...) {
		if (listener >= 0) {
			try { Close(listener); } catch (...) {}
		}
		if (descriptors[0] >= 0) {
			try { Close(descriptors[0]); } catch (...) {}
		}
		if (descriptors[1] >= 0) {
			try { Close(descriptors[1]); } catch (...) {}
		}
		descriptors[0] = -1;
		descriptors[1] = -1;
		throw;
	}
}

int DarwinSyscalls::SocketPeerDescriptor(int descriptor) const
{
	std::scoped_lock lock(m_mutex);
	const auto peer = m_socket_peers.find(descriptor);
	return peer == m_socket_peers.end() ? -1 : peer->second;
}

void DarwinSyscalls::BindSocket(int descriptor, const sockaddr* address, int address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	if (bind(socket, address, address_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "bind");
	}
}

void DarwinSyscalls::ConnectSocket(int descriptor, const sockaddr* address, int address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	if (connect(socket, address, address_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "connect");
	}
}

void DarwinSyscalls::ListenSocket(int descriptor, int backlog)
{
	const SOCKET socket = GetSocket(descriptor);
	if (listen(socket, backlog) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "listen");
	}
}

int DarwinSyscalls::AcceptSocket(int descriptor, sockaddr* address, int* address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	const SOCKET accepted = accept(socket, address, address_length);
	if (accepted == INVALID_SOCKET) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "accept");
	}
	std::scoped_lock lock(m_mutex);
	for (int result = 3; result < static_cast<int>(m_handles.size()); ++result) {
		if (m_handles[result] == nullptr) {
			m_handles[result] = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));
			m_sockets.emplace(result, accepted);
			return result;
		}
	}
	m_handles.push_back(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
	const int result = static_cast<int>(m_handles.size() - 1);
	m_sockets.emplace(result, accepted);
	return result;
}

void DarwinSyscalls::GetSocketName(int descriptor, sockaddr* address, int* address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	if (getsockname(socket, address, address_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "getsockname");
	}
}

void DarwinSyscalls::GetPeerSocketName(int descriptor, sockaddr* address, int* address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	if (getpeername(socket, address, address_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "getpeername");
	}
}

void DarwinSyscalls::GetSocketOption(int descriptor, int level, int option, void* value,
	int* value_length)
{
	constexpr int darwin_so_nosigpipe = 0x1022;
	constexpr int darwin_so_reuseport = 0x0200;
	if (level == SOL_SOCKET && option == darwin_so_nosigpipe) {
		if (value == nullptr || value_length == nullptr || *value_length < static_cast<int>(sizeof(int))) {
			DarwinErrno::Set(22);
			throw std::invalid_argument("invalid SO_NOSIGPIPE result buffer");
		}
		std::scoped_lock lock(m_mutex);
		*static_cast<int*>(value) = m_nosigpipe_descriptors.contains(descriptor) ? 1 : 0;
		*value_length = sizeof(int);
		return;
	}
	if (level == SOL_SOCKET && option == darwin_so_reuseport) {
		if (value == nullptr || value_length == nullptr || *value_length < static_cast<int>(sizeof(int))) {
			DarwinErrno::Set(22);
			throw std::invalid_argument("invalid SO_REUSEPORT result buffer");
		}
		std::scoped_lock lock(m_mutex);
		*static_cast<int*>(value) = m_reuseport_descriptors.contains(descriptor) ? 1 : 0;
		*value_length = sizeof(int);
		return;
	}
	const SOCKET socket = GetSocket(descriptor);
	if (getsockopt(socket, level, option, static_cast<char*>(value), value_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "getsockopt");
	}
}

void DarwinSyscalls::SetSocketOption(int descriptor, int level, int option, const void* value,
	int value_length)
{
	constexpr int darwin_so_nosigpipe = 0x1022;
	constexpr int darwin_so_reuseport = 0x0200;
	if (level == SOL_SOCKET && option == darwin_so_nosigpipe) {
		if (value == nullptr || value_length < static_cast<int>(sizeof(int))) {
			DarwinErrno::Set(22);
			throw std::invalid_argument("invalid SO_NOSIGPIPE value");
		}
		std::scoped_lock lock(m_mutex);
		if (*static_cast<const int*>(value) != 0)
			m_nosigpipe_descriptors.insert(descriptor);
		else
			m_nosigpipe_descriptors.erase(descriptor);
		return;
	}
	if (level == SOL_SOCKET && option == darwin_so_reuseport) {
		if (value == nullptr || value_length < static_cast<int>(sizeof(int))) {
			DarwinErrno::Set(22);
			throw std::invalid_argument("invalid SO_REUSEPORT value");
		}
		const int enabled = *static_cast<const int*>(value);
		const SOCKET socket = GetSocket(descriptor);
		if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
			static_cast<const char*>(value), sizeof(int)) == SOCKET_ERROR) {
			const int error = WSAGetLastError();
			DarwinErrno::Set(error);
			throw std::system_error(error, std::system_category(), "setsockopt(SO_REUSEPORT)");
		}
		std::scoped_lock lock(m_mutex);
		if (enabled != 0)
			m_reuseport_descriptors.insert(descriptor);
		else
			m_reuseport_descriptors.erase(descriptor);
		return;
	}
	const SOCKET socket = GetSocket(descriptor);
	if (setsockopt(socket, level, option, static_cast<const char*>(value), value_length) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "setsockopt");
	}
}

std::size_t DarwinSyscalls::SendToSocket(int descriptor, const void* buffer, std::size_t bytes,
	int flags, const sockaddr* address, int address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	TemporarySocketNonblocking temporary_nonblocking(socket, (flags & 0x0080) != 0,
		IsNonblocking(descriptor));
	const int result = sendto(socket, static_cast<const char*>(buffer),
		static_cast<int>((std::min)(bytes, static_cast<std::size_t>(INT_MAX))),
		DarwinSocketFlags(flags),
		address, address_length);
	if (result == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "sendto");
	}
	return static_cast<std::size_t>(result);
}

std::size_t DarwinSyscalls::ReceiveFromSocket(int descriptor, void* buffer, std::size_t bytes,
	int flags, sockaddr* address, int* address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	TemporarySocketNonblocking temporary_nonblocking(socket, (flags & 0x0080) != 0,
		IsNonblocking(descriptor));
	const int result = recvfrom(socket, static_cast<char*>(buffer),
		static_cast<int>((std::min)(bytes, static_cast<std::size_t>(INT_MAX))),
		DarwinSocketFlags(flags),
		address, address_length);
	if (result == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "recvfrom");
	}
	return static_cast<std::size_t>(result);
}

std::size_t DarwinSyscalls::SendMessage(int descriptor, const WSABUF* buffers, DWORD count,
	int flags, const sockaddr* address, int address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	TemporarySocketNonblocking temporary_nonblocking(socket, (flags & 0x0080) != 0,
		IsNonblocking(descriptor));
	DWORD transferred = 0;
	const int result = address == nullptr ? WSASend(socket, const_cast<LPWSABUF>(buffers), count,
		&transferred, static_cast<DWORD>(DarwinSocketFlags(flags)), nullptr, nullptr) :
		WSASendTo(socket, const_cast<LPWSABUF>(buffers), count, &transferred,
			static_cast<DWORD>(DarwinSocketFlags(flags)), address, address_length, nullptr, nullptr);
	if (result != 0) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "WSASend message");
	}
	return transferred;
}

std::size_t DarwinSyscalls::ReceiveMessage(int descriptor, WSABUF* buffers, DWORD count,
	int* flags, sockaddr* address, int* address_length)
{
	const SOCKET socket = GetSocket(descriptor);
	TemporarySocketNonblocking temporary_nonblocking(socket, flags != nullptr &&
		((*flags & 0x0080) != 0), IsNonblocking(descriptor));
	DWORD transferred = 0;
	DWORD native_flags = flags == nullptr ? 0 : static_cast<DWORD>(*flags);
	if (flags != nullptr)
		native_flags = static_cast<DWORD>(DarwinSocketFlags(*flags));
	const int result = address == nullptr ? WSARecv(socket, buffers, count, &transferred,
		&native_flags, nullptr, nullptr) : WSARecvFrom(socket, buffers, count,
		&transferred, &native_flags, address, address_length, nullptr, nullptr);
	if (result != 0) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "WSARecv message");
	}
	if (flags != nullptr)
		*flags = DarwinSocketResultFlags(static_cast<int>(native_flags));
	return transferred;
}

void DarwinSyscalls::ShutdownSocket(int descriptor, int how)
{
	const SOCKET socket = GetSocket(descriptor);
	if (shutdown(socket, how) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		DarwinErrno::Set(error);
		throw std::system_error(error, std::system_category(), "shutdown");
	}
}

void DarwinSyscalls::Truncate(int descriptor, std::int64_t length)
{
	if (length < 0) {
		DarwinErrno::Set(22);
		throw std::invalid_argument("negative Darwin file length");
	}
	const HANDLE handle = GetHandle(descriptor);
	LARGE_INTEGER zero{};
	LARGE_INTEGER current{};
	if (!SetFilePointerEx(handle, zero, &current, FILE_CURRENT)) {
		ThrowLastError("SetFilePointerEx(current)");
	}
	LARGE_INTEGER requested{};
	requested.QuadPart = length;
	if (!SetFilePointerEx(handle, requested, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
		ThrowLastError("SetEndOfFile");
	}
	if (!SetFilePointerEx(handle, current, nullptr, FILE_BEGIN)) {
		ThrowLastError("SetFilePointerEx(restore)");
	}
}

void DarwinSyscalls::CloseUnlocked(int descriptor) noexcept
{
	const auto socket = m_sockets.find(descriptor);
	if (socket != m_sockets.end()) {
		closesocket(socket->second);
		m_sockets.erase(socket);
		const auto peer = m_socket_peers.find(descriptor);
		if (peer != m_socket_peers.end()) {
			m_socket_peers.erase(peer->second);
			m_socket_peers.erase(peer);
		}
		if (descriptor >= 0 && static_cast<std::size_t>(descriptor) < m_handles.size())
			m_handles[descriptor] = nullptr;
		m_paths.erase(descriptor);
		m_append_descriptors.erase(descriptor);
		m_nonblocking_descriptors.erase(descriptor);
		m_cloexec_descriptors.erase(descriptor);
		m_nosigpipe_descriptors.erase(descriptor);
		m_reuseport_descriptors.erase(descriptor);
		return;
	}
	if (descriptor >= 0 && static_cast<std::size_t>(descriptor) < m_handles.size() &&
		m_handles[descriptor] != nullptr) {
		CloseHandle(m_handles[descriptor]);
		m_handles[descriptor] = nullptr;
		m_paths.erase(descriptor);
		m_append_descriptors.erase(descriptor);
		m_nonblocking_descriptors.erase(descriptor);
		m_cloexec_descriptors.erase(descriptor);
		m_nosigpipe_descriptors.erase(descriptor);
		m_reuseport_descriptors.erase(descriptor);
	}
}

void DarwinSyscalls::Close(int descriptor)
{
	std::scoped_lock lock(m_mutex);
	if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= m_handles.size() ||
		m_handles[descriptor] == nullptr) {
		throw std::invalid_argument("invalid Darwin file descriptor");
	}
	CloseUnlocked(descriptor);
}

} // namespace darling::windows_host
