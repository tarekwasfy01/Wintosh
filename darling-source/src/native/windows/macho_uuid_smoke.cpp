/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "darling_windows_macho.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>

int wmain()
{
	wchar_t temp[MAX_PATH]{};
	const DWORD length = GetTempPathW(MAX_PATH, temp);
	if (length == 0 || length >= MAX_PATH)
		return 2;
	const auto path = std::wstring(temp) + L"darling-macho-uuid-smoke.bin";
	struct UUIDCommand final {
		std::uint32_t command;
		std::uint32_t command_bytes;
		std::array<std::uint8_t, 16> uuid;
	};
	struct LinkeditCommand final {
		std::uint32_t command;
		std::uint32_t command_bytes;
		std::uint32_t data_offset;
		std::uint32_t data_size;
	};
	const UUIDCommand command{darling::windows_host::LC_UUID, 24,
		{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
	darling::windows_host::MachHeader64 header{};
	header.magic = darling::windows_host::MH_MAGIC_64;
	header.cpu_type = darling::windows_host::CPU_TYPE_X86_64;
	header.cpu_subtype = 3;
	header.file_type = darling::windows_host::MH_DYLIB;
	header.command_count = 2;
	header.command_bytes = sizeof(command) + sizeof(LinkeditCommand);
	const LinkeditCommand signature{darling::windows_host::LC_CODE_SIGNATURE, 16,
		static_cast<std::uint32_t>(sizeof(header) + sizeof(command) + sizeof(LinkeditCommand)), 4};
	{
		std::ofstream output(path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(&header), sizeof(header));
		output.write(reinterpret_cast<const char*>(&command), sizeof(command));
		output.write(reinterpret_cast<const char*>(&signature), sizeof(signature));
		output.write("SIGN", 4);
	}
	try {
		const auto image = darling::windows_host::MachOImage::Open(path);
		if (image.UUID() != command.uuid) {
			DeleteFileW(path.c_str());
			std::cerr << "MACHO_UUID_ERROR=mismatch\n";
			return 3;
		}
		DeleteFileW(path.c_str());
		const auto invalid_path = std::wstring(temp) + L"darling-macho-uuid-invalid.bin";
		const LinkeditCommand invalid_signature{darling::windows_host::LC_CODE_SIGNATURE, 16,
			0xfffffff0u, 4};
		{
			std::ofstream output(invalid_path, std::ios::binary);
			output.write(reinterpret_cast<const char*>(&header), sizeof(header));
			output.write(reinterpret_cast<const char*>(&command), sizeof(command));
			output.write(reinterpret_cast<const char*>(&invalid_signature), sizeof(invalid_signature));
		}
		bool rejected = false;
		try {
			(void)darling::windows_host::MachOImage::Open(invalid_path);
		} catch (const std::exception&) {
			rejected = true;
		}
		DeleteFileW(invalid_path.c_str());
		if (!rejected) {
			std::cerr << "MACHO_UUID_ERROR=invalid signature accepted\n";
			return 5;
		}
		std::cout << "MACHO_UUID=PASS MACHO_CODE_SIGNATURE_RANGE=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		DeleteFileW(path.c_str());
		std::cerr << "MACHO_UUID_ERROR=" << error.what() << "\n";
		return 4;
	}
}
