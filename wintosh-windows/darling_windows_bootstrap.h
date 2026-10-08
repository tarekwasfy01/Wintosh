/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "darling_windows_macho.h"

namespace darling::windows_host {

struct DarwinLaunchOptions final {
	std::filesystem::path prefix;
	std::vector<std::filesystem::path> rpaths;
	std::vector<std::string> arguments;
	std::vector<std::string> environment;
	std::vector<DyldResolvedBinding> host_bindings;
};

class DarwinBootstrap final {
public:
	[[nodiscard]] static int Run(
		const std::filesystem::path& image_path,
		const DarwinLaunchOptions& options);
};

} // namespace darling::windows_host
