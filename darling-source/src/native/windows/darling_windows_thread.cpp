/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_thread.h"

#include <memory>
#include <system_error>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

} // namespace

TlsSlot::TlsSlot()
{
	m_index = TlsAlloc();
	if (m_index == TLS_OUT_OF_INDEXES) {
		ThrowLastError("TlsAlloc");
	}
}

TlsSlot::TlsSlot(TlsSlot&& other) noexcept : m_index(other.m_index)
{
	other.m_index = TLS_OUT_OF_INDEXES;
}

TlsSlot& TlsSlot::operator=(TlsSlot&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_index = other.m_index;
		other.m_index = TLS_OUT_OF_INDEXES;
	}
	return *this;
}

TlsSlot::~TlsSlot() noexcept
{
	Reset();
}

void TlsSlot::Set(void* value) const
{
	if (m_index == TLS_OUT_OF_INDEXES || !TlsSetValue(m_index, value)) {
		ThrowLastError("TlsSetValue");
	}
}

void* TlsSlot::Get() const noexcept
{
	return m_index == TLS_OUT_OF_INDEXES ? nullptr : TlsGetValue(m_index);
}

void TlsSlot::Reset() noexcept
{
	if (m_index != TLS_OUT_OF_INDEXES) {
		TlsFree(m_index);
		m_index = TLS_OUT_OF_INDEXES;
	}
}

Thread Thread::Start(std::function<void()> entry)
{
	auto data = std::make_unique<StartData>(StartData{std::move(entry)});
	DWORD id = 0;
	const auto handle = CreateThread(nullptr, 0, &Thread::Trampoline,
		data.get(), 0, &id);
	if (handle == nullptr) {
		ThrowLastError("CreateThread");
	}
	data.release();
	return Thread(handle, id);
}

DWORD WINAPI Thread::Trampoline(void* argument) noexcept
{
	std::unique_ptr<StartData> data(static_cast<StartData*>(argument));
	try {
		data->entry();
		return 0;
	} catch (...) {
		return 1;
	}
}

Thread::Thread(Thread&& other) noexcept : m_handle(other.m_handle), m_id(other.m_id)
{
	other.m_handle = nullptr;
	other.m_id = 0;
}

Thread& Thread::operator=(Thread&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_handle = other.m_handle;
		m_id = other.m_id;
		other.m_handle = nullptr;
		other.m_id = 0;
	}
	return *this;
}

Thread::~Thread() noexcept
{
	Reset();
}

DWORD Thread::Join(DWORD timeout_ms) const noexcept
{
	return m_handle == nullptr ? WAIT_FAILED : WaitForSingleObject(m_handle, timeout_ms);
}

DWORD Thread::ExitCode() const noexcept
{
	DWORD exit_code = STILL_ACTIVE;
	if (m_handle == nullptr || !GetExitCodeThread(m_handle, &exit_code)) {
		return STILL_ACTIVE;
	}
	return exit_code;
}

void Thread::Reset() noexcept
{
	if (m_handle != nullptr) {
		CloseHandle(m_handle);
		m_handle = nullptr;
	}
	m_id = 0;
}

} // namespace darling::windows_host
