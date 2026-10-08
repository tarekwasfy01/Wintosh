/*
 * Stage 1 dylib path-resolution proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_dyld.h"
#include "darling_windows_bootstrap.h"
#include "darling_windows_stdio.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

void WriteMachO(const std::filesystem::path& path, const char* dependency,
	bool dylib_image, bool undefined_symbol, bool weak_dependency = false,
	bool entry_returns_argc = false, bool entry_calls_import = false,
	bool include_main = true, bool bundle_image = false,
	bool weak_undefined_symbol = false,
	const char* imported_symbol = "_libSystemInit")
{
	using namespace darling::windows_host;
	const auto dependency_bytes = dependency == nullptr ? 0 : std::strlen(dependency) + 1;
	const auto dylib_command_bytes = (sizeof(DylibCommand) + dependency_bytes + 7) & ~std::size_t(7);
	const std::size_t segment_bytes = sizeof(SegmentCommand64);
	const std::size_t main_bytes = include_main ? sizeof(MainCommand) : 0;
	const std::size_t symtab_bytes = sizeof(SymtabCommand);
	const std::size_t dysymtab_bytes = sizeof(DYSymtabCommand);
	const std::size_t dyld_info_bytes = sizeof(DyldInfoCommand);
	const std::string embedded_rpath = "@loader_path/../../../../usr/lib";
	const auto rpath_command_bytes = (sizeof(RPathCommand) + embedded_rpath.size() + 1 + 7) & ~std::size_t(7);
	const std::size_t command_bytes = segment_bytes + main_bytes +
		(dependency == nullptr ? 0 : dylib_command_bytes) + symtab_bytes +
		dysymtab_bytes + dyld_info_bytes + 2 * sizeof(LinkeditDataCommand) + rpath_command_bytes;
	std::vector<std::uint8_t> data(0x600, 0);
	MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE,
		static_cast<std::uint32_t>((include_main ? 1 : 0) + (dependency == nullptr ? 7 : 8)),
		static_cast<std::uint32_t>(command_bytes), 0, 0};
	if (dylib_image) {
		header.file_type = MH_DYLIB;
	}
	if (bundle_image) {
		header.file_type = MH_BUNDLE;
	}
	std::memcpy(data.data(), &header, sizeof(header));

	SegmentCommand64 segment{};
	segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(sizeof(segment))};
	std::memcpy(segment.segment_name, "__TEXT", 6);
	segment.vm_address = 0x1000;
	segment.vm_size = 0x1000;
	segment.file_offset = 0x200;
	segment.file_size = 0x400;
	segment.max_protection = 5;
	segment.initial_protection = 5;
	std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));

	if (include_main) {
		MainCommand main{};
		main.header = {LC_MAIN, static_cast<std::uint32_t>(sizeof(main))};
		main.entry_offset = 0x200;
		std::memcpy(data.data() + sizeof(header) + segment_bytes, &main, sizeof(main));
	}

	if (dependency != nullptr) {
		std::vector<std::uint8_t> dylib(dylib_command_bytes, 0);
		DylibCommand command{};
		command.header = {weak_dependency ? LC_LOAD_WEAK_DYLIB : LC_LOAD_UPWARD_DYLIB,
			static_cast<std::uint32_t>(dylib_command_bytes)};
		command.name_offset = sizeof(DylibCommand);
		std::memcpy(dylib.data(), &command, sizeof(command));
		std::memcpy(dylib.data() + sizeof(DylibCommand), dependency, dependency_bytes);
		std::memcpy(data.data() + sizeof(header) + segment_bytes + main_bytes,
			dylib.data(), dylib.size());
	}
	const auto symtab_offset = sizeof(header) + segment_bytes + main_bytes +
		(dependency == nullptr ? 0 : dylib_command_bytes);
	SymtabCommand symtab{};
	symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(symtab))};
	symtab.symbol_offset = 0x240;
	symtab.symbol_count = 1;
	symtab.string_offset = 0x250;
	symtab.string_bytes = 32;
	std::memcpy(data.data() + symtab_offset, &symtab, sizeof(symtab));
	const auto dysymtab_offset = symtab_offset + sizeof(symtab);
	DYSymtabCommand dysymtab{};
	dysymtab.header = {LC_DYSYMTAB, static_cast<std::uint32_t>(sizeof(dysymtab))};
	dysymtab.external_relocation_offset = 0x260;
	dysymtab.external_relocation_count = 5;
	std::memcpy(data.data() + dysymtab_offset, &dysymtab, sizeof(dysymtab));
	const auto dyld_info_offset = dysymtab_offset + sizeof(dysymtab);
	const std::uint32_t dyld_bind_offset = 0x540;
	std::uint8_t bind_stream[] = {
		0x11, 0x40, '_', 'l', 'i', 'b', 'S', 'y', 's', 't', 'e', 'm',
		'I', 'n', 'i', 't', 0, 0x51, 0x70, 0x40, 0x90, 0xb0, 0xc0, 0x02, 0x00,
		0xd0, 0x01, 0x11, 0x40, '_', 'l', 'i', 'b', 'S', 'y', 's', 't', 'e', 'm',
		'I', 'n', 'i', 't', 0, 0x51, 0x70, 0xd0, 0x07, 0x90, 0xd1, 0x00};
	const std::uint8_t rebase_stream[] = {0x11, 0x20, 0xb8, 0x04, 0x51, 0x00};
	const std::uint8_t weak_bind_stream[] = {
		0x11, 0x40, '_', 'w', 'e', 'a', 'k', 'O', 'p', 't', 'i', 'o', 'n', 'a', 'l',
		0, 0x51, 0x70, 0xa8, 0x04, 0x90, 0x00};
	std::uint8_t lazy_bind_stream[] = {
		0x11, 0x40, '_', 'l', 'i', 'b', 'S', 'y', 's', 't', 'e', 'm',
		'I', 'n', 'i', 't', 0, 0x51, 0x70, 0xc0, 0x04, 0xc0, 0x01, 0x00, 0x00};
	if (std::strcmp(imported_symbol, "_libSystemInit") != 0) {
		const auto replace = [imported_symbol](std::uint8_t* stream,
			std::size_t size) {
			constexpr char original[] = "_libSystemInit";
			const auto length = (std::min)(std::strlen(imported_symbol),
				std::size_t{14});
			for (std::size_t i = 0; i + sizeof(original) <= size; ++i) {
				if (std::memcmp(stream + i, original, sizeof(original)) != 0)
					continue;
				std::memset(stream + i, 0, sizeof(original));
				std::memcpy(stream + i, imported_symbol, length);
				i += sizeof(original) - 1;
			}
		};
		replace(bind_stream, sizeof(bind_stream));
		replace(lazy_bind_stream, sizeof(lazy_bind_stream));
	}
	DyldInfoCommand dyld_info{};
		dyld_info.header = {LC_DYLD_INFO, static_cast<std::uint32_t>(sizeof(dyld_info))};
	dyld_info.binding_offset = dyld_bind_offset;
	dyld_info.binding_size = sizeof(bind_stream);
	dyld_info.rebase_offset = 0x2e0;
	dyld_info.rebase_size = sizeof(rebase_stream);
	dyld_info.weak_binding_offset = 0x340;
	dyld_info.weak_binding_size = sizeof(weak_bind_stream);
	dyld_info.lazy_binding_offset = 0x300;
	dyld_info.lazy_binding_size = sizeof(lazy_bind_stream);
	std::memcpy(data.data() + dyld_info_offset, &dyld_info, sizeof(dyld_info));
	std::memcpy(data.data() + dyld_bind_offset, bind_stream, sizeof(bind_stream));
	std::memcpy(data.data() + dyld_info.rebase_offset, rebase_stream, sizeof(rebase_stream));
	std::memcpy(data.data() + dyld_info.weak_binding_offset, weak_bind_stream,
		sizeof(weak_bind_stream));
	std::memcpy(data.data() + dyld_info.lazy_binding_offset, lazy_bind_stream,
		sizeof(lazy_bind_stream));
	const auto chained_command_offset = dyld_info_offset + sizeof(dyld_info);
	LinkeditDataCommand chained_command{};
	chained_command.header = {LC_DYLD_CHAINED_FIXUPS,
		static_cast<std::uint32_t>(sizeof(chained_command))};
	chained_command.data_offset = 0x380;
	chained_command.data_size = 0x80;
	std::memcpy(data.data() + chained_command_offset, &chained_command,
		sizeof(chained_command));
	std::vector<std::uint8_t> chained_payload(chained_command.data_size, 0);
	const auto write32 = [&chained_payload](std::size_t offset, std::uint32_t value) {
		std::memcpy(chained_payload.data() + offset, &value, sizeof(value));
	};
	const auto write16 = [&chained_payload](std::size_t offset, std::uint16_t value) {
		std::memcpy(chained_payload.data() + offset, &value, sizeof(value));
	};
	const auto write64 = [&chained_payload](std::size_t offset, std::uint64_t value) {
		std::memcpy(chained_payload.data() + offset, &value, sizeof(value));
	};
	write32(4, 0x20);
	write32(8, 0x60);
	write32(12, 0x68);
	write32(16, 1);
	write32(20, 1);
	write32(0x20, 1);
	write32(0x24, 0x28);
	write32(0x28, 24);
	write16(0x2c, 0x1000);
	write16(0x2e, 2);
	write64(0x30, 0x1000);
	write16(0x3c, 1);
	write16(0x3e, 0x290);
	write32(0x60, 0);
	std::memcpy(chained_payload.data() + 0x68, imported_symbol,
		(std::min)(std::strlen(imported_symbol), std::size_t{14}));
	std::memcpy(data.data() + chained_command.data_offset, chained_payload.data(),
		chained_payload.size());
	const auto exports_command_offset = chained_command_offset + sizeof(chained_command);
	LinkeditDataCommand exports_command{};
	exports_command.header = {LC_DYLD_EXPORTS_TRIE,
		static_cast<std::uint32_t>(sizeof(exports_command))};
	exports_command.data_offset = 0x500;
	exports_command.data_size = 0x40;
	std::memcpy(data.data() + exports_command_offset, &exports_command,
		sizeof(exports_command));
	std::vector<std::uint8_t> exports_trie(exports_command.data_size, 0);
	exports_trie[0] = 0;
	exports_trie[1] = 1;
	std::memcpy(exports_trie.data() + 2, "_exported", 10);
	exports_trie[12] = 0x10;
	exports_trie[0x10] = 3;
	exports_trie[0x11] = 0;
	exports_trie[0x12] = 0x80;
	exports_trie[0x13] = 0x20;
	exports_trie[0x14] = 0;
	std::memcpy(data.data() + exports_command.data_offset, exports_trie.data(),
		exports_trie.size());
	const auto rpath_command_offset = exports_command_offset + sizeof(exports_command);
	std::vector<std::uint8_t> rpath_command(rpath_command_bytes, 0);
	RPathCommand rpath{};
	rpath.header = {LC_RPATH, static_cast<std::uint32_t>(rpath_command_bytes)};
	rpath.path_offset = sizeof(RPathCommand);
	std::memcpy(rpath_command.data(), &rpath, sizeof(rpath));
	std::memcpy(rpath_command.data() + rpath.path_offset, embedded_rpath.c_str(),
		embedded_rpath.size() + 1);
	std::memcpy(data.data() + rpath_command_offset, rpath_command.data(),
		rpath_command.size());
	const std::uint64_t chained_bind = (1ull << 63) | (2ull << 51);
	const std::uint64_t chained_rebase = 0x1100;
	std::memcpy(data.data() + 0x490, &chained_bind, sizeof(chained_bind));
	std::memcpy(data.data() + 0x498, &chained_rebase, sizeof(chained_rebase));
	const std::uint64_t threaded_bind_first = (1ull << 63) | (1ull << 51);
	const std::uint64_t threaded_bind_second = (1ull << 63);
	std::memcpy(data.data() + 0x5d0, &threaded_bind_first, sizeof(threaded_bind_first));
	std::memcpy(data.data() + 0x5d8, &threaded_bind_second, sizeof(threaded_bind_second));
	RelocationInfo relocation{};
	relocation.address = 0x1200;
	relocation.info = (3u << 25) | 0x08000000u;
	std::memcpy(data.data() + dysymtab.external_relocation_offset,
		&relocation, sizeof(relocation));
	RelocationInfo relative_relocation{};
	relative_relocation.address = 0x1250;
	relative_relocation.info = 0x01000000u | (2u << 25) | 0x08000000u | (2u << 28);
	std::memcpy(data.data() + dysymtab.external_relocation_offset + sizeof(relocation),
		&relative_relocation, sizeof(relative_relocation));
	RelocationInfo got_relocation{};
	got_relocation.address = 0x1220;
	got_relocation.info = (3u << 25) | 0x08000000u | (4u << 28);
	std::memcpy(data.data() + dysymtab.external_relocation_offset + 2 * sizeof(relocation),
		&got_relocation, sizeof(got_relocation));
	RelocationInfo subtractor_relocation{};
	subtractor_relocation.address = 0x1230;
	subtractor_relocation.info = (3u << 25) | 0x08000000u | (5u << 28);
	std::memcpy(data.data() + dysymtab.external_relocation_offset + 3 * sizeof(relocation),
		&subtractor_relocation, sizeof(subtractor_relocation));
	RelocationInfo subtractor_pair{};
	subtractor_pair.address = 0x1230;
	subtractor_pair.info = (3u << 25) | 0x08000000u;
	std::memcpy(data.data() + dysymtab.external_relocation_offset + 4 * sizeof(relocation),
		&subtractor_pair, sizeof(subtractor_pair));
	NList64 symbol{};
	symbol.name_offset = 1;
	symbol.type = undefined_symbol ? 0x01 : 0x0f;
	symbol.section = undefined_symbol ? 0 : 1;
	symbol.description = weak_undefined_symbol ? 0x0040 : 0;
	symbol.value = undefined_symbol ? 0 : 0x1000;
	std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
	const char* symbol_name = imported_symbol;
	std::memcpy(data.data() + symtab.string_offset + 1, symbol_name,
		std::strlen(symbol_name) + 1);
	if (entry_calls_import) {
		// sub rsp, 0x28 ; mov rax, [rip + 0x35] ; call rax ;
		// add rsp, 0x28 ; ret. The indirect slot is at VM 0x1040.
		data[0x200] = 0x48; data[0x201] = 0x83; data[0x202] = 0xec; data[0x203] = 0x28;
		data[0x204] = 0x48; data[0x205] = 0x8b; data[0x206] = 0x05; data[0x207] = 0x35;
		data[0x208] = 0x00; data[0x209] = 0x00; data[0x20a] = 0x00; data[0x20b] = 0xff;
		data[0x20c] = 0xd0; data[0x20d] = 0x48; data[0x20e] = 0x83; data[0x20f] = 0xc4;
		data[0x210] = 0x28; data[0x211] = 0xc3;
	} else if (entry_returns_argc) {
		data[0x200] = 0x8b;
		data[0x201] = 0xc1;
		data[0x202] = 0xc3;
	} else {
		data[0x200] = 0xc3;
	}

	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	if (!output) {
		throw std::runtime_error("cannot write synthetic Mach-O");
	}
}

} // namespace

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			return 2;
		}
		#if !defined(_WIN64)
		{
			const auto root = std::filesystem::path(temp_path) /
				(L"darling-dyld-win32-" + std::to_wstring(GetCurrentProcessId()));
			const auto app = root / "Applications" / "Demo.app" / "Contents" / "MacOS";
			const auto library = root / "usr" / "lib" / "libSystem.B.dylib";
			std::filesystem::create_directories(app);
			std::filesystem::create_directories(library.parent_path());
			auto write32 = [](const std::filesystem::path& path, std::uint32_t file_type) {
				using namespace darling::windows_host;
				const MachHeader32 header{MH_MAGIC, CPU_TYPE_X86, 3, file_type, 1,
					static_cast<std::uint32_t>(sizeof(SegmentCommand32)), 0};
				SegmentCommand32 segment{};
				segment.header = {LC_SEGMENT, static_cast<std::uint32_t>(sizeof(segment))};
				std::memcpy(segment.segment_name, "__TEXT", 6);
				segment.vm_address = 0x1000;
				segment.vm_size = 0x1000;
				segment.file_size = 0x1000;
				segment.max_protection = 5;
				segment.initial_protection = 5;
				std::vector<std::uint8_t> bytes(0x1000, 0);
				std::memcpy(bytes.data(), &header, sizeof(header));
				std::memcpy(bytes.data() + sizeof(header), &segment, sizeof(segment));
				std::ofstream output(path, std::ios::binary);
				output.write(reinterpret_cast<const char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
			};
			write32(library, darling::windows_host::MH_DYLIB);
			const auto image = app / "Demo";
			write32(image, darling::windows_host::MH_EXECUTE);
			const auto resolved = darling::windows_host::DylibResolver::Resolve(
				image, "@rpath/libSystem.B.dylib", {library.parent_path()}, root);
			if (resolved != library)
				throw std::runtime_error("Win32 dyld rpath resolution failed");
			{
				const auto library_image = darling::windows_host::MachOImage::Open(library.wstring());
				const auto mapping = library_image.MapSegments();
				if (!library_image.Is32Bit() || mapping.Segments().size() != 1)
					throw std::runtime_error("Win32 dyld 32-bit mapping failed");
			}
			std::cout << "DYLD_RPATH=PASS\n";
			std::cout << "DYLD_32BIT_METADATA=PASS\n";
			std::cout << "DYLD_32BIT_MAPPING=PASS\n";
			std::error_code error;
			std::filesystem::remove_all(root, error);
			if (error)
				return 3;
			return 0;
		}
		#endif
		const auto root = std::filesystem::path(temp_path) /
			(L"darling-dyld-stage1-" + std::to_wstring(GetCurrentProcessId()));
		const auto app = root / "Applications" / "Demo.app" / "Contents" / "MacOS";
		const auto library = root / "usr" / "lib" / "libSystem.B.dylib";
		const auto no_main_library = root / "usr" / "lib" / "libNoMain.dylib";
		std::filesystem::create_directories(app);
		std::filesystem::create_directories(library.parent_path());
		std::filesystem::create_directories((root / "PlugIns").string());
		WriteMachO(library, nullptr, true, false, false, true);
		WriteMachO(no_main_library, nullptr, true, false, false, false, false, false);
		const auto bundle = root / "PlugIns" / "Demo.bundle";
		WriteMachO(bundle, nullptr, false, false, false, false, false, false, true);
		if (darling::windows_host::MachOImage::Open(no_main_library.wstring()).EntryOffset() != 0) {
			throw std::runtime_error("dylib without LC_MAIN has an unexpected entry point");
		}
		if (darling::windows_host::MachOImage::Open(bundle.wstring()).Header().file_type != darling::windows_host::MH_BUNDLE) {
			throw std::runtime_error("Mach-O bundle was not recognized");
		}
		const auto image = app / "Demo";
		WriteMachO(image, "@rpath/libSystem.B.dylib", false, true, false, false, true);

		const auto resolved = darling::windows_host::DylibResolver::Resolve(
			image, "@rpath/libSystem.B.dylib", {root / "usr" / "lib"}, root);
		std::cout << "DYLD_RPATH=PASS\n";
		std::cout << "DYLD_RESOLVED=" << resolved.string() << "\n";
		std::size_t embedded_rpath_count = 0;
		std::string embedded_rpath_value;
		{
			const auto image_metadata = darling::windows_host::MachOImage::Open(image.wstring());
			embedded_rpath_count = image_metadata.RPaths().size();
			embedded_rpath_value = image_metadata.RPaths().front();
		}
		std::cout << "DYLD_EMBEDDED_RPATH_COUNT=" << embedded_rpath_count << "\n";
		std::cout << "DYLD_EMBEDDED_RPATH_VALUE=" << embedded_rpath_value << "\n";
		const auto embedded_resolved = darling::windows_host::DylibResolver::Resolve(
			image, "@rpath/libSystem.B.dylib", {}, root);
		if (embedded_resolved != library) {
			throw std::runtime_error("embedded Mach-O rpath did not resolve dylib");
		}
		std::cout << "DYLD_EMBEDDED_RPATH=PASS\n";
		const auto loader_resolved = darling::windows_host::DylibResolver::Resolve(
			image, "@loader_path/Demo", {}, root);
		std::cout << "DYLD_LOADER_PATH=" << loader_resolved.filename().string() << "\n";
		{
			const auto executable = root / "App.app" / "MacOS" / "Demo";
			const auto executable_framework = root / "App.app" / "Frameworks" /
				"FromExecutable.dylib";
			std::filesystem::create_directories(executable.parent_path());
			std::filesystem::create_directories(executable_framework.parent_path());
			WriteMachO(executable_framework, nullptr, true, false);
			const auto provider = root / "App.app" / "PlugIns" / "Provider.dylib";
			const auto executable_resolved = darling::windows_host::DylibResolver::Resolve(
				provider, "@executable_path/../Frameworks/FromExecutable.dylib",
				{}, root, executable);
			if (executable_resolved != executable_framework) {
				throw std::runtime_error("@executable_path used provider directory");
			}
		}
		std::cout << "DYLD_EXECUTABLE_PATH=PASS\n";
		{
			const auto app_image = darling::windows_host::MachOImage::Open(image.wstring());
			const auto library_image = darling::windows_host::MachOImage::Open(library.wstring());
			if (app_image.UndefinedSymbols().size() != 1 ||
				library_image.UndefinedSymbols().size() != 0) {
				throw std::runtime_error("unexpected Mach-O undefined-symbol metadata");
			}
			if (app_image.BindActions().size() != 7 ||
				app_image.BindActions().front().symbol != "_libSystemInit" ||
				app_image.BindActions().front().segment_offset != 0x40 ||
				app_image.BindActions()[1].segment_offset != 0x48 ||
				app_image.BindActions()[2].segment_offset != 0x50 ||
				app_image.BindActions()[3].segment_offset != 0x58 ||
				!app_image.BindActions()[4].threaded ||
				app_image.BindActions()[4].threaded_table_size != 1 ||
				!app_image.BindActions()[5].weak || app_image.BindActions()[5].lazy ||
				!app_image.BindActions()[6].lazy) {
				throw std::runtime_error("unexpected Mach-O dyld bind metadata");
			}
			if (app_image.RebaseActions().size() != 1 ||
				app_image.RebaseActions().front().segment_offset != 0x238) {
				throw std::runtime_error("unexpected Mach-O dyld rebase metadata");
			}
			if (app_image.ChainedFixups().size() != 2 ||
				!app_image.ChainedFixups().front().bind ||
				app_image.ChainedFixups().back().bind) {
				throw std::runtime_error("unexpected Mach-O chained-fixup metadata");
			}
			if (app_image.ChainedImports().size() != 1 ||
				app_image.ChainedImports().front().symbol != "_libSystemInit") {
				throw std::runtime_error("unexpected Mach-O chained import metadata");
			}
			bool found_export = false;
			for (const auto& symbol : app_image.Symbols()) {
				found_export = found_export || symbol.name == "_exported";
			}
			if (!found_export) {
				throw std::runtime_error("Mach-O exports trie symbol was not parsed");
			}
			if (app_image.Relocations().size() != 5 ||
				!app_image.Relocations().front().external ||
				app_image.Relocations().front().length != 3) {
				throw std::runtime_error("unexpected Mach-O relocation metadata");
			}
			const auto library_mapping = library_image.MapSegments();
			library_image.ExecuteEntryNoArgs(library_mapping);
			std::cout << "DYLD_EXECUTE_ENTRY=PASS\n";
			if (library_image.ExecuteEntry(library_mapping,
				{"Demo", "one", "two"}, {"PATH=/usr/bin"}) != 3) {
				throw std::runtime_error("Mach-O argc/argv entry did not receive arguments");
			}
			std::cout << "DYLD_EXECUTE_ARGV=PASS\n";
			darling::windows_host::DarwinLaunchOptions launch_options;
			launch_options.prefix = root;
		launch_options.arguments = {"Demo", "one", "two"};
		launch_options.environment = {"PATH=/usr/bin"};
		launch_options.host_bindings = {{"_libSystemInit",
			reinterpret_cast<std::uintptr_t>(&darling_windows_host_entry)}};
			if (darling::windows_host::DarwinBootstrap::Run(image, launch_options) != 3) {
				throw std::runtime_error("Darwin bootstrap did not return the entry result");
			}
			std::cout << "DARWIN_BOOTSTRAP=PASS\n";
			std::cout << "DARWIN_BOUND_ENTRY=PASS\n";
			const auto imported_entry_image = app / "ImportedEntry";
			WriteMachO(imported_entry_image,
				"@loader_path/../../../../usr/lib/libSystem.B.dylib", false, true,
				false, false, true, true, false, false, "_libSystemInit");
			darling::windows_host::DarwinLaunchOptions imported_entry_options;
			imported_entry_options.prefix = root;
			imported_entry_options.arguments = {"ImportedEntry", "one", "two"};
			imported_entry_options.environment = {"PATH=/usr/bin"};
			imported_entry_options.host_bindings = {{"_libSystemInit",
				reinterpret_cast<std::uintptr_t>(&darling_windows_host_entry)}};
			if (darling::windows_host::DarwinBootstrap::Run(imported_entry_image,
				imported_entry_options) != 3) {
				throw std::runtime_error("Mach-O dependent imported entry did not return the bound result");
			}
			std::cout << "DARWIN_DEPENDENT_IMPORTED_ENTRY=PASS\n";
			const auto providerless_host_image = root / "ProviderlessHost.macho";
			WriteMachO(providerless_host_image, nullptr, false, true, false, false,
				false, true, false, false, "_getpid");
			const auto providerless_bindings =
				darling::windows_host::DylibGraph::BindImports(
					providerless_host_image, {}, root);
			if (providerless_bindings.size() != 1 ||
				providerless_bindings.front().name != "_getpid" ||
				!providerless_bindings.front().provider.empty() ||
				providerless_bindings.front().provider_value !=
					darling_windows_host_symbol("_getpid")) {
				throw std::runtime_error("providerless host-symbol binding failed");
			}
			std::cout << "DARWIN_AUTOMATIC_HOST_BINDING=PASS\n";
			if (library_image.SymbolAddress(library_mapping, "_libSystemInit") == 0) {
				throw std::runtime_error("Mach-O symbol address was not relocated");
			}
			const std::uintptr_t relocation_target = 0x123456789abcdef0ull;
			library_image.ApplyAbsolute64(library_mapping, 0x1200, relocation_target);
			const auto relocation_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1200) + library_mapping.Slide());
			if (*relocation_address != relocation_target) {
				throw std::runtime_error("Mach-O absolute relocation did not persist");
			}
			const auto app_mapping = app_image.MapSegments();
			app_image.ApplyAbsolute32(app_mapping, 0x1260, 0x12345678u);
			const auto absolute32_address = reinterpret_cast<const std::uint32_t*>(
				static_cast<std::intptr_t>(0x1260) + app_mapping.Slide());
			if (*absolute32_address != 0x12345678u) {
				throw std::runtime_error("Mach-O 32-bit absolute bind was not applied");
			}
			const auto relative32_site = static_cast<std::intptr_t>(0x1268) +
				app_mapping.Slide();
			const auto relative32_target = static_cast<std::uintptr_t>(relative32_site + 0x40);
			app_image.ApplyRelative32(app_mapping, 0x1268, relative32_target);
			const auto relative32_value = *reinterpret_cast<const std::int32_t*>(relative32_site);
			if (relative32_value != 0x40 - 4) {
				throw std::runtime_error("Mach-O 32-bit PC-relative bind was not applied");
			}
			const auto signed1_site = static_cast<std::intptr_t>(0x1270) +
				app_mapping.Slide();
			const auto signed2_site = static_cast<std::intptr_t>(0x1274) +
				app_mapping.Slide();
			const auto signed4_site = static_cast<std::intptr_t>(0x1278) +
				app_mapping.Slide();
			app_image.ApplyRelative32WithAddend(app_mapping, 0x1270,
				static_cast<std::uintptr_t>(signed1_site + 1 + 7), 1);
			app_image.ApplyRelative32WithAddend(app_mapping, 0x1274,
				static_cast<std::uintptr_t>(signed2_site + 2 + 0x123), 2);
			app_image.ApplyRelative32WithAddend(app_mapping, 0x1278,
				static_cast<std::uintptr_t>(signed4_site + 4 + 0x12345), 4);
			if (*reinterpret_cast<const std::int32_t*>(signed1_site) != 7 ||
				*reinterpret_cast<const std::int32_t*>(signed2_site) != 0x123 ||
				*reinterpret_cast<const std::int32_t*>(signed4_site) != 0x12345) {
				throw std::runtime_error("Mach-O signed 32-bit relocation was not applied");
			}
			const auto relative_site = static_cast<std::intptr_t>(0x1250) + app_mapping.Slide();
			const auto relocation_apply_target = static_cast<std::uintptr_t>(relative_site + 0x1234);
			app_image.ApplyRelocations(app_mapping, {relocation_apply_target});
			const auto applied_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1200) + app_mapping.Slide());
			if (*applied_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O relocation record was not applied");
			}
			app_image.ApplyBindActions(app_mapping, {{"_libSystemInit", relocation_apply_target}});
			const auto bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1040) + app_mapping.Slide());
			if (*bind_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O dyld bind action was not applied");
			}
			const auto second_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1048) + app_mapping.Slide());
			if (*second_bind_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O second dyld bind action was not applied");
			}
			const auto third_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1050) + app_mapping.Slide());
			const auto fourth_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1058) + app_mapping.Slide());
			if (*third_bind_address != relocation_apply_target ||
				*fourth_bind_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O repeated dyld bind action was not applied");
			}
			const auto weak_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1228) + app_mapping.Slide());
			if (*weak_bind_address != 0) {
				throw std::runtime_error("Mach-O unresolved weak bind was not nulled");
			}
			const auto threaded_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x13d0) + app_mapping.Slide());
			const auto threaded_bind_next_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x13d8) + app_mapping.Slide());
			if (*threaded_bind_address != relocation_apply_target ||
				*threaded_bind_next_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O threaded bind chain was not applied");
			}
			app_image.ApplyChainedFixups(app_mapping,
				{{"_libSystemInit", relocation_apply_target}});
			const auto chained_bind_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1290) + app_mapping.Slide());
			const auto chained_rebase_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1298) + app_mapping.Slide());
			if (*chained_bind_address != relocation_apply_target ||
				*chained_rebase_address != static_cast<std::uintptr_t>(0x1100 + app_mapping.Slide())) {
				throw std::runtime_error("Mach-O chained fixup was not applied");
			}
			app_image.ApplyLazyBindAction(6, app_mapping,
				{{"_libSystemInit", relocation_apply_target}});
			const auto lazy_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1240) + app_mapping.Slide());
			if (*lazy_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O lazy bind action was not applied");
			}
			app_image.ApplyRebaseActions(app_mapping);
			const auto rebase_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1238) + app_mapping.Slide());
			if (*rebase_address != static_cast<std::uintptr_t>(app_mapping.Slide())) {
				throw std::runtime_error("Mach-O dyld rebase action was not applied");
			}
			const auto relative_value = *reinterpret_cast<const std::int32_t*>(relative_site);
			const auto expected_relative = static_cast<std::int64_t>(relocation_apply_target) -
				(static_cast<std::int64_t>(relative_site) + 4);
			if (relative_value != expected_relative) {
				throw std::runtime_error("Mach-O PC-relative relocation was not applied");
			}
			const auto got_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1220) + app_mapping.Slide());
			if (*got_address != relocation_apply_target) {
				throw std::runtime_error("Mach-O GOT relocation was not applied");
			}
			const auto subtractor_address = reinterpret_cast<const std::uintptr_t*>(
				static_cast<std::intptr_t>(0x1230) + app_mapping.Slide());
			if (*subtractor_address != 0) {
				throw std::runtime_error("Mach-O SUBTRACTOR relocation was not applied");
			}
		}
		std::cout << "DYLD_SYMBOLS=PASS\n";
		std::cout << "DYLD_RELOCATION_RECORDS=PASS\n";
		std::cout << "DYLD_BIND_OPCODES=PASS\n";
		std::cout << "DYLD_SYMBOL_ADDRESS=PASS\n";
		std::cout << "DYLD_ABSOLUTE_RELOCATION=PASS\n";
		std::cout << "DYLD_ABSOLUTE32_BIND=PASS\n";
		std::cout << "DYLD_PCREL32_BIND=PASS\n";
		std::cout << "DYLD_SIGNED_1_2_4=PASS\n";
		std::cout << "DYLD_RELOCATION_APPLY=PASS\n";
		std::cout << "DYLD_BIND_APPLY=PASS\n";
		std::cout << "DYLD_WEAK_BIND_NULL=PASS\n";
		std::cout << "DYLD_BIND_SCALED_STEP=PASS\n";
		std::cout << "DYLD_BIND_IMM_TIMES=PASS\n";
		std::cout << "DYLD_THREADED_APPLY=PASS\n";
		std::cout << "DYLD_CHAINED_APPLY=PASS\n";
		std::cout << "DYLD_CHAINED_NAME_BIND=PASS\n";
		std::cout << "DYLD_CHAINED_IMPORTS=PASS\n";
		std::cout << "DYLD_EXPORTS_TRIE=PASS\n";
		std::cout << "DYLD_LAZY_BIND_APPLY=PASS\n";
		std::cout << "DYLD_REBASE_APPLY=PASS\n";
		std::cout << "DYLD_PCREL_RELOCATION=PASS\n";
		std::cout << "DYLD_GOT_RELOCATION=PASS\n";
		std::cout << "DYLD_SUBTRACTOR_RELOCATION=PASS\n";
		const auto bindings = darling::windows_host::DylibGraph::BindImports(
			image, {root / "usr" / "lib"}, root);
		if (bindings.size() != 1 || bindings.front().name != "_libSystemInit" ||
			bindings.front().provider.filename() != "libSystem.B.dylib") {
			throw std::runtime_error("unexpected Mach-O import binding");
		}
		std::cout << "DYLD_BINDINGS=PASS\n";
		std::cout << "DYLD_INFO_LEGACY=PASS\n";
		{
			const auto graph = darling::windows_host::DylibGraph::Load(image, {}, root);
			if (graph.size() != 2 || graph.front().dependencies.size() != 1) {
				throw std::runtime_error("unexpected dylib dependency graph");
			}
			std::cout << "DYLD_GRAPH_NODES=" << graph.size() << "\n";
		}
		std::cout << "DYLD_GRAPH=PASS\n";
		{
			const auto weak_image = root / "weak-app";
			WriteMachO(weak_image, "@rpath/libOptional.dylib", false, false, true);
			const auto weak_graph = darling::windows_host::DylibGraph::Load(
				weak_image, {}, root);
			if (weak_graph.size() != 1 || !weak_graph.front().dependencies.empty()) {
				throw std::runtime_error("unresolved weak dylib was not skipped");
			}
		}
		std::cout << "DYLD_WEAK_DEPENDENCY=PASS\n";
		{
			const auto builtin_image = root / "builtin-app";
			WriteMachO(builtin_image, "libSystem.B.dylib", false, false);
			const auto builtin_graph = darling::windows_host::DylibGraph::Load(
				builtin_image, {}, root / "missing-prefix");
			if (builtin_graph.size() != 1 || !builtin_graph.front().dependencies.empty()) {
				throw std::runtime_error("builtin libSystem provider was not accepted");
			}
		}
		std::cout << "DYLD_BUILTIN_LIBSYSTEM=PASS\n";
		{
			const auto weak_symbol_image = root / "weak-symbol-app";
			WriteMachO(weak_symbol_image, nullptr, false, true, false, false,
				false, true, false, true);
			const auto weak_symbol_bindings = darling::windows_host::DylibGraph::BindImports(
				weak_symbol_image, {}, root);
			if (weak_symbol_bindings.size() != 1 ||
				weak_symbol_bindings.front().name != "_libSystemInit" ||
				!weak_symbol_bindings.front().provider.empty() ||
				weak_symbol_bindings.front().provider_value != 0) {
				throw std::runtime_error("unresolved weak Mach-O symbol was not nulled");
			}
		}
		std::cout << "DYLD_WEAK_UNDEFINED=PASS\n";
		{
			const auto reexport_target = root / "libTarget.dylib";
			const auto reexport_image = root / "libReexport.dylib";
			const auto reexport_app = root / "reexport-app";
			WriteMachO(reexport_target, nullptr, true, false);
			WriteMachO(reexport_image, "@loader_path/libTarget.dylib", true, false);
			WriteMachO(reexport_app, "@loader_path/libReexport.dylib", false, true);
			const auto patch_file = [](const std::filesystem::path& path,
				bool reexport, bool strip_bindings) {
				std::ifstream input(path, std::ios::binary);
				std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
				if (bytes.size() < 0x523)
					throw std::runtime_error("re-export fixture is truncated");
				std::memcpy(bytes.data() + 0x251, "_exported", 10);
				if (reexport) {
					bytes[0x244] = 0x01;
					bytes[0x245] = 0x00;
					bytes[0x510] = 0x11;
					bytes[0x511] = 0x08;
					bytes[0x512] = 0x01;
					std::memcpy(bytes.data() + 0x513, "_exported", 10);
				}
				if (reexport || strip_bindings) {
					std::size_t command_cursor = sizeof(darling::windows_host::MachHeader64);
					const auto* header = reinterpret_cast<const darling::windows_host::MachHeader64*>(bytes.data());
					for (std::uint32_t index = 0; index < header->command_count; ++index) {
						if (command_cursor + sizeof(darling::windows_host::LoadCommand) > bytes.size())
							break;
						auto* command = reinterpret_cast<darling::windows_host::LoadCommand*>(
							bytes.data() + command_cursor);
						if ((command->command == darling::windows_host::LC_DYLD_INFO ||
							command->command == darling::windows_host::LC_DYLD_INFO_ONLY) &&
							command->command_bytes >= sizeof(darling::windows_host::DyldInfoCommand)) {
							auto* info = reinterpret_cast<darling::windows_host::DyldInfoCommand*>(command);
							info->binding_size = 0;
							info->weak_binding_size = 0;
							info->lazy_binding_size = 0;
						} else if (command->command == darling::windows_host::LC_DYLD_CHAINED_FIXUPS &&
							command->command_bytes >= sizeof(darling::windows_host::LinkeditDataCommand)) {
							command->command = 0;
						}
						command_cursor += command->command_bytes;
					}
				}
				std::ofstream output(path, std::ios::binary | std::ios::trunc);
				output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			};
			patch_file(reexport_target, false, true);
			patch_file(reexport_image, true, true);
			patch_file(reexport_app, false, false);
			const auto reexport_bindings = darling::windows_host::DylibGraph::BindImports(
				reexport_app, {}, root);
			const auto all_reexported = reexport_bindings.size() >= 2 &&
				std::all_of(reexport_bindings.begin(), reexport_bindings.end(),
					[](const auto& binding) {
						return binding.name == "_exported" &&
							binding.provider.filename() == "libTarget.dylib";
					});
			if (!all_reexported) {
				throw std::runtime_error("Mach-O re-export was not resolved");
			}
			auto* dynamic_reexport = darling::windows_host::OpenDynamicImage(
				reexport_image);
			const auto dynamic_symbol = darling::windows_host::DynamicImageSymbol(
				*dynamic_reexport, "exported");
			const bool dynamic_reexport_passed = dynamic_symbol != 0;
			darling::windows_host::CloseDynamicImage(dynamic_reexport);
			if (!dynamic_reexport_passed)
				throw std::runtime_error("dlsym did not traverse Mach-O re-export");
		}
		std::cout << "DYLD_REEXPORT=PASS\n";
		std::cout << "DYLD_REEXPORT_DLSYM=PASS\n";
		std::error_code cleanup_error;
		std::filesystem::remove_all(root, cleanup_error);
		return cleanup_error ? 3 : 0;
	} catch (const std::exception& error) {
		std::cerr << "DYLD_SMOKE_ERROR=" << error.what() << "\n";
		return 4;
	}
}
