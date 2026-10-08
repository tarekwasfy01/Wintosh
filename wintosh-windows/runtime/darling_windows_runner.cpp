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
#include <stdexcept>
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

void Usage()
{
	std::wcerr << L"usage: wintosh.exe [options] <mach-o> [args...]\n"
		<< L"\nOptions:\n"
		<< L"  --help                 Show this help\n"
		<< L"  --version              Show the Wintosh version\n"
		<< L"  --prefix <directory>   Set the initial Wintosh prefix\n"
		<< L"  --rpath <directory>    Add a dynamic-library search path\n"
		<< L"  --env KEY=VALUE        Add an environment entry\n"
		<< L"  --                    End options before the Mach-O path\n";
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2) {
		Usage();
		return 64;
	}
	try {
		darling::windows_host::DarwinLaunchOptions options;
		options.environment = Environment();
		std::filesystem::path image;
		bool end_options = false;
		for (int index = 1; index < argc; ++index) {
			const std::wstring argument(argv[index]);
			if (!end_options && argument == L"--help") {
				Usage();
				return 0;
			}
			if (!end_options && argument == L"--version") {
				std::wcout << L"Wintosh 0.1.1\n";
				return 0;
			}
			if (!end_options && argument == L"--") {
				end_options = true;
				continue;
			}
			if (!end_options && (argument == L"--prefix" ||
				argument == L"--rpath" || argument == L"--env")) {
				if (++index >= argc)
					throw std::runtime_error("missing value for CLI option");
				const auto value = Utf8(argv[index]);
				if (argument == L"--prefix")
					options.prefix = std::filesystem::path(argv[index]);
				else if (argument == L"--rpath")
					options.rpaths.emplace_back(argv[index]);
				else
					options.environment.push_back(value);
				continue;
			}
			if (image.empty()) {
				image = std::filesystem::path(argv[index]);
				options.arguments.push_back(Utf8(argv[index]));
				continue;
			}
			options.arguments.push_back(Utf8(argv[index]));
		}
		if (image.empty()) {
			Usage();
			return 64;
		}
		if (options.prefix.empty())
			options.prefix = image.parent_path();
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
