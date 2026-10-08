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

void WriteEnvironmentMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	std::vector<std::uint8_t> data(0x400, 0);
#if defined(_WIN64)
	MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 5,
		static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(MainCommand) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand) + sizeof(DyldInfoCommand)), 0, 0};
	std::memcpy(data.data(), &header, sizeof(header));
	SegmentCommand64 segment{};
	segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x200;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));

	const auto symtab_offset = sizeof(header) + sizeof(segment) + sizeof(main);
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(symtab))};
	symtab.symbol_offset = 0x240;
	symtab.symbol_count = 1;
	symtab.string_offset = 0x260;
	symtab.string_bytes = 9;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	const auto dysymtab_offset = symtab_offset + sizeof(symtab);
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, static_cast<std::uint32_t>(sizeof(dysymtab))};
	dysymtab.undefined_symbol_index = 0;
	dysymtab.undefined_symbol_count = 1;
	std::memcpy(data.data() + dysymtab_offset, &dysymtab, sizeof(dysymtab));
	const auto dyld_info_offset = dysymtab_offset + sizeof(dysymtab);
	DyldInfoCommand dyld_info{};
	dyld_info.header = {LC_DYLD_INFO, static_cast<std::uint32_t>(sizeof(dyld_info))};
	dyld_info.binding_offset = 0x300;
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'g', 'e', 't', 'e', 'n', 'v', 0,
		0x51, 0x70, 0x40, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream, sizeof(bind_stream));

	NList64 symbol{};
	symbol.name_offset = 1;
	symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(data.data() + symtab.string_offset + 1, "_getenv", 8);
	const char variable_name[] = "DARLING_RUNNER_ENV";
	std::memcpy(data.data() + 0x250, variable_name, sizeof(variable_name));

	// getenv("DARLING_RUNNER_ENV"); return result != NULL.
	const std::uint8_t entry[] = {
		0x48, 0x8d, 0x0d, 0x49, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x28,
		0x48, 0x8b, 0x05, 0x2e, 0x00, 0x00, 0x00,
		0xff, 0xd0,
		0x48, 0x83, 0xc4, 0x28,
		0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
#else
	MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, MH_EXECUTE, 5,
		static_cast<std::uint32_t>(sizeof(SegmentCommand32) + sizeof(MainCommand) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand) + sizeof(DyldInfoCommand)), 0};
	std::memcpy(data.data(), &header, sizeof(header));
	SegmentCommand32 segment{};
	segment.header = {LC_SEGMENT, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x200;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));
	const auto symtab_offset = sizeof(header) + sizeof(segment) + sizeof(main);
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(symtab))};
	symtab.symbol_offset = 0x240;
	symtab.symbol_count = 1;
	symtab.string_offset = 0x260;
	symtab.string_bytes = 9;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	const auto dysymtab_offset = symtab_offset + sizeof(symtab);
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, static_cast<std::uint32_t>(sizeof(dysymtab))};
	dysymtab.undefined_symbol_index = 0;
	dysymtab.undefined_symbol_count = 1;
	std::memcpy(data.data() + dysymtab_offset, &dysymtab, sizeof(dysymtab));
	const auto dyld_info_offset = dysymtab_offset + sizeof(dysymtab);
	DyldInfoCommand dyld_info{};
	dyld_info.header = {LC_DYLD_INFO, static_cast<std::uint32_t>(sizeof(dyld_info))};
	dyld_info.binding_offset = 0x300;
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'g', 'e', 't', 'e', 'n', 'v', 0,
		0x52, 0x70, 0x40, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream, sizeof(bind_stream));
	NList32 symbol{};
	symbol.name_offset = 1;
	symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(data.data() + symtab.string_offset + 1, "_getenv", 8);
	const char variable_name[] = "DARLING_RUNNER_ENV";
	std::memcpy(data.data() + 0x250, variable_name, sizeof(variable_name));
	// call/pop obtains the x86 entry address; getenv receives the variable name on the stack.
	const std::uint8_t entry[] = {
		0xe8, 0x00, 0x00, 0x00, 0x00, 0x59,
		0x81, 0xc1, 0x4b, 0x00, 0x00, 0x00,
		0x51, 0x8d, 0x51, 0xf0, 0x8b, 0x02, 0xff, 0xd0,
		0x83, 0xc4, 0x04, 0x85, 0xc0, 0x0f, 0x95, 0xc0,
		0x0f, 0xb6, 0xc0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
#endif

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write environment Mach-O fixture");
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
		SetEnvironmentVariableW(L"DARLING_RUNNER_ENV", L"present");
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			return 1;
		}
		const auto image = std::filesystem::path(temp_path) /
			(L"darling-runner-env-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		WriteEnvironmentMachO(image);
		wchar_t executable[MAX_PATH]{};
		const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
		if (executable_length == 0 || executable_length >= MAX_PATH) {
			std::filesystem::remove(image);
			return 2;
		}
		const auto runner = std::filesystem::path(executable, executable + executable_length).parent_path() /
			L"wintosh.exe";
		const std::wstring command_line = L"\"" + runner.wstring() + L"\" \"" + image.wstring() + L"\"";
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
		std::filesystem::remove(image);
		SetEnvironmentVariableW(L"DARLING_RUNNER_ENV", nullptr);
		return queried != FALSE && status == 1 ? 0 : 4;
	} catch (...) {
		SetEnvironmentVariableW(L"DARLING_RUNNER_ENV", nullptr);
		return 5;
	}
}
