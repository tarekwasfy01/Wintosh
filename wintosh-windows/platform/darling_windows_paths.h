/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <filesystem>

namespace darling::windows_host {

class DarwinPaths final {
public:
	[[nodiscard]] static std::filesystem::path CurrentWorkingDirectory();
	[[nodiscard]] static std::filesystem::path AbsolutePath(
		const std::filesystem::path& path);
	static void ChangeWorkingDirectory(const std::filesystem::path& path);
};

} // namespace darling::windows_host
