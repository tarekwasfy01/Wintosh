/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <functional>

namespace darling::windows_host {

class TlsSlot final {
public:
	TlsSlot();
	TlsSlot(const TlsSlot&) = delete;
	TlsSlot& operator=(const TlsSlot&) = delete;
	TlsSlot(TlsSlot&& other) noexcept;
	TlsSlot& operator=(TlsSlot&& other) noexcept;
	~TlsSlot() noexcept;

	void Set(void* value) const;
	[[nodiscard]] void* Get() const noexcept;

private:
	void Reset() noexcept;
	DWORD m_index = TLS_OUT_OF_INDEXES;
};

class Thread final {
public:
	static Thread Start(std::function<void()> entry);
	Thread(const Thread&) = delete;
	Thread& operator=(const Thread&) = delete;
	Thread(Thread&& other) noexcept;
	Thread& operator=(Thread&& other) noexcept;
	~Thread() noexcept;

	[[nodiscard]] DWORD Join(DWORD timeout_ms = INFINITE) const noexcept;
	[[nodiscard]] DWORD ExitCode() const noexcept;
	[[nodiscard]] DWORD Id() const noexcept { return m_id; }

private:
	struct StartData final {
		std::function<void()> entry;
	};

	Thread(HANDLE handle, DWORD id) noexcept : m_handle(handle), m_id(id) {}
	static DWORD WINAPI Trampoline(void* argument) noexcept;
	void Reset() noexcept;

	HANDLE m_handle = nullptr;
	DWORD m_id = 0;
};

} // namespace darling::windows_host
