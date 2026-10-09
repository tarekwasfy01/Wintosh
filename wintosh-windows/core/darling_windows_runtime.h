/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace darling::windows_host {

struct MachMessage final {
	std::vector<std::uint8_t> inline_data;
	std::vector<std::uint8_t> out_of_line_data;
};

enum class MachIpcOperation : std::uint16_t {
	Allocate = 1,
	Send = 2,
	Receive = 3,
	Deallocate = 4,
	Destroy = 5
};

struct MachIpcEnvelope final {
	MachIpcOperation operation = MachIpcOperation::Allocate;
	std::uint64_t request_id = 0;
	std::uint64_t port_token = 0;
	std::uint32_t disposition_count = 0;
	std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::vector<std::uint8_t> EncodeMachIpcEnvelope(
	const MachIpcEnvelope& envelope);
[[nodiscard]] MachIpcEnvelope DecodeMachIpcEnvelope(
	const std::vector<std::uint8_t>& bytes);

class MachIpcSharedMemory final {
public:
	static MachIpcSharedMemory Create(const std::wstring& name, std::size_t size);
	static MachIpcSharedMemory Open(const std::wstring& name, std::size_t size);
	MachIpcSharedMemory(const MachIpcSharedMemory&) = delete;
	MachIpcSharedMemory& operator=(const MachIpcSharedMemory&) = delete;
	MachIpcSharedMemory(MachIpcSharedMemory&& other) noexcept;
	MachIpcSharedMemory& operator=(MachIpcSharedMemory&& other) noexcept;
	~MachIpcSharedMemory() noexcept;

	[[nodiscard]] void* Data() const noexcept { return m_view; }
	[[nodiscard]] std::size_t Size() const noexcept { return m_size; }

private:
	MachIpcSharedMemory(HANDLE mapping, void* view, std::size_t size) noexcept :
		m_mapping(mapping), m_view(view), m_size(size) {}
	void Reset() noexcept;
	HANDLE m_mapping = nullptr;
	void* m_view = nullptr;
	std::size_t m_size = 0;
};

class TaskPort final {
public:
	enum class MemoryProtection { ReadOnly, ReadWrite, ReadExecute, ReadWriteExecute };
	static TaskPort Current();
	static TaskPort Open(DWORD process_id);
	TaskPort(const TaskPort&) = delete;
	TaskPort& operator=(const TaskPort&) = delete;
	TaskPort(TaskPort&& other) noexcept;
	TaskPort& operator=(TaskPort&& other) noexcept;
	~TaskPort() noexcept;

	[[nodiscard]] DWORD ProcessId() const noexcept { return m_process_id; }
	[[nodiscard]] bool IsAlive() const noexcept;
	[[nodiscard]] bool Wait(DWORD timeout_ms) const noexcept;
	[[nodiscard]] bool ExitCode(DWORD& code) const noexcept;
	[[nodiscard]] bool Read(const void* address, void* buffer, std::size_t bytes) const noexcept;
	[[nodiscard]] bool Write(void* address, const void* buffer, std::size_t bytes) const noexcept;
	[[nodiscard]] bool Protect(void* address, std::size_t bytes,
		MemoryProtection protection) const noexcept;
	[[nodiscard]] bool Query(const void* address, MEMORY_BASIC_INFORMATION& information) const noexcept;

private:
	TaskPort(HANDLE process, DWORD process_id) noexcept : m_process(process), m_process_id(process_id) {}
	void Reset() noexcept;
	HANDLE m_process = nullptr;
	DWORD m_process_id = 0;
};

class ThreadPort final {
public:
	static ThreadPort Current();
	static ThreadPort Open(DWORD thread_id);
	ThreadPort(const ThreadPort&) = delete;
	ThreadPort& operator=(const ThreadPort&) = delete;
	ThreadPort(ThreadPort&& other) noexcept;
	ThreadPort& operator=(ThreadPort&& other) noexcept;
	~ThreadPort() noexcept;

	[[nodiscard]] DWORD ThreadId() const noexcept { return m_thread_id; }
	[[nodiscard]] DWORD ProcessId() const noexcept;
	[[nodiscard]] bool IsAlive() const noexcept;
	[[nodiscard]] bool ExitCode(DWORD& code) const noexcept;
	[[nodiscard]] bool Wait(DWORD timeout_ms) const noexcept;
	[[nodiscard]] bool Suspend() const noexcept;
	[[nodiscard]] bool Resume() const noexcept;
	[[nodiscard]] int Priority() const noexcept;
	[[nodiscard]] bool SetPriority(int priority) const noexcept;

private:
	ThreadPort(HANDLE thread, DWORD thread_id) noexcept : m_thread(thread), m_thread_id(thread_id) {}
	void Reset() noexcept;
	HANDLE m_thread = nullptr;
	DWORD m_thread_id = 0;
};

class MachPort final {
public:
	MachPort() = default;
	MachPort(const MachPort&) = delete;
	MachPort& operator=(const MachPort&) = delete;

	[[nodiscard]] bool Send(MachMessage message);
	[[nodiscard]] std::optional<MachMessage> Receive(DWORD timeout_ms);
	void Close() noexcept;
	[[nodiscard]] bool IsClosed() const noexcept;

private:
	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	std::deque<MachMessage> m_messages;
	bool m_closed = false;
};

class Prefix final {
public:
	static Prefix Create(const std::wstring& root);
	[[nodiscard]] const std::wstring& Root() const noexcept { return m_root; }

private:
	explicit Prefix(std::wstring root) : m_root(std::move(root)) {}
	std::wstring m_root;
};

class NamedPipeRpcServer final {
public:
	static NamedPipeRpcServer Create(const std::wstring& name);
	NamedPipeRpcServer(const NamedPipeRpcServer&) = delete;
	NamedPipeRpcServer& operator=(const NamedPipeRpcServer&) = delete;
	NamedPipeRpcServer(NamedPipeRpcServer&& other) noexcept;
	NamedPipeRpcServer& operator=(NamedPipeRpcServer&& other) noexcept;
	~NamedPipeRpcServer() noexcept;

	void WaitForClient();
	std::string Read();
	void Write(const std::string& payload);
	[[nodiscard]] const std::wstring& Name() const noexcept { return m_name; }

private:
	NamedPipeRpcServer(std::wstring name, HANDLE pipe) noexcept;
	void Reset() noexcept;

	std::wstring m_name;
	HANDLE m_pipe = INVALID_HANDLE_VALUE;
};

class NamedPipeRpcClient final {
public:
	static NamedPipeRpcClient Connect(const std::wstring& name);
	NamedPipeRpcClient(const NamedPipeRpcClient&) = delete;
	NamedPipeRpcClient& operator=(const NamedPipeRpcClient&) = delete;
	NamedPipeRpcClient(NamedPipeRpcClient&& other) noexcept;
	NamedPipeRpcClient& operator=(NamedPipeRpcClient&& other) noexcept;
	~NamedPipeRpcClient() noexcept;

	void Write(const std::string& payload);
	std::string Read();

private:
	 explicit NamedPipeRpcClient(HANDLE pipe) noexcept : m_pipe(pipe) {}
	void Reset() noexcept;

	HANDLE m_pipe = INVALID_HANDLE_VALUE;
};

} // namespace darling::windows_host
