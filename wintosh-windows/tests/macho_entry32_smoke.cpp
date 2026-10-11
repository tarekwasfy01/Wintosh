/*
 * i386 Mach-O LC_UNIXTHREAD entry-frame proof.
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
static int RunEntry32()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const auto length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) return 2;
		const auto path = std::filesystem::path(temp_path) /
			(L"wintosh-entry32-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		constexpr std::uint32_t thread_bytes = 16 + 16 * sizeof(std::uint32_t);
		MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, MH_EXECUTE, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + thread_bytes), 0};
		SegmentCommand32 segment{};
		segment.header = {LC_SEGMENT, sizeof(SegmentCommand32)};
		std::memcpy(segment.segment_name, "__TEXT", 6);
		segment.vm_address = 0x1000;
		segment.vm_size = 0x1000;
		segment.file_offset = 0;
		segment.file_size = 0x300;
		segment.max_protection = 5;
		segment.initial_protection = 5;
		std::vector<std::uint8_t> thread(thread_bytes, 0);
		const LoadCommand thread_header{LC_UNIXTHREAD, thread_bytes};
		const std::uint32_t flavor = 1;
		const std::uint32_t count = 16;
		const std::uint32_t eip = 0x1100;
		std::memcpy(thread.data(), &thread_header, sizeof(thread_header));
		std::memcpy(thread.data() + 8, &flavor, sizeof(flavor));
		std::memcpy(thread.data() + 12, &count, sizeof(count));
		std::memcpy(thread.data() + 16 + 10 * sizeof(std::uint32_t), &eip, sizeof(eip));
		std::vector<std::uint8_t> bytes(0x300, 0);
		std::size_t cursor = 0;
		std::memcpy(bytes.data() + cursor, &header, sizeof(header)); cursor += sizeof(header);
		std::memcpy(bytes.data() + cursor, &segment, sizeof(segment)); cursor += sizeof(segment);
		std::memcpy(bytes.data() + cursor, thread.data(), thread.size());
		const std::uint8_t code[] = {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3};
		std::memcpy(bytes.data() + 0x100, code, sizeof(code));
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		output.close();
		const auto image = MachOImage::Open(path.wstring());
		const auto mapping = image.MapSegments();
		const auto result = image.ExecuteEntry(mapping, {"entry32", "one"}, {"PATH=/usr/bin"});
		DeleteFileW(path.c_str());
		const bool ok = image.Is32Bit() && image.EntryOffset() == 0x100 && result == 42;
		std::cout << "MACHO_ENTRY_32=" << (ok ? "PASS" : "FAIL") << "\n";
		return ok ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << "\n";
		return 1;
	}
}
#endif

int wmain()
{
#if defined(_WIN64)
	std::cout << "MACHO_ENTRY_32=SKIP_X64\n";
	return 0;
#else
	return RunEntry32();
#endif
}
