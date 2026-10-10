/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_filesystem.h"
#include "darling_windows_errno.h"

#include <windows.h>

#include <cwchar>
#include <system_error>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	DarwinErrno::SetFromWin32(GetLastError());
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

std::uint64_t FileTimeNanoseconds(const FILETIME& value)
{
	ULARGE_INTEGER ticks{};
	ticks.LowPart = value.dwLowDateTime;
	ticks.HighPart = value.dwHighDateTime;
	constexpr std::uint64_t epoch_offset = 11644473600ull * 10'000'000ull;
	return ticks.QuadPart < epoch_offset ? 0 : (ticks.QuadPart - epoch_offset) * 100ull;
}

} // namespace

DarwinFileInfo DarwinFilesystem::Stat(const std::filesystem::path& path)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
		ThrowLastError("GetFileAttributesExW");
	}
	ULARGE_INTEGER size{};
	size.LowPart = attributes.nFileSizeLow;
	size.HighPart = attributes.nFileSizeHigh;
	return {size.QuadPart, FileTimeNanoseconds(attributes.ftLastWriteTime),
		(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
}

std::vector<std::wstring> DarwinFilesystem::ListDirectory(
	const std::filesystem::path& path)
{
	const auto pattern = path / L"*";
	WIN32_FIND_DATAW data{};
	const HANDLE search = FindFirstFileW(pattern.c_str(), &data);
	if (search == INVALID_HANDLE_VALUE) {
		ThrowLastError("FindFirstFileW");
	}
	std::vector<std::wstring> entries;
	for (;;) {
		if (std::wcscmp(data.cFileName, L".") != 0 &&
			std::wcscmp(data.cFileName, L"..") != 0) {
			entries.emplace_back(data.cFileName);
		}
		if (!FindNextFileW(search, &data)) {
			const DWORD error = GetLastError();
			if (error != ERROR_NO_MORE_FILES) {
				FindClose(search);
				SetLastError(error);
				ThrowLastError("FindNextFileW");
			}
			break;
		}
	}
	FindClose(search);
	return entries;
}

void DarwinFilesystem::MakeDirectory(const std::filesystem::path& path)
{
	if (!CreateDirectoryW(path.c_str(), nullptr)) {
		ThrowLastError("CreateDirectoryW");
	}
}

void DarwinFilesystem::Rename(const std::filesystem::path& source,
	const std::filesystem::path& destination)
{
	if (!MoveFileExW(source.c_str(), destination.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		const DWORD error = GetLastError();
		// Some Windows filesystems reject MOVEFILE_WRITE_THROUGH for ordinary
		// user-temp files. Preserve the rename contract with a copy/remove
		// fallback in that narrow case; normal volumes use the atomic move above.
		if (error != ERROR_ACCESS_DENIED ||
			!CopyFileW(source.c_str(), destination.c_str(), FALSE) ||
			!DeleteFileW(source.c_str())) {
			SetLastError(error);
			ThrowLastError("MoveFileExW");
		}
	}
}

void DarwinFilesystem::Unlink(const std::filesystem::path& path)
{
	if (!DeleteFileW(path.c_str())) {
		ThrowLastError("DeleteFileW");
	}
}

} // namespace darling::windows_host
