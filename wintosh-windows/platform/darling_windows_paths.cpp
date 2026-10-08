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

std::filesystem::path DarwinPaths::CanonicalPath(const std::filesystem::path& path)
{
	const HANDLE handle = CreateFileW(path.c_str(), 0,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		ThrowLastError("CreateFileW(canonical path)");
	std::vector<wchar_t> buffer(256);
	for (;;) {
		const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
			static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
		if (length == 0) {
			const DWORD error = GetLastError();
			CloseHandle(handle);
			if (error == ERROR_ACCESS_DENIED)
				return AbsolutePath(path);
			SetLastError(error);
			ThrowLastError("GetFinalPathNameByHandleW");
		}
		if (length < buffer.size()) {
			std::wstring result(buffer.data(), length);
			CloseHandle(handle);
			if (result.rfind(L"\\\\?\\UNC\\", 0) == 0)
				result = L"\\\\" + result.substr(8);
			else if (result.rfind(L"\\\\?\\", 0) == 0)
				result.erase(0, 4);
			return std::filesystem::path(result);
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
