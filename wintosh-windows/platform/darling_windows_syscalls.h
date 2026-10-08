/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace darling::windows_host {

struct darling_pollfd final {
	int fd = -1;
	short events = 0;
	short revents = 0;
};

class DarwinSyscalls final {
public:
	DarwinSyscalls();
	DarwinSyscalls(const DarwinSyscalls&) = delete;
	DarwinSyscalls& operator=(const DarwinSyscalls&) = delete;
	~DarwinSyscalls() noexcept;

	[[nodiscard]] int OpenRead(const std::filesystem::path& path);
	[[nodiscard]] int OpenWrite(const std::filesystem::path& path);
	[[nodiscard]] int OpenReadWrite(const std::filesystem::path& path);
	[[nodiscard]] int Open(const std::filesystem::path& path, int flags);
	[[nodiscard]] std::size_t Read(int descriptor, void* buffer, std::size_t bytes);
	[[nodiscard]] std::size_t Write(int descriptor, const void* buffer, std::size_t bytes);
	[[nodiscard]] std::size_t ReadAt(int descriptor, void* buffer, std::size_t bytes,
		std::int64_t offset);
	[[nodiscard]] std::size_t WriteAt(int descriptor, const void* buffer, std::size_t bytes,
		std::int64_t offset);
	void Lock(int descriptor, int operation);
	void Flush(int descriptor);
	[[nodiscard]] HANDLE MappingHandle(int descriptor) const;
	void GetFileInformation(int descriptor, BY_HANDLE_FILE_INFORMATION* information) const;
	[[nodiscard]] std::filesystem::path PathForDescriptor(int descriptor) const;
	[[nodiscard]] int GetDescriptorFlags(int descriptor) const;
	void SetDescriptorFlags(int descriptor, int flags);
	[[nodiscard]] int GetDescriptorFdFlags(int descriptor) const;
	void SetDescriptorFdFlags(int descriptor, int flags);
	[[nodiscard]] std::int64_t Seek(int descriptor, std::int64_t offset, int whence);
	[[nodiscard]] int Duplicate(int descriptor, int target = -1);
	[[nodiscard]] int DuplicateAtLeast(int descriptor, int minimum);
	void Truncate(int descriptor, std::int64_t length);
	void CreatePipe(int descriptors[2]);
	[[nodiscard]] int Poll(darling_pollfd* descriptors, std::size_t count, int timeout_ms);
	[[nodiscard]] bool IsNonblocking(int descriptor) const;
	[[nodiscard]] int Socket(int domain, int type, int protocol);
	void SocketPair(int domain, int type, int protocol, int descriptors[2]);
	[[nodiscard]] int SocketPeerDescriptor(int descriptor) const;
	void BindSocket(int descriptor, const sockaddr* address, int address_length);
	void ConnectSocket(int descriptor, const sockaddr* address, int address_length);
	void ListenSocket(int descriptor, int backlog);
	[[nodiscard]] int AcceptSocket(int descriptor, sockaddr* address, int* address_length);
	void GetSocketName(int descriptor, sockaddr* address, int* address_length);
	void GetPeerSocketName(int descriptor, sockaddr* address, int* address_length);
	void GetSocketOption(int descriptor, int level, int option, void* value, int* value_length);
	void SetSocketOption(int descriptor, int level, int option, const void* value, int value_length);
	[[nodiscard]] std::size_t SendToSocket(int descriptor, const void* buffer, std::size_t bytes,
		int flags, const sockaddr* address, int address_length);
	[[nodiscard]] std::size_t ReceiveFromSocket(int descriptor, void* buffer, std::size_t bytes,
		int flags, sockaddr* address, int* address_length);
	[[nodiscard]] std::size_t SendMessage(int descriptor, const WSABUF* buffers, DWORD count,
		int flags, const sockaddr* address, int address_length);
	[[nodiscard]] std::size_t ReceiveMessage(int descriptor, WSABUF* buffers, DWORD count,
		int* flags, sockaddr* address, int* address_length);
	void ShutdownSocket(int descriptor, int how);
	void Close(int descriptor);

private:
	[[nodiscard]] HANDLE GetHandle(int descriptor) const;
	[[nodiscard]] SOCKET GetSocket(int descriptor) const;
	void CloseUnlocked(int descriptor) noexcept;

	mutable std::mutex m_mutex;
	std::vector<HANDLE> m_handles;
	std::unordered_map<int, SOCKET> m_sockets;
	std::unordered_map<int, int> m_socket_peers;
	std::unordered_map<int, std::filesystem::path> m_paths;
	std::unordered_set<int> m_append_descriptors;
	std::unordered_set<int> m_nonblocking_descriptors;
	std::unordered_set<int> m_cloexec_descriptors;
	std::unordered_set<int> m_nosigpipe_descriptors;
	std::unordered_set<int> m_reuseport_descriptors;
};

} // namespace darling::windows_host
