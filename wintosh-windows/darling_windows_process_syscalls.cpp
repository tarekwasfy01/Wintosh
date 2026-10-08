/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_process_syscalls.h"

namespace darling::windows_host {

namespace {

std::wstring QuoteArgument(const std::wstring& value)
{
	if (!value.empty() && value.find_first_of(L" \t\"") == std::wstring::npos) {
		return value;
	}
	std::wstring result = L"\"";
	std::size_t backslashes = 0;
	for (const wchar_t character : value) {
		if (character == L'\\') {
			++backslashes;
		} else if (character == L'\"') {
			result.append(backslashes * 2 + 1, L'\\');
			result.push_back(L'\"');
			backslashes = 0;
		} else {
			result.append(backslashes, L'\\');
			backslashes = 0;
			result.push_back(character);
		}
	}
	result.append(backslashes * 2, L'\\');
	result.push_back(L'\"');
	return result;
}

} // namespace

DarwinProcessSyscalls::DarwinProcessSyscalls(Process process) noexcept :
	m_process(std::move(process))
{
}

DarwinProcessSyscalls DarwinProcessSyscalls::Spawn(const std::wstring& executable,
	const std::vector<std::wstring>& arguments,
	const std::filesystem::path& working_directory)

{
	return Spawn(executable, arguments, working_directory, {});
}

DarwinProcessSyscalls DarwinProcessSyscalls::Spawn(const std::wstring& executable,
	const std::vector<std::wstring>& arguments,
	const std::filesystem::path& working_directory,
	const std::vector<std::wstring>& environment)
{
	std::wstring command_line = QuoteArgument(executable);
	for (const auto& argument : arguments) {
		command_line.push_back(L' ');
		command_line += QuoteArgument(argument);
	}
	return DarwinProcessSyscalls(Process::Launch(executable, command_line,
		working_directory.wstring(), environment));
}

DWORD DarwinProcessSyscalls::Wait(DWORD timeout_ms) const noexcept
{
	return m_process.Wait(timeout_ms);
}

DWORD DarwinProcessSyscalls::ExitCode() const noexcept
{
	return m_process.ExitCode();
}

} // namespace darling::windows_host
