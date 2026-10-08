/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_runtime.h"

#include <chrono>
#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	const auto error = GetLastError();
	std::fprintf(stderr, "%s failed with Win32 error %lu\n", operation,
		static_cast<unsigned long>(error));
	throw std::system_error(static_cast<int>(error),
		std::system_category(), operation);
}

void WriteAll(HANDLE pipe, const void* data, DWORD size)
{
	auto* cursor = static_cast<const std::uint8_t*>(data);
	while (size != 0) {
		DWORD written = 0;
		if (!WriteFile(pipe, cursor, size, &written, nullptr) || written == 0) {
			ThrowLastError("WriteFile");
		}
		cursor += written;
		size -= written;
	}
}

void ReadAll(HANDLE pipe, void* data, DWORD size)
{
	auto* cursor = static_cast<std::uint8_t*>(data);
	while (size != 0) {
		DWORD read = 0;
		if (!ReadFile(pipe, cursor, size, &read, nullptr) || read == 0) {
			ThrowLastError("ReadFile");
		}
		cursor += read;
		size -= read;
	}
}

void WriteFrame(HANDLE pipe, const std::string& payload)
{
	if (payload.size() > 1024 * 1024) {
		throw std::length_error("RPC payload is too large");
	}
	const auto size = static_cast<DWORD>(payload.size());
	WriteAll(pipe, &size, sizeof(size));
	if (size != 0) {
		WriteAll(pipe, payload.data(), size);
	}
}

std::string ReadFrame(HANDLE pipe)
{
	DWORD size = 0;
	ReadAll(pipe, &size, sizeof(size));
	if (size > 1024 * 1024) {
		throw std::length_error("RPC payload is too large");
	}
	std::string payload(size, '\0');
	if (size != 0) {
		ReadAll(pipe, payload.data(), size);
	}
	return payload;
}

void EnsureDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::create_directories(path, error);
	if (error) {
		throw std::system_error(error, "create_directories");
	}
}

} // namespace

TaskPort TaskPort::Current()
{
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
		&duplicate, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION |
		PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE, FALSE, 0))
		ThrowLastError("DuplicateHandle(current task)");
	return TaskPort(duplicate, GetCurrentProcessId());
}

TaskPort TaskPort::Open(DWORD process_id)
{
	const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION |
		PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE,
		FALSE, process_id);
	if (process == nullptr)
		ThrowLastError("OpenProcess(task port)");
	return TaskPort(process, process_id);
}

TaskPort::TaskPort(TaskPort&& other) noexcept :
	m_process(other.m_process), m_process_id(other.m_process_id)
{
	other.m_process = nullptr;
	other.m_process_id = 0;
}

ThreadPort ThreadPort::Current()
{
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
		&duplicate, THREAD_QUERY_LIMITED_INFORMATION | THREAD_SUSPEND_RESUME |
		THREAD_SET_INFORMATION | SYNCHRONIZE, FALSE, 0))
		ThrowLastError("DuplicateHandle(current thread)");
	return ThreadPort(duplicate, GetCurrentThreadId());
}

ThreadPort ThreadPort::Open(DWORD thread_id)
{
	const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SUSPEND_RESUME |
		THREAD_SET_INFORMATION | SYNCHRONIZE,
		FALSE, thread_id);
	if (thread == nullptr)
		ThrowLastError("OpenThread(thread port)");
	return ThreadPort(thread, thread_id);
}

ThreadPort::ThreadPort(ThreadPort&& other) noexcept :
	m_thread(other.m_thread), m_thread_id(other.m_thread_id)
{
	other.m_thread = nullptr;
	other.m_thread_id = 0;
}

ThreadPort& ThreadPort::operator=(ThreadPort&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_thread = other.m_thread;
		m_thread_id = other.m_thread_id;
		other.m_thread = nullptr;
		other.m_thread_id = 0;
	}
	return *this;
}

ThreadPort::~ThreadPort() noexcept
{
	Reset();
}

bool ThreadPort::IsAlive() const noexcept
{
	return m_thread != nullptr && WaitForSingleObject(m_thread, 0) == WAIT_TIMEOUT;
}

DWORD ThreadPort::ProcessId() const noexcept
{
	return m_thread == nullptr ? 0 : GetProcessIdOfThread(m_thread);
}

bool ThreadPort::ExitCode(DWORD& code) const noexcept
{
	return m_thread != nullptr && GetExitCodeThread(m_thread, &code) != FALSE;
}

