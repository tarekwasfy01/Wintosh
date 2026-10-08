/*
 * This file is part of the Darling Windows port.
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3.
 */

#include "darling_windows_macho.h"

#include <windows.h>

#include <fstream>
#include <iostream>

int wmain()
{
	wchar_t temp[MAX_PATH]{};
	const DWORD length = GetTempPathW(MAX_PATH, temp);
	if (length == 0 || length >= MAX_PATH)
		return 2;
	const auto path = std::wstring(temp) + L"darling-macho-install-name-smoke.bin";
	constexpr char name[] = "/usr/lib/libDemo.dylib";
	constexpr std::uint32_t command_bytes = 24 + sizeof(name);
	darling::windows_host::MachHeader64 header{};
	header.magic = darling::windows_host::MH_MAGIC_64;
	header.cpu_type = darling::windows_host::CPU_TYPE_X86_64;
	header.cpu_subtype = 3;
	header.file_type = darling::windows_host::MH_DYLIB;
	header.command_count = 1;
	header.command_bytes = command_bytes;
	darling::windows_host::DylibCommand command{};
	command.header = {darling::windows_host::LC_ID_DYLIB, command_bytes};
	command.name_offset = 24;
	{
		std::ofstream output(path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(&header), sizeof(header));
		output.write(reinterpret_cast<const char*>(&command), sizeof(command));
		output.write(name, sizeof(name));
	}
	try {
		const auto image = darling::windows_host::MachOImage::Open(path);
		const bool pass = image.InstallName() == name;
		DeleteFileW(path.c_str());
		if (!pass) {
			std::cerr << "MACHO_INSTALL_NAME_ERROR=mismatch\n";
			return 3;
		}
		std::cout << "MACHO_INSTALL_NAME=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		DeleteFileW(path.c_str());
		std::cerr << "MACHO_INSTALL_NAME_ERROR=" << error.what() << "\n";
		return 4;
	}
}
