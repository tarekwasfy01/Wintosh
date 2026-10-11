/*
 * Minimal Windows Mach-O runner for the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"
#include "darling_windows_dyld.h"
#include "darling_windows_runtime.h"

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::uint64_t ReadBigEndian(const std::vector<std::uint8_t>& bytes,
	std::size_t offset, std::size_t width)
{
	if (width == 0 || width > sizeof(std::uint64_t) || offset > bytes.size() ||
		width > bytes.size() - offset)
		throw std::runtime_error("invalid binary plist integer");
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < width; ++index)
		value = (value << 8) | bytes[offset + index];
	return value;
}

std::optional<std::string> BinaryPlistExecutable(const std::vector<std::uint8_t>& bytes)
{
	if (bytes.size() < 40 || std::string(bytes.begin(), bytes.begin() + 8) != "bplist00")
		return std::nullopt;
	const auto offset_size = bytes[6];
	const auto reference_size = bytes[7];
	const auto object_count = ReadBigEndian(bytes, bytes.size() - 32, 8);
	const auto top_object = ReadBigEndian(bytes, bytes.size() - 24, 8);
	const auto offset_table = ReadBigEndian(bytes, bytes.size() - 8, 8);
	if (offset_size == 0 || reference_size == 0 || object_count == 0 ||
		top_object >= object_count || offset_table >= bytes.size() ||
		object_count > (bytes.size() - offset_table) / offset_size)
		return std::nullopt;
	auto object_offset = [&](std::uint64_t index) {
		return ReadBigEndian(bytes, static_cast<std::size_t>(offset_table + index * offset_size),
			offset_size);
	};
	auto object_string = [&](std::uint64_t index) -> std::optional<std::string> {
		const auto offset = object_offset(index);
		if (offset >= offset_table || offset >= bytes.size()) return std::nullopt;
		const auto marker = bytes[static_cast<std::size_t>(offset)];
		const auto type = marker >> 4;
		std::uint64_t count = marker & 0x0f;
		std::size_t cursor = static_cast<std::size_t>(offset + 1);
		if (count == 0x0f) {
			if (cursor >= bytes.size() || bytes[cursor] >> 4 != 0x1) return std::nullopt;
			const auto width = std::size_t(1) << (bytes[cursor] & 0x0f);
			count = ReadBigEndian(bytes, cursor + 1, width);
			cursor += 1 + width;
		}
		if (type == 0x5 && count <= bytes.size() - cursor)
			return std::string(reinterpret_cast<const char*>(bytes.data() + cursor),
				static_cast<std::size_t>(count));
		if (type == 0x6 && count <= (bytes.size() - cursor) / 2) {
			std::string result;
			result.reserve(static_cast<std::size_t>(count));
			for (std::uint64_t index = 0; index < count; ++index) {
				const auto code = ReadBigEndian(bytes, cursor + index * 2, 2);
				if (code > 0x7f) return std::nullopt;
				result.push_back(static_cast<char>(code));
			}
			return result;
		}
		return std::nullopt;
	};
	const auto root_offset = object_offset(top_object);
	if (root_offset >= bytes.size() || (bytes[static_cast<std::size_t>(root_offset)] >> 4) != 0xd)
		return std::nullopt;
	const auto root_marker = bytes[static_cast<std::size_t>(root_offset)];
	std::uint64_t pair_count = root_marker & 0x0f;
	std::size_t refs = static_cast<std::size_t>(root_offset + 1);
	if (pair_count == 0x0f) {
		if (refs >= bytes.size() || bytes[refs] >> 4 != 0x1) return std::nullopt;
		const auto width = std::size_t(1) << (bytes[refs] & 0x0f);
		if (width > bytes.size() - refs - 1) return std::nullopt;
		pair_count = ReadBigEndian(bytes, refs + 1, width);
		refs += 1 + width;
	}
	if (refs > bytes.size()) return std::nullopt;
	if (pair_count > (bytes.size() - refs) / (2 * reference_size)) return std::nullopt;
	for (std::uint64_t index = 0; index < pair_count; ++index) {
		const auto key = ReadBigEndian(bytes, refs + static_cast<std::size_t>(index * reference_size),
			reference_size);
		const auto value = ReadBigEndian(bytes, refs + static_cast<std::size_t>(
			(pair_count + index) * reference_size), reference_size);
		if (object_string(key) == std::optional<std::string>("CFBundleExecutable"))
			return object_string(value);
	}
	return std::nullopt;
}

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

void AddDarwinLibraryPaths(const std::vector<std::string>& environment,
	std::vector<std::filesystem::path>& rpaths)
{
	const auto append = [&environment, &rpaths](std::string_view prefix) {
		for (const auto& entry : environment) {
			if (entry.rfind(prefix, 0) != 0)
				continue;
		std::size_t begin = prefix.size();
		while (begin <= entry.size()) {
			const auto semicolon = entry.find(';', begin);
			std::size_t end = semicolon;
			if (end == std::string::npos) {
				end = entry.find(':', begin);
				if (end == begin + 1 && std::isalpha(
					static_cast<unsigned char>(entry[begin])) != 0)
					end = entry.find(':', end + 1);
			}
			const auto value = entry.substr(begin,
				end == std::string::npos ? std::string::npos : end - begin);
			if (!value.empty())
				rpaths.emplace_back(value);
			if (end == std::string::npos)
				break;
			begin = end + 1;
		}
		}
	};
	append("DYLD_LIBRARY_PATH=");
	append("DYLD_FRAMEWORK_PATH=");
	append("DYLD_FALLBACK_LIBRARY_PATH=");
	append("DYLD_FALLBACK_FRAMEWORK_PATH=");
}

void Usage()
{
	std::wcerr << L"usage: wintosh.exe [options] <mach-o> [args...]\n"
		<< L"\nOptions:\n"
		<< L"  --help                 Show this help\n"
		<< L"  --version              Show the Wintosh version\n"
		<< L"  --diagnose-host        Report host and loader capabilities\n"
		<< L"  --init-prefix <dir>    Create a minimal Darwin prefix layout\n"
		<< L"  --inspect              Inspect a Mach-O without executing it\n"
		<< L"  --prefix <directory>   Set the initial Wintosh prefix\n"
		<< L"  --rpath <directory>    Add a dynamic-library search path\n"
		<< L"  --env KEY=VALUE        Add an environment entry\n"
		<< L"  --startup-abi <mode>   Use main or darwin entry ABI (default: main)\n"
		<< L"  --apple-param VALUE    Add a Darwin Apple startup parameter\n"
		<< L"                         WINTOSH_PREFIX may provide a default runtime root\n"
		<< L"  --                    End options before the Mach-O path\n";
}

void DiagnoseHost()
{
	SYSTEM_INFO system{};
	GetNativeSystemInfo(&system);
	const wchar_t* architecture = L"unknown";
	switch (system.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: architecture = L"x86_64"; break;
	case PROCESSOR_ARCHITECTURE_ARM64: architecture = L"arm64"; break;
	case PROCESSOR_ARCHITECTURE_INTEL: architecture = L"x86"; break;
	}
	wchar_t prefix_buffer[32768]{};
	const auto prefix_length = GetEnvironmentVariableW(L"WINTOSH_PREFIX",
		prefix_buffer, static_cast<DWORD>(std::size(prefix_buffer)));
	std::filesystem::path prefix;
	if (prefix_length != 0 && prefix_length < std::size(prefix_buffer))
		prefix.assign(prefix_buffer, prefix_buffer + prefix_length);
	std::wcout << L"host_architecture=" << architecture << L"\n"
		<< L"pointer_bits=" << (sizeof(void*) * 8) << L"\n"
		<< L"mach_o_x86_64=" << (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"yes" : L"no") << L"\n"
		<< L"mach_o_arm64=" << (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"yes" : L"no") << L"\n"
		<< L"mach_o_i386=" << (sizeof(void*) == 4 ? L"inspect-only" : L"fail-closed") << L"\n"
		<< L"prefix_source=" << (prefix.empty() ? L"image-parent-or-explicit" : L"WINTOSH_PREFIX") << L"\n";
	if (!prefix.empty())
		std::wcout << L"prefix=" << prefix.wstring() << L"\n"
			<< L"prefix_exists=" << (std::filesystem::is_directory(prefix) ? L"yes" : L"no") << L"\n";
	std::wcout << L"runtime_scope=loader-bootstrap-and-selected-host-primitives\n"
		<< L"darwin_system_frameworks=not-bundled\n"
		<< L"full_macos_compatibility=no\n";
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
	std::filesystem::path executable_name = image.stem();
	const auto plist = image / L"Contents" / L"Info.plist";
	std::ifstream plist_input(plist, std::ios::binary);
	if (plist_input) {
		const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(plist_input)), {});
		if (const auto binary_value = BinaryPlistExecutable(bytes); binary_value.has_value() &&
			!binary_value->empty() && binary_value->find('/') == std::string::npos &&
			binary_value->find('\\') == std::string::npos) {
			executable_name = std::filesystem::path(*binary_value);
		}
		const std::string contents(bytes.begin(), bytes.end());
		const auto key_position = contents.find("CFBundleExecutable");
		if (key_position != std::string::npos) {
			const auto string_tag_start = contents.find("<string", key_position);
			const auto value_start = string_tag_start == std::string::npos ? string_tag_start :
				contents.find('>', string_tag_start);
			const auto value_end = value_start == std::string::npos ? value_start :
				contents.find("</string>", value_start);
			if (value_start != std::string::npos && value_end != std::string::npos &&
				value_end > value_start && contents.find('<', value_start) >= value_end) {
				const auto value = contents.substr(value_start + 1, value_end - value_start - 1);
				const auto first = value.find_first_not_of(" \t\r\n");
				const auto last = value.find_last_not_of(" \t\r\n");
				const auto trimmed = first == std::string::npos ? std::string{} :
					value.substr(first, last - first + 1);
				if (!trimmed.empty() && trimmed.find('/') == std::string::npos &&
					trimmed.find('\\') == std::string::npos)
					executable_name = std::filesystem::path(trimmed);
			}
		}
	}
	const auto executable = image / L"Contents" / L"MacOS" / executable_name;
	if (std::filesystem::is_regular_file(executable, error))
		return executable;
	throw std::runtime_error(".app bundle has no matching Contents/MacOS executable: " + image.string());
}

std::filesystem::path DefaultPrefix(const std::filesystem::path& image)
{
	wchar_t buffer[32768]{};
	const auto length = GetEnvironmentVariableW(L"WINTOSH_PREFIX", buffer,
		static_cast<DWORD>(std::size(buffer)));
	if (length != 0 && length < std::size(buffer))
		return std::filesystem::path(buffer, buffer + length);
	const auto image_prefix = image.parent_path() / L"darwin-prefix";
	std::error_code error;
	if (std::filesystem::is_directory(image_prefix, error) && !error)
		return image_prefix;
	return image.parent_path();
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	if (argc == 3 && std::wstring(argv[1]) == L"--init-prefix") {
		try {
			const auto prefix = darling::windows_host::Prefix::Create(argv[2]);
			std::wcout << L"prefix=" << prefix.Root() << L"\n"
				<< L"status=created\n"
				<< L"system_libraries=host-resolved\n"
				<< L"apple_frameworks=not-bundled\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << "DARLING_PREFIX_ERROR=" << error.what() << "\n";
			return 1;
		}
	}
	if (argc == 2 && std::wstring(argv[1]) == L"--diagnose-host") {
		DiagnoseHost();
		return 0;
	}
	if (argc < 2) {
		Usage();
		return 64;
	}
	try {
		darling::windows_host::DarwinLaunchOptions options;
		options.environment = Environment();
		AddDarwinLibraryPaths(options.environment, options.rpaths);
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
				argument == L"--rpath" || argument == L"--env" ||
				argument == L"--startup-abi" || argument == L"--apple-param")) {
				if (++index >= argc)
					throw std::runtime_error("missing value for CLI option");
				const auto value = Utf8(argv[index]);
				if (argument == L"--prefix")
					options.prefix = std::filesystem::path(argv[index]);
				else if (argument == L"--rpath")
					options.rpaths.emplace_back(argv[index]);
				else if (argument == L"--env")
					SetEnvironment(options.environment, value);
				else if (argument == L"--apple-param")
					options.apple_parameters.push_back(value);
				else if (value == "main")
					options.entry_abi = darling::windows_host::DarwinEntryAbi::CompatibleMain;
				else if (value == "darwin")
					options.entry_abi = darling::windows_host::DarwinEntryAbi::DarwinStart;
				else
					throw std::runtime_error("--startup-abi expects main or darwin");
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
		options.apple_parameters.insert(options.apple_parameters.begin(),
			"executable_path=" + Utf8(image.c_str()));
		if (!options.arguments.empty())
			options.arguments.front() = Utf8(image.c_str());
		if (options.prefix.empty())
			options.prefix = DefaultPrefix(image);
		if (inspect) {
			const auto inspected = darling::windows_host::MachOImage::Open(image.wstring());
			const auto& header = inspected.Header();
			std::wcout << L"path=" << image.wstring() << L"\n"
				<< L"architecture=" << (inspected.Is32Bit() ? L"x86" :
					(header.cpu_type == darling::windows_host::CPU_TYPE_ARM64 ? L"arm64" : L"x86_64")) << L"\n"
				<< L"file_type=" << header.file_type << L"\n"
				<< L"segments=" << inspected.Segments().size() << L"\n"
				<< L"sections=" << inspected.Sections().size() << L"\n"
				<< L"dependencies=" << inspected.Dependencies().size() << L"\n"
				<< L"code_signature=" << (inspected.HasCodeSignature() ? L"present" : L"absent") << L"\n";
			std::uint32_t signature_offset = 0;
			std::uint32_t signature_size = 0;
			if (inspected.CodeSignatureRange(signature_offset, signature_size))
				std::wcout << L"code_signature_offset=" << signature_offset << L"\n"
					<< L"code_signature_size=" << signature_size << L"\n";
			for (const auto& section : inspected.Sections())
				std::wcout << L"section="
					<< std::wstring(section.segment_name.begin(), section.segment_name.end())
					<< L"," << std::wstring(section.section_name.begin(), section.section_name.end())
					<< L" address=0x" << std::hex << section.address
					<< L" size=0x" << section.size
					<< L" file_offset=0x" << section.file_offset << std::dec << L"\n";
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