bool ThreadPort::Wait(DWORD timeout_ms) const noexcept
{
	return m_thread != nullptr && WaitForSingleObject(m_thread, timeout_ms) == WAIT_OBJECT_0;
}

bool ThreadPort::Suspend() const noexcept
{
	return m_thread != nullptr && SuspendThread(m_thread) != static_cast<DWORD>(-1);
}

bool ThreadPort::Resume() const noexcept
{
	return m_thread != nullptr && ResumeThread(m_thread) != static_cast<DWORD>(-1);
}

int ThreadPort::Priority() const noexcept
{
	return m_thread == nullptr ? THREAD_PRIORITY_ERROR_RETURN : GetThreadPriority(m_thread);
}

bool ThreadPort::SetPriority(int priority) const noexcept
{
	return m_thread != nullptr && SetThreadPriority(m_thread, priority) != FALSE;
}

void ThreadPort::Reset() noexcept
{
	if (m_thread != nullptr) {
		CloseHandle(m_thread);
		m_thread = nullptr;
	}
	m_thread_id = 0;
}

TaskPort& TaskPort::operator=(TaskPort&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_process = other.m_process;
		m_process_id = other.m_process_id;
		other.m_process = nullptr;
		other.m_process_id = 0;
	}
	return *this;
}

TaskPort::~TaskPort() noexcept
{
	Reset();
}

bool TaskPort::IsAlive() const noexcept
{
	return m_process != nullptr && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
}

bool TaskPort::Wait(DWORD timeout_ms) const noexcept
{
	return m_process != nullptr && WaitForSingleObject(m_process, timeout_ms) == WAIT_OBJECT_0;
}

bool TaskPort::ExitCode(DWORD& code) const noexcept
{
	return m_process != nullptr && GetExitCodeProcess(m_process, &code) != FALSE;
}

bool TaskPort::Read(const void* address, void* buffer, std::size_t bytes) const noexcept
{
	if (m_process == nullptr || address == nullptr || buffer == nullptr || bytes == 0)
		return false;
	SIZE_T transferred = 0;
	return ReadProcessMemory(m_process, address, buffer, bytes, &transferred) != FALSE &&
		transferred == bytes;
}

bool TaskPort::Write(void* address, const void* buffer, std::size_t bytes) const noexcept
{
	if (m_process == nullptr || address == nullptr || buffer == nullptr || bytes == 0)
		return false;
	SIZE_T transferred = 0;
	return WriteProcessMemory(m_process, address, buffer, bytes, &transferred) != FALSE &&
		transferred == bytes;
}

bool TaskPort::Protect(void* address, std::size_t bytes, MemoryProtection protection) const noexcept
{
	if (m_process == nullptr || address == nullptr || bytes == 0)
		return false;
	DWORD native_protection = PAGE_READONLY;
	switch (protection) {
	case MemoryProtection::ReadOnly: native_protection = PAGE_READONLY; break;
	case MemoryProtection::ReadWrite: native_protection = PAGE_READWRITE; break;
	case MemoryProtection::ReadExecute: native_protection = PAGE_EXECUTE_READ; break;
	case MemoryProtection::ReadWriteExecute: native_protection = PAGE_EXECUTE_READWRITE; break;
	}
	DWORD previous = 0;
	return VirtualProtectEx(m_process, address, bytes, native_protection, &previous) != FALSE;
}

bool TaskPort::Query(const void* address, MEMORY_BASIC_INFORMATION& information) const noexcept
{
	if (m_process == nullptr || address == nullptr)
		return false;
	return VirtualQueryEx(m_process, address, &information, sizeof(information)) == sizeof(information);
}

void TaskPort::Reset() noexcept
{
	if (m_process != nullptr) {
		CloseHandle(m_process);
		m_process = nullptr;
	}
	m_process_id = 0;
}

bool MachPort::Send(MachMessage message)
{
	{
		std::lock_guard lock(m_mutex);
		if (m_closed)
			return false;
		m_messages.push_back(std::move(message));
	}
	m_condition.notify_one();
	return true;
}

std::optional<MachMessage> MachPort::Receive(DWORD timeout_ms)
{
	std::unique_lock lock(m_mutex);
	const auto ready = [this] { return m_closed || !m_messages.empty(); };
	if (!m_condition.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready))
		return std::nullopt;
	if (m_messages.empty())
		return std::nullopt;
	auto message = std::move(m_messages.front());
	m_messages.pop_front();
	return message;
}

void MachPort::Close() noexcept
{
	{
		std::lock_guard lock(m_mutex);
		m_closed = true;
	}
	m_condition.notify_all();
}

