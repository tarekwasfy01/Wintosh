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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path marker_path;

void AppendMarker(wchar_t marker)
{
	const HANDLE file = CreateFileW(marker_path.c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;
	SetFilePointer(file, 0, nullptr, FILE_END);
	const char value = static_cast<char>(marker);
	DWORD written = 0;
	WriteFile(file, &value, 1, &written, nullptr);
	CloseHandle(file);
}

void HandlerA()
{
	AppendMarker(L'A');
}

void HandlerB()
{
	AppendMarker(L'B');
}

void CxaHandler(void* argument)
{
	AppendMarker(*static_cast<wchar_t*>(argument));
}

std::vector<wchar_t> CommandLine(const std::wstring& value)
{
	std::vector<wchar_t> result(value.begin(), value.end());
	result.push_back(L'\0');
	return result;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	if (argc > 2 && std::wcscmp(argv[1], L"--child") == 0) {
		marker_path = argv[2];
		wchar_t cxa_marker_a = L'A';
		wchar_t cxa_marker_c = L'C';
		int dso_one = 1;
		int dso_two = 2;
		if (darling_windows_host_symbol("_atexit") == 0 ||
			darling_windows_host_symbol("___cxa_atexit") == 0 ||
			darling_windows_host_symbol("___cxa_finalize") == 0 ||
			darling_windows_cxa_atexit(&CxaHandler, &cxa_marker_a, &dso_one) != 0 ||
			darling_windows_cxa_atexit(&CxaHandler, &cxa_marker_c, &dso_two) != 0 ||
			darling_windows_atexit(&HandlerB) != 0) {
			return 1;
		}
		darling_windows_cxa_finalize(&dso_one);
		darling_windows_exit(23);
	}

	wchar_t temp_path[MAX_PATH]{};
	const DWORD length = GetTempPathW(MAX_PATH, temp_path);
	if (length == 0 || length >= MAX_PATH) {
		return 2;
	}
	marker_path = std::filesystem::path(temp_path) /
		(L"darling-atexit-" + std::to_wstring(GetCurrentProcessId()) + L".marker");
	std::filesystem::remove(marker_path);
	wchar_t executable[MAX_PATH]{};
	const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
	if (executable_length == 0 || executable_length >= MAX_PATH) {
		return 3;
	}
	const std::wstring command_line = L"\"" + std::wstring(executable, executable_length) +
		L"\" --child \"" + marker_path.wstring() + L"\"";
	auto command = CommandLine(command_line);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
		return 4;
	}
	CloseHandle(process.hThread);
	const DWORD wait_result = WaitForSingleObject(process.hProcess, 5000);
	if (wait_result == WAIT_TIMEOUT) {
		TerminateProcess(process.hProcess, 8);
		WaitForSingleObject(process.hProcess, INFINITE);
	}
	DWORD status = 0;
	const BOOL queried = GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hProcess);
	std::string marker;
	{
		std::ifstream input(marker_path);
		marker.assign((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	}
	std::filesystem::remove(marker_path);
	if (queried == FALSE || status != 23 || marker != "ABC") {
		std::cerr << "CXA_STATUS=" << status << " CXA_MARKER=" << marker << "\n";
		return 5;
	}
	return 0;
}
