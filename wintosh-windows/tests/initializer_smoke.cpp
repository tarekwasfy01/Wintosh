/*
 * Stage 1 Mach-O __mod_init_func execution proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_macho.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace darling::windows_host;

namespace {

std::atomic<int> initializer_calls{0};
std::atomic<int> terminator_calls{0};

void TestInitializer()
{
	initializer_calls.fetch_add(1, std::memory_order_relaxed);
}

void TestTerminator()
{
	terminator_calls.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH)
			return 2;
		#if !defined(_WIN64)
		const auto win32_path = std::filesystem::path(temp_path) /
			(L"darling-initializer-win32-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		const MachHeader32 win32_header{
			MH_MAGIC, CPU_TYPE_X86, 3, MH_DYLIB, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + 2 * sizeof(Section32) + 80), 0};
		SegmentCommand32 win32_segment{};
		win32_segment.header = {LC_SEGMENT,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + 2 * sizeof(Section32))};
		std::memcpy(win32_segment.segment_name, "__DATA", 6);
		win32_segment.vm_address = 0x1000;
		win32_segment.vm_size = 0x1000;
		win32_segment.file_size = 0x1000;
		win32_segment.max_protection = 3;
		win32_segment.initial_protection = 3;
		win32_segment.section_count = 2;
		Section32 win32_init_section{};
		std::memcpy(win32_init_section.section_name, "__mod_init_func", 15);
		std::memcpy(win32_init_section.segment_name, "__DATA", 6);
		win32_init_section.address = 0x1200;
		win32_init_section.size = sizeof(std::uint32_t);
		win32_init_section.file_offset = 0x200;
		Section32 win32_term_section{};
		std::memcpy(win32_term_section.section_name, "__mod_term_func", 15);
		std::memcpy(win32_term_section.segment_name, "__DATA", 6);
		win32_term_section.address = 0x1204;
		win32_term_section.size = sizeof(std::uint32_t);
		win32_term_section.file_offset = 0x204;
		std::vector<std::uint8_t> win32_data(0x1000, 0);
		std::memcpy(win32_data.data(), &win32_header, sizeof(win32_header));
		std::memcpy(win32_data.data() + sizeof(win32_header), &win32_segment,
			sizeof(win32_segment));
		std::memcpy(win32_data.data() + sizeof(win32_header) + sizeof(win32_segment),
			&win32_init_section, sizeof(win32_init_section));
		std::memcpy(win32_data.data() + sizeof(win32_header) + sizeof(win32_segment) +
			sizeof(win32_init_section), &win32_term_section, sizeof(win32_term_section));
		std::vector<std::uint8_t> win32_thread(80, 0);
		const LoadCommand win32_thread_header{LC_UNIXTHREAD, 80};
		const std::uint32_t win32_thread_flavor = 1;
		const std::uint32_t win32_thread_count = 16;
		std::memcpy(win32_thread.data(), &win32_thread_header, sizeof(win32_thread_header));
		std::memcpy(win32_thread.data() + 8, &win32_thread_flavor, sizeof(win32_thread_flavor));
		std::memcpy(win32_thread.data() + 12, &win32_thread_count, sizeof(win32_thread_count));
		std::memcpy(win32_data.data() + sizeof(win32_header) + sizeof(win32_segment) +
			2 * sizeof(Section32), win32_thread.data(), win32_thread.size());
		const auto win32_init_callback = static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(&TestInitializer));
		const auto win32_term_callback = static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(&TestTerminator));
		std::memcpy(win32_data.data() + win32_init_section.file_offset,
			&win32_init_callback, sizeof(win32_init_callback));
		std::memcpy(win32_data.data() + win32_term_section.file_offset,
			&win32_term_callback, sizeof(win32_term_callback));
		std::ofstream win32_output(win32_path, std::ios::binary);
		win32_output.write(reinterpret_cast<const char*>(win32_data.data()),
			static_cast<std::streamsize>(win32_data.size()));
		win32_output.close();
		std::size_t win32_section_count = 0;
		{
			const auto win32_image = MachOImage::Open(win32_path.wstring());
			const auto win32_mapping = win32_image.MapSegments();
			win32_section_count = win32_image.Sections().size();
			win32_image.ExecuteInitializers(win32_mapping);
			win32_image.ExecuteTerminators(win32_mapping);
		}
		std::error_code win32_error;
		std::filesystem::remove(win32_path, win32_error);
		if (win32_error || win32_section_count != 2 ||
			initializer_calls.load(std::memory_order_relaxed) != 1 ||
			terminator_calls.load(std::memory_order_relaxed) != 1) {
			return 3;
		}
		std::cout << "MACHO_MOD_INIT_FUNC=PASS\n";
		std::cout << "MACHO_MOD_TERM_FUNC=PASS\n";
		return 0;
		#endif
		const auto path = std::filesystem::path(temp_path) /
			(L"darling-initializer-stage1-" + std::to_wstring(GetCurrentProcessId()) + L".mach-o");
		MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand64) + 2 * sizeof(Section64) +
				sizeof(MainCommand)), 0, 0};
		SegmentCommand64 segment{};
		segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(
			sizeof(SegmentCommand64) + 2 * sizeof(Section64))};
		std::memcpy(segment.segment_name, "__DATA", 6);
		segment.vm_address = 0x1000;
		segment.vm_size = 0x2000;
		segment.file_offset = 0;
		segment.file_size = 0x1000;
		segment.max_protection = 7;
		segment.initial_protection = 3;
		segment.section_count = 2;
		Section64 init_section{};
		std::memcpy(init_section.section_name, "__mod_init_func", 15);
		std::memcpy(init_section.segment_name, "__DATA", 6);
		init_section.address = 0x1200;
		init_section.size = sizeof(std::uintptr_t);
		init_section.file_offset = 0x200;
		Section64 term_section{};
		std::memcpy(term_section.section_name, "__mod_term_func", 15);
		std::memcpy(term_section.segment_name, "__DATA", 6);
		term_section.address = 0x1208;
		term_section.size = sizeof(std::uintptr_t);
		term_section.file_offset = 0x208;
		MainCommand main_command{{LC_MAIN, sizeof(MainCommand)}, 0x100, 0};
		std::vector<std::uint8_t> data(0x1000, 0);
		std::memcpy(data.data(), &header, sizeof(header));
		std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
		std::memcpy(data.data() + sizeof(header) + sizeof(segment), &init_section,
			sizeof(init_section));
		std::memcpy(data.data() + sizeof(header) + sizeof(segment) + sizeof(init_section),
			&term_section, sizeof(term_section));
		std::memcpy(data.data() + sizeof(header) + sizeof(segment) +
			2 * sizeof(init_section),
			&main_command, sizeof(main_command));
		const auto init_callback = reinterpret_cast<std::uintptr_t>(&TestInitializer);
		const auto term_callback = reinterpret_cast<std::uintptr_t>(&TestTerminator);
		std::memcpy(data.data() + init_section.file_offset, &init_callback, sizeof(init_callback));
		std::memcpy(data.data() + term_section.file_offset, &term_callback, sizeof(term_callback));
		data[0x100] = 0xc3;
		std::ofstream output(path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(data.data()),
			static_cast<std::streamsize>(data.size()));
		output.close();
		std::size_t section_count = 0;
		{
			const auto image = MachOImage::Open(path.wstring());
			const auto mapping = image.MapSegments();
			section_count = image.Sections().size();
			image.ExecuteInitializers(mapping);
			image.ExecuteTerminators(mapping);
		}
		std::error_code error;
		std::filesystem::remove(path, error);
		if (error || section_count != 2 ||
			initializer_calls.load(std::memory_order_relaxed) != 1 ||
			terminator_calls.load(std::memory_order_relaxed) != 1) {
			std::cerr << "INIT_CLEANUP=" << error.value()
				<< " INIT_CALLS=" << initializer_calls.load(std::memory_order_relaxed)
				<< " TERM_CALLS=" << terminator_calls.load(std::memory_order_relaxed)
				<< " INIT_SECTIONS=" << section_count << "\n";
			return 3;
		}
		std::cout << "MACHO_MOD_INIT_FUNC=PASS\n";
		std::cout << "MACHO_MOD_TERM_FUNC=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "INITIALIZER_SMOKE_ERROR=" << error.what() << "\n";
		return 4;
	}
}