bool MachPort::IsClosed() const noexcept
{
	std::lock_guard lock(m_mutex);
	return m_closed;
}

Prefix Prefix::Create(const std::wstring& root)
{
	const std::filesystem::path prefix(root);
	EnsureDirectory(prefix);
	EnsureDirectory(prefix / L"private" / L"etc");
	EnsureDirectory(prefix / L"private" / L"var" / L"run");
	EnsureDirectory(prefix / L"private" / L"tmp");
	EnsureDirectory(prefix / L"usr" / L"local");
	EnsureDirectory(prefix / L"Library" / L"Preferences");
	return Prefix(prefix.lexically_normal().wstring());
}

NamedPipeRpcServer NamedPipeRpcServer::Create(const std::wstring& name)
{
	const std::wstring full_name = L"\\\\.\\pipe\\" + name;
	const HANDLE pipe = CreateNamedPipeW(
		full_name.c_str(),
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		1024 * 1024,
		1024 * 1024,
		0,
		nullptr);
	if (pipe == INVALID_HANDLE_VALUE) {
		ThrowLastError("CreateNamedPipeW");
	}
	return NamedPipeRpcServer(full_name, pipe);
}

NamedPipeRpcServer::NamedPipeRpcServer(std::wstring name, HANDLE pipe) noexcept :
	m_name(std::move(name)), m_pipe(pipe)
{
}

NamedPipeRpcServer::NamedPipeRpcServer(NamedPipeRpcServer&& other) noexcept :
	m_name(std::move(other.m_name)), m_pipe(other.m_pipe)
{
	other.m_pipe = INVALID_HANDLE_VALUE;
}

NamedPipeRpcServer& NamedPipeRpcServer::operator=(NamedPipeRpcServer&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_name = std::move(other.m_name);
		m_pipe = other.m_pipe;
		other.m_pipe = INVALID_HANDLE_VALUE;
	}
	return *this;
}

NamedPipeRpcServer::~NamedPipeRpcServer() noexcept
{
	Reset();
}

void NamedPipeRpcServer::WaitForClient()
{
	if (!ConnectNamedPipe(m_pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
		ThrowLastError("ConnectNamedPipe");
	}
}

std::string NamedPipeRpcServer::Read()
{
	return ReadFrame(m_pipe);
}

void NamedPipeRpcServer::Write(const std::string& payload)
{
	WriteFrame(m_pipe, payload);
}

void NamedPipeRpcServer::Reset() noexcept
{
	if (m_pipe != INVALID_HANDLE_VALUE) {
		DisconnectNamedPipe(m_pipe);
		CloseHandle(m_pipe);
		m_pipe = INVALID_HANDLE_VALUE;
	}
}

NamedPipeRpcClient NamedPipeRpcClient::Connect(const std::wstring& name)
{
	const std::wstring full_name = name.rfind(L"\\\\.\\pipe\\", 0) == 0
		? name : L"\\\\.\\pipe\\" + name;
	for (int attempt = 0; attempt != 300; ++attempt) {
		const HANDLE pipe = CreateFileW(full_name.c_str(), GENERIC_READ | GENERIC_WRITE,
			0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) {
			return NamedPipeRpcClient(pipe);
		}
		const DWORD error = GetLastError();
		if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND &&
			error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_NO_DATA) {
			SetLastError(error);
			ThrowLastError("CreateFileW(named pipe)");
		}
		(void)WaitNamedPipeW(full_name.c_str(), 100);
	}
	SetLastError(ERROR_SEM_TIMEOUT);
	ThrowLastError("WaitNamedPipeW(timeout)");
}

NamedPipeRpcClient::NamedPipeRpcClient(NamedPipeRpcClient&& other) noexcept :
	m_pipe(other.m_pipe)
{
	other.m_pipe = INVALID_HANDLE_VALUE;
}

NamedPipeRpcClient& NamedPipeRpcClient::operator=(NamedPipeRpcClient&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_pipe = other.m_pipe;
		other.m_pipe = INVALID_HANDLE_VALUE;
	}
	return *this;
}

NamedPipeRpcClient::~NamedPipeRpcClient() noexcept
{
	Reset();
}

void NamedPipeRpcClient::Write(const std::string& payload)
{
	WriteFrame(m_pipe, payload);
}

std::string NamedPipeRpcClient::Read()
{
	return ReadFrame(m_pipe);
}

void NamedPipeRpcClient::Reset() noexcept
{
	if (m_pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(m_pipe);
		m_pipe = INVALID_HANDLE_VALUE;
	}
}

} // namespace darling::windows_host
