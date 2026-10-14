/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"
#include "darling_windows_dyld.h"
#include "darling_windows_macho.h"

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void WriteExitMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	std::vector<std::uint8_t> data(0x400, 0);
	MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 5,
		static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(MainCommand) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand) + sizeof(DyldInfoCommand)), 0, 0};
	std::memcpy(data.data(), &header, sizeof(header));

	SegmentCommand64 segment{};
	segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x200;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));

	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));

	const auto symtab_offset = sizeof(header) + sizeof(segment) + sizeof(main);
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(symtab))};
	symtab.symbol_offset = 0x240;
	symtab.symbol_count = 1;
	symtab.string_offset = 0x260;
	symtab.string_bytes = 7;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));

	const auto dysymtab_offset = symtab_offset + sizeof(symtab);
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, static_cast<std::uint32_t>(sizeof(dysymtab))};
	dysymtab.undefined_symbol_index = 0;
	dysymtab.undefined_symbol_count = 1;
	std::memcpy(data.data() + dysymtab_offset, &dysymtab, sizeof(dysymtab));

	const auto dyld_info_offset = dysymtab_offset + sizeof(dysymtab);
	DyldInfoCommand dyld_info{};
	dyld_info.header = {LC_DYLD_INFO, static_cast<std::uint32_t>(sizeof(dyld_info))};
	dyld_info.binding_offset = 0x300;
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'e', 'x', 'i', 't', 0, 0x51, 0x70, 0x40, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream, sizeof(bind_stream));

	NList64 symbol{};
	symbol.name_offset = 1;
	symbol.type = 0x01;
	symbol.section = 0;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(data.data() + symtab.string_offset + 1, "_exit", 6);

	// Set the exit argument in the active guest ABI, then call the bound slot.
	const std::uint8_t entry[] = {
		#if defined(__clang__) && defined(_WIN64)
		0xbf, 0x17, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x08,
		#else
		0xb9, 0x17, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x28,
		#endif
		0x48, 0x8b, 0x05, 0x30, 0x00, 0x00, 0x00,
		0xff, 0xd0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write Mach-O exit fixture");
	}
}

void WriteDarwinStartMachO(const std::filesystem::path& path)
{
	WriteExitMachO(path);
	std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
	if (!output)
		throw std::runtime_error("cannot reopen Darwin startup fixture");
	// Load Darwin argc from the initial stack and call _exit(argc).
	// The bound _exit slot is at 0x240.
	const std::uint8_t entry[] = {
		0x8b, 0x3c, 0x24,
		0x48, 0x8b, 0x05, 0x36, 0x00, 0x00, 0x00,
		0xff, 0xd0, 0xc3};
	output.seekp(0x200);
	output.write(reinterpret_cast<const char*>(entry), sizeof(entry));
	if (!output)
		throw std::runtime_error("cannot write Darwin startup fixture");
}

void WriteDarwinStartEnvMachO(const std::filesystem::path& path)
{
	WriteExitMachO(path);
	std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
	if (!output)
		throw std::runtime_error("cannot reopen Darwin env startup fixture");
	// DarwinStart stack layout with argc=1 places argv[1] at [rsp+16] and
	// envp[0] at [rsp+24].
	// The existing _exit bind slot remains at file offset 0x240.
	const std::uint8_t entry[] = {
		0x83, 0x3c, 0x24, 0x01, 0x75, 0x1e,
		0x48, 0x83, 0x7c, 0x24, 0x10, 0x00, 0x75, 0x16,
		0x48, 0x83, 0x7c, 0x24, 0x18, 0x00, 0x74, 0x0e,
		0x31, 0xff, 0x48, 0x8b, 0x05, 0x21, 0x00, 0x00,
		0x00, 0xff, 0xd0, 0xc3, 0xbf, 0x5b, 0x00, 0x00,
		0x00, 0x48, 0x8b, 0x05, 0x10, 0x00, 0x00, 0x00,
		0xff, 0xd0, 0xc3};
	output.seekp(0x200);
	output.write(reinterpret_cast<const char*>(entry), sizeof(entry));
	if (!output)
		throw std::runtime_error("cannot write Darwin env startup fixture");
}

void WriteHelloMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	WriteExitMachO(path);
	std::ifstream input(path, std::ios::binary);
	std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
	if (data.empty())
		throw std::runtime_error("cannot reopen Mach-O hello fixture");

	const auto symtab_offset = sizeof(MachHeader64) + sizeof(SegmentCommand64) +
		sizeof(MainCommand);
	SymtabCommand symtab{};
	std::memcpy(&symtab, data.data() + symtab_offset, sizeof(symtab));
	// The longer hello entry reaches past the compact exit fixture's tables.
	// Keep the symbol/string/bind records outside the generated text bytes.
	symtab.symbol_offset = 0x2c0;
	symtab.string_offset = 0x300;
	symtab.symbol_count = 2;
	// The string table contains the leading NUL, both symbol names, and
	// the terminating NUL after _exit: "\0_puts\0_exit\0".
	symtab.string_bytes = 13;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	DYSymtabCommand dysymtab{};
	std::memcpy(&dysymtab, data.data() + symtab_offset + sizeof(SymtabCommand),
		sizeof(dysymtab));
	dysymtab.undefined_symbol_count = 2;
	std::memcpy(data.data() + symtab_offset + sizeof(SymtabCommand), &dysymtab,
		sizeof(dysymtab));

	NList64 puts_symbol{};
	puts_symbol.name_offset = 1;
	puts_symbol.type = 0x01;
	NList64 exit_symbol{};
	exit_symbol.name_offset = 7;
	exit_symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &puts_symbol, sizeof(puts_symbol));
	std::memcpy(data.data() + symtab.symbol_offset + sizeof(NList64), &exit_symbol,
		sizeof(exit_symbol));
	std::memset(data.data() + symtab.string_offset, 0, 32);
	std::memcpy(data.data() + symtab.string_offset + 1, "_puts", 5);
	std::memcpy(data.data() + symtab.string_offset + 7, "_exit", 5);

	const auto dyld_info_offset = symtab_offset + sizeof(SymtabCommand) +
		sizeof(DYSymtabCommand);
	DyldInfoCommand dyld_info{};
	std::memcpy(&dyld_info, data.data() + dyld_info_offset, sizeof(dyld_info));
	dyld_info.binding_offset = 0x340;
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'p', 'u', 't', 's', 0, 0x51, 0x70, 0x40, 0x90,
		0x11, 0x40, '_', 'e', 'x', 'i', 't', 0, 0x51, 0x70, 0x48, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memset(data.data() + dyld_info.binding_offset, 0, 64);
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream, sizeof(bind_stream));

	// argc is in edi under the real x86_64 Darwin main ABI. With one argument
	// (the program name), call puts; otherwise call _exit(7).
	const std::uint8_t entry[] = {
		0x83, 0xff, 0x01, 0x75, 0x29,
		0x48, 0x8d, 0x3d, 0x74, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8b, 0x05, 0x29, 0x00, 0x00, 0x00,
		0xff, 0xd0, 0x48, 0x83, 0xc4, 0x08, 0x31, 0xc0, 0xc3,
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90,
		0xbf, 0x07, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8b, 0x05, 0x0a, 0x00, 0x00, 0x00, 0xff, 0xd0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
	const char text[] = "hello from guest\n";
	std::memcpy(data.data() + 0x280, text, sizeof(text));
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output)
		throw std::runtime_error("cannot write Mach-O hello fixture");
}

void WriteAtexitMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	WriteHelloMachO(path);
	std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
	if (!output)
		throw std::runtime_error("cannot reopen Mach-O atexit fixture");
	std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(output)), {});
	if (data.empty())
		throw std::runtime_error("cannot read Mach-O atexit fixture");

	const auto symtab_offset = sizeof(MachHeader64) + sizeof(SegmentCommand64) +
		 sizeof(MainCommand);
	SymtabCommand symtab{};
	std::memcpy(&symtab, data.data() + symtab_offset, sizeof(symtab));
	symtab.string_bytes = 16; // "\0_atexit\0_puts\0"
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	NList64 atexit_symbol{};
	atexit_symbol.name_offset = 1;
	atexit_symbol.type = 0x01;
	NList64 puts_symbol{};
	puts_symbol.name_offset = 10;
	puts_symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &atexit_symbol,
		sizeof(atexit_symbol));
	std::memcpy(data.data() + symtab.symbol_offset + sizeof(NList64), &puts_symbol,
		sizeof(puts_symbol));
	std::memset(data.data() + symtab.string_offset, 0, 32);
	std::memcpy(data.data() + symtab.string_offset + 1, "_atexit", 7);
	std::memcpy(data.data() + symtab.string_offset + 10, "_puts", 5);

	const auto dyld_info_offset = symtab_offset + sizeof(SymtabCommand) +
		 sizeof(DYSymtabCommand);
	DyldInfoCommand dyld_info{};
	std::memcpy(&dyld_info, data.data() + dyld_info_offset, sizeof(dyld_info));
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'a', 't', 'e', 'x', 'i', 't', 0, 0x51, 0x70, 0x40,
		0x90, 0x11, 0x40, '_', 'p', 'u', 't', 's', 0, 0x51, 0x70, 0x48,
		0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memset(data.data() + dyld_info.binding_offset, 0, 64);
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream,
		sizeof(bind_stream));

	// Entry: register callback at 0x250, return 0. Callback at 0x250 calls
	// _puts("guest atexit callback\n") and returns to the host finalizer.
	const std::uint8_t entry[] = {
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8d, 0x3d, 0x45, 0x00, 0x00, 0x00,
		0x48, 0x8b, 0x05, 0x2e, 0x00, 0x00, 0x00, 0xff, 0xd0,
		0x48, 0x83, 0xc4, 0x08, 0x31, 0xc0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
	const std::uint8_t callback[] = {
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8d, 0x3d, 0x25, 0x00, 0x00, 0x00,
		0x48, 0x8b, 0x05, 0xe6, 0xff, 0xff, 0xff, 0xff, 0xd0,
		0x48, 0x83, 0xc4, 0x08, 0xc3};
	std::memcpy(data.data() + 0x250, callback, sizeof(callback));
	const char text[] = "guest atexit callback\n";
	std::memcpy(data.data() + 0x280, text, sizeof(text));

	output.seekp(0);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output)
		throw std::runtime_error("cannot write Mach-O atexit fixture");
}

void WriteCxaAtexitMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	WriteAtexitMachO(path);
	std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
	if (!output)
		throw std::runtime_error("cannot reopen Mach-O cxa_atexit fixture");
	std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(output)), {});
	if (data.empty())
		throw std::runtime_error("cannot read Mach-O cxa_atexit fixture");
	const auto symtab_offset = sizeof(MachHeader64) + sizeof(SegmentCommand64) +
		 sizeof(MainCommand);
	SymtabCommand symtab{};
	std::memcpy(&symtab, data.data() + symtab_offset, sizeof(symtab));
	symtab.string_bytes = 21; // "\0___cxa_atexit\0_puts\0"
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	NList64 cxa_symbol{};
	cxa_symbol.name_offset = 1;
	cxa_symbol.type = 0x01;
	NList64 puts_symbol{};
	puts_symbol.name_offset = 15;
	puts_symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &cxa_symbol, sizeof(cxa_symbol));
	std::memcpy(data.data() + symtab.symbol_offset + sizeof(NList64), &puts_symbol,
		sizeof(puts_symbol));
	std::memset(data.data() + symtab.string_offset, 0, 32);
	std::memcpy(data.data() + symtab.string_offset + 1, "___cxa_atexit", 13);
	std::memcpy(data.data() + symtab.string_offset + 15, "_puts", 5);

	const auto dyld_info_offset = symtab_offset + sizeof(SymtabCommand) +
		 sizeof(DYSymtabCommand);
	DyldInfoCommand dyld_info{};
	std::memcpy(&dyld_info, data.data() + dyld_info_offset, sizeof(dyld_info));
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', '_', '_', 'c', 'x', 'a', '_', 'a', 't', 'e', 'x', 'i', 't', 0,
		0x51, 0x70, 0x40, 0x90,
		0x11, 0x40, '_', 'p', 'u', 't', 's', 0, 0x51, 0x70, 0x48, 0x90, 0x00};
	dyld_info.binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memset(data.data() + dyld_info.binding_offset, 0, 64);
	std::memcpy(data.data() + dyld_info.binding_offset, bind_stream,
		sizeof(bind_stream));

	// Pass callback, null argument and null DSO handle using the Darwin ABI.
	const std::uint8_t entry[] = {
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8d, 0x3d, 0x45, 0x00, 0x00, 0x00,
		0x31, 0xf6, 0x31, 0xd2,
		0x48, 0x8b, 0x05, 0x2a, 0x00, 0x00, 0x00, 0xff, 0xd0,
		0x48, 0x83, 0xc4, 0x08, 0x31, 0xc0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
	output.seekp(0);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output)
		throw std::runtime_error("cannot write Mach-O cxa_atexit fixture");
}

