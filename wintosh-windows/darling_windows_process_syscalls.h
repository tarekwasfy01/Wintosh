/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include "darwin_windows_process.h"

#include <filesystem>
#include <string>
#include <vector>

namespace darling::windows_host {

class DarwinProcessSyscalls final {
public:
	DarwinProcessSyscalls() noexcept = default;
	DarwinProcessSyscalls(const DarwinProcessSyscalls&) = delete;
	DarwinProcessSyscalls& operator=(const DarwinProcessSyscalls&) = delete;
	DarwinProcessSyscalls(DarwinProcessSyscalls&&) noexcept = default;
	DarwinProcessSyscalls& operator=(DarwinProcessSyscalls&&) noexcept = default;

	static DarwinProcessSyscalls Spawn(const std::wstring& executable,
		const std::vector<std::wstring>& arguments,
		const std::filesystem::path& working_directory = {});
	static DarwinProcessSyscalls Spawn(const std::wstring& executable,
		const std::vector<std::wstring>& arguments,
		const std::filesystem::path& working_directory,
		const std::vector<std::wstring>& environment);

	[[nodiscard]] DWORD Wait(DWORD timeout_ms = INFINITE) const noexcept;
	[[nodiscard]] DWORD ExitCode() const noexcept;
	[[nodiscard]] bool Valid() const noexcept { return m_process.Valid(); }

private:
	 explicit DarwinProcessSyscalls(Process process) noexcept;
	Process m_process;
};

} // namespace darling::windows_host
