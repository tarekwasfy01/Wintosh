/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"
#include "darling_windows_dyld.h"
#include "darling_windows_macho.h"

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void WriteExitMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	std::vector<std::uint8_t> data(0x400, 0);
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
	symtab.string_bytes = 7;
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
		0x11, 0x40, '_', 'e', 'x', 'i', 't', 0, 0x51, 0x70, 0x40, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream, sizeof(bind_stream));

	NList64 symbol{};
	symbol.name_offset = 1;
	symbol.type = 0x01;
	symbol.section = 0;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(data.data() + symtab.string_offset + 1, "_exit", 6);

	// mov ecx, 23; sub rsp, 0x28; mov rax, [rip + 0x30]; call rax.
	const std::uint8_t entry[] = {
		0xb9, 0x17, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x28,
		0x48, 0x8b, 0x05, 0x30, 0x00, 0x00, 0x00,
		0xff, 0xd0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write Mach-O exit fixture");
	}
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
	try {
#if !defined(_WIN64)
		// The fixture is an x86-64 Mach-O execution proof; Win32 is metadata-only.
		return 0;
#else
		if (argc > 2 && std::wcscmp(argv[1], L"--child") == 0) {
			if (argc > 3) {
				std::ofstream diagnostic(argv[3], std::ios::trunc);
				diagnostic << "before-bootstrap";
			}
			darling::windows_host::DarwinLaunchOptions options;
			const int result = darling::windows_host::DarwinBootstrap::Run(argv[2], options);
			if (argc > 3) {
				std::ofstream diagnostic(argv[3], std::ios::trunc);
				diagnostic << "after-bootstrap:" << result;
			}
			return result;
		}

		wchar_t temp_path[MAX_PATH]{};
		const DWORD length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) {
			return 1;
		}
		const auto image = std::filesystem::path(temp_path) /
			(L"darling-macho-exit-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		const auto diagnostic_path = image.wstring() + L".error";
		WriteExitMachO(image);

		wchar_t executable[MAX_PATH]{};
		const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
		if (executable_length == 0 || executable_length >= MAX_PATH) {
			std::filesystem::remove(image);
			return 2;
		}
		const std::wstring command_line = L"\"" + std::wstring(executable, executable_length) +
			L"\" --child \"" + image.wstring() + L"\" \"" + diagnostic_path + L"\"";
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
		if (queried == FALSE || status != 23) {
			std::string diagnostic_text;
			{
				std::ifstream diagnostic(diagnostic_path);
				diagnostic_text.assign((std::istreambuf_iterator<char>(diagnostic)),
					std::istreambuf_iterator<char>());
			}
			std::cerr << "MACHO_EXIT_CHILD_STATUS=" << status << "\n";
			if (!diagnostic_text.empty()) {
				std::cerr << "MACHO_EXIT_CHILD_ERROR=" << diagnostic_text << "\n";
			}
			std::filesystem::remove(diagnostic_path);
			return 4;
		}
		std::filesystem::remove(diagnostic_path);
		return 0;
#endif
	} catch (const std::exception& error) {
		if (argc > 3) {
			std::ofstream diagnostic(argv[3], std::ios::trunc);
			diagnostic << error.what();
		}
		std::cerr << "MACHO_EXIT_CHILD_ERROR=" << error.what() << "\n";
		return 6;
	} catch (...) {
		if (argc > 3) {
			std::ofstream diagnostic(argv[3], std::ios::trunc);
			diagnostic << "unknown";
		}
		std::cerr << "MACHO_EXIT_CHILD_ERROR=unknown\n";
		return 7;
	}
}