void WritePrintfMachO(const std::filesystem::path& path)
{
	using namespace darling::windows_host;
	std::vector<std::uint8_t> data(0x400, 0);
	MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, 5,
		static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(MainCommand) +
			sizeof(SymtabCommand) + sizeof(DYSymtabCommand) + sizeof(DyldInfoCommand)), 0, 0};
	std::memcpy(data.data(), &header, sizeof(header));

	SegmentCommand64 segment{};
	segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x200;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));

	MainCommand main{};
	main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
	main.entry_offset = 0x200;
	std::memcpy(data.data() + sizeof(header) + sizeof(segment), &main, sizeof(main));

	const auto symtab_offset = sizeof(header) + sizeof(segment) + sizeof(main);
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(symtab))};
	symtab.symbol_offset = 0x240;
	symtab.symbol_count = 1;
	symtab.string_offset = 0x260;
	symtab.string_bytes = 9;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));

	const auto dysymtab_offset = symtab_offset + sizeof(symtab);
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, static_cast<std::uint32_t>(sizeof(dysymtab))};
	dysymtab.undefined_symbol_index = 0;
	dysymtab.undefined_symbol_count = 1;
	std::memcpy(data.data() + dysymtab_offset, &dysymtab, sizeof(dysymtab));

	const auto dyld_info_offset = dysymtab_offset + sizeof(dysymtab);
	DyldInfoCommand dyld_info{};
	dyld_info.header = {LC_DYLD_INFO, static_cast<std::uint32_t>(sizeof(dyld_info))};
	dyld_info.lazy_binding_offset = 0x300;
	const std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'p', 'r', 'i', 'n', 't', 'f', 0, 0x51, 0x70, 0x40, 0x90, 0x00};
	dyld_info.lazy_binding_size = sizeof(bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_info.lazy_binding_offset, bind_stream, sizeof(bind_stream));

	NList64 symbol{};
	symbol.name_offset = 1;
	symbol.type = 0x01;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	std::memcpy(data.data() + symtab.string_offset + 1, "_printf", 7);

	// lea rdi, format; mov esi, 7; align SysV stack; call bound printf; return 0.
	const std::uint8_t entry[] = {
		0x48, 0x8d, 0x3d, 0x19, 0x00, 0x00, 0x00,
		0xbe, 0x07, 0x00, 0x00, 0x00,
		0x48, 0x83, 0xec, 0x08,
		0x48, 0x8b, 0x05, 0x29, 0x00, 0x00, 0x00,
		0xff, 0xd0,
		0x48, 0x83, 0xc4, 0x08,
		0x31, 0xc0, 0xc3};
	std::memcpy(data.data() + 0x200, entry, sizeof(entry));
	const char format[] = "mach-o printf %d\n";
	std::memcpy(data.data() + 0x220, format, sizeof(format));

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write Mach-O printf fixture");
	}
}

void WriteLazySessionMachO(const std::filesystem::path& path)
{
	WritePrintfMachO(path);
	using namespace darling::windows_host;
	std::ifstream input(path, std::ios::binary);
	std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
	if (data.empty())
		throw std::runtime_error("cannot reopen Mach-O session fixture");
	const std::string symbol = "_wintosh_lazy";
	std::memset(data.data() + 0x260, 0, 32);
	std::memcpy(data.data() + 0x260, symbol.c_str(), symbol.size());
	SymtabCommand symtab{};
	const auto symtab_offset = sizeof(MachHeader64) + sizeof(SegmentCommand64) +
		sizeof(MainCommand);
	std::memcpy(&symtab, data.data() + symtab_offset, sizeof(symtab));
	symtab.string_bytes = static_cast<std::uint32_t>(symbol.size() + 1);
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	const auto dyld_info_offset = symtab_offset + sizeof(SymtabCommand) + sizeof(DYSymtabCommand);
	DyldInfoCommand dyld_info{};
	std::memcpy(&dyld_info, data.data() + dyld_info_offset, sizeof(dyld_info));
	std::vector<std::uint8_t> bind{0x11, 0x40};
	bind.insert(bind.end(), symbol.begin(), symbol.end());
	bind.insert(bind.end(), {0, 0x51, 0x70, 0x40, 0x90, 0});
	dyld_info.lazy_binding_size = static_cast<std::uint32_t>(bind.size());
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memset(data.data() + dyld_info.lazy_binding_offset, 0, 64);
	std::memcpy(data.data() + dyld_info.lazy_binding_offset, bind.data(), bind.size());
	for (std::size_t i = 0x200; i + 2 < data.size(); ++i) {
		if (data[i] == 0x31 && data[i + 1] == 0xc0 && data[i + 2] == 0xc3) {
			data[i] = 0xc3;
			data[i + 1] = 0x90;
			data[i + 2] = 0x90;
			break;
		}
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output)
		throw std::runtime_error("cannot write Mach-O session fixture");
}

