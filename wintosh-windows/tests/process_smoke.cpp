/*
 * Stage 1 smoke test for the Darling Windows host process boundary.
 * GPL-3.0-only; see ../../../../../../licenses and the Darling source
 * license bundle.
 */

#include "darwin_windows_process.h"

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>

int wmain()
{
	wchar_t* command_shell = nullptr;
	if (_wdupenv_s(&command_shell, nullptr, L"ComSpec") != 0 || command_shell == nullptr) {
		std::cerr << "ComSpec is not defined\n";
		return 2;
	}

	auto child = darling::windows_host::Process::Launch(
		command_shell,
		std::wstring(L"\"") + command_shell + L"\" /c exit 7");

	if (!child.Valid() || child.Wait(10000) != WAIT_OBJECT_0) {
		std::cerr << "child did not terminate\n";
		return 3;
	}

	const auto exit_code = child.ExitCode();
	if (exit_code != 7) {
		std::cerr << "PROCESS_SMOKE_ERROR=unexpected exit=" << exit_code << "\n";
		return 4;
	}
	const std::wstring long_command = std::wstring(L"\"") + command_shell +
		L"\" /c \"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoProfile -Command Start-Sleep -Seconds 30\"";
	auto terminated_child = darling::windows_host::Process::Launch(command_shell, long_command);
	if (!terminated_child.Valid()) {
		std::cerr << "PROCESS_SMOKE_ERROR=termination child invalid\n";
		return 5;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	terminated_child.Terminate(23);
	if (terminated_child.Wait(10000) != WAIT_OBJECT_0 || terminated_child.ExitCode() != 23) {
		std::cerr << "PROCESS_SMOKE_ERROR=job termination failed exit=" <<
			terminated_child.ExitCode() << "\n";
		return 6;
	}
	free(command_shell);
	std::cout << "PROCESS_SMOKE_EXIT=" << exit_code << " PROCESS_TERMINATE=PASS\n";
	return 0;
}
