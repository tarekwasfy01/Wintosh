/*
 * Stage 1 Mach-O dlopen/dlsym/dlclose proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_macho.h"
#include "darling_windows_stdio.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

using namespace darling::windows_host;

namespace {

std::atomic<int> initializer_calls{0};

void DynamicInitializer()
{
	initializer_calls.fetch_add(1, std::memory_order_relaxed);
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
		{
		const auto win32_path = std::filesystem::path(temp_path) /
			(L"darling-dlopen-win32-" + std::to_wstring(GetCurrentProcessId()) + L".dylib");
		const MachHeader32 win32_header{
			MH_MAGIC, CPU_TYPE_X86, 3, MH_DYLIB, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + sizeof(Section32) +
				sizeof(SymtabCommand)), 0};
		SegmentCommand32 win32_segment{};
		win32_segment.header = {LC_SEGMENT,
			static_cast<std::uint32_t>(sizeof(SegmentCommand32) + sizeof(Section32))};
		std::memcpy(win32_segment.segment_name, "__DATA", 6);
		win32_segment.vm_address = 0x1000;
		win32_segment.vm_size = 0x2000;
		win32_segment.file_size = 0x1000;
		win32_segment.max_protection = 7;
		win32_segment.initial_protection = 7;
		win32_segment.section_count = 1;
		Section32 win32_init_section{};
		std::memcpy(win32_init_section.section_name, "__mod_init_func", 15);
		std::memcpy(win32_init_section.segment_name, "__DATA", 6);
		win32_init_section.address = 0x1200;
		win32_init_section.size = sizeof(std::uint32_t);
		win32_init_section.file_offset = 0x200;
		SymtabCommand win32_symtab{};
		win32_symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(SymtabCommand))};
		win32_symtab.symbol_offset = 0x300;
		win32_symtab.symbol_count = 1;
		win32_symtab.string_offset = 0x310;
		win32_symtab.string_bytes = 16;
		std::vector<std::uint8_t> win32_data(0x1000, 0);
		std::memcpy(win32_data.data(), &win32_header, sizeof(win32_header));
		std::memcpy(win32_data.data() + sizeof(win32_header), &win32_segment,
			sizeof(win32_segment));
		std::memcpy(win32_data.data() + sizeof(win32_header) + sizeof(win32_segment),
			&win32_init_section, sizeof(win32_init_section));
		std::memcpy(win32_data.data() + sizeof(win32_header) + sizeof(win32_segment) +
			sizeof(win32_init_section), &win32_symtab, sizeof(win32_symtab));
		const auto win32_callback = static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(&DynamicInitializer));
		std::memcpy(win32_data.data() + win32_init_section.file_offset,
			&win32_callback, sizeof(win32_callback));
		const NList32 win32_symbol{1, 0x0f, 1, 0, 0x1100};
		std::memcpy(win32_data.data() + win32_symtab.symbol_offset, &win32_symbol,
			sizeof(win32_symbol));
		std::memcpy(win32_data.data() + win32_symtab.string_offset + 1,
			"_exported", 10);
		std::ofstream win32_output(win32_path, std::ios::binary);
		win32_output.write(reinterpret_cast<const char*>(win32_data.data()),
			static_cast<std::streamsize>(win32_data.size()));
		win32_output.close();
		void* memory_image = nullptr;
		const bool memory_image_ok =
			darling_windows_NSCreateObjectFileImageFromMemory(win32_data.data(),
				win32_data.size(), &memory_image) == darling_NSObjectFileImageSuccess &&
			memory_image != nullptr &&
			darling_windows_NSDestroyObjectFileImage(memory_image) == 1;
		const auto utf8_path = win32_path.string();
		const bool preflight_ok = darling_windows_dlopen_preflight(utf8_path.c_str(), 0) == 1;
		void* handle = darling_windows_dlopen(utf8_path.c_str(), 0);
		void* second_handle = darling_windows_dlopen(utf8_path.c_str(), 0);
		void* object_file_image = nullptr;
		const bool object_image_ok =
			darling_windows_NSCreateObjectFileImageFromFile(utf8_path.c_str(),
				&object_file_image) == darling_NSObjectFileImageSuccess &&
			object_file_image != nullptr;
		const bool object_symbols_ok = object_image_ok &&
			darling_windows_NSSymbolDefinitionCountInObjectFileImage(object_file_image) >= 1 &&
			darling_windows_NSIsSymbolDefinedInObjectFileImage(object_file_image,
				"exported") == 1 &&
			darling_windows_NSSymbolDefinitionNameInObjectFileImage(object_file_image, 0) != nullptr;
		void* linked_module = object_image_ok ? darling_windows_NSLinkModule(
			object_file_image, "wintosh-test-module", 0) : nullptr;
		const bool module_link_ok = linked_module != nullptr &&
			darling_windows_NSUnLinkModule(linked_module, 0) == 1;
		void* symbol_address = darling_windows_dlsym(handle, "exported");
		const void* image_header = nullptr;
		void* image_symbol = nullptr;
		for (std::uint32_t index = 0; index < darling_windows_dyld_image_count(); ++index) {
			const char* image_name = darling_windows_dyld_get_image_name(index);
			if (image_name != nullptr && std::strstr(image_name, path.filename().string().c_str()) != nullptr) {
				image_header = darling_windows_dyld_get_image_header(index);
				image_symbol = darling_windows_NSLookupSymbolInImage(image_header, 0, "exported");
				break;
			}
		}
		const bool passed = preflight_ok && memory_image_ok && object_image_ok && object_symbols_ok && module_link_ok &&
			handle != nullptr &&
			darling_windows_NSDestroyObjectFileImage(object_file_image) == 1 &&
			second_handle == handle &&
			symbol_address != nullptr &&
			image_header != nullptr && image_symbol == symbol_address &&
			darling_windows_NSAddressOfSymbol(image_symbol) == symbol_address &&
			darling_windows_dlclose(handle) == 0 &&
			darling_windows_dlclose(second_handle) == 0 &&
			initializer_calls.load(std::memory_order_relaxed) == 1;
		const bool invalid_lookup_rejected =
			darling_windows_NSLookupSymbolInImage(image_header, 0, nullptr) == nullptr &&
			std::string(darling_windows_dlerror()) ==
				"NSLookupSymbolInImage: invalid symbol name" &&
			darling_windows_NSLookupSymbolInImage(reinterpret_cast<const void*>(0x1), 0,
				"exported") == nullptr &&
			std::string(darling_windows_dlerror()) ==
				"NSLookupSymbolInImage: image header not found";
		const bool lookup_options_ok =
			darling_windows_NSLookupSymbolInImage(image_header,
				DARLING_NSLOOKUPSYMBOLINIMAGE_OPTION_BIND_NOW, "exported") == symbol_address &&
			darling_windows_NSLookupSymbolInImage(image_header,
				DARLING_NSLOOKUPSYMBOLINIMAGE_OPTION_BIND_FULLY |
				DARLING_NSLOOKUPSYMBOLINIMAGE_OPTION_RETURN_ON_ERROR, "exported") == symbol_address &&
			darling_windows_NSLookupSymbolInImage(image_header, 0x80, "exported") == nullptr &&
			std::string(darling_windows_dlerror()) ==
				"NSLookupSymbolInImage: unsupported options";
		const bool module_lookup_ok =
			darling_windows_NSLookupSymbolInModule(handle, "exported") == symbol_address &&
			darling_windows_NSLookupSymbolInModule(reinterpret_cast<void*>(0x1), "exported") == nullptr &&
			std::string(darling_windows_dlerror()) ==
				"NSLookupSymbolInModule: module not found";
		const bool global_lookup_ok =
			darling_windows_NSLookupAndBindSymbol("exported") == symbol_address &&
			darling_windows_NSLookupAndBindSymbolWithHint("exported",
				path.filename().string().c_str()) == symbol_address &&
			darling_windows_NSLookupAndBindSymbol("missing") == nullptr &&
			std::string(darling_windows_dlerror()) ==
				"NSLookupAndBindSymbol: symbol not found";
		const bool symbol_predicates_ok =
			darling_windows_NSIsSymbolNameDefined("exported") == 1 &&
			darling_windows_NSIsSymbolNameDefinedWithHint("exported",
				path.filename().string().c_str()) == 1 &&
			darling_windows_NSIsSymbolNameDefined("missing") == 0;
		std::error_code win32_error;
		std::filesystem::remove(win32_path, win32_error);
		if (!passed || !invalid_lookup_rejected || !lookup_options_ok ||
			!module_lookup_ok || !global_lookup_ok || !symbol_predicates_ok || win32_error)
			return 3;
		std::cout << "DARWIN_DLOPEN=PASS\n";
		std::cout << "DARWIN_DLSYM=PASS\n";
		std::cout << "DARWIN_DLCLOSE=PASS\n";
		std::cout << "DARWIN_WIN32_DYNAMIC=PASS\n";
		return 0;
		}
		#endif
		const auto path = std::filesystem::path(temp_path) /
			(L"darling-dlopen-stage1-" + std::to_wstring(GetCurrentProcessId()) + L".dylib");
		MachHeader64 header{MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_DYLIB, 2,
			static_cast<std::uint32_t>(sizeof(SegmentCommand64) + sizeof(Section64) +
				sizeof(SymtabCommand)), 0, 0};
		SegmentCommand64 segment{};
		segment.header = {LC_SEGMENT_64, static_cast<std::uint32_t>(
			sizeof(SegmentCommand64) + sizeof(Section64))};
		std::memcpy(segment.segment_name, "__DATA", 6);
		segment.vm_address = 0x1000;
		segment.vm_size = 0x2000;
		segment.file_offset = 0;
		segment.file_size = 0x1000;
		segment.max_protection = 7;
		segment.initial_protection = 7;
		segment.section_count = 1;
		Section64 init_section{};
		std::memcpy(init_section.section_name, "__mod_init_func", 15);
		std::memcpy(init_section.segment_name, "__DATA", 6);
		init_section.address = 0x1200;
		init_section.size = sizeof(std::uintptr_t);
		init_section.file_offset = 0x200;
		SymtabCommand symtab{};
		symtab.header = {LC_SYMTAB, static_cast<std::uint32_t>(sizeof(SymtabCommand))};
		symtab.symbol_offset = 0x300;
		symtab.symbol_count = 1;
		symtab.string_offset = 0x320;
		symtab.string_bytes = 32;
		std::vector<std::uint8_t> data(0x1000, 0);
		std::memcpy(data.data(), &header, sizeof(header));
		std::memcpy(data.data() + sizeof(header), &segment, sizeof(segment));
		std::memcpy(data.data() + sizeof(header) + sizeof(segment), &init_section,
			sizeof(init_section));
		std::memcpy(data.data() + sizeof(header) + sizeof(segment) + sizeof(init_section),
			&symtab, sizeof(symtab));
		const auto callback = reinterpret_cast<std::uintptr_t>(&DynamicInitializer);
		std::memcpy(data.data() + init_section.file_offset, &callback, sizeof(callback));
		NList64 symbol{1, 0x0f, 1, 0, 0x1100};
		std::memcpy(data.data() + symtab.symbol_offset, &symbol, sizeof(symbol));
		data[symtab.string_offset + 1] = '_';
		std::memcpy(data.data() + symtab.string_offset + 2, "exported", 9);
		data[0x100] = 0xc3;
		std::ofstream output(path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(data.data()),
			static_cast<std::streamsize>(data.size()));
		output.close();
		const auto utf8_path = path.string();
		const auto image_count_before = darling_windows_dyld_image_count();
		void* handle = darling_windows_dlopen(utf8_path.c_str(), 0);
		void* second_handle = darling_windows_dlopen(utf8_path.c_str(), 0);
		const auto image_count_after = darling_windows_dyld_image_count();
		bool image_visible = false;
		for (std::uint32_t index = 0; index < image_count_after; ++index) {
			const auto* image_name = darling_windows_dyld_get_image_name(index);
			if (image_name != nullptr && std::strcmp(image_name, utf8_path.c_str()) == 0) {
				image_visible = darling_windows_dyld_get_image_header(index) != nullptr;
				break;
			}
		}
		void* symbol_address = darling_windows_dlsym(handle, "exported");
		darling_dl_info dynamic_info{};
		const bool dynamic_dladdr = symbol_address != nullptr &&
			darling_windows_dladdr(symbol_address, &dynamic_info) == 1 &&
			dynamic_info.dli_fname != nullptr &&
			std::strcmp(dynamic_info.dli_fname, utf8_path.c_str()) == 0 &&
			dynamic_info.dli_fbase != nullptr && dynamic_info.dli_sname != nullptr &&
			std::strcmp(dynamic_info.dli_sname, "_exported") == 0 &&
			dynamic_info.dli_saddr == symbol_address;
		void* default_symbol_address = darling_windows_dlsym(
			reinterpret_cast<void*>(static_cast<std::intptr_t>(-2)), "exported");
		void* process_handle = darling_windows_dlopen(nullptr, 0);
		void* process_symbol_address = darling_windows_dlsym(process_handle, "exported");
		const bool passed = handle != nullptr && second_handle == handle &&
			image_count_after > image_count_before && image_visible &&
			dynamic_dladdr &&
			default_symbol_address == symbol_address &&
			process_symbol_address == symbol_address &&
			darling_windows_dlclose(process_handle) == 0 &&
			initializer_calls.load(std::memory_order_relaxed) == 1 &&
			darling_windows_dlclose(handle) == 0 &&
			darling_windows_dlsym(second_handle, "exported") != nullptr &&
			darling_windows_dlclose(second_handle) == 0;
		bool image_removed = true;
		const auto image_count_final = darling_windows_dyld_image_count();
		for (std::uint32_t index = 0; index < image_count_final; ++index) {
			const auto* image_name = darling_windows_dyld_get_image_name(index);
			if (image_name != nullptr && std::strcmp(image_name, utf8_path.c_str()) == 0) {
				image_removed = false;
				break;
			}
		}
		std::error_code error;
		std::filesystem::remove(path, error);
		if (!passed || !image_removed || error)
			return 3;
		const auto concurrent_path = std::filesystem::path(temp_path) /
			(L"darling-dlopen-concurrent-" + std::to_wstring(GetCurrentProcessId()) + L".dylib");
		{
			std::ofstream concurrent_output(concurrent_path, std::ios::binary);
			concurrent_output.write(reinterpret_cast<const char*>(data.data()),
				static_cast<std::streamsize>(data.size()));
		}
		constexpr std::size_t thread_count = 8;
		std::atomic<std::size_t> ready{0};
		std::atomic<bool> start{false};
		std::vector<void*> concurrent_handles(thread_count, nullptr);
		std::vector<std::thread> workers;
		workers.reserve(thread_count);
		for (std::size_t index = 0; index < thread_count; ++index) {
			workers.emplace_back([&, index]() {
				ready.fetch_add(1, std::memory_order_release);
				while (!start.load(std::memory_order_acquire))
					std::this_thread::yield();
				concurrent_handles[index] = darling_windows_dlopen(
					concurrent_path.string().c_str(), 0);
			});
		}
		while (ready.load(std::memory_order_acquire) != thread_count)
			std::this_thread::yield();
		start.store(true, std::memory_order_release);
		for (auto& worker : workers)
			worker.join();
		const auto concurrent_handle = concurrent_handles.front();
		bool concurrent_passed = concurrent_handle != nullptr;
		for (const auto handle : concurrent_handles)
			concurrent_passed = concurrent_passed && handle == concurrent_handle;
		for (const auto handle : concurrent_handles) {
			if (handle != nullptr)
				concurrent_passed = concurrent_passed && darling_windows_dlclose(handle) == 0;
		}
		concurrent_passed = concurrent_passed &&
			initializer_calls.load(std::memory_order_relaxed) == 2;
		std::filesystem::remove(concurrent_path, error);
		if (!concurrent_passed || error)
			return 5;
		std::cout << "DARWIN_DLOPEN=PASS\n";
		std::cout << "DARWIN_DLSYM=PASS\n";
		std::cout << "DARWIN_RTLD_DEFAULT=PASS\n";
		std::cout << "DARWIN_DLOPEN_CONCURRENT=PASS\n";
		std::cout << "DARWIN_DLCLOSE=PASS\n";
		std::cout << "DARWIN_DYLD_DYNAMIC_IMAGE=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "DYNAMIC_SMOKE_ERROR=" << error.what() << "\n";
		return 4;
	}
}
