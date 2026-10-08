/*
 * Stage 1 Mach-O mapping and load-command validation proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_macho.h"
#include "darling_windows_bootstrap.h"
#include "darling_windows_tlv.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace darling::windows_host;

#if !defined(_WIN64)
extern "C" __declspec(noinline) int darling_windows_win32_import_probe()
{
	return 43;
}
#endif

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			return 2;
		}
		const auto path = std::filesystem::path(temp_path) /
			(L"darling-stage1-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto mach32_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-32bit-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto thread_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-thread-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto arm64_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-arm64-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto arm64_userland_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-arm64-userland-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto arm64_userland24_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-arm64-userland24-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto arm64_auth_path = std::filesystem::path(temp_path) /
			(L"darling-stage1-arm64-auth-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const auto mach32_header = MachHeader32{
			MH_MAGIC, CPU_TYPE_X86, 3, MH_EXECUTE, 4,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + 2 * sizeof(Section32) +
				sizeof(SymtabCommand) + sizeof(DYSymtabCommand) + 80), 0};
		SegmentCommand32 mach32_segment{};
		mach32_segment.header = {LC_SEGMENT,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + 2 * sizeof(Section32))};
		std::memcpy(mach32_segment.segment_name, "__TEXT", 6);
		mach32_segment.vm_address = 0x1000;
		mach32_segment.vm_size = 0x1000;
		mach32_segment.file_offset = 0;
		mach32_segment.file_size = 0x1000;
		mach32_segment.max_protection = 5;
		mach32_segment.initial_protection = 5;
		mach32_segment.section_count = 2;
		Section32 mach32_thread_vars{};
		std::memcpy(mach32_thread_vars.section_name, "__thread_vars", 13);
		std::memcpy(mach32_thread_vars.segment_name, "__TEXT", 6);
		mach32_thread_vars.address = 0x1100;
		mach32_thread_vars.size = sizeof(DarwinTLVDescriptor);
		mach32_thread_vars.file_offset = 0x100;
		Section32 mach32_thread_ptrs{};
		std::memcpy(mach32_thread_ptrs.section_name, "__thread_ptrs", 13);
		std::memcpy(mach32_thread_ptrs.segment_name, "__TEXT", 6);
		mach32_thread_ptrs.address = 0x1120;
		mach32_thread_ptrs.size = sizeof(std::uintptr_t);
		mach32_thread_ptrs.file_offset = 0x120;
		SymtabCommand mach32_symtab{};
		mach32_symtab.header = {LC_SYMTAB, sizeof(SymtabCommand)};
		mach32_symtab.symbol_offset = 0x200;
		mach32_symtab.symbol_count = 2;
		mach32_symtab.string_offset = 0x280;
		mach32_symtab.string_bytes = 32;
		DYSymtabCommand mach32_dysymtab{};
		mach32_dysymtab.header = {LC_DYSYMTAB, sizeof(DYSymtabCommand)};
		mach32_dysymtab.external_relocation_offset = 0x220;
		mach32_dysymtab.external_relocation_count = 3;
		mach32_dysymtab.local_relocation_offset = 0x240;
		mach32_dysymtab.local_relocation_count = 3;
		std::vector<std::uint8_t> mach32_thread_command(80, 0);
		const LoadCommand mach32_thread_header{LC_UNIXTHREAD, 80};
		const std::uint32_t mach32_thread_flavor = 1;
		const std::uint32_t mach32_thread_count = 16;
		const std::uint32_t mach32_thread_eip = 0x1600;
		std::memcpy(mach32_thread_command.data(), &mach32_thread_header,
			sizeof(mach32_thread_header));
		std::memcpy(mach32_thread_command.data() + 8, &mach32_thread_flavor,
			sizeof(mach32_thread_flavor));
		std::memcpy(mach32_thread_command.data() + 12, &mach32_thread_count,
			sizeof(mach32_thread_count));
		std::memcpy(mach32_thread_command.data() + 16 + 10 * sizeof(std::uint32_t),
			&mach32_thread_eip, sizeof(mach32_thread_eip));
		{
			std::vector<std::uint8_t> bytes(0x1000, 0);
			std::memcpy(bytes.data(), &mach32_header, sizeof(mach32_header));
			std::memcpy(bytes.data() + sizeof(mach32_header), &mach32_segment,
				sizeof(mach32_segment));
			std::memcpy(bytes.data() + sizeof(mach32_header) + sizeof(mach32_segment),
				&mach32_thread_vars, sizeof(mach32_thread_vars));
			std::memcpy(bytes.data() + sizeof(mach32_header) + sizeof(mach32_segment) +
				sizeof(mach32_thread_vars), &mach32_thread_ptrs,
				sizeof(mach32_thread_ptrs));
			std::memcpy(bytes.data() + sizeof(mach32_header) + sizeof(mach32_segment) +
				2 * sizeof(Section32),
				&mach32_symtab, sizeof(mach32_symtab));
			std::memcpy(bytes.data() + sizeof(mach32_header) + sizeof(mach32_segment) +
				2 * sizeof(Section32) + sizeof(mach32_symtab), &mach32_dysymtab,
				sizeof(mach32_dysymtab));
			std::memcpy(bytes.data() + sizeof(mach32_header) + sizeof(mach32_segment) +
				2 * sizeof(Section32) + sizeof(mach32_symtab) + sizeof(mach32_dysymtab),
				mach32_thread_command.data(), mach32_thread_command.size());
			const NList32 mach32_symbol{1, 0x0f, 1, 0, 0x1200};
			std::memcpy(bytes.data() + mach32_symtab.symbol_offset, &mach32_symbol,
				sizeof(mach32_symbol));
			const NList32 mach32_tlv_symbol{11, 0x0e, 1, 0, 0x1100};
			std::memcpy(bytes.data() + mach32_symtab.symbol_offset + sizeof(mach32_symbol),
				&mach32_tlv_symbol, sizeof(mach32_tlv_symbol));
			std::memcpy(bytes.data() + mach32_symtab.string_offset + 1, "_legacy32", 10);
			std::memcpy(bytes.data() + mach32_symtab.string_offset + 11, "_tlv", 5);
			const RelocationInfo mach32_absolute_relocation{
				0x1300, (2u << 25) | 0x08000000u};
			const RelocationInfo mach32_pcrel_relocation{
				0x1304, 0x01000000u | (2u << 25) | 0x08000000u};
			const RelocationInfo mach32_tlv_relocation{
				0x1310, 1u | 0x01000000u | (2u << 25) | 0x08000000u |
					(5u << 28)};
			std::memcpy(bytes.data() + 0x220, &mach32_absolute_relocation,
				sizeof(mach32_absolute_relocation));
			std::memcpy(bytes.data() + 0x220 + sizeof(mach32_absolute_relocation),
				&mach32_pcrel_relocation, sizeof(mach32_pcrel_relocation));
			std::memcpy(bytes.data() + 0x220 + 2 * sizeof(mach32_absolute_relocation),
				&mach32_tlv_relocation, sizeof(mach32_tlv_relocation));
			const RelocationInfo mach32_sectdiff_relocation{
				static_cast<std::int32_t>(0x80000000u | (2u << 24) | (2u << 28) | 0x1308u),
				0x1500};
			const RelocationInfo mach32_sectdiff_pair{
				static_cast<std::int32_t>(0x80000000u | (1u << 24) | (2u << 28)),
				0x1200};
			const RelocationInfo mach32_pb_la_ptr{
				static_cast<std::int32_t>(0x80000000u | (3u << 24) | (2u << 28) | 0x130cu),
				0x1400};
			std::memcpy(bytes.data() + 0x240, &mach32_sectdiff_relocation,
				sizeof(mach32_sectdiff_relocation));
			std::memcpy(bytes.data() + 0x240 + sizeof(mach32_sectdiff_relocation),
				&mach32_sectdiff_pair, sizeof(mach32_sectdiff_pair));
			std::memcpy(bytes.data() + 0x250, &mach32_pb_la_ptr,
				sizeof(mach32_pb_la_ptr));
			const std::uint32_t mach32_absolute_addend = 4;
			const std::int32_t mach32_pcrel_addend = -4;
			const std::uint32_t mach32_sectdiff_addend = 3;
			const std::int32_t mach32_tlv_addend = 0;
			std::memcpy(bytes.data() + 0x300, &mach32_absolute_addend,
				sizeof(mach32_absolute_addend));
			std::memcpy(bytes.data() + 0x304, &mach32_pcrel_addend,
				sizeof(mach32_pcrel_addend));
			std::memcpy(bytes.data() + 0x308, &mach32_sectdiff_addend,
				sizeof(mach32_sectdiff_addend));
			std::memcpy(bytes.data() + 0x310, &mach32_tlv_addend,
				sizeof(mach32_tlv_addend));
			const std::uint8_t mach32_entry_code[] = {0xb8, 42, 0, 0, 0, 0xc3};
			std::memcpy(bytes.data() + 0x600, mach32_entry_code,
				sizeof(mach32_entry_code));
			std::ofstream mach32_output(mach32_path, std::ios::binary);
			mach32_output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}
		{
			const auto mach32_image = MachOImage::Open(mach32_path.wstring());
			if (!mach32_image.Is32Bit() || mach32_image.Header().magic != MH_MAGIC ||
				mach32_image.Header().cpu_type != CPU_TYPE_X86 ||
				mach32_image.Segments().size() != 1 || mach32_image.Symbols().size() != 2 ||
				mach32_image.Symbols().front().name != "_legacy32" ||
				mach32_image.Symbols().front().value != 0x1200) {
				return 23;
			}
			auto mach32_mapping = mach32_image.MapSegments(0x10000000);
			if (mach32_mapping.Segments().size() != 1)
				return 24;
			const auto mach32_target = mach32_mapping.Segments()[0].address + 0x180;
			mach32_image.ApplyRelocations(mach32_mapping, {mach32_target, mach32_mapping.Segments()[0].address + 0x100});
			std::uint32_t mach32_absolute_value = 0;
			std::int32_t mach32_pcrel_value = 0;
			std::uint32_t mach32_sectdiff_value = 0;
			std::uint32_t mach32_pb_la_value = 0;
			std::int32_t mach32_tlv_value = 0;
			std::memcpy(&mach32_absolute_value,
				reinterpret_cast<const void*>(mach32_mapping.Segments()[0].address + 0x300),
				sizeof(mach32_absolute_value));
			std::memcpy(&mach32_pcrel_value,
				reinterpret_cast<const void*>(mach32_mapping.Segments()[0].address + 0x304),
				sizeof(mach32_pcrel_value));
			std::memcpy(&mach32_sectdiff_value,
				reinterpret_cast<const void*>(mach32_mapping.Segments()[0].address + 0x308),
				sizeof(mach32_sectdiff_value));
			std::memcpy(&mach32_pb_la_value,
				reinterpret_cast<const void*>(mach32_mapping.Segments()[0].address + 0x30c),
				sizeof(mach32_pb_la_value));
			std::memcpy(&mach32_tlv_value,
				reinterpret_cast<const void*>(mach32_mapping.Segments()[0].address + 0x310),
				sizeof(mach32_tlv_value));
			const auto pcrel_site = static_cast<std::int64_t>(
				mach32_mapping.Segments()[0].address + 0x304);
			const auto tlv_site = static_cast<std::int64_t>(
				mach32_mapping.Segments()[0].address + 0x310);
			const auto tlv_pointer = static_cast<std::int64_t>(
				mach32_mapping.Segments()[0].address + 0x120);
			if (mach32_absolute_value != mach32_target + 4 ||
				mach32_pcrel_value != mach32_target - 4 - pcrel_site - 4 ||
				mach32_sectdiff_value != 0x303 ||
				mach32_pb_la_value != static_cast<std::uint32_t>(0x1400 + mach32_mapping.Slide()) ||
				mach32_tlv_value != tlv_pointer - tlv_site - 4)
			{
				return 27;
			}
			#if defined(_WIN64)
			try {
				mach32_image.ExecuteEntryNoArgs(mach32_mapping);
				return 25;
			} catch (const std::exception& error) {
				if (std::string(error.what()).find("32-bit Mach-O execution") ==
					std::string::npos)
					return 26;
			}
			#else
			if (mach32_image.ExecuteEntry(mach32_mapping, {}, {}) != 42)
				return 28;
			DWORD old_protection = PAGE_NOACCESS;
			const auto mapped_base = mach32_mapping.Segments()[0].address;
			if (!VirtualProtect(reinterpret_cast<void*>(mapped_base + 0x600), 0x110,
				PAGE_EXECUTE_READWRITE, &old_protection))
				return 30;
			const auto imported_address = reinterpret_cast<std::uintptr_t>(
				&darling_windows_win32_import_probe);
			std::memcpy(reinterpret_cast<void*>(mapped_base + 0x700),
				&imported_address, sizeof(std::uint32_t));
			const std::uint8_t imported_entry_code[] = {
				0xa1,
				static_cast<std::uint8_t>((mapped_base + 0x700) & 0xff),
				static_cast<std::uint8_t>(((mapped_base + 0x700) >> 8) & 0xff),
				static_cast<std::uint8_t>(((mapped_base + 0x700) >> 16) & 0xff),
				static_cast<std::uint8_t>(((mapped_base + 0x700) >> 24) & 0xff),
				0xff, 0xd0, 0xc3};
			std::memcpy(reinterpret_cast<void*>(mapped_base + 0x600),
				imported_entry_code, sizeof(imported_entry_code));
			DWORD ignored_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(mapped_base + 0x600), 0x110,
				old_protection, &ignored_protection))
				return 31;
			mach32_image.ExecuteEntryNoArgs(mach32_mapping);
			if (*reinterpret_cast<volatile std::uint32_t*>(mapped_base + 0x700) !=
				static_cast<std::uint32_t>(imported_address))
				return 32;
			std::cout << "MACHO_32BIT_IMPORTED_INDIRECT=PASS\n";
			#endif
		}
		std::cout << "MACHO_32BIT_METADATA=PASS\n";
		std::cout << "MACHO_32BIT_RELOCATIONS=PASS\n";
		std::cout << "MACHO_32BIT_PB_LA_PTR=PASS\n";
		std::cout << "MACHO_32BIT_TLV_RELOCATION=PASS\n";
		#if !defined(_WIN64)
		std::cout << "MACHO_32BIT_EXECUTION=PASS\n";
		if (DarwinBootstrap::Run(mach32_path, {}) != 42)
			return 29;
		std::cout << "DARWIN_BOOTSTRAP_32BIT=PASS\n";
		return 0;
		#endif
		std::cout << "MACHO_32BIT_EXECUTION_GUARD=PASS\n";

		const MachHeader64 header{
			MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 3,
			static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(Section64) +
				sizeof(MainCommand) + 56), 0, 0};
		SegmentCommand64 segment{};
		segment.header = {LC_SEGMENT_64, sizeof(SegmentCommand64) + sizeof(Section64)};
		std::memcpy(segment.segment_name, "__TEXT", 6);
		segment.vm_address = 0x100000000;
		segment.vm_size = 0x2000;
		segment.file_offset = 0;
		segment.file_size = 0x1000;
		segment.max_protection = 5;
		segment.initial_protection = 5;
		segment.section_count = 1;
		Section64 objc_classlist_section{};
		std::memcpy(objc_classlist_section.section_name, "__objc_classlist", 16);
		std::memcpy(objc_classlist_section.segment_name, "__DATA", 6);
		objc_classlist_section.address = 0x100000200;
		objc_classlist_section.size = sizeof(std::uintptr_t);
		objc_classlist_section.file_offset = 0x200;
		MainCommand main_command{{LC_MAIN, sizeof(MainCommand)}, 0x200, 0};
		std::vector<std::uint8_t> dylib_command(56, 0);
		const DylibCommand dylib{{LC_LOAD_DYLIB, 56}, sizeof(DylibCommand), 0, 0, 0};
		std::memcpy(dylib_command.data(), &dylib, sizeof(dylib));
		constexpr char dependency[] = "@rpath/libSystem.B.dylib";
		std::memcpy(dylib_command.data() + sizeof(DylibCommand), dependency, sizeof(dependency));

		std::ofstream output(path, std::ios::binary);
		if (!output) {
			return 3;
		}
		output.write(reinterpret_cast<const char*>(&header), sizeof(header));
		output.write(reinterpret_cast<const char*>(&segment), sizeof(segment));
		output.write(reinterpret_cast<const char*>(&objc_classlist_section),
			sizeof(objc_classlist_section));
		output.write(reinterpret_cast<const char*>(&main_command), sizeof(main_command));
		output.write(reinterpret_cast<const char*>(dylib_command.data()), dylib_command.size());
		output.seekp(0x1000 - 1);
		output.put('\0');
		output.close();
		std::ifstream thin_input(path, std::ios::binary);
		const std::vector<char> thin_bytes((std::istreambuf_iterator<char>(thin_input)),
			std::istreambuf_iterator<char>());
		thin_input.close();
		std::ofstream fat_output(path, std::ios::binary | std::ios::trunc);
		const auto write_be32 = [&fat_output](std::uint32_t value) {
			const char bytes[] = {
				static_cast<char>((value >> 24) & 0xff), static_cast<char>((value >> 16) & 0xff),
				static_cast<char>((value >> 8) & 0xff), static_cast<char>(value & 0xff)};
			fat_output.write(bytes, sizeof(bytes));
		};
		write_be32(0xcafebabe);
		write_be32(2);
		write_be32(static_cast<std::uint32_t>(CPU_TYPE_X86_64));
		write_be32(3);
		write_be32(0x1000);
		write_be32(static_cast<std::uint32_t>(thin_bytes.size()));
		write_be32(12);
		write_be32(0x0100000c);
		write_be32(0);
		write_be32(0x800);
		write_be32(4);
		write_be32(11);
		fat_output.seekp(0x800);
		fat_output.write("ARM!", 4);
		fat_output.seekp(0x1000);
		fat_output.write(thin_bytes.data(), static_cast<std::streamsize>(thin_bytes.size()));
		fat_output.close();
		std::vector<char> arm64_bytes = thin_bytes;
		const auto arm64_cpu = static_cast<std::int32_t>(CPU_TYPE_ARM64);
		std::memcpy(arm64_bytes.data() + sizeof(std::uint32_t), &arm64_cpu, sizeof(arm64_cpu));
		const auto arm64_command_bytes = static_cast<std::uint32_t>(
			sizeof(SegmentCommand64) + sizeof(Section64) + sizeof(MainCommand) +
			56 + sizeof(DYSymtabCommand) + sizeof(LinkeditDataCommand));
		const auto arm64_command_count = static_cast<std::uint32_t>(5);
		std::memcpy(arm64_bytes.data() + 16, &arm64_command_count, sizeof(arm64_command_count));
		std::memcpy(arm64_bytes.data() + 20, &arm64_command_bytes, sizeof(arm64_command_bytes));
		const DYSymtabCommand arm64_dysymtab{
			{LC_DYSYMTAB, sizeof(DYSymtabCommand)}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0x300, 11, 0x358, 1};
		const auto arm64_dysymtab_offset = sizeof(MachHeader64) +
			sizeof(SegmentCommand64) + sizeof(Section64) + sizeof(MainCommand) + 56;
		std::memcpy(arm64_bytes.data() + arm64_dysymtab_offset,
			&arm64_dysymtab, sizeof(arm64_dysymtab));
		const LinkeditDataCommand arm64_chained_command{
			{LC_DYLD_CHAINED_FIXUPS, sizeof(LinkeditDataCommand)}, 0x400, 100};
		const auto arm64_chained_command_offset = arm64_dysymtab_offset + sizeof(DYSymtabCommand);
		std::memcpy(arm64_bytes.data() + arm64_chained_command_offset,
			&arm64_chained_command, sizeof(arm64_chained_command));
		const auto write_arm64_u16 = [&arm64_bytes](std::size_t offset, std::uint16_t value) {
			std::memcpy(arm64_bytes.data() + offset, &value, sizeof(value));
		};
		const auto write_arm64_u32 = [&arm64_bytes](std::size_t offset, std::uint32_t value) {
			std::memcpy(arm64_bytes.data() + offset, &value, sizeof(value));
		};
		const auto write_arm64_u64 = [&arm64_bytes](std::size_t offset, std::uint64_t value) {
			std::memcpy(arm64_bytes.data() + offset, &value, sizeof(value));
		};
		write_arm64_u32(0x400, 0);
		write_arm64_u32(0x404, 28);
		write_arm64_u32(0x408, 28);
		write_arm64_u32(0x40c, 99);
		write_arm64_u32(0x410, 0);
		write_arm64_u32(0x414, 1);
		write_arm64_u32(0x418, 0);
		write_arm64_u32(0x41c, 1);
		write_arm64_u32(0x420, 36);
		write_arm64_u32(0x424, 24);
		write_arm64_u16(0x428, 0x1000);
		write_arm64_u16(0x42a, 6);
		write_arm64_u64(0x42c, 0);
		write_arm64_u32(0x434, 0);
		write_arm64_u16(0x438, 1);
		write_arm64_u16(0x43a, 0x380);
		write_arm64_u64(0x380, 0x350ull);
		const RelocationInfo arm64_branch_relocation{
			0x204, 0x20000000u | 0x01000000u | 0x04000000u | 0x08000000u};
		const RelocationInfo arm64_pageoff_addend{0x20c,
			0xa4000010u};
		const RelocationInfo arm64_page_relocation{
			0x208, 0x30000000u | 0x01000000u | 0x04000000u | 0x08000001u};
		const RelocationInfo arm64_page_addend{0x208,
			0xa4001000u};
		const RelocationInfo arm64_pageoff_relocation{
			0x20c, 0x40000000u | 0x04000000u | 0x08000001u};
		const RelocationInfo arm64_subtractor_relocation{
			0x220, 0x10000000u | 0x06000000u | 0x08000002u};
		const RelocationInfo arm64_subtractor_pair{
			0x220, 0x00000000u | 0x06000000u | 0x08000003u};
		const RelocationInfo arm64_got_absolute{0x230,
			0x70000000u | 0x06000000u | 0x08000004u};
		const RelocationInfo arm64_got_relative{0x238,
			0x70000000u | 0x01000000u | 0x04000000u | 0x08000005u};
		const RelocationInfo arm64_tlv_page{0x240,
			0x80000000u | 0x01000000u | 0x04000000u | 0x08000001u};
		const RelocationInfo arm64_tlv_pageoff{0x244,
			0x90000000u | 0x04000000u | 0x08000001u};
		std::memcpy(arm64_bytes.data() + 0x300, &arm64_branch_relocation,
			sizeof(arm64_branch_relocation));
		std::memcpy(arm64_bytes.data() + 0x300 + sizeof(RelocationInfo),
			&arm64_pageoff_addend, sizeof(arm64_pageoff_addend));
		std::memcpy(arm64_bytes.data() + 0x300 + 2 * sizeof(RelocationInfo),
			&arm64_pageoff_relocation, sizeof(arm64_pageoff_relocation));
		std::memcpy(arm64_bytes.data() + 0x300 + 3 * sizeof(RelocationInfo),
			&arm64_page_addend, sizeof(arm64_page_addend));
		std::memcpy(arm64_bytes.data() + 0x300 + 4 * sizeof(RelocationInfo),
			&arm64_page_relocation, sizeof(arm64_page_relocation));
		std::memcpy(arm64_bytes.data() + 0x300 + 5 * sizeof(RelocationInfo),
			&arm64_subtractor_relocation, sizeof(arm64_subtractor_relocation));
		std::memcpy(arm64_bytes.data() + 0x300 + 6 * sizeof(RelocationInfo),
			&arm64_subtractor_pair, sizeof(arm64_subtractor_pair));
		std::memcpy(arm64_bytes.data() + 0x300 + 7 * sizeof(RelocationInfo),
			&arm64_got_absolute, sizeof(arm64_got_absolute));
		std::memcpy(arm64_bytes.data() + 0x300 + 8 * sizeof(RelocationInfo),
			&arm64_got_relative, sizeof(arm64_got_relative));
		std::memcpy(arm64_bytes.data() + 0x300 + 9 * sizeof(RelocationInfo),
			&arm64_tlv_page, sizeof(arm64_tlv_page));
		std::memcpy(arm64_bytes.data() + 0x300 + 10 * sizeof(RelocationInfo),
			&arm64_tlv_pageoff, sizeof(arm64_tlv_pageoff));
		const RelocationInfo arm64_local_relocation{0x218, 0x06000000u};
		std::memcpy(arm64_bytes.data() + 0x358, &arm64_local_relocation,
			sizeof(arm64_local_relocation));
		const std::uint32_t arm64_branch = 0x14000000u;
		std::memcpy(arm64_bytes.data() + 0x204, &arm64_branch, sizeof(arm64_branch));
		const std::uint32_t arm64_adrp = 0x90000000u;
		const std::uint32_t arm64_add = 0x91000000u;
		std::memcpy(arm64_bytes.data() + 0x208, &arm64_adrp, sizeof(arm64_adrp));
		std::memcpy(arm64_bytes.data() + 0x20c, &arm64_add, sizeof(arm64_add));
		std::memcpy(arm64_bytes.data() + 0x240, &arm64_adrp, sizeof(arm64_adrp));
		std::memcpy(arm64_bytes.data() + 0x244, &arm64_add, sizeof(arm64_add));
		const std::uint64_t arm64_local_pointer = 0x100000360ull;
		std::memcpy(arm64_bytes.data() + 0x218, &arm64_local_pointer,
			sizeof(arm64_local_pointer));
		std::ofstream arm64_output(arm64_path, std::ios::binary);
		arm64_output.write(arm64_bytes.data(), static_cast<std::streamsize>(arm64_bytes.size()));
		arm64_output.close();
		{
			const auto arm64_image = darling::windows_host::MachOImage::Open(arm64_path.wstring());
			if (!arm64_image.IsArm64() || arm64_image.EntryOffset() != 0x200 ||
				arm64_image.Dependencies().size() != 1) {
				return 10;
			}
			auto arm64_mapping = arm64_image.MapSegments();
			const auto arm64_branch_target = arm64_mapping.Segments()[0].address + 0x208;
			const auto arm64_page_target = arm64_mapping.Segments()[0].address + 0x2345;
			const auto arm64_subtractor_target = arm64_mapping.Segments()[0].address + 0x380;
			const auto arm64_subtractor_pair_target = arm64_mapping.Segments()[0].address + 0x350;
			const auto arm64_got_absolute_target = arm64_mapping.Segments()[0].address + 0x3a0;
			const auto arm64_got_relative_target = arm64_mapping.Segments()[0].address + 0x3c0;
			arm64_image.ApplyRelocations(arm64_mapping,
				{arm64_branch_target, arm64_page_target, arm64_subtractor_target,
					arm64_subtractor_pair_target, arm64_got_absolute_target,
					arm64_got_relative_target});
			std::uint32_t patched_branch = 0;
			std::uint32_t patched_adrp = 0;
			std::uint32_t patched_add = 0;
			std::memcpy(&patched_branch,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x204),
				sizeof(patched_branch));
			std::memcpy(&patched_adrp,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x208),
				sizeof(patched_adrp));
			std::memcpy(&patched_add,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x20c),
				sizeof(patched_add));
			if (patched_branch != 0x14000001u || patched_adrp != 0xf0000000u ||
				patched_add != 0x910d5400u) {
				return 11;
			}
			const auto chained_location = arm64_mapping.Segments()[0].address + 0x380;
			arm64_image.ApplyChainedFixups(arm64_mapping, std::vector<std::uintptr_t>{});
			std::uint64_t patched_chained = 0;
			std::memcpy(&patched_chained, reinterpret_cast<const void*>(chained_location),
				sizeof(patched_chained));
			if (patched_chained != arm64_mapping.Segments()[0].address + 0x350) {
				return 12;
			}
			std::uint64_t patched_local = 0;
			std::memcpy(&patched_local,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x218),
				sizeof(patched_local));
			if (patched_local != arm64_mapping.Segments()[0].address + 0x360) {
				return 13;
			}
			std::uint64_t patched_subtractor = 0;
			std::memcpy(&patched_subtractor,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x220),
				sizeof(patched_subtractor));
			if (patched_subtractor != 0x30u) {
				return 14;
			}
			std::uint64_t patched_got_absolute = 0;
			std::uint32_t patched_got_relative = 0;
			std::memcpy(&patched_got_absolute,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x230),
				sizeof(patched_got_absolute));
			std::memcpy(&patched_got_relative,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x238),
				sizeof(patched_got_relative));
			if (patched_got_absolute != arm64_got_absolute_target ||
				patched_got_relative != 0x184u) {
				return 17;
			}
			std::uint32_t patched_tlv_page = 0;
			std::uint32_t patched_tlv_pageoff = 0;
			std::memcpy(&patched_tlv_page,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x240),
				sizeof(patched_tlv_page));
			std::memcpy(&patched_tlv_pageoff,
				reinterpret_cast<const void*>(arm64_mapping.Segments()[0].address + 0x244),
				sizeof(patched_tlv_pageoff));
			if (patched_tlv_page != 0xd0000000u || patched_tlv_pageoff != 0x910d1400u) {
				return 18;
			}
			std::cout << "MACHO_ARM64_RELOCATIONS=PASS\n";
			std::cout << "MACHO_ARM64_POINTER_TO_GOT=PASS\n";
			std::cout << "MACHO_ARM64_TLV_RELOCATIONS=PASS\n";
			std::cout << "MACHO_CHAINED_PTR_64_OFFSET=PASS\n";
			std::cout << "MACHO_ARM64_ADDEND=PASS\n";
			std::cout << "MACHO_ARM64_CHAINED_FIXUP=PASS\n";
			std::cout << "MACHO_ARM64_LOCAL_RELOCATION=PASS\n";
			std::cout << "MACHO_ARM64_SUBTRACTOR=PASS\n";
			std::vector<char> userland_bytes = arm64_bytes;
			const std::uint16_t userland_format = 9;
			const std::uint64_t userland_chain = 0x350ull;
			std::memcpy(userland_bytes.data() + 0x42a, &userland_format,
				sizeof(userland_format));
			std::memcpy(userland_bytes.data() + 0x380, &userland_chain,
				sizeof(userland_chain));
			std::ofstream userland_output(arm64_userland_path, std::ios::binary);
			userland_output.write(userland_bytes.data(),
				static_cast<std::streamsize>(userland_bytes.size()));
			userland_output.close();
			const auto userland_image = darling::windows_host::MachOImage::Open(
				arm64_userland_path.wstring());
			auto userland_mapping = userland_image.MapSegments();
			userland_image.ApplyChainedFixups(userland_mapping,
				std::vector<std::uintptr_t>{});
			std::uint64_t userland_value = 0;
			std::memcpy(&userland_value,
				reinterpret_cast<const void*>(userland_mapping.Segments()[0].address + 0x380),
				sizeof(userland_value));
			if (userland_value != userland_mapping.Segments()[0].address + 0x350)
				return 21;
			std::cout << "MACHO_ARM64E_USERLAND_REBASE=PASS\n";
			std::vector<char> userland24_bytes = arm64_bytes;
			const std::uint16_t userland24_format = 12;
			std::memcpy(userland24_bytes.data() + 0x42a, &userland24_format,
				sizeof(userland24_format));
			std::ofstream userland24_output(arm64_userland24_path, std::ios::binary);
			userland24_output.write(userland24_bytes.data(),
				static_cast<std::streamsize>(userland24_bytes.size()));
			userland24_output.close();
			const auto userland24_image = darling::windows_host::MachOImage::Open(
				arm64_userland24_path.wstring());
			auto userland24_mapping = userland24_image.MapSegments();
			userland24_image.ApplyChainedFixups(userland24_mapping,
				std::vector<std::uintptr_t>{});
			std::uint64_t userland24_value = 0;
			std::memcpy(&userland24_value,
				reinterpret_cast<const void*>(userland24_mapping.Segments()[0].address + 0x380),
				sizeof(userland24_value));
			if (userland24_value != userland24_mapping.Segments()[0].address + 0x350)
				return 22;
			std::cout << "MACHO_ARM64E_USERLAND24_REBASE=PASS\n";
			std::vector<char> auth_bytes = arm64_bytes;
			DYSymtabCommand auth_dysymtab = arm64_dysymtab;
			auth_dysymtab.local_relocation_count = 2;
			std::memcpy(auth_bytes.data() + arm64_dysymtab_offset,
				&auth_dysymtab, sizeof(auth_dysymtab));
			const RelocationInfo auth_relocation{0x228, 0xb6000000u};
			std::memcpy(auth_bytes.data() + 0x360, &auth_relocation, sizeof(auth_relocation));
			const std::uint64_t auth_metadata = 0x8000000000000000ull |
				(1ull << 49) | (1ull << 48) | (0x1234ull << 32) | 0x20ull;
			std::memcpy(auth_bytes.data() + 0x228, &auth_metadata, sizeof(auth_metadata));
			const std::uint16_t auth_chain_format = 1;
			const std::uint64_t auth_chain_metadata = 0x8000000000000000ull | 0x350ull;
			std::memcpy(auth_bytes.data() + 0x42a, &auth_chain_format,
				sizeof(auth_chain_format));
			std::memcpy(auth_bytes.data() + 0x380, &auth_chain_metadata,
				sizeof(auth_chain_metadata));
			std::ofstream auth_output(arm64_auth_path, std::ios::binary);
			auth_output.write(auth_bytes.data(), static_cast<std::streamsize>(auth_bytes.size()));
			auth_output.close();
			const auto auth_image = darling::windows_host::MachOImage::Open(arm64_auth_path.wstring());
			auto auth_mapping = auth_image.MapSegments();
			try {
				auth_image.ApplyRelocations(auth_mapping,
					{arm64_branch_target, arm64_page_target, arm64_subtractor_target,
						arm64_subtractor_pair_target, arm64_got_absolute_target,
						arm64_got_relative_target});
				return 15;
			} catch (const std::exception& error) {
				const std::string message = error.what();
				if (message.find("authenticated pointer requires PAC backend") == std::string::npos ||
					message.find("key=1") == std::string::npos ||
					message.find("discriminator=4660") == std::string::npos ||
					message.find("addend=32") == std::string::npos) {
					return 16;
				}
			}
			try {
				auth_image.ApplyChainedFixups(auth_mapping, std::vector<std::uintptr_t>{});
				return 19;
			} catch (const std::exception& error) {
				if (std::string(error.what()).find(
						"ARM64e chained authenticated pointer requires PAC backend") ==
					std::string::npos)
					return 20;
			}
			std::cout << "MACHO_ARM64_AUTH_METADATA=PASS\n";
			std::cout << "MACHO_ARM64E_CHAINED_AUTH_METADATA=PASS\n";
		}

		const MachHeader64 thread_header{
			MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand64) + 152), 0, 0};
		SegmentCommand64 thread_segment = segment;
		thread_segment.header.command_bytes = sizeof(SegmentCommand64);
		thread_segment.section_count = 0;
		std::vector<std::uint8_t> thread_command(152, 0);
		const LoadCommand thread_load_command{LC_UNIXTHREAD, 152};
		std::memcpy(thread_command.data(), &thread_load_command, sizeof(thread_load_command));
		const std::uint32_t thread_flavor = 4;
		const std::uint32_t thread_count = 42;
		const std::uint64_t thread_rip = 0x100000200ull;
		std::memcpy(thread_command.data() + 8, &thread_flavor, sizeof(thread_flavor));
		std::memcpy(thread_command.data() + 12, &thread_count, sizeof(thread_count));
		std::memcpy(thread_command.data() + 16 + 16 * sizeof(std::uint64_t),
			&thread_rip, sizeof(thread_rip));
		std::ofstream thread_output(thread_path, std::ios::binary);
		thread_output.write(reinterpret_cast<const char*>(&thread_header), sizeof(thread_header));
		thread_output.write(reinterpret_cast<const char*>(&thread_segment), sizeof(thread_segment));
		thread_output.write(reinterpret_cast<const char*>(thread_command.data()),
			static_cast<std::streamsize>(thread_command.size()));
		thread_output.seekp(0x1000 - 1);
		thread_output.put('\0');
		thread_output.close();
		{
			const auto thread_image = darling::windows_host::MachOImage::Open(thread_path.wstring());
			if (thread_image.EntryOffset() != 0x200 ||
				thread_image.MapSegments().EntryAddress() == 0) {
				return 8;
			}
		}

		{
			const auto image = darling::windows_host::MachOImage::Open(path.wstring());
			std::cout << "MACHO_MAGIC=PASS\n";
			std::cout << "MACHO_CPU=" << image.Header().cpu_type << "\n";
			std::cout << "MACHO_SEGMENTS=" << image.Segments().size() << "\n";
			std::cout << "MACHO_SECTIONS=" << image.Sections().size() << "\n";
			std::cout << "MACHO_DEPENDENCIES=" << image.Dependencies().size() << "\n";
			if (image.Dependencies().size() != 1 || image.Dependencies()[0] != "@rpath/libSystem.B.dylib") {
				return 6;
			}
			if (image.FileBytes() != thin_bytes.size()) {
				return 7;
			}
			const auto mapping = image.MapSegments();
			if (image.Sections().size() != 1 || image.Sections()[0].section_name !=
				"__objc_classlist" || image.SectionAddress(mapping, "__DATA",
				"__objc_classlist") != static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(0x100000200) + mapping.Slide())) {
				return 9;
			}
			std::cout << "MACHO_OBJC_SECTION=PASS\n";
			std::cout << "MACHO_ENTRY=0x" << std::hex << image.EntryOffset() << std::dec << "\n";
			std::cout << "MACHO_MAPPING=PASS\n";
			std::cout << "MACHO_MAPPED_SEGMENTS=" << mapping.Segments().size() << "\n";
			std::cout << "MACHO_ENTRY_ADDRESS=0x" << std::hex << mapping.EntryAddress() << std::dec << "\n";
		}
		std::error_code cleanup_error;
		std::filesystem::remove(path, cleanup_error);
		std::filesystem::remove(mach32_path, cleanup_error);
		std::filesystem::remove(thread_path, cleanup_error);
		std::filesystem::remove(arm64_path, cleanup_error);
		std::filesystem::remove(arm64_userland_path, cleanup_error);
		std::filesystem::remove(arm64_userland24_path, cleanup_error);
		std::filesystem::remove(arm64_auth_path, cleanup_error);
		return cleanup_error ? 4 : 0;
	} catch (const std::exception& error) {
		std::cerr << "MACHO_SMOKE_ERROR=" << error.what() << "\n";
		return 5;
	}
}
