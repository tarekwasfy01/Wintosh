/*
 * Mach-O LC_DYSYMTAB indirect-symbol and lazy-pointer section proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_macho.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace darling::windows_host;

#if !defined(_WIN64)
static int Run32Fixture()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) return 2;
		const auto path = std::filesystem::path(temp_path) /
			(L"wintosh-indirect-import-32-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		constexpr std::uint32_t commands = sizeof(SegmentCommand32) + sizeof(Section32) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand);
		MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, MH_DYLIB, 3, commands, 0};
		SegmentCommand32 segment{};
		segment.header = {LC_SEGMENT, static_cast<std::uint32_t>(sizeof(segment) + sizeof(Section32))};
		std::memcpy(segment.segment_name, "__DATA", 6);
		segment.vm_address = 0x1000; segment.vm_size = 0x1000;
		segment.file_size = 0x300; segment.max_protection = 3;
		segment.initial_protection = 3; segment.section_count = 1;
		Section32 section{};
		std::memcpy(section.section_name, "__la_symbol_ptr", 15);
		std::memcpy(section.segment_name, "__DATA", 6);
		section.address = 0x1100; section.size = sizeof(std::uint32_t);
		section.file_offset = 0x200; section.alignment = 2;
		SymtabCommand symtab{};
		symtab.header = {LC_SYMTAB, sizeof(SymtabCommand)};
		symtab.symbol_offset = 0x220; symtab.symbol_count = 1;
		symtab.string_offset = 0x230; symtab.string_bytes = 8;
		DYSymtabCommand dysymtab{};
		dysymtab.header = {LC_DYSYMTAB, sizeof(DYSymtabCommand)};
		dysymtab.indirect_symbol_offset = 0x240; dysymtab.indirect_symbol_count = 1;
		std::vector<std::uint8_t> bytes(0x300, 0);
		std::size_t cursor = 0;
		std::memcpy(bytes.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
		std::memcpy(bytes.data() + cursor, &segment, sizeof(segment)); cursor += sizeof(segment);
		std::memcpy(bytes.data() + cursor, &section, sizeof(section)); cursor += sizeof(section);
		std::memcpy(bytes.data() + cursor, &symtab, sizeof(symtab)); cursor += sizeof(symtab);
		std::memcpy(bytes.data() + cursor, &dysymtab, sizeof(dysymtab));
		NList32 symbol{1, 0x01, 0, 0, 0};
		std::memcpy(bytes.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
		std::memcpy(bytes.data() + symtab.string_offset, "\0probe\0", 7);
		const std::uint32_t index = 0;
		std::memcpy(bytes.data() + dysymtab.indirect_symbol_offset, &index, sizeof(index));
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.close();
		const auto image = MachOImage::Open(path.wstring());
		const auto slots = image.IndirectImportSlots();
		const auto mapping = image.MapSegments();
		image.ApplyIndirectImportBindings(mapping, {{"probe", 0x12345678u}});
		const auto value = *reinterpret_cast<const std::uint32_t*>(
			static_cast<std::intptr_t>(slots.front()) + mapping.Slide());
		const bool ok = slots.size() == 1 && slots.front() == 0x1100 && value == 0x12345678u;
		DeleteFileW(path.c_str());
		std::cout << "MACHO_INDIRECT_IMPORT_BIND_32=" << (ok ? "PASS" : "FAIL") << "\n";
		return ok ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n"; return 1;
	}
}
#endif

int wmain()
{
#if !defined(_WIN64)
	return Run32Fixture();
#else
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH)
			return 2;
		const auto path = std::filesystem::path(temp_path) /
			(L"wintosh-indirect-import-" + std::to_wstring(GetCurrentProcessId()) + L".macho");

		constexpr std::uint32_t command_count = 3;
		constexpr auto command_bytes = sizeof(SegmentCommand64) + sizeof(Section64) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand);
		MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_DYLIB,
			command_count, static_cast<std::uint32_t>(command_bytes), 0, 0};
		SegmentCommand64 segment{};
		segment.header = {LC_SEGMENT_64,
			static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(Section64))};
		std::memcpy(segment.segment_name, "__DATA", 6);
		segment.vm_address = 0x1000;
		segment.vm_size = 0x1000;
		segment.file_offset = 0;
		segment.file_size = 0x400;
		segment.max_protection = 3;
		segment.initial_protection = 3;
		segment.section_count = 1;
		Section64 pointer_section{};
		std::memcpy(pointer_section.section_name, "__la_symbol_ptr", 15);
		std::memcpy(pointer_section.segment_name, "__DATA", 6);
		pointer_section.address = 0x1100;
		pointer_section.size = 2 * sizeof(std::uint64_t);
		pointer_section.file_offset = 0x300;
		pointer_section.alignment = 3;

		SymtabCommand symtab{};
		symtab.header = {LC_SYMTAB, sizeof(SymtabCommand)};
		symtab.symbol_offset = 0x320;
		symtab.symbol_count = 2;
		symtab.string_offset = 0x340;
		symtab.string_bytes = 24;
		DYSymtabCommand dysymtab{};
		dysymtab.header = {LC_DYSYMTAB, sizeof(DYSymtabCommand)};
		dysymtab.indirect_symbol_offset = 0x350;
		dysymtab.indirect_symbol_count = 2;

		std::vector<std::uint8_t> bytes(0x400, 0);
		std::size_t cursor = 0;
		std::memcpy(bytes.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
		std::memcpy(bytes.data() + cursor, &segment, sizeof(segment)); cursor += sizeof(segment);
		std::memcpy(bytes.data() + cursor, &pointer_section, sizeof(pointer_section)); cursor += sizeof(pointer_section);
		std::memcpy(bytes.data() + cursor, &symtab, sizeof(symtab)); cursor += sizeof(symtab);
		std::memcpy(bytes.data() + cursor, &dysymtab, sizeof(dysymtab));
		NList64 symbol{};
		symbol.name_offset = 1;
		symbol.type = 0x01; // N_EXT | N_UNDF
		std::memcpy(bytes.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
		NList64 weak_symbol{};
		weak_symbol.name_offset = 8;
		weak_symbol.type = 0x01; // N_EXT | N_UNDF
		weak_symbol.description = 0x0040; // N_WEAK_REF
		std::memcpy(bytes.data() + symtab.symbol_offset + sizeof(symbol),
			&weak_symbol, sizeof(weak_symbol));
		std::memcpy(bytes.data() + symtab.string_offset, "\0_probe\0_weak\0", 15);
		const std::uint32_t indirect_indices[] = {0, 1};
		std::memcpy(bytes.data() + dysymtab.indirect_symbol_offset,
			indirect_indices, sizeof(indirect_indices));

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		output.close();
		const auto image = MachOImage::Open(path.wstring());
		const auto& indirect = image.IndirectSymbols();
		const auto slots = image.IndirectImportSlots();
		const auto mapping = image.MapSegments();
		const std::uintptr_t target = 0x123456789abcdef0ull;
		image.ApplyIndirectImportBindings(mapping, {{"_probe", target}});
		const auto slot_address = static_cast<std::intptr_t>(slots.front()) + mapping.Slide();
		const auto bound_value = *reinterpret_cast<const std::uintptr_t*>(slot_address);
		const auto weak_slot_address = static_cast<std::intptr_t>(slots.back()) + mapping.Slide();
		const auto weak_value = *reinterpret_cast<const std::uintptr_t*>(weak_slot_address);
		const bool ok = indirect.size() == 2 && indirect.front() == 0 && indirect.back() == 1 &&
			slots.size() == 2 && slots.front() == 0x1100 && slots.back() == 0x1108 &&
			bound_value == target &&
			weak_value == 0 && image.UndefinedSymbols().size() == 2 &&
			image.UndefinedSymbols().front().name == "_probe";
		DeleteFileW(path.c_str());
		std::cout << "MACHO_INDIRECT_IMPORT_BIND=" << (ok ? "PASS" : "FAIL") << "\n";
		return ok ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
#endif
}