std::vector<wchar_t> CommandLine(const std::wstring& value)
{
	std::vector<wchar_t> result(value.begin(), value.end());
	result.push_back(L'\0');
	return result;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	try {
#if !defined(_WIN64)
		// The fixture is an x86-64 Mach-O execution proof; Win32 is metadata-only.
		return 0;
#else
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-darwin") == 0) {
			WriteDarwinStartMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-darwin-env") == 0) {
			WriteDarwinStartEnvMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit") == 0) {
			WriteExitMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-hello") == 0) {
			WriteHelloMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-atexit") == 0) {
			WriteAtexitMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-cxa-atexit") == 0) {
			WriteCxaAtexitMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-printf") == 0) {
			WritePrintfMachO(argv[2]);
			return 0;
		}
		if (argc == 3 && std::wcscmp(argv[1], L"--emit-session") == 0) {
			WriteLazySessionMachO(argv[2]);
			return 0;
		}
		if (argc > 2 && std::wcscmp(argv[1], L"--child") == 0) {
			if (argc > 3) {
				std::ofstream diagnostic(argv[3], std::ios::trunc);
				diagnostic << "before-bootstrap";
			}
			darling::windows_host::DarwinLaunchOptions options;
			options.entry_abi = darling::windows_host::DarwinEntryAbi::SyntheticCompatibleMain;
			const int result = darling::windows_host::DarwinBootstrap::Run(argv[2], options);
			if (argc > 3) {
				std::ofstream diagnostic(argv[3], std::ios::trunc);
				diagnostic << "after-bootstrap:" << result;
			}
			return result;
		}

		wchar_t temp_path[MAX_PATH]{};
		const DWORD length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) {
			return 1;
		}
		const auto image = std::filesystem::path(temp_path) /
			(L"darling-macho-exit-" + std::to_wstring(GetCurrentProcessId()) + L".macho");
		const auto diagnostic_path = image.wstring() + L".error";
		WriteExitMachO(image);

		wchar_t executable[MAX_PATH]{};
		const DWORD executable_length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
		if (executable_length == 0 || executable_length >= MAX_PATH) {
			std::filesystem::remove(image);
			return 2;
		}
		const std::wstring command_line = L"\"" + std::wstring(executable, executable_length) +
			L"\" --child \"" + image.wstring() + L"\" \"" + diagnostic_path + L"\"";
		auto command = CommandLine(command_line);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
			std::filesystem::remove(image);
			return 3;
		}
		CloseHandle(process.hThread);
		const DWORD wait_result = WaitForSingleObject(process.hProcess, 5000);
		if (wait_result == WAIT_TIMEOUT) {
			TerminateProcess(process.hProcess, 8);
			WaitForSingleObject(process.hProcess, INFINITE);
		}
		DWORD status = 0;
		const BOOL queried = GetExitCodeProcess(process.hProcess, &status);
		CloseHandle(process.hProcess);
		std::filesystem::remove(image);
		if (queried == FALSE || status != 23) {
			std::string diagnostic_text;
			{
				std::ifstream diagnostic(diagnostic_path);
				diagnostic_text.assign((std::istreambuf_iterator<char>(diagnostic)),
					std::istreambuf_iterator<char>());
			}
			std::cerr << "MACHO_EXIT_CHILD_STATUS=" << status << "\n";
			if (!diagnostic_text.empty()) {
				std::cerr << "MACHO_EXIT_CHILD_ERROR=" << diagnostic_text << "\n";
			}
			std::filesystem::remove(diagnostic_path);
			return 4;
		}
		std::filesystem::remove(diagnostic_path);
		return 0;
#endif
	} catch (const std::exception& error) {
		if (argc > 3) {
			std::ofstream diagnostic(argv[3], std::ios::trunc);
			diagnostic << error.what();
		}
		std::cerr << "MACHO_EXIT_CHILD_ERROR=" << error.what() << "\n";
		return 6;
	} catch (...) {
		if (argc > 3) {
			std::ofstream diagnostic(argv[3], std::ios::trunc);
			diagnostic << "unknown";
		}
		std::cerr << "MACHO_EXIT_CHILD_ERROR=unknown\n";
		return 7;
	}
}
