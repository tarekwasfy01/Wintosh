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
#include "darling_windows_objc.h"
#include "darling_windows_stdio.h"
#include "darling_windows_tlv.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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

WritableSection MakeSectionWritable(const MachOImage& image,
	const MachOImage::Mapping& mapping, const MachOSection& section)
{
	const auto address = image.SectionAddress(mapping, section.segment_name,
		section.section_name);
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
	const MachOSection* thread_data = nullptr;
	std::size_t thread_template_bytes = 0;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_data") {
			thread_data = &section;
			thread_template_bytes += static_cast<std::size_t>(section.size);
		} else if (section.section_name == "__thread_bss") {
			thread_template_bytes += static_cast<std::size_t>(section.size);
		}
	}
	const void* thread_template = thread_data == nullptr ? nullptr :
		reinterpret_cast<const void*>(image.SectionAddress(mapping,
			thread_data->segment_name, thread_data->section_name));
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__thread_vars" || section.size == 0)
			continue;
		if (section.size % sizeof(DarwinTLVDescriptor) != 0)
			throw std::runtime_error("Mach-O __thread_vars section is not descriptor-aligned");
		const auto writable = MakeSectionWritable(image, mapping, section);
		auto* descriptors = reinterpret_cast<DarwinTLVDescriptor*>(
			image.SectionAddress(mapping, section.segment_name, section.section_name));
		InitializeDarwinTLV(descriptors,
			static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor)),
			thread_template, thread_template_bytes);
		RestoreSectionProtection(writable);
	}
	const MachOSection* thread_vars = nullptr;
	const MachOSection* thread_ptrs = nullptr;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_vars")
			thread_vars = &section;
		else if (section.section_name == "__thread_ptrs")
			thread_ptrs = &section;
	}
	if (thread_vars == nullptr || thread_ptrs == nullptr)
		return;
	if (thread_vars->size % sizeof(DarwinTLVDescriptor) != 0 ||
		thread_ptrs->size % sizeof(std::uintptr_t) != 0)
		throw std::runtime_error("Mach-O TLV sections are not pointer-aligned");
	const auto descriptor_address = image.SectionAddress(mapping,
		thread_vars->segment_name, thread_vars->section_name);
	const auto pointer_address = image.SectionAddress(mapping,
		thread_ptrs->segment_name, thread_ptrs->section_name);
	const auto descriptor_count = static_cast<std::size_t>(
		thread_vars->size / sizeof(DarwinTLVDescriptor));
	const auto pointer_count = static_cast<std::size_t>(
		thread_ptrs->size / sizeof(std::uintptr_t));
	const auto writable = MakeSectionWritable(image, mapping, *thread_ptrs);
	for (std::size_t index = 0; index < std::min(descriptor_count, pointer_count); ++index) {
		const auto descriptor = descriptor_address +
			index * sizeof(DarwinTLVDescriptor);
		const auto slot = pointer_address + index * sizeof(std::uintptr_t);
		std::memcpy(reinterpret_cast<void*>(slot), &descriptor, sizeof(descriptor));
	}
	RestoreSectionProtection(writable);
}

}

int DarwinBootstrap::Run(const std::filesystem::path& image_path,
	const DarwinLaunchOptions& options)
{
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
				resolved.push_back({binding.name, host_address});
				continue;
			}
			resolved.push_back({binding.name,
				static_cast<std::uintptr_t>(binding.provider_value)});
			continue;
		}
		const auto normalized = binding.provider.lexically_normal();
		auto provider = std::find_if(providers.begin(), providers.end(),
			[&normalized](const Provider& candidate) {
				return candidate.path == normalized;
			});
		if (provider == providers.end()) {
			MachOImage provider_image = MachOImage::Open(normalized.wstring());
			providers.push_back({normalized, std::move(provider_image), {}});
			providers.back().mapping = providers.back().image.MapSegments(
				preferred_provider_address);
			RegisterObjectiveCSections(providers.back().image,
				providers.back().mapping);
			provider = std::prev(providers.end());
		}
		const auto descriptor_address = provider->image.SymbolAddress(
			provider->mapping, binding.name);
		const auto pointer_address = provider->image.TLVPointerAddress(
			provider->mapping, descriptor_address);
		resolved.push_back({binding.name,
			pointer_address == 0 ? descriptor_address : pointer_address});
	}
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
	for (auto& provider : providers) {
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
			const auto binding = std::find_if(resolved.begin(), resolved.end(),
				[&symbol](const DyldResolvedBinding& candidate) {
					return candidate.symbol == symbol.name;
				});
			if (binding != resolved.end())
				relocation_addresses[index] = binding->address;
		}
		if (!provider.image.Relocations().empty())
			provider.image.ApplyRelocations(provider.mapping, relocation_addresses, true);
		if (!resolved.empty()) {
			provider.image.ApplyBindActions(provider.mapping, resolved);
			provider.image.ApplyLazyBindActions(provider.mapping, resolved);
			provider.image.ApplyChainedFixups(provider.mapping, resolved);
		}
		provider.image.ExecuteInitializers(provider.mapping);
		RegisterObjectiveCSections(provider.image, provider.mapping);
	}
	image.ApplyRebaseActions(mapping);
	InitializeTLVSections(image, mapping);
	std::vector<std::uintptr_t> relocation_addresses(image.Symbols().size());
	for (std::size_t index = 0; index < image.Symbols().size(); ++index) {
		const auto& symbol = image.Symbols()[index];
		if ((symbol.type & 0x0e) != 0) {
			relocation_addresses[index] = image.SymbolAddress(mapping, symbol.name);
			continue;
		}
		const auto binding = std::find_if(resolved.begin(), resolved.end(),
			[&symbol](const DyldResolvedBinding& candidate) {
				return candidate.symbol == symbol.name;
			});
		if (binding != resolved.end())
			relocation_addresses[index] = binding->address;
	}
	if (!image.Relocations().empty())
		image.ApplyRelocations(mapping, relocation_addresses, true);
	if (!resolved.empty()) {
		image.ApplyBindActions(mapping, resolved);
		image.ApplyLazyBindActions(mapping, resolved);
		image.ApplyChainedFixups(mapping, resolved);
	}
	image.ExecuteInitializers(mapping);
	RegisterObjectiveCSections(image, mapping);
	const auto exit_code = image.ExecuteEntry(mapping, options.arguments, options.environment);
	image.ExecuteTerminators(mapping);
	for (auto provider = providers.rbegin(); provider != providers.rend(); ++provider)
		provider->image.ExecuteTerminators(provider->mapping);
	return exit_code;
}

} // namespace darling::windows_host
