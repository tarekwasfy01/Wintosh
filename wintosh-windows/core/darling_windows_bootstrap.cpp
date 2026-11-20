/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_bootstrap.h"
#include "darling_windows_corefoundation.h"
#include "darling_windows_abi_bridge.h"

#include "darling_windows_dyld.h"
#include "darling_windows_errno.h"
#include "darling_windows_macho.h"
#include "darling_windows_objc.h"
#include "darling_windows_stdio.h"
#include "darling_windows_tlv.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace darling::windows_host {

namespace {

DWORD ProtectionForMachOSegment(std::int32_t protection)
{
	const bool readable = (protection & 1) != 0;
	const bool writable = (protection & 2) != 0;
	const bool executable = (protection & 4) != 0;
	if (writable && executable)
		return PAGE_EXECUTE_READWRITE;
	if (writable)
		return PAGE_READWRITE;
	if (executable && readable)
		return PAGE_EXECUTE_READ;
	if (executable)
		return PAGE_EXECUTE;
	if (readable)
		return PAGE_READONLY;
	return PAGE_NOACCESS;
}

struct WritableSection final {
	void* address = nullptr;
	std::size_t bytes = 0;
	DWORD restore_protection = PAGE_NOACCESS;
};

std::uintptr_t MappedTLVSectionAddress(const MachOSection& section,
	const MachOImage::Mapping& mapping)
{
	if (section.address > static_cast<std::uint64_t>(
		(std::numeric_limits<std::intptr_t>::max)()))
		throw std::overflow_error("Mach-O TLV section address cannot be slid safely");
	const auto address = static_cast<std::intptr_t>(section.address) + mapping.Slide();
	if (address <= 0)
		throw std::runtime_error("Mach-O TLV section address is invalid");
	return static_cast<std::uintptr_t>(address);
}

std::vector<std::byte> BuildTLVTemplate(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	std::uint64_t begin = (std::numeric_limits<std::uint64_t>::max)();
	std::uint64_t end = 0;
	std::vector<const MachOSection*> data_sections;
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__thread_data" &&
			section.section_name != "__thread_bss")
			continue;
		if (section.size > (std::numeric_limits<std::uint64_t>::max)() - section.address)
			throw std::runtime_error("Mach-O TLV template extent overflows");
		begin = (std::min)(begin, section.address);
		end = (std::max)(end, section.address + section.size);
		if (section.section_name == "__thread_data")
			data_sections.push_back(&section);
	}
	if (begin == (std::numeric_limits<std::uint64_t>::max)())
		return {};
	const auto extent = end - begin;
	if (extent > (std::numeric_limits<std::size_t>::max)())
		throw std::runtime_error("Mach-O TLV template exceeds host address space");
	std::vector<std::byte> result(static_cast<std::size_t>(extent));
	for (const auto* section : data_sections) {
		if (section->size == 0)
			continue;
		const auto address = MappedTLVSectionAddress(*section, mapping);
		std::memcpy(result.data() + static_cast<std::size_t>(section->address - begin),
			reinterpret_cast<const void*>(address), static_cast<std::size_t>(section->size));
	}
	return result;
}

WritableSection MakeSectionWritable(const MachOImage& image,
	const MachOImage::Mapping& mapping, const MachOSection& section)
{
	const auto address = MappedTLVSectionAddress(section, mapping);
	for (const auto& segment : image.Segments()) {
		if (segment.name != section.segment_name ||
			section.address < segment.vm_address ||
			section.address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (section.address - segment.vm_address) < section.size)
			continue;
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address),
			static_cast<SIZE_T>(section.size), PAGE_READWRITE, &old_protection)) {
			throw std::system_error(static_cast<int>(GetLastError()),
				std::system_category(), "VirtualProtect(Mach-O TLV initialization)");
		}
		return {reinterpret_cast<void*>(address),
			static_cast<std::size_t>(section.size),
			ProtectionForMachOSegment(segment.initial_protection)};
	}
	throw std::runtime_error("Mach-O TLV section is outside its segment");
}

void RestoreSectionProtection(const WritableSection& section)
{
	if (section.address == nullptr || section.bytes == 0)
		return;
	DWORD ignored = PAGE_NOACCESS;
	if (!VirtualProtect(section.address, static_cast<SIZE_T>(section.bytes),
		section.restore_protection, &ignored)) {
		throw std::system_error(static_cast<int>(GetLastError()),
			std::system_category(), "VirtualProtect(Mach-O TLV restore)");
	}
}

