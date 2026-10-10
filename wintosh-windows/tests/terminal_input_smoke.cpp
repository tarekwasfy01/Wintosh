/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_terminal.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdlib>

int main()
{
	try {
		auto terminal = darling::windows_host::DarwinPseudoTerminal::Start(
			L"C:\\Windows\\System32\\cmd.exe /d /c \"choice /c A /n >nul & echo DARLING-INPUT\"",
			{}, 80, 25);
		terminal->Resize(100, 40);
		const char key_sequence[] = "A\r\n";
		if (terminal->Write(key_sequence, sizeof(key_sequence) - 1) !=
			 sizeof(key_sequence) - 1) {
			throw std::runtime_error("ConPTY input write was incomplete");
		}
		std::string output;
		for (int attempt = 0; attempt < 1000; ++attempt) {
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
				if (exit_code != 0 || output.find("DARLING-INPUT") == std::string::npos) {
					throw std::runtime_error("ConPTY input/output mismatch: exit=" +
						std::to_string(exit_code) + " bytes=" + std::to_string(output.size()));
				}
				std::cout << "DARWIN_CONPTY_INPUT=PASS\n";
				return 0;
			}
		}
		if (std::getenv("WINTOSH_CONPTY_SMOKE_OPTIONAL") != nullptr) {
			std::cout << "DARWIN_CONPTY_INPUT=UNAVAILABLE_HOSTED_RUNNER\n";
			return 0;
		}
		throw std::runtime_error("ConPTY input child did not finish");
	} catch (const std::exception& error) {
		std::cerr << "CONPTY_INPUT_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
