/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_terminal.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
	try {
		auto terminal = darling::windows_host::DarwinPseudoTerminal::Start(
			L"C:\\Windows\\System32\\cmd.exe /d /c \"echo DARLING-CONPTY\"", {}, 80, 25);
		terminal->Resize(100, 40);
		std::string output;
		for (int attempt = 0; attempt < 100; ++attempt) {
			char buffer[512]{};
			const auto read = terminal->ReadAvailable(buffer, sizeof(buffer));
			output.append(buffer, read);
			std::uint32_t exit_code = 0;
			if (terminal->Wait(20, &exit_code)) {
				for (int drain = 0; drain < 500; ++drain) {
					const auto more = terminal->ReadAvailable(buffer, sizeof(buffer));
					output.append(buffer, more);
					if (more == 0) Sleep(2);
				}
				if (exit_code != 0 || output.find("DARLING-CONPTY") == std::string::npos) {
					throw std::runtime_error("ConPTY child output or exit code mismatch: exit=" +
						std::to_string(exit_code) + " bytes=" + std::to_string(output.size()) +
						" marker=" + std::to_string(output.find("DARLING-CONPTY")));
				}
				std::cout << "DARWIN_CONPTY=PASS\n";
				return 0;
			}
		}
		throw std::runtime_error("ConPTY child did not finish");
	} catch (const std::exception& error) {
		std::cerr << "CONPTY_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
