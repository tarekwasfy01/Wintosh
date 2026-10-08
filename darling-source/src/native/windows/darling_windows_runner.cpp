/*
 * Minimal Windows Mach-O runner for the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"

#include <windows.h>

#include <filesystem>
#include <cwchar>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string Utf8(const wchar_t* value)
{
	if (value == nullptr || *value == L'\0') {
		return {};
	}
	const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		value, -1, nullptr, 0, nullptr, nullptr);
	if (required <= 0) {
		throw std::runtime_error("cannot convert Windows argument to UTF-8");
	}
	std::string result(static_cast<std::size_t>(required), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
		result.data(), required, nullptr, nullptr) <= 0) {
		throw std::runtime_error("cannot convert Windows argument to UTF-8");
	}
	result.resize(static_cast<std::size_t>(required - 1));
	return result;
}

std::vector<std::string> Environment()
{
	LPWCH block = GetEnvironmentStringsW();
	if (block == nullptr) {
		throw std::runtime_error("cannot read Windows environment");
	}
	std::vector<std::string> result;
	for (const wchar_t* entry = block; *entry != L'\0';
		entry += std::wcslen(entry) + 1) {
		result.push_back(Utf8(entry));
	}
	FreeEnvironmentStringsW(block);
	return result;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) {
		std::wcerr << L"usage: darling_windows_runner.exe <mach-o> [args...]\n";
		return 64;
	}
	try {
		const std::filesystem::path image(argv[1]);
		darling::windows_host::DarwinLaunchOptions options;
		options.prefix = image.parent_path();
		options.environment = Environment();
		for (int index = 1; index < argc; ++index) {
			options.arguments.push_back(Utf8(argv[index]));
		}
		const int result = darling::windows_host::DarwinBootstrap::Run(image, options);
		if (result < 0) {
			return 1;
		}
		return result > 255 ? 255 : result;
	} catch (const std::exception& error) {
		std::cerr << "DARLING_RUNNER_ERROR=" << error.what() << "\n";
		return 1;
	}
}
