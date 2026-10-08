/*
 * This file is part of the Darling Windows port.
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3.
 */

#include "darling_windows_stdio.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int wmain()
{
	wchar_t module[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return 2;
	const std::wstring base(module, length);
	const auto slash = base.find_last_of(L"\\/");
	const std::wstring directory = slash == std::wstring::npos ? L"." : base.substr(0, slash);
	const std::string directoryUtf8 = std::filesystem::path(directory).string();
	const std::string source = directoryUtf8 + "\\darling-rename-source.txt";
	const std::string target = directoryUtf8 + "\\darling-rename-target.txt";
	DeleteFileA(source.c_str());
	DeleteFileA(target.c_str());
	{
		std::ofstream output(source, std::ios::binary);
		output << "rename";
	}
	const int result = darling_windows_rename(source.c_str(), target.c_str());
	const bool passed = result == 0 && GetFileAttributesA(target.c_str()) != INVALID_FILE_ATTRIBUTES;
	DeleteFileA(source.c_str());
	DeleteFileA(target.c_str());
	if (!passed) {
		std::cerr << "RENAME_HOST_ERROR=" << GetLastError() << " RESULT=" << result << "\n";
		return 3;
	}
	std::cout << "RENAME_HOST=PASS\n";
	return 0;
}
