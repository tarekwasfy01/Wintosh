/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_stdio.h"

#include <windows.h>

#include <cwchar>
#include <vector>

int wmain(int argc, wchar_t** argv)
{
	if (argc > 1 && std::wcscmp(argv[1], L"--child") == 0) {
		darling_windows_exit(23);
	}

	wchar_t executable[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return 1;
	}
	std::vector<wchar_t> command(executable, executable + length);
	const wchar_t suffix[] = L" --child";
	command.insert(command.end(), suffix, suffix + (sizeof(suffix) / sizeof(wchar_t)));

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
		return 2;
	}
	CloseHandle(process.hThread);
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD status = 0;
	const BOOL queried = GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hProcess);
	return queried != FALSE && status == 23 ? 0 : 3;
}
