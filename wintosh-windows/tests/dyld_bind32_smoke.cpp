/*
 * 32-bit Mach-O provider-symbol binding proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_dyld.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace darling::windows_host;

static void WriteBindImage(const std::filesystem::path& path, bool provider)
{
	const char* dependency = "@loader_path/Provider32.dylib";
	const auto dependency_bytes = provider ? 0u :
		static_cast<std::uint32_t>(std::strlen(dependency) + 1);
	const auto dylib_bytes = provider ? 0u :
		static_cast<std::uint32_t>((sizeof(DylibCommand) + dependency_bytes + 7u) & ~7u);
	const auto main_bytes = 0u;
	constexpr std::uint32_t thread_bytes = 16 + 16 * sizeof(std::uint32_t);
	constexpr std::uint32_t main_segment_bytes = sizeof(SegmentCommand32) + sizeof(Section32) + 4u;
	const auto commands = static_cast<std::uint32_t>(sizeof(SegmentCommand32) +
		(provider ? 0u : main_segment_bytes - sizeof(SegmentCommand32)) +
		main_bytes + dylib_bytes + sizeof(SymtabCommand) + sizeof(DYSymtabCommand) +
		(provider ? 0u : thread_bytes));
	MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, provider ? MH_DYLIB : MH_EXECUTE,
		provider ? 3u : 5u, commands, 0};
	SegmentCommand32 segment{};
	segment.header = {LC_SEGMENT, sizeof(SegmentCommand32)};
	if (!provider)
		segment.header.command_bytes = main_segment_bytes;
	segment.section_count = provider ? 0u : 1u;
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000; segment.vm_size = 0x1000;
	segment.file_size = 0x300; segment.max_protection = 5;
	segment.initial_protection = 5;
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, sizeof(SymtabCommand)};
	symtab.symbol_offset = 0x220; symtab.symbol_count = 1;
	symtab.string_offset = 0x230; symtab.string_bytes = 16;
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, sizeof(DYSymtabCommand)};
	if (!provider) {
		dysymtab.undefined_symbol_index = 0;
		dysymtab.indirect_symbol_offset = 0x250;
		dysymtab.indirect_symbol_count = 1;
	}
	std::vector<std::uint8_t> bytes(0x300, 0);
	std::size_t cursor = 0;
	std::memcpy(bytes.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
	std::memcpy(bytes.data() + cursor, &segment, sizeof(segment)); cursor += sizeof(segment);
	if (!provider) {
		Section32 pointer_section{};
		std::memcpy(pointer_section.section_name, "__la_symbol_ptr", 15);
		std::memcpy(pointer_section.segment_name, "__TEXT", 6);
		pointer_section.address = 0x1180;
		pointer_section.size = sizeof(std::uint32_t);
		pointer_section.file_offset = 0x180;
		std::memcpy(bytes.data() + cursor, &pointer_section, sizeof(pointer_section));
		cursor += sizeof(pointer_section) + 4;
	}
	if (!provider) {
		std::vector<std::uint8_t> command(dylib_bytes, 0);
		DylibCommand dylib_command{};
		dylib_command.header = {LC_LOAD_DYLIB, dylib_bytes};
		dylib_command.name_offset = sizeof(DylibCommand);
		std::memcpy(command.data(), &dylib_command, sizeof(dylib_command));
		std::memcpy(command.data() + dylib_command.name_offset, dependency, dependency_bytes);
		std::memcpy(bytes.data() + cursor, command.data(), command.size());
		cursor += command.size();
	}
	std::memcpy(bytes.data() + cursor, &symtab, sizeof(symtab)); cursor += sizeof(symtab);
	std::memcpy(bytes.data() + cursor, &dysymtab, sizeof(dysymtab));
	cursor += sizeof(dysymtab);
	if (!provider) {
		std::vector<std::uint8_t> thread(thread_bytes, 0);
		const LoadCommand thread_header{LC_UNIXTHREAD, thread_bytes};
		const std::uint32_t flavor = 1;
		const std::uint32_t count = 16;
		const std::uint32_t eip = 0x1100;
		std::memcpy(thread.data(), &thread_header, sizeof(thread_header));
		std::memcpy(thread.data() + 8, &flavor, sizeof(flavor));
		std::memcpy(thread.data() + 12, &count, sizeof(count));
		std::memcpy(thread.data() + 16 + 10 * sizeof(std::uint32_t), &eip, sizeof(eip));
		std::memcpy(bytes.data() + cursor, thread.data(), thread.size());
	}
	NList32 symbol{};
	symbol.name_offset = 1;
	symbol.type = provider ? 0x0f : 0x01; // N_EXT|N_SECT or N_EXT|N_UNDF
	symbol.section = provider ? 1 : 0;
	symbol.value = provider ? 0x1100 : 0;
	std::memcpy(bytes.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(bytes.data() + symtab.string_offset, "\0_probe\0", 8);
	if (!provider) {
		const std::uint32_t indirect_symbol_index = 0;
		std::memcpy(bytes.data() + dysymtab.indirect_symbol_offset,
			&indirect_symbol_index, sizeof(indirect_symbol_index));
	}
	if (provider) {
		const std::uint8_t code[] = {0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3};
		std::memcpy(bytes.data() + 0x100, code, sizeof(code));
	} else {
		const std::uint8_t code[] = {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x4c,
			0x24, 0x0c, 0x8b, 0x09, 0x0f, 0xb6, 0x09, 0x01, 0xc8, 0xc3};
		std::memcpy(bytes.data() + 0x100, code, sizeof(code));
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int wmain()
{
#if defined(_WIN64)
	std::cout << "DYLD_BIND_32=SKIP_X64\n";
	std::cout << "DYLD_PROVIDER_ENTRY_32=SKIP_X64\n";
	std::cout << "DYLD_DEPENDENT_SLOT_CALL_32=SKIP_X64\n";
	return 0;
#else
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) return 2;
		const auto root = std::filesystem::path(temp_path) /
			(L"wintosh-dyld-bind32-" + std::to_wstring(GetCurrentProcessId()));
		std::filesystem::create_directories(root);
		const auto main_path = root / "Main32.macho";
		const auto provider_path = root / "Provider32.dylib";
		WriteBindImage(provider_path, true);
		WriteBindImage(main_path, false);
		const auto bindings = DylibGraph::BindImports(main_path, {}, root);
		const bool ok = bindings.size() == 1 && bindings.front().name == "_probe" &&
			bindings.front().provider == provider_path && bindings.front().provider_value == 0x1100;
		const auto provider_image = MachOImage::Open(provider_path.wstring());
		const auto provider_mapping = provider_image.MapSegments();
		const auto provider_entry = reinterpret_cast<int (*)()>(
			static_cast<std::intptr_t>(bindings.front().provider_value) + provider_mapping.Slide());
		const auto provider_result = provider_entry();
		auto* loader_image = OpenDynamicImage(main_path);
		const auto loader_slot = reinterpret_cast<std::uintptr_t>(DynamicImageBase(*loader_image)) + 0x180;
		const auto loader_symbol = *reinterpret_cast<const std::uint32_t*>(loader_slot);
		const auto loader_binding = DynamicImageSymbol(*loader_image, "_probe");
		const bool loader_slot_patched = loader_symbol == static_cast<std::uint32_t>(loader_binding) &&
			loader_binding != 0;
		const auto loader_entry_result = DynamicImageExecuteEntry(*loader_image,
			{"Main32", "argument"}, {"PATH=/usr/bin"});
		CloseDynamicImage(loader_image);
		auto* dependent = static_cast<std::uint8_t*>(VirtualAlloc(
			nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (dependent == nullptr) return 2;
		const auto slot = reinterpret_cast<std::uintptr_t>(dependent + 16);
		const auto target = reinterpret_cast<std::uintptr_t>(provider_entry);
		const std::uint32_t slot_address = static_cast<std::uint32_t>(slot);
		const std::uint32_t target_address = static_cast<std::uint32_t>(target);
		std::memcpy(dependent + 16, &target_address, sizeof(target_address));
		const std::uint8_t call_stub[] = {0xa1,
			static_cast<std::uint8_t>(slot_address), static_cast<std::uint8_t>(slot_address >> 8),
			static_cast<std::uint8_t>(slot_address >> 16), static_cast<std::uint8_t>(slot_address >> 24),
			0xff, 0xd0, 0xc3};
		std::memcpy(dependent, call_stub, sizeof(call_stub));
		DWORD old_protection = 0;
		const bool executable = VirtualProtect(dependent, 4096, PAGE_EXECUTE_READ, &old_protection) != FALSE;
		FlushInstructionCache(GetCurrentProcess(), dependent, 4096);
		const auto dependent_result = executable ?
			reinterpret_cast<int (*)()>(dependent)() : -1;
		VirtualFree(dependent, 0, MEM_RELEASE);
		DeleteFileW(main_path.c_str());
		DeleteFileW(provider_path.c_str());
		RemoveDirectoryW(root.c_str());
		std::cout << "DYLD_BIND_32=" << (ok ? "PASS" : "FAIL") << "\n";
		std::cout << "DYLD_PROVIDER_ENTRY_32=" << (provider_result == 7 ? "PASS" : "FAIL") << "\n";
		std::cout << "DYLD_DEPENDENT_SLOT_CALL_32=" << (dependent_result == 7 ? "PASS" : "FAIL") << "\n";
		std::cout << "DYLD_LOADER_SLOT_PATCH_32=" << (loader_slot_patched ? "PASS" : "FAIL") << "\n";
		std::cout << "DYLD_LOADER_MAIN_ENTRY_32=" << (loader_entry_result == 82 ? "PASS" : "FAIL") << "\n";
		return ok && provider_result == 7 && dependent_result == 7 && loader_slot_patched &&
			loader_entry_result == 82 ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
#endif
}
