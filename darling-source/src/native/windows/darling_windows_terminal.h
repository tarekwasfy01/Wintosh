/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace darling::windows_host {

class DarwinPseudoTerminal final {
public:
	static std::unique_ptr<DarwinPseudoTerminal> Start(
		const std::wstring& command_line, const std::wstring& working_directory,
		std::uint16_t columns = 80, std::uint16_t rows = 25);

	DarwinPseudoTerminal(const DarwinPseudoTerminal&) = delete;
	DarwinPseudoTerminal& operator=(const DarwinPseudoTerminal&) = delete;
	~DarwinPseudoTerminal() noexcept;

	[[nodiscard]] std::size_t ReadAvailable(void* buffer, std::size_t bytes);
	[[nodiscard]] std::size_t Write(const void* buffer, std::size_t bytes);
	void Resize(std::uint16_t columns, std::uint16_t rows);
	[[nodiscard]] bool Wait(std::uint32_t timeout_ms, std::uint32_t* exit_code);

private:
	DarwinPseudoTerminal(HANDLE pseudo_console, HANDLE input_write,
		HANDLE output_read, PROCESS_INFORMATION process) noexcept;
	void ReadOutputLoop() noexcept;

	HANDLE m_pseudo_console = nullptr;
	HANDLE m_input_write = nullptr;
	HANDLE m_output_read = nullptr;
	PROCESS_INFORMATION m_process{};
	std::mutex m_output_mutex;
	std::vector<char> m_pending_output;
	std::thread m_output_thread;
};

} // namespace darling::windows_host