void RegisterObjectiveCSections(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__objc_classlist" &&
			section.section_name != "__objc_nlclslist" &&
			section.section_name != "__objc_catlist" &&
			section.section_name != "__objc_selrefs")
			continue;
		const auto address = image.SectionAddress(mapping, section.segment_name,
			section.section_name);
		if (section.section_name == "__objc_catlist")
			darling_objc_register_macho_categories(
				reinterpret_cast<const void*>(address),
				static_cast<std::size_t>(section.size));
		else if (section.section_name == "__objc_selrefs")
			darling_objc_register_macho_selrefs(
				reinterpret_cast<const void*>(address),
				static_cast<std::size_t>(section.size));
		else
			darling_objc_register_macho_classlist(
				reinterpret_cast<const void*>(address),
				static_cast<std::size_t>(section.size));
	}
}

void InitializeTLVSections(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	const auto thread_template_storage = BuildTLVTemplate(image, mapping);
	const void* thread_template = thread_template_storage.empty() ? nullptr :
		thread_template_storage.data();
	const auto thread_template_bytes = thread_template_storage.size();
	std::vector<DarwinTLVDescriptorRange> tlv_ranges;
	std::vector<WritableSection> tlv_protections;
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__thread_vars" || section.size == 0)
			continue;
		if (section.size % sizeof(DarwinTLVDescriptor) != 0)
			throw std::runtime_error("Mach-O __thread_vars section is not descriptor-aligned");
		auto* descriptors = reinterpret_cast<DarwinTLVDescriptor*>(
			MappedTLVSectionAddress(section, mapping));
		tlv_ranges.push_back({descriptors,
			static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor))});
		tlv_protections.push_back(MakeSectionWritable(image, mapping, section));
	}
	if (!tlv_ranges.empty())
		InitializeDarwinTLV(tlv_ranges.data(), tlv_ranges.size(),
			thread_template, thread_template_bytes);
	for (const auto& protection : tlv_protections)
		RestoreSectionProtection(protection);
	std::vector<std::uintptr_t> descriptor_addresses;
	std::vector<std::uintptr_t> pointer_slots;
	std::vector<WritableSection> pointer_protections;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_vars") {
			if (section.size % sizeof(DarwinTLVDescriptor) != 0)
				throw std::runtime_error("Mach-O __thread_vars section is not descriptor-aligned");
			const auto address = MappedTLVSectionAddress(section, mapping);
			const auto count = static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor));
			for (std::size_t index = 0; index < count; ++index)
				descriptor_addresses.push_back(address + index * sizeof(DarwinTLVDescriptor));
		} else if (section.section_name == "__thread_ptrs") {
			if (section.size % sizeof(std::uintptr_t) != 0)
				throw std::runtime_error("Mach-O __thread_ptrs section is not pointer-aligned");
			const auto address = MappedTLVSectionAddress(section, mapping);
			const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
			for (std::size_t index = 0; index < count; ++index)
				pointer_slots.push_back(address + index * sizeof(std::uintptr_t));
			if (count != 0)
				pointer_protections.push_back(MakeSectionWritable(image, mapping, section));
		}
	}
	if (descriptor_addresses.size() != pointer_slots.size())
		throw std::runtime_error("Mach-O TLV descriptor and pointer counts do not match");
	for (std::size_t index = 0; index < descriptor_addresses.size(); ++index)
		std::memcpy(reinterpret_cast<void*>(pointer_slots[index]),
			&descriptor_addresses[index], sizeof(std::uintptr_t));
	for (const auto& protection : pointer_protections)
		RestoreSectionProtection(protection);
	if (std::getenv("WINTOSH_TRACE_TLV") != nullptr) {
		if (!descriptor_addresses.empty()) {
			const auto* descriptor = reinterpret_cast<const DarwinTLVDescriptor*>(
				descriptor_addresses.front());
			std::fprintf(stderr,
				"WINTOSH_TLV_TRACE descriptor=0x%llx thunk=0x%llx key=0x%llx pointer=0x%llx\\n",
				static_cast<unsigned long long>(descriptor_addresses.front()),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(descriptor->thunk)),
				static_cast<unsigned long long>(descriptor->key),
				static_cast<unsigned long long>(pointer_slots.front()));
		}
	}
}

std::string PathUtf8(const std::filesystem::path& path)
{
	const auto value = path.u8string();
	return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

}

