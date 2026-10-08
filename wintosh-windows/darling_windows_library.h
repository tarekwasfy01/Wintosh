/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <string>

namespace darling::windows_host {

class Library final {
public:
	static Library Open(const std::wstring& path);
	Library(const Library&) = delete;
	Library& operator=(const Library&) = delete;
	Library(Library&& other) noexcept;
	Library& operator=(Library&& other) noexcept;
	~Library() noexcept;

	[[nodiscard]] FARPROC Symbol(const char* name) const;
	[[nodiscard]] HMODULE Handle() const noexcept { return m_module; }

private:
	explicit Library(HMODULE module) noexcept : m_module(module) {}
	void Reset() noexcept;

	HMODULE m_module = nullptr;
};

} // namespace darling::windows_host

