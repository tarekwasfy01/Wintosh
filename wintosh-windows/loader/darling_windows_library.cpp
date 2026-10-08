/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_library.h"

#include <system_error>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

} // namespace

Library Library::Open(const std::wstring& path)
{
	const auto module = LoadLibraryW(path.c_str());
	if (module == nullptr) {
		ThrowLastError("LoadLibraryW");
	}
	return Library(module);
}

Library::Library(Library&& other) noexcept : m_module(other.m_module)
{
	other.m_module = nullptr;
}

Library& Library::operator=(Library&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_module = other.m_module;
		other.m_module = nullptr;
	}
	return *this;
}

Library::~Library() noexcept
{
	Reset();
}

FARPROC Library::Symbol(const char* name) const
{
	if (m_module == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		ThrowLastError("GetProcAddress");
	}
	const auto symbol = GetProcAddress(m_module, name);
	if (symbol == nullptr) {
		ThrowLastError("GetProcAddress");
	}
	return symbol;
}

void Library::Reset() noexcept
{
	if (m_module != nullptr) {
		FreeLibrary(m_module);
		m_module = nullptr;
	}
}

} // namespace darling::windows_host

