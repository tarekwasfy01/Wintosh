/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_sync.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

} // namespace

Mutex::Mutex() noexcept
{
	InitializeCriticalSection(&m_native);
}

Mutex::~Mutex() noexcept
{
	DeleteCriticalSection(&m_native);
}

void Mutex::Lock() noexcept
{
	EnterCriticalSection(&m_native);
}

bool Mutex::TryLock() noexcept
{
	return TryEnterCriticalSection(&m_native) != FALSE;
}

void Mutex::Unlock() noexcept
{
	LeaveCriticalSection(&m_native);
}

ConditionVariable::ConditionVariable() noexcept
{
	InitializeConditionVariable(&m_native);
}

bool ConditionVariable::Wait(Mutex& mutex, DWORD timeout_ms) noexcept
{
	return SleepConditionVariableCS(&m_native, &mutex.m_native, timeout_ms) != FALSE;
}

void ConditionVariable::NotifyOne() noexcept
{
	WakeConditionVariable(&m_native);
}

void ConditionVariable::NotifyAll() noexcept
{
	WakeAllConditionVariable(&m_native);
}

Semaphore Semaphore::Create(const std::wstring& name, LONG initial_count, LONG maximum_count)
{
	const auto handle = CreateSemaphoreW(nullptr, initial_count, maximum_count, name.c_str());
	if (handle == nullptr) {
		ThrowLastError("CreateSemaphoreW");
	}
	return Semaphore(handle);
}

Semaphore::Semaphore(Semaphore&& other) noexcept : m_handle(other.m_handle)
{
	other.m_handle = nullptr;
}

Semaphore& Semaphore::operator=(Semaphore&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_handle = other.m_handle;
		other.m_handle = nullptr;
	}
	return *this;
}

Semaphore::~Semaphore() noexcept
{
	Reset();
}

bool Semaphore::TryWait(DWORD timeout_ms) const noexcept
{
	return m_handle != nullptr && WaitForSingleObject(m_handle, timeout_ms) == WAIT_OBJECT_0;
}

void Semaphore::Post(LONG release_count)
{
	if (m_handle == nullptr || !ReleaseSemaphore(m_handle, release_count, nullptr)) {
		ThrowLastError("ReleaseSemaphore");
	}
}

void Semaphore::Reset() noexcept
{
	if (m_handle != nullptr) {
		CloseHandle(m_handle);
		m_handle = nullptr;
	}
}

SharedMemory SharedMemory::Create(const std::wstring& name, std::size_t bytes)
{
	if (bytes == 0 || bytes > std::numeric_limits<DWORD>::max() * std::uint64_t{0x100000000ULL}) {
		throw std::length_error("shared memory size is invalid");
	}
	const DWORD high = static_cast<DWORD>(static_cast<std::uint64_t>(bytes) >> 32);
	const DWORD low = static_cast<DWORD>(static_cast<std::uint64_t>(bytes) & 0xffffffffU);
	const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
		high, low, name.c_str());
	if (mapping == nullptr) {
		ThrowLastError("CreateFileMappingW");
	}
	const auto view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
	if (view == nullptr) {
		CloseHandle(mapping);
		ThrowLastError("MapViewOfFile(shared memory)");
	}
	return SharedMemory(mapping, view, bytes);
}

SharedMemory::SharedMemory(HANDLE mapping, void* view, std::size_t bytes) noexcept :
	m_mapping(mapping), m_view(view), m_bytes(bytes)
{
}

SharedMemory::SharedMemory(SharedMemory&& other) noexcept :
	m_mapping(other.m_mapping), m_view(other.m_view), m_bytes(other.m_bytes)
{
	other.m_mapping = nullptr;
	other.m_view = nullptr;
	other.m_bytes = 0;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_mapping = other.m_mapping;
		m_view = other.m_view;
		m_bytes = other.m_bytes;
		other.m_mapping = nullptr;
		other.m_view = nullptr;
		other.m_bytes = 0;
	}
	return *this;
}

SharedMemory::~SharedMemory() noexcept
{
	Reset();
}

void SharedMemory::Reset() noexcept
{
	if (m_view != nullptr) {
		UnmapViewOfFile(m_view);
		m_view = nullptr;
	}
	if (m_mapping != nullptr) {
		CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
	m_bytes = 0;
}

} // namespace darling::windows_host