int DarwinBootstrap::Run(const std::filesystem::path& image_path,
	const DarwinLaunchOptions& options)
{
	struct GuestExecutablePathScope final {
		std::wstring path;
		explicit GuestExecutablePathScope(const std::filesystem::path& image)
			: path(std::filesystem::absolute(image).lexically_normal().wstring())
		{
			const auto utf8_path = std::filesystem::path(path).u8string();
			darling_windows_set_guest_executable_path(path.c_str());
			darling_windows_CFBundleSetMainExecutablePath(
				reinterpret_cast<const char*>(utf8_path.c_str()));
		}
		~GuestExecutablePathScope()
		{
			darling_windows_set_guest_executable_path(nullptr);
			darling_windows_CFBundleSetMainExecutablePath(nullptr);
		}
	} guest_executable_path(image_path);
	// Darwin libSystem starts each process with a fresh thread-local errno
	// slot before dyld/entry initialization begins.
	DarwinErrno::Clear();
	// Bind the launch-owned environment before loading providers.  Their module
	// initializers are allowed to observe `environ`; binding only in
	// MachOImage::ExecuteEntry would expose the host environment during dyld
	// initialization and change Darwin's startup ordering.
	std::vector<const char*> launch_environment;
	launch_environment.reserve(options.environment.size() + 1);
	for (const auto& value : options.environment)
		launch_environment.push_back(value.c_str());
	launch_environment.push_back(nullptr);
	darling_windows_set_environ_vector(launch_environment.data());
	// Open through MachOImage first so fat binaries are sliced to the best
	// supported architecture before the bootstrap validates the header.
	const auto image = MachOImage::Open(image_path.wstring());
	const auto& header = image.Header();
	if ((header.magic == MH_MAGIC && header.cpu_type != CPU_TYPE_X86) ||
		(header.magic == MH_MAGIC_64 && header.cpu_type != CPU_TYPE_X86_64 &&
			header.cpu_type != CPU_TYPE_ARM64)) {
		throw std::runtime_error("Darwin bootstrap received an unsupported Mach-O CPU");
	}
	// Loading the graph first makes missing required dylibs fail before any
	// native entry code executes. Weak dependencies retain their optional
	// semantics in DylibGraph::Load.
	const auto dependency_graph = DylibGraph::Load(
		image_path, options.rpaths, options.prefix);
	const auto bindings = DylibGraph::BindImports(
		image_path, options.rpaths, options.prefix);
	const auto mapping = image.MapSegments();
	const auto preferred_provider_address = mapping.Segments().empty() ? 0 :
		mapping.Segments().front().address;
	struct Provider final {
		std::filesystem::path path;
		MachOImage image;
		MachOImage::Mapping mapping;
	};
	std::vector<Provider> providers;
	for (const auto& node : dependency_graph) {
		const auto normalized = node.path.lexically_normal();
		if (normalized == image_path.lexically_normal())
			continue;
		MachOImage provider_image = MachOImage::Open(normalized.wstring());
		providers.push_back({normalized, std::move(provider_image), {}});
		providers.back().mapping = providers.back().image.MapSegments(
			preferred_provider_address);
	}
	std::vector<DyldResolvedBinding> resolved;
	for (const auto& binding : bindings) {
		const auto host_binding = std::find_if(options.host_bindings.begin(),
			options.host_bindings.end(), [&binding](const auto& candidate) {
				return candidate.symbol == binding.name;
			});
		if (host_binding != options.host_bindings.end()) {
			resolved.push_back(*host_binding);
			continue;
		}
		if (binding.provider.empty()) {
			// Resolve the native Darling ABI table only for providerless imports.
			// A real dylib provider must retain precedence over an equally named
			// host export.
			const auto host_address = darling_windows_host_symbol(binding.name.c_str());
			if (host_address != 0) {
				resolved.push_back({binding.name, host_address,
					binding.dylib_ordinal, binding.has_dylib_ordinal});
				continue;
			}
			resolved.push_back({binding.name,
				static_cast<std::uintptr_t>(binding.provider_value),
				binding.dylib_ordinal, binding.has_dylib_ordinal});
			continue;
		}
		const auto normalized = binding.provider.lexically_normal();
		if (normalized == image_path.lexically_normal()) {
			const auto descriptor_address = image.SymbolAddress(
				mapping, binding.provider_symbol);
			const auto pointer_address = image.TLVPointerAddress(
				mapping, descriptor_address);
			resolved.push_back({binding.name,
				pointer_address == 0 ? descriptor_address : pointer_address,
				binding.dylib_ordinal, binding.has_dylib_ordinal});
			continue;
		}
		auto provider = std::find_if(providers.begin(), providers.end(),
			[&normalized](const Provider& candidate) {
				return candidate.path == normalized;
			});
		if (provider == providers.end()) {
			MachOImage provider_image = MachOImage::Open(normalized.wstring());
			providers.push_back({normalized, std::move(provider_image), {}});
			providers.back().mapping = providers.back().image.MapSegments(
				preferred_provider_address);
			provider = std::prev(providers.end());
		}
		const auto descriptor_address = provider->image.SymbolAddress(
			provider->mapping, binding.provider_symbol);
		const auto pointer_address = provider->image.TLVPointerAddress(
			provider->mapping, descriptor_address);
		resolved.push_back({binding.name,
			pointer_address == 0 ? descriptor_address : pointer_address,
			binding.dylib_ordinal, binding.has_dylib_ordinal});
	}
	if (resolved.size() != bindings.size())
		throw std::logic_error("Darwin bootstrap lost a dependency binding record");
	// Bindings are collected for the full dependency graph. Keep the client
	// image's subset for each relocation/bind pass: dylib ordinal 1 can name a
	// different provider in two different images.
	const auto bindings_for_client = [&bindings, &resolved, &options](
		const std::filesystem::path& client_path) {
		std::vector<DyldResolvedBinding> client_bindings = options.host_bindings;
		const auto normalized_client = client_path.lexically_normal();
		for (std::size_t index = 0; index < bindings.size(); ++index) {
			if (bindings[index].client_image.lexically_normal() == normalized_client)
				client_bindings.push_back(resolved[index]);
		}
		return client_bindings;
	};
	const auto initialization_order = DylibGraph::InitializationOrder(
		image_path, options.rpaths, options.prefix);
	const auto provider_rank = [&initialization_order](
		const std::filesystem::path& path) {
		const auto found = std::find(initialization_order.begin(),
			initialization_order.end(), path.lexically_normal());
		return found == initialization_order.end() ? initialization_order.size() :
			static_cast<std::size_t>(std::distance(initialization_order.begin(), found));
	};
	std::stable_sort(providers.begin(), providers.end(),
		[&provider_rank](const Provider& left, const Provider& right) {
			return provider_rank(left.path) < provider_rank(right.path);
		});
	std::vector<std::string> guest_image_paths;
	guest_image_paths.reserve(providers.size() + 1);
	guest_image_paths.push_back(PathUtf8(image_path));
	for (const auto& provider : providers)
		guest_image_paths.push_back(PathUtf8(provider.path));
	std::vector<darling_dyld_image_info> guest_image_info;
	guest_image_info.reserve(providers.size() + 1);
	const auto header_address = [](const MachOImage::Mapping& image_mapping) -> const void* {
		const auto& segments = image_mapping.Segments();
		const auto text = std::find_if(segments.begin(), segments.end(),
			[](const MappedMachOSegment& segment) { return segment.name == "__TEXT"; });
		if (text != segments.end()) return reinterpret_cast<const void*>(text->address);
		const auto loadable = std::find_if(segments.begin(), segments.end(),
			[](const MappedMachOSegment& segment) { return segment.name != "__PAGEZERO"; });
		return loadable == segments.end() ? nullptr :
			reinterpret_cast<const void*>(loadable->address);
	};
	const auto append_image = [&guest_image_info, &guest_image_paths, &header_address](
		std::size_t index, const MachOImage::Mapping& image_mapping) {
		if (image_mapping.Segments().empty()) return;
		const auto* header = header_address(image_mapping);
		if (header == nullptr) return;
		guest_image_info.push_back({guest_image_paths[index].c_str(),
			header,
			image_mapping.Slide()});
	};
	append_image(0, mapping);
	for (std::size_t index = 0; index < providers.size(); ++index)
		append_image(index + 1, providers[index].mapping);
	struct GuestDyldImageListScope final {
		explicit GuestDyldImageListScope(
			const std::vector<darling_dyld_image_info>& images)
		{
			darling_windows_dyld_set_guest_image_list(images.data(), images.size());
		}
		~GuestDyldImageListScope()
		{
			darling_windows_dyld_set_guest_image_list(nullptr, 0);
		}
	} guest_dyld_image_scope(guest_image_info);
	for (auto& provider : providers) {
		auto provider_bindings = bindings_for_client(provider.path);
		provider.image.ApplyRebaseActions(provider.mapping);
		InitializeTLVSections(provider.image, provider.mapping);
		std::vector<std::uintptr_t> relocation_addresses(provider.image.Symbols().size());
		for (std::size_t index = 0; index < provider.image.Symbols().size(); ++index) {
			const auto& symbol = provider.image.Symbols()[index];
			if ((symbol.type & 0x0e) != 0) {
				relocation_addresses[index] = provider.image.SymbolAddress(
					provider.mapping, symbol.name);
				continue;
			}
			const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
				MachOLibraryOrdinalFromSymbolDescription(symbol.description);
			const auto binding = std::find_if(provider_bindings.begin(),
				provider_bindings.end(), [&symbol, ordinal](const DyldResolvedBinding& candidate) {
					return candidate.symbol == symbol.name &&
						(!candidate.has_dylib_ordinal ||
							candidate.dylib_ordinal == ordinal);
				});
			if (binding != provider_bindings.end())
				relocation_addresses[index] = binding->address;
		}
		if (!provider.image.Relocations().empty())
			provider.image.ApplyRelocations(provider.mapping, relocation_addresses, true);
		if (!provider_bindings.empty()) {
			provider.image.ApplyBindActions(provider.mapping, provider_bindings);
			MachOLazyBindingSession(provider.image, provider.mapping,
				provider_bindings).ApplyAll();
			provider.image.ApplyChainedFixups(provider.mapping, provider_bindings);
		}
		provider.image.ApplyIndirectImportBindings(provider.mapping, provider_bindings);
	}
	auto main_bindings = bindings_for_client(image_path);
	image.ApplyRebaseActions(mapping);
	InitializeTLVSections(image, mapping);
	std::vector<std::uintptr_t> relocation_addresses(image.Symbols().size());
	for (std::size_t index = 0; index < image.Symbols().size(); ++index) {
		const auto& symbol = image.Symbols()[index];
		if ((symbol.type & 0x0e) != 0) {
			relocation_addresses[index] = image.SymbolAddress(mapping, symbol.name);
			continue;
		}
		const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
			MachOLibraryOrdinalFromSymbolDescription(symbol.description);
		const auto binding = std::find_if(main_bindings.begin(), main_bindings.end(),
			[&symbol, ordinal](const DyldResolvedBinding& candidate) {
				return candidate.symbol == symbol.name &&
					(!candidate.has_dylib_ordinal || candidate.dylib_ordinal == ordinal);
			});
		if (binding != main_bindings.end())
			relocation_addresses[index] = binding->address;
	}
	if (!image.Relocations().empty())
		image.ApplyRelocations(mapping, relocation_addresses, true);
	if (!main_bindings.empty()) {
		image.ApplyBindActions(mapping, main_bindings);
		MachOLazyBindingSession(image, mapping, main_bindings).ApplyAll();
		image.ApplyChainedFixups(mapping, main_bindings);
	}
	image.ApplyIndirectImportBindings(mapping, main_bindings);
	const auto reapply_client_bindings = [](const MachOImage& target_image,
		const MachOImage::Mapping& target_mapping,
		const std::vector<DyldResolvedBinding>& client_bindings,
		bool defer_unreachable_external) {
		std::vector<std::uintptr_t> symbol_addresses(target_image.Symbols().size());
		for (std::size_t index = 0; index < target_image.Symbols().size(); ++index) {
			const auto& symbol = target_image.Symbols()[index];
			if ((symbol.type & 0x0e) != 0) {
				symbol_addresses[index] = target_image.SymbolAddress(
					target_mapping, symbol.name);
				continue;
			}
			const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
				MachOLibraryOrdinalFromSymbolDescription(symbol.description);
			const auto found = std::find_if(client_bindings.begin(),
				client_bindings.end(), [&symbol, ordinal](
					const DyldResolvedBinding& candidate) {
						return candidate.symbol == symbol.name &&
							(!candidate.has_dylib_ordinal ||
								candidate.dylib_ordinal == ordinal);
					});
			if (found != client_bindings.end())
				symbol_addresses[index] = found->address;
		}
		if (!target_image.Relocations().empty())
			target_image.ApplyRelocations(target_mapping, symbol_addresses,
				defer_unreachable_external);
		target_image.ApplyBindActions(target_mapping, client_bindings);
		MachOLazyBindingSession(target_image, target_mapping,
			client_bindings).ApplyAll();
		target_image.ApplyChainedFixups(target_mapping, client_bindings);
		target_image.ApplyIndirectImportBindings(target_mapping, client_bindings);
	};
	auto reapply_all_images = [&]() {
		for (auto& provider : providers) {
			const auto client_bindings = bindings_for_client(provider.path);
			reapply_client_bindings(provider.image, provider.mapping,
				client_bindings, true);
		}
		main_bindings = bindings_for_client(image_path);
		reapply_client_bindings(image, mapping, main_bindings, false);
	};
	const auto resolve_export = [&](const DylibExportResolver& resolver) {
		const MachOImage* owner_image = nullptr;
		const MachOImage::Mapping* owner_mapping = nullptr;
		if (resolver.provider == image_path.lexically_normal()) {
			owner_image = &image;
			owner_mapping = &mapping;
		} else {
			const auto provider = std::find_if(providers.begin(), providers.end(),
				[&resolver](const Provider& candidate) {
					return candidate.path == resolver.provider;
				});
			if (provider == providers.end())
				throw std::logic_error("export resolver provider was not mapped");
			owner_image = &provider->image;
			owner_mapping = &provider->mapping;
		}
		const auto exported = std::find_if(owner_image->Symbols().begin(),
			owner_image->Symbols().end(), [&resolver](const MachOSymbol& candidate) {
				return candidate.name == resolver.symbol && candidate.has_resolver;
			});
		if (exported == owner_image->Symbols().end())
			throw std::logic_error("ordered Mach-O export resolver disappeared");
		const auto stub_address = owner_image->SymbolAddress(*owner_mapping,
			resolver.symbol);
		bool requested = false;
		for (std::size_t index = 0; index < bindings.size(); ++index) {
			if (bindings[index].provider.lexically_normal() == resolver.provider &&
				bindings[index].provider_symbol == resolver.symbol &&
				resolved[index].address == stub_address) {
				requested = true;
				break;
			}
		}
		if (!requested) return;
		const auto resolver_address = owner_image->SymbolResolverAddress(
			*owner_mapping, resolver.symbol);
		const auto implementation = abi::darling_windows_abi_call_export_resolver(
			resolver_address);
		if (implementation == 0)
			throw std::runtime_error("Mach-O export resolver failed or returned null");
		for (std::size_t index = 0; index < bindings.size(); ++index) {
			if (bindings[index].provider.lexically_normal() == resolver.provider &&
				bindings[index].provider_symbol == resolver.symbol)
				resolved[index].address = implementation;
		}
		reapply_all_images();
	};
	// All images are now mapped, rebased and initially bound. Resolver code can
	// therefore call into any already loaded image before it selects targets.
	for (const auto& resolver : DylibGraph::ExportResolverOrder(bindings))
		resolve_export(resolver);
	// Finish binding every image before running any initializer. A dylib
	// initializer may call into the main image or another dependency.
	for (auto& provider : providers) {
		auto provider_bindings = bindings_for_client(provider.path);
		MachOLazyBindingSession provider_binding_session(provider.image,
			provider.mapping, provider_bindings);
		auto provider_binding_activation = provider_binding_session.Activate();
		RegisterObjectiveCSections(provider.image, provider.mapping);
		provider.image.ExecuteInitializers(provider.mapping);
	}
	RegisterObjectiveCSections(image, mapping);
	image.ExecuteInitializers(mapping);
	// Keep the main image's lazy-binding context active for the complete guest
	// entry lifetime.  The eager ApplyAll() calls above intentionally use
	// short-lived sessions; dyld_stub_binder needs a session that still exists
	// when guest code later enters an unresolved lazy stub.
	MachOLazyBindingSession entry_binding_session(image, mapping, main_bindings);
	auto entry_binding_activation = entry_binding_session.Activate();
	const auto exit_code = image.ExecuteEntry(mapping, options.arguments,
		options.environment, options.apple_parameters, options.entry_abi);
	// A normal return from the Darwin entry is the process-level exit path.
	// Run the existing atexit/__cxa_atexit registry before image terminators;
	// guest _exit remains an immediate ExitProcess and never reaches here.
	darling_windows_cxa_finalize(nullptr);
	image.ExecuteTerminators(mapping);
	for (auto provider = providers.rbegin(); provider != providers.rend(); ++provider)
		provider->image.ExecuteTerminators(provider->mapping);
	return exit_code;
}

} // namespace darling::windows_host
