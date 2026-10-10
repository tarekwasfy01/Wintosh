/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_macho.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void WriteArgcMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	std::vector<std::uint8_t> data(0x400, 0);
#if defined(_WIN64)
	MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 2,
		static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(MainCommand)), 0, 0};
	std::memcpy(data.data(), &header, sizeof(header));
	SegmentCommand64 segment{};
	segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x100;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));
	// Windows x64 ABI: argc is in ECX when ExecuteEntry invokes the Mach-O entry.
	const std::uint8_t entry[] = {0x89, 0xc8, 0xc3}; // mov eax, ecx; ret
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
#else
	MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, MH_EXECUTE, 2,
		static_cast<std::uint32_t>(sizeof(SegmentCommand32) + sizeof(MainCommand)), 0};
	std::memcpy(data.data(), &header, sizeof(header));
	SegmentCommand32 segment{};
	segment.header = {LC_SEGMENT, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x100;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));
	// Win32 cdecl ABI: argc is the first stack argument and the caller cleans up.
	const std::uint8_t entry[] = {0x8b, 0x44, 0x24, 0x04, 0xc3}; // mov eax,[esp+4]; ret
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
#endif
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write runner Mach-O fixture");
	}
}

std::vector<wchar_t> CommandLine(const std::wstring& value)
{
	std::vector<wchar_t> result(value.begin(), value.end());
	result.push_back(L'\0');
	return result;
}

} // namespace

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			return 1;
		}
		const auto image = std::filesystem::path(temp_path) /
			(L"darling-runner-argc-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		WriteArgcMachO(image);
		wchar_t executable[MAX_PATH]{};
		const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
		if (executable_length == 0 || executable_length >= MAX_PATH) {
			std::filesystem::remove(image);
			return 2;
		}
		const auto runner = std::filesystem::path(executable, executable + executable_length).parent_path() /
			L"wintosh.exe";
		const std::wstring command_line = L"\"" + runner.wstring() + L"\" \"" +
			image.wstring() + L"\" one two";
		auto command = CommandLine(command_line);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
			std::filesystem::remove(image);
			return 3;
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
		const std::wstring inspect_command_line = L"\"" + runner.wstring() + L"\" --inspect \"" + image.wstring() + L"\"";
		auto inspect_command = CommandLine(inspect_command_line);
		PROCESS_INFORMATION inspect_process{};
		const BOOL inspect_started = CreateProcessW(nullptr, inspect_command.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &inspect_process);
		DWORD inspect_status = 1;
		BOOL inspect_queried = FALSE;
		if (inspect_started != FALSE) {
			CloseHandle(inspect_process.hThread);
			const DWORD inspect_wait = WaitForSingleObject(inspect_process.hProcess, 5000);
			if (inspect_wait == WAIT_TIMEOUT) {
				TerminateProcess(inspect_process.hProcess, 8);
				WaitForSingleObject(inspect_process.hProcess, INFINITE);
			}
			inspect_queried = GetExitCodeProcess(inspect_process.hProcess, &inspect_status);
			CloseHandle(inspect_process.hProcess);
		}
		std::filesystem::remove(image);
		return queried != FALSE && status == 3 && inspect_queried != FALSE && inspect_status == 0 ? 0 : 4;
	} catch (...) {
		return 5;
	}
}
