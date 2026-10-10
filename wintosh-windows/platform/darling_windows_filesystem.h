/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace darling::windows_host {

struct DarwinFileInfo final {
	std::uint64_t size = 0;
	std::uint64_t modification_time_ns = 0;
	bool directory = false;
};

class DarwinFilesystem final {
public:
	[[nodiscard]] static DarwinFileInfo Stat(const std::filesystem::path& path);
	[[nodiscard]] static std::vector<std::wstring> ListDirectory(
		const std::filesystem::path& path);
	static void MakeDirectory(const std::filesystem::path& path);
	static void Rename(const std::filesystem::path& source,
		const std::filesystem::path& destination);
	static void Unlink(const std::filesystem::path& path);
};

} // namespace darling::windows_host
