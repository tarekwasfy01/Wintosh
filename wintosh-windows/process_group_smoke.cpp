/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */
#include "darwin_windows_process.h"

#include <cstdlib>
#include <iostream>
#include <string>

int wmain()
{
	wchar_t* shell = nullptr;
	if (_wdupenv_s(&shell, nullptr, L"ComSpec") != 0 || shell == nullptr) {
		std::cerr << "ComSpec is not defined\n";
		return 2;
	}

	try {
		darling::windows_host::ProcessGroup group;
		const std::wstring command = std::wstring(L"\"") + shell +
			L"\" /c \"ping.exe 127.0.0.1 -n 30 >nul\"";
		auto first = darling::windows_host::Process::LaunchInGroup(group, shell, command);
		auto second = darling::windows_host::Process::LaunchInGroup(group, shell, command);
		if (!group.Valid() || !first.Valid() || !second.Valid()) {
			std::cerr << "PROCESS_GROUP_ERROR=invalid\n";
			free(shell);
			return 3;
		}
		group.Terminate(29);
		const auto first_wait = first.Wait(10000);
		const auto second_wait = second.Wait(10000);
		const auto first_exit = first.ExitCode();
		const auto second_exit = second.ExitCode();
		free(shell);
		if (first_wait != WAIT_OBJECT_0 || second_wait != WAIT_OBJECT_0 ||
			first_exit != 29 || second_exit != 29) {
			std::cerr << "PROCESS_GROUP_ERROR=termination first=" << first_exit <<
				" second=" << second_exit << "\n";
			return 4;
		}
		std::cout << "PROCESS_GROUP_TERMINATE=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		free(shell);
		std::cerr << "PROCESS_GROUP_ERROR=" << error.what() << "\n";
		return 5;
	}
}
