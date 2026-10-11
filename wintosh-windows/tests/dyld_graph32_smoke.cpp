/*
 * 32-bit Mach-O dependency graph proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_dyld.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace darling::windows_host;

static void WriteImage(const std::filesystem::path& path, const char* dependency,
	bool dylib)
{
	const auto name_bytes = dependency == nullptr ? 0u :
		static_cast<std::uint32_t>(std::strlen(dependency) + 1);
	const auto dylib_bytes = dependency == nullptr ? 0u :
		static_cast<std::uint32_t>((sizeof(DylibCommand) + name_bytes + 7u) & ~7u);
	const auto main_bytes = dylib ? 0u : static_cast<std::uint32_t>(sizeof(MainCommand));
	MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3,
		dylib ? MH_DYLIB : MH_EXECUTE, dependency == nullptr ? 1u : 3u,
		static_cast<std::uint32_t>(sizeof(SegmentCommand32) + dylib_bytes + main_bytes), 0};
	SegmentCommand32 segment{};
	segment.header = {LC_SEGMENT, sizeof(SegmentCommand32)};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0;
	segment.file_size = 0x100;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::vector<std::uint8_t> bytes(0x100, 0);
	std::size_t cursor = 0;
	std::memcpy(bytes.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
	std::memcpy(bytes.data() + cursor, &segment, sizeof(segment)); cursor += sizeof(segment);
	if (dependency != nullptr) {
		std::vector<std::uint8_t> command(dylib_bytes, 0);
		DylibCommand dylib_command{};
		dylib_command.header = {LC_LOAD_DYLIB, dylib_bytes};
		dylib_command.name_offset = sizeof(DylibCommand);
		std::memcpy(command.data(), &dylib_command, sizeof(dylib_command));
		std::memcpy(command.data() + dylib_command.name_offset, dependency, name_bytes);
		std::memcpy(bytes.data() + cursor, command.data(), command.size());
		cursor += command.size();
	}
	if (!dylib) {
		MainCommand main_command{};
		main_command.header = {LC_MAIN, sizeof(MainCommand)};
		main_command.entry_offset = 0x80;
		std::memcpy(bytes.data() + cursor, &main_command, sizeof(main_command));
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
}

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) return 2;
		const auto root = std::filesystem::path(temp_path) /
			(L"wintosh-dyld-graph32-" + std::to_wstring(GetCurrentProcessId()));
		std::filesystem::create_directories(root);
		const auto main_path = root / "Main32.macho";
		const auto provider_path = root / "Provider32.dylib";
		WriteImage(provider_path, nullptr, true);
		WriteImage(main_path, "@loader_path/Provider32.dylib", false);
		const auto graph = DylibGraph::Load(main_path, {}, root);
		const bool ok = graph.size() == 2;
		DeleteFileW(main_path.c_str());
		DeleteFileW(provider_path.c_str());
		RemoveDirectoryW(root.c_str());
		std::cout << "DYLD_GRAPH_32=" << (ok ? "PASS" : "FAIL") << "\n";
		return ok ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
}
