/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

namespace darling::windows_host {

class Mutex final {
public:
	Mutex() noexcept;
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;
	~Mutex() noexcept;

	void Lock() noexcept;
	[[nodiscard]] bool TryLock() noexcept;
	void Unlock() noexcept;

private:
	friend class ConditionVariable;
	CRITICAL_SECTION m_native{};
};

class ConditionVariable final {
public:
	ConditionVariable() noexcept;
	ConditionVariable(const ConditionVariable&) = delete;
	ConditionVariable& operator=(const ConditionVariable&) = delete;

	[[nodiscard]] bool Wait(Mutex& mutex, DWORD timeout_ms = INFINITE) noexcept;
	void NotifyOne() noexcept;
	void NotifyAll() noexcept;

private:
	CONDITION_VARIABLE m_native{};
};

class Semaphore final {
public:
	static Semaphore Create(const std::wstring& name, LONG initial_count, LONG maximum_count);
	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;
	Semaphore(Semaphore&& other) noexcept;
	Semaphore& operator=(Semaphore&& other) noexcept;
	~Semaphore() noexcept;

	[[nodiscard]] bool TryWait(DWORD timeout_ms = 0) const noexcept;
	void Post(LONG release_count = 1);

private:
	 explicit Semaphore(HANDLE handle) noexcept : m_handle(handle) {}
	void Reset() noexcept;
	HANDLE m_handle = nullptr;
};

class SharedMemory final {
public:
	static SharedMemory Create(const std::wstring& name, std::size_t bytes);
	SharedMemory(const SharedMemory&) = delete;
	SharedMemory& operator=(const SharedMemory&) = delete;
	SharedMemory(SharedMemory&& other) noexcept;
	SharedMemory& operator=(SharedMemory&& other) noexcept;
	~SharedMemory() noexcept;

	[[nodiscard]] void* Data() const noexcept { return m_view; }
	[[nodiscard]] std::size_t Size() const noexcept { return m_bytes; }

private:
	SharedMemory(HANDLE mapping, void* view, std::size_t bytes) noexcept;
	void Reset() noexcept;
	HANDLE m_mapping = nullptr;
	void* m_view = nullptr;
	std::size_t m_bytes = 0;
};

} // namespace darling::windows_host
