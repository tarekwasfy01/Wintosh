/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_paths.h"
#include "darling_windows_errno.h"

#include <windows.h>

#include <system_error>
#include <vector>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	DarwinErrno::SetFromWin32(GetLastError());
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

} // namespace

std::filesystem::path DarwinPaths::CurrentWorkingDirectory()
{
	std::vector<wchar_t> buffer(256);
	for (;;) {
		const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
		if (length == 0) {
			ThrowLastError("GetCurrentDirectoryW");
		}
		if (length < buffer.size()) {
			return std::filesystem::path(std::wstring(buffer.data(), length));
		}
		buffer.resize(static_cast<std::size_t>(length) + 1);
	}
}

std::filesystem::path DarwinPaths::AbsolutePath(const std::filesystem::path& path)
{
	const std::wstring input = path.wstring();
	std::vector<wchar_t> buffer(256);
	for (;;) {
		const DWORD length = GetFullPathNameW(input.c_str(),
			static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
		if (length == 0) {
			ThrowLastError("GetFullPathNameW");
		}
		if (length < buffer.size()) {
			return std::filesystem::path(std::wstring(buffer.data(), length));
		}
		buffer.resize(static_cast<std::size_t>(length) + 1);
	}
}

void DarwinPaths::ChangeWorkingDirectory(const std::filesystem::path& path)
{
	if (!SetCurrentDirectoryW(path.c_str())) {
		ThrowLastError("SetCurrentDirectoryW");
	}
}

} // namespace darling::windows_host
