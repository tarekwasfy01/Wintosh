/*
 * Minimal Windows Mach-O runner for the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"
#include "darling_windows_dyld.h"

#include <windows.h>

#include <filesystem>
#include <cwchar>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
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
		<< L"  --inspect              Inspect a Mach-O without executing it\n"
		<< L"  --prefix <directory>   Set the initial Wintosh prefix\n"
		<< L"  --rpath <directory>    Add a dynamic-library search path\n"
		<< L"  --env KEY=VALUE        Add an environment entry\n"
		<< L"  --                    End options before the Mach-O path\n";
}

void SetEnvironment(std::vector<std::string>& environment, std::string value)
{
	const auto separator = value.find('=');
	if (separator == std::string::npos || separator == 0)
		throw std::runtime_error("--env expects KEY=VALUE");
	const auto key = value.substr(0, separator);
	for (auto& existing : environment) {
		if (existing.size() > key.size() &&
			existing.compare(0, key.size(), key) == 0 &&
			existing[key.size()] == '=') {
			existing = std::move(value);
			return;
		}
	}
	environment.push_back(std::move(value));
}

std::filesystem::path ResolveImage(std::filesystem::path image)
{
	std::error_code error;
	if (!std::filesystem::exists(image, error))
		throw std::runtime_error("Mach-O image does not exist: " + image.string());
	if (!std::filesystem::is_directory(image, error))
		return image;
	if (image.extension() != L".app")
		throw std::runtime_error("image path is a directory, expected a Mach-O file or .app bundle");
	const auto executable = image / L"Contents" / L"MacOS" / image.stem();
	if (std::filesystem::is_regular_file(executable, error))
		return executable;
	throw std::runtime_error(".app bundle has no matching Contents/MacOS executable: " + image.string());
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
		bool inspect = false;
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
			if (!end_options && argument == L"--inspect") {
				inspect = true;
				continue;
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
					SetEnvironment(options.environment, value);
				continue;
			}
			if (!end_options && image.empty() && !argument.empty() && argument[0] == L'-')
				throw std::runtime_error("unknown CLI option: " + Utf8(argv[index]));
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
		image = ResolveImage(image);
		if (!options.arguments.empty())
			options.arguments.front() = Utf8(image.c_str());
		if (options.prefix.empty())
			options.prefix = image.parent_path();
		if (inspect) {
			const auto inspected = darling::windows_host::MachOImage::Open(image.wstring());
			const auto& header = inspected.Header();
			std::wcout << L"path=" << image.wstring() << L"\n"
				<< L"architecture=" << (inspected.Is32Bit() ? L"x86" :
					(header.cpu_type == darling::windows_host::CPU_TYPE_ARM64 ? L"arm64" : L"x86_64")) << L"\n"
				<< L"file_type=" << header.file_type << L"\n"
				<< L"segments=" << inspected.Segments().size() << L"\n"
				<< L"dependencies=" << inspected.Dependencies().size() << L"\n";
			for (const auto& dependency : inspected.Dependencies())
				std::wcout << L"dependency=" << std::wstring(dependency.begin(), dependency.end()) << L"\n";
			const auto graph = darling::windows_host::DylibGraph::Load(
				image, options.rpaths, options.prefix);
			for (const auto& node : graph) {
				std::wcout << L"image=" << node.path.wstring() << L"\n";
				for (const auto& dependency : node.dependencies)
					std::wcout << L"resolved=" << dependency.wstring() << L"\n";
			}
			const auto order = darling::windows_host::DylibGraph::InitializationOrder(
				image, options.rpaths, options.prefix);
			for (const auto& path : order)
				std::wcout << L"init=" << path.wstring() << L"\n";
			return 0;
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
