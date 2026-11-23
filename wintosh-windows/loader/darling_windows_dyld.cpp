/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_dyld.h"
#include "darling_windows_macho.h"
#include "darling_windows_abi_bridge.h"
#include "darling_windows_objc.h"
#include "darling_windows_stdio.h"
#include "darling_windows_tlv.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include <windows.h>

namespace darling::windows_host {

namespace {

std::atomic<std::int32_t> dynamic_image_preferred_cpu_type{0};

bool BindLazyAtLaunch(const MachOImage& image)
{
	const bool force_eager = std::getenv("WINTOSH_FORCE_EAGER_BIND") != nullptr;
	return force_eager || image.IsArm64() || image.Is32Bit() ||
		!image.ChainedFixups().empty() ||
		std::none_of(image.BindActions().begin(), image.BindActions().end(),
			[](const DyldBindAction& action) { return action.lazy; });
}

DWORD DarwinProtectionToWindows(std::int32_t protection)
{
	const bool read = (protection & 1) != 0;
	const bool write = (protection & 2) != 0;
	const bool execute = (protection & 4) != 0;
	if (execute)
		return write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	return write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS);
}

std::uintptr_t MappedTLVSectionAddress(const MachOSection& section,
	const MachOImage::Mapping& mapping)
{
	if (section.address > static_cast<std::uint64_t>(
		(std::numeric_limits<std::intptr_t>::max)()))
		throw std::overflow_error("Mach-O dynamic TLV section address cannot be slid safely");
	const auto address = static_cast<std::intptr_t>(section.address) + mapping.Slide();
	if (address <= 0)
		throw std::runtime_error("Mach-O dynamic TLV section address is invalid");
	return static_cast<std::uintptr_t>(address);
}

std::vector<std::byte> BuildDynamicTLVTemplate(const MachOImage& image,
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
			throw std::runtime_error("Mach-O dynamic TLV template extent overflows");
		begin = (std::min)(begin, section.address);
		end = (std::max)(end, section.address + section.size);
		if (section.section_name == "__thread_data")
			data_sections.push_back(&section);
	}
	if (begin == (std::numeric_limits<std::uint64_t>::max)())
		return {};
	const auto extent = end - begin;
	if (extent > (std::numeric_limits<std::size_t>::max)())
		throw std::runtime_error("Mach-O dynamic TLV template exceeds host address space");
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

void AddCandidate(std::vector<std::filesystem::path>& candidates,
	const std::filesystem::path& candidate)
{
	candidates.push_back(candidate.lexically_normal());
}

void AddFrameworkCandidates(std::vector<std::filesystem::path>& candidates,
	const std::filesystem::path& root, const std::string& name)
{
	AddCandidate(candidates, root / name);
	if (name.find('/') == std::string::npos &&
		std::filesystem::path(name).extension().empty())
	{
		const auto framework = root / (name + ".framework");
		AddCandidate(candidates, framework / name);
		// Versioned framework bundles conventionally expose either a
		// Versions/Current symlink or a concrete version directory.  Windows
		// has no symlink requirement here, so both layouts are deterministic
		// candidate paths and the existing regular-file check selects one.
		AddCandidate(candidates, framework / "Versions" / "Current" / name);
		AddCandidate(candidates, framework / "Versions" / "A" / name);
		// Real bundles are allowed to use a version directory other than A.
		// Enumerate only immediate, directory-backed children so resolution stays
		// bounded and deterministic; lexical sorting gives stable precedence.
		std::error_code error;
		std::vector<std::filesystem::path> versions;
		const auto versions_root = framework / "Versions";
		for (const auto& entry : std::filesystem::directory_iterator(
			versions_root, std::filesystem::directory_options::skip_permission_denied, error)) {
			if (error) break;
			std::error_code entry_error;
			if (entry.is_directory(entry_error) && !entry_error &&
				entry.path().filename() != "Current") {
				versions.push_back(entry.path());
			}
		}
		std::sort(versions.begin(), versions.end());
		for (const auto& version : versions)
			AddCandidate(candidates, version / name);
	}
}

std::filesystem::path ExpandRPath(const std::filesystem::path& image_directory,
	const std::filesystem::path& executable_directory,
	const std::filesystem::path& rpath)
{
	const auto value = rpath.string();
	if (value.rfind("@loader_path/", 0) == 0) {
		const auto separator = value.find('/');
		return image_directory / value.substr(separator + 1);
	}
	if (value.rfind("@executable_path/", 0) == 0) {
		const auto separator = value.find('/');
		return executable_directory / value.substr(separator + 1);
	}
	return rpath;
}

bool IsBuiltinDarwinProvider(const std::string& dependency)
{
	const auto name = std::filesystem::path(dependency).filename().string();
	return name == "libSystem.B.dylib" || name == "libSystem.dylib" ||
		name == "libobjc.A.dylib" || name == "libobjc.dylib" ||
		// These framework APIs are implemented by Wintosh host providers and
		// resolved per symbol through darling_windows_host_symbol.  They are
		// virtual providers: no macOS framework binary is required on Windows.
		name == "CoreFoundation" || name == "CoreFoundation.framework" ||
		name == "CoreGraphics" || name == "CoreGraphics.framework" ||
		name == "ImageIO" || name == "ImageIO.framework" ||
		name == "Foundation" || name == "Foundation.framework" ||
		name == "UniformTypeIdentifiers" ||
		name == "UniformTypeIdentifiers.framework" ||
		name == "AppKit" || name == "AppKit.dylib" ||
		name == "AppKit.framework";
}

darling_objc_image_token* RegisterDynamicObjectiveCSections(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	auto* token = darling_objc_begin_image_registration();
	if (token == nullptr) return nullptr;
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__objc_classlist" &&
			section.section_name != "__objc_nlclslist" &&
			section.section_name != "__objc_catlist" &&
			section.section_name != "__objc_selrefs" &&
			section.section_name != "__cfstring")
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
		else if (section.section_name == "__cfstring")
			darling_objc_register_macho_constant_strings(
				reinterpret_cast<const void*>(address),
				static_cast<std::size_t>(section.size));
		else
			darling_objc_register_macho_classlist(
				reinterpret_cast<const void*>(address),
				static_cast<std::size_t>(section.size));
	}
	darling_objc_end_image_registration(token);
	return token;
}

std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>>
InitializeDynamicTLVSections(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> registrations;
	const auto template_storage = BuildDynamicTLVTemplate(image, mapping);
	const void* template_data = template_storage.empty() ? nullptr : template_storage.data();
	const auto template_bytes = template_storage.size();
	std::vector<DarwinTLVDescriptorRange> ranges;
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__thread_vars" || section.size == 0)
			continue;
		if (section.size % sizeof(DarwinTLVDescriptor) != 0)
			throw std::runtime_error("Mach-O dynamic __thread_vars section is misaligned");
		auto* descriptors = reinterpret_cast<DarwinTLVDescriptor*>(
			MappedTLVSectionAddress(section, mapping));
		const auto count = static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor));
		ranges.push_back({descriptors, count});
		registrations.emplace_back(descriptors, count);
	}
	if (!ranges.empty())
		InitializeDarwinTLV(ranges.data(), ranges.size(), template_data, template_bytes);
	std::vector<std::uintptr_t> descriptor_addresses;
	std::vector<std::uintptr_t> pointer_slots;
	struct PointerSectionProtection final {
		std::uintptr_t address;
		std::size_t bytes;
		DWORD original;
	};
	std::vector<PointerSectionProtection> pointer_protections;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_vars") {
			if (section.size % sizeof(DarwinTLVDescriptor) != 0)
				throw std::runtime_error("Mach-O dynamic __thread_vars section is misaligned");
			const auto address = MappedTLVSectionAddress(section, mapping);
			const auto count = static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor));
			for (std::size_t index = 0; index < count; ++index)
				descriptor_addresses.push_back(address + index * sizeof(DarwinTLVDescriptor));
		} else if (section.section_name == "__thread_ptrs") {
			if (section.size % sizeof(std::uintptr_t) != 0)
				throw std::runtime_error("Mach-O dynamic __thread_ptrs section is misaligned");
			const auto address = MappedTLVSectionAddress(section, mapping);
			const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
			for (std::size_t index = 0; index < count; ++index)
				pointer_slots.push_back(address + index * sizeof(std::uintptr_t));
			if (section.size != 0) {
				const auto segment = std::find_if(image.Segments().begin(), image.Segments().end(),
					[&](const auto& candidate) { return candidate.name == section.segment_name; });
				if (segment == image.Segments().end())
					throw std::runtime_error("Mach-O dynamic TLV pointer section has no segment");
				DWORD ignored = 0;
				if (!VirtualProtect(reinterpret_cast<void*>(address),
					static_cast<SIZE_T>(section.size), PAGE_READWRITE, &ignored))
					throw std::runtime_error("cannot make dynamic Mach-O TLV pointer section writable");
				pointer_protections.push_back({address,
					static_cast<std::size_t>(section.size),
					DarwinProtectionToWindows(segment->initial_protection)});
			}
		}
	}
	if (descriptor_addresses.size() != pointer_slots.size())
		throw std::runtime_error("Mach-O dynamic TLV descriptor and pointer counts do not match");
	for (std::size_t index = 0; index < descriptor_addresses.size(); ++index)
		std::memcpy(reinterpret_cast<void*>(pointer_slots[index]),
			&descriptor_addresses[index], sizeof(std::uintptr_t));
	for (const auto& protection : pointer_protections) {
		DWORD ignored = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(protection.address),
			static_cast<SIZE_T>(protection.bytes), protection.original, &ignored))
			throw std::runtime_error("cannot restore dynamic Mach-O TLV pointer protection");
	}
	return registrations;
}

const void* MappedMachOHeader(const MachOImage::Mapping& mapping) noexcept
{
	const auto& segments = mapping.Segments();
	const auto text = std::find_if(segments.begin(), segments.end(),
		[](const MappedMachOSegment& segment) { return segment.name == "__TEXT"; });
	if (text != segments.end()) return reinterpret_cast<const void*>(text->address);
	const auto loadable = std::find_if(segments.begin(), segments.end(),
		[](const MappedMachOSegment& segment) { return segment.name != "__PAGEZERO"; });
	return loadable == segments.end() ? nullptr :
		reinterpret_cast<const void*>(loadable->address);
}

} // namespace

std::filesystem::path DylibResolver::Resolve(
	const std::filesystem::path& image_path,
	const std::string& dependency,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix,
	const std::filesystem::path& executable_path,
	std::int32_t preferred_cpu_type)
{
	const std::filesystem::path image_directory = image_path.parent_path();
	const std::filesystem::path executable_directory = executable_path.empty() ?
		image_directory : executable_path.parent_path();
	const std::string loader_token = "@loader_path";
	const std::string executable_token = "@executable_path";
	const std::string rpath_token = "@rpath/";
	std::vector<std::filesystem::path> candidates;
	std::vector<std::filesystem::path> effective_rpaths = rpaths;
	// Darwin combines loader-supplied runpaths with the LC_RPATH commands
	// embedded in the image.  Do not skip the image runpaths merely because a
	// caller supplied one or more search roots.
	if (dependency.rfind(rpath_token, 0) == 0) {
		const auto image = MachOImage::Open(image_path.wstring(), preferred_cpu_type);
		for (const auto& rpath : image.RPaths()) {
			// Keep @loader_path/@executable_path tokens intact; lexical
			// normalization would treat the token as an ordinary directory and
			// can erase it before ExpandRPath sees it.
			const std::filesystem::path candidate_rpath(rpath);
			if (std::find(effective_rpaths.begin(), effective_rpaths.end(), candidate_rpath) ==
				effective_rpaths.end()) {
				effective_rpaths.emplace_back(candidate_rpath);
			}
		}
	}

	if (dependency.rfind(rpath_token, 0) == 0) {
		const auto suffix = dependency.substr(rpath_token.size());
		for (const auto& rpath : effective_rpaths) {
			const auto expanded = ExpandRPath(image_directory,
				executable_directory, rpath) / suffix;
			AddFrameworkCandidates(candidates, expanded.parent_path(),
				expanded.filename().string());
		}
		// A normal .app executable commonly relies on dyld's bundle layout
		// without carrying an explicit LC_RPATH: Contents/Frameworks and
		// Contents/PlugIns are adjacent to Contents/MacOS.  Add these only as
		// scoped fallbacks so ordinary prefix resolution keeps its precedence.
		const auto contents = executable_directory.parent_path();
		for (const auto& bundle_directory : {
			contents / "Frameworks", contents / "PlugIns"}) {
			AddFrameworkCandidates(candidates, bundle_directory,
				suffix);
		}
	} else if (dependency.rfind(loader_token + "/", 0) == 0) {
		AddFrameworkCandidates(candidates, image_directory,
			dependency.substr(loader_token.size() + 1));
	} else if (dependency.rfind(executable_token + "/", 0) == 0) {
		AddFrameworkCandidates(candidates, executable_directory,
			dependency.substr(executable_token.size() + 1));
	} else if (!dependency.empty() && dependency.front() == '/') {
		const auto rooted = dependency.substr(1);
		const auto rooted_path = std::filesystem::path(rooted);
		if (rooted_path.parent_path().filename() == "Frameworks")
		{
			AddFrameworkCandidates(candidates, prefix / rooted_path.parent_path(),
				rooted_path.filename().string());
			// Keep the two Darwin framework domains distinct: the system domain
			// is rooted at /System/Library, while /Library is the local domain.
			if (rooted_path.parent_path() == std::filesystem::path("Library") / "Frameworks")
				AddFrameworkCandidates(candidates, prefix / "Library" / "Frameworks",
					rooted_path.filename().string());
		}
		else
			AddCandidate(candidates, prefix / rooted);
	} else {
		AddCandidate(candidates, image_directory / dependency);
		AddCandidate(candidates, prefix / "usr" / "lib" / dependency);
		AddFrameworkCandidates(candidates,
			prefix / "System" / "Library" / "Frameworks", dependency);
		AddFrameworkCandidates(candidates,
			prefix / "Library" / "Frameworks", dependency);
	}

	for (const auto& candidate : candidates) {
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate, error) && !error) {
			return candidate;
		}
	}
	throw std::runtime_error("cannot resolve Mach-O dylib: " + dependency);
}

std::vector<DylibGraphNode> DylibGraph::Load(
	const std::filesystem::path& image_path,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix,
	std::int32_t preferred_cpu_type)
{
	if (preferred_cpu_type == 0)
		preferred_cpu_type = MachOImage::Open(image_path.wstring()).Header().cpu_type;
	std::vector<DylibGraphNode> graph;
	struct PendingImage final {
		std::filesystem::path path;
		std::vector<std::filesystem::path> inherited_rpaths;
	};
	std::vector<PendingImage> pending{{image_path.lexically_normal(), rpaths}};
	std::set<std::filesystem::path> visited;

	while (!pending.empty()) {
		auto work = std::move(pending.back());
		pending.pop_back();
		const auto normalized = work.path.lexically_normal();
		if (!visited.insert(normalized).second) {
			continue;
		}

		const auto image = MachOImage::Open(normalized.wstring(), preferred_cpu_type);
		auto image_rpaths = std::move(work.inherited_rpaths);
		for (const auto& rpath : image.RPaths()) {
			const std::filesystem::path candidate(rpath);
			if (std::find(image_rpaths.begin(), image_rpaths.end(), candidate) ==
				image_rpaths.end())
				image_rpaths.push_back(candidate);
		}
		DylibGraphNode node;
		node.path = normalized;
		std::vector<PendingImage> children;
		for (const auto& dependency : image.Dependencies()) {
			std::filesystem::path ordinal_dependency;
			try {
				const auto resolved = DylibResolver::Resolve(
					normalized, dependency, image_rpaths, prefix,
					image_path.lexically_normal(), preferred_cpu_type);
				ordinal_dependency = resolved.lexically_normal();
				node.dependencies.push_back(resolved);
				if (visited.find(resolved.lexically_normal()) == visited.end()) {
					children.push_back({resolved.lexically_normal(), image_rpaths});
				}
			} catch (const std::exception&) {
				if (!image.IsWeakDependency(dependency) && !IsBuiltinDarwinProvider(dependency)) {
					throw;
				}
			}
			node.ordinal_dependencies.push_back(std::move(ordinal_dependency));
		}
		graph.push_back(std::move(node));
		// The work list is LIFO; reverse insertion preserves load-command order
		// when selecting the first runpath context for a shared dependency.
		for (auto child = children.rbegin(); child != children.rend(); ++child)
			pending.push_back(std::move(*child));
	}

	std::sort(graph.begin(), graph.end(), [](const auto& left, const auto& right) {
		return left.path.string() < right.path.string();
	});
	return graph;
}

std::vector<std::filesystem::path> DylibGraph::InitializationOrder(
	const std::filesystem::path& image_path,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix,
	std::int32_t preferred_cpu_type)
{
	const auto graph = Load(image_path, rpaths, prefix, preferred_cpu_type);
	std::vector<std::filesystem::path> order;
	std::set<std::filesystem::path> visiting;
	std::set<std::filesystem::path> visited;
	std::function<void(const std::filesystem::path&)> visit =
		[&](const std::filesystem::path& path) {
			const auto normalized = path.lexically_normal();
			if (visited.find(normalized) != visited.end())
				return;
			if (!visiting.insert(normalized).second)
				throw std::runtime_error("cyclic Mach-O dylib initialization graph");
			const auto node = std::find_if(graph.begin(), graph.end(),
				[&normalized](const DylibGraphNode& candidate) {
					return candidate.path == normalized;
				});
			if (node != graph.end()) {
				for (const auto& dependency : node->dependencies)
					visit(dependency);
			}
			visiting.erase(normalized);
			visited.insert(normalized);
			order.push_back(normalized);
		};
	visit(image_path.lexically_normal());
	for (const auto& node : graph)
		visit(node.path);
	return order;
}

std::vector<DylibBinding> DylibGraph::BindImports(
	const std::filesystem::path& image_path,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix,
	std::int32_t preferred_cpu_type)
{
	if (preferred_cpu_type == 0)
		preferred_cpu_type = MachOImage::Open(image_path.wstring()).Header().cpu_type;
	const auto graph = Load(image_path, rpaths, prefix, preferred_cpu_type);
	const auto main_image = MachOImage::Open(image_path.wstring(), preferred_cpu_type);
	const bool force_flat_namespace =
		(main_image.Header().flags & MH_FORCE_FLAT) != 0;
	struct Definition final {
		std::string name;
		std::filesystem::path provider;
		std::uint64_t value;
		bool weak = false;
	};
	std::function<bool(const std::filesystem::path&, const std::string&,
		std::set<std::string>&, Definition&)> resolve_export =
		[&](const std::filesystem::path& owner, const std::string& name,
			std::set<std::string>& visiting, Definition& result) {
			const auto key = owner.lexically_normal().string() + "\n" + name;
			if (!visiting.insert(key).second)
				return false;
			const auto image = MachOImage::Open(owner.wstring(), preferred_cpu_type);
			for (const auto& symbol : image.Symbols()) {
				if (!symbol.name.empty() && symbol.name == name &&
					(symbol.type & 0x0e) != 0) {
					if ((symbol.type & 0x0e) == 0x0a) {
						if (!symbol.indirect_target.empty() && resolve_export(owner,
							symbol.indirect_target, visiting, result)) {
							visiting.erase(key);
							return true;
						}
						continue;
					}
					result = {name, owner, symbol.value,
						(symbol.description & 0x0080u) != 0};
					visiting.erase(key);
					return true;
				}
			}
			for (const auto& reexport : image.Reexports()) {
				if (reexport.name != name || reexport.dependency_ordinal == 0)
					continue;
				const auto owner_node = std::find_if(graph.begin(), graph.end(),
					[&owner](const DylibGraphNode& candidate) {
						return candidate.path == owner.lexically_normal();
					});
				if (owner_node == graph.end() ||
					reexport.dependency_ordinal > owner_node->ordinal_dependencies.size())
					continue;
				const auto& dependency = owner_node->ordinal_dependencies[
					reexport.dependency_ordinal - 1];
				const auto target = reexport.target.empty() ? name : reexport.target;
				if (dependency.empty()) {
					const auto host_address = darling_windows_host_symbol(target.c_str());
					if (host_address != 0) {
						result = {target, {}, host_address, false};
						visiting.erase(key);
						return true;
					}
					continue;
				}
				if (resolve_export(dependency, target, visiting, result)) {
					visiting.erase(key);
					return true;
				}
			}
			visiting.erase(key);
			return false;
		};
	std::vector<std::filesystem::path> flat_lookup_order;
	std::set<std::filesystem::path> flat_lookup_seen;
	std::function<void(const std::filesystem::path&)> append_flat_lookup_image =
		[&](const std::filesystem::path& candidate_path) {
			const auto normalized = candidate_path.lexically_normal();
			if (!flat_lookup_seen.insert(normalized).second)
				return;
			flat_lookup_order.push_back(normalized);
			const auto node = std::find_if(graph.begin(), graph.end(),
				[&normalized](const DylibGraphNode& candidate) {
					return candidate.path == normalized;
				});
			if (node == graph.end())
				return;
			for (const auto& dependency : node->dependencies)
				append_flat_lookup_image(dependency);
		};
	append_flat_lookup_image(image_path);

	std::vector<DylibBinding> bindings;
	for (const auto& node : graph) {
		const auto image = MachOImage::Open(node.path.wstring(), preferred_cpu_type);
		const bool two_level = image.UsesTwoLevelNamespace() &&
			!force_flat_namespace;
		struct ImportRequest final {
			std::string name;
			std::int32_t ordinal;
			bool weak;
			bool weak_bind;
		};
		std::vector<ImportRequest> imports;
		const auto add_import = [&imports](const std::string& name,
			std::int32_t ordinal, bool weak, bool weak_bind) {
			auto existing = std::find_if(imports.begin(), imports.end(),
				[&name, ordinal](const ImportRequest& candidate) {
					return candidate.name == name && candidate.ordinal == ordinal;
				});
			if (existing == imports.end())
				imports.push_back({name, ordinal, weak, weak_bind});
			else {
				existing->weak = existing->weak && weak;
				existing->weak_bind = existing->weak_bind && weak_bind;
			}
		};
		for (const auto& symbol : image.UndefinedSymbols()) {
			const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
				MachOLibraryOrdinalFromSymbolDescription(symbol.description);
			add_import(symbol.name, ordinal, MachOImage::IsWeakUndefined(symbol), false);
		}
		for (const auto& action : image.BindActions()) {
			if (!action.threaded)
				add_import(action.symbol, action.dylib_ordinal, action.weak,
					action.weak_bind);
		}
		for (const auto& target : image.ThreadedBindTargets())
			add_import(target.symbol, target.dylib_ordinal, target.weak,
				target.weak_bind);
		for (const auto& import : image.ChainedImports()) {
			add_import(import.symbol, import.dylib_ordinal, import.weak, false);
		}
		for (const auto& import : imports) {
			const std::int32_t binding_ordinal = import.ordinal;
			if (two_level && import.ordinal < -3)
				throw std::runtime_error("Mach-O import has an invalid special dylib ordinal: " +
					import.name);
			std::filesystem::path requested_provider;
			if (two_level && import.ordinal == 0) {
				requested_provider = node.path;
			} else if (two_level && import.ordinal == -1) {
				requested_provider = image_path.lexically_normal();
			} else if (two_level && import.ordinal > 0 &&
				static_cast<std::size_t>(import.ordinal) <=
					node.ordinal_dependencies.size()) {
				requested_provider = node.ordinal_dependencies[
					static_cast<std::size_t>(import.ordinal - 1)];
			}
			if (two_level && import.ordinal > 0 &&
				static_cast<std::size_t>(import.ordinal) >
					node.ordinal_dependencies.size())
				throw std::runtime_error("Mach-O import has an out-of-range dylib ordinal: " +
					import.name);

			Definition forwarded;
			bool resolved = false;
			if (!requested_provider.empty()) {
				std::set<std::string> visiting;
				resolved = resolve_export(requested_provider, import.name, visiting,
					forwarded);
				if (resolved && forwarded.weak) {
					Definition first_weak_definition;
					bool have_weak_definition = false;
					for (const auto& candidate : flat_lookup_order) {
						visiting.clear();
						Definition candidate_definition;
						if (!resolve_export(candidate, import.name, visiting,
							candidate_definition))
							continue;
						if (!candidate_definition.weak) {
							forwarded = std::move(candidate_definition);
							have_weak_definition = false;
							break;
						}
						if (!have_weak_definition) {
							first_weak_definition = std::move(candidate_definition);
							have_weak_definition = true;
						}
					}
					if (have_weak_definition)
						forwarded = std::move(first_weak_definition);
				}
			} else if (!two_level || import.ordinal == -2 || import.ordinal == -3) {
				// Dynamic and weak lookup ordinals search the flat namespace.
				// Keep the first weak definition, but let the first strong
				// definition in load order override it.
				std::set<std::string> visiting;
				Definition first_weak_definition;
				bool have_weak_definition = false;
				for (const auto& candidate : flat_lookup_order) {
					visiting.clear();
					Definition candidate_definition;
					if (!resolve_export(candidate, import.name, visiting,
						candidate_definition))
						continue;
					if (!candidate_definition.weak) {
						forwarded = std::move(candidate_definition);
						resolved = true;
						break;
					}
					if (!have_weak_definition) {
						first_weak_definition = std::move(candidate_definition);
						have_weak_definition = true;
					}
				}
				if (!resolved && have_weak_definition) {
					forwarded = std::move(first_weak_definition);
					resolved = true;
				}
			}
			if (resolved) {
				bindings.push_back({import.name, forwarded.provider, forwarded.name,
					forwarded.value, node.path, binding_ordinal, two_level});
				continue;
			}
			{
				const auto host_address = darling_windows_host_symbol(import.name.c_str());
				if (host_address != 0 &&
					(!two_level || import.ordinal == -2 || import.ordinal == -3 ||
						requested_provider.empty())) {
					bindings.push_back({import.name, {}, {}, host_address,
						node.path, binding_ordinal, two_level});
					continue;
				}
				if (import.weak || import.ordinal == -3) {
					if (import.weak_bind)
						continue;
					// Darwin leaves an unresolved weak reference as NULL.
					bindings.push_back({import.name, {}, {}, 0,
						node.path, binding_ordinal, two_level});
					continue;
				}
				throw std::runtime_error("unresolved Mach-O import in " +
					node.path.filename().string() + " from ordinal " +
					std::to_string(import.ordinal) + ": " + import.name);
			}
		}
	}
	return bindings;
}

std::vector<DylibExportResolver> DylibGraph::ExportResolverOrder(
	const std::vector<DylibBinding>& bindings,
	std::int32_t preferred_cpu_type)
{
	if (preferred_cpu_type == 0 && !bindings.empty()) {
		const auto& path = bindings.front().client_image.empty() ?
			bindings.front().provider : bindings.front().client_image;
		if (!path.empty())
			preferred_cpu_type = MachOImage::Open(path.wstring()).Header().cpu_type;
	}
	struct CachedImage final {
		std::filesystem::path path;
		MachOImage image;
	};
	std::vector<CachedImage> images;
	const auto image_for = [&images, preferred_cpu_type](
		const std::filesystem::path& input_path)
		-> const MachOImage& {
		const auto path = input_path.lexically_normal();
		auto found = std::find_if(images.begin(), images.end(),
			[&path](const CachedImage& candidate) { return candidate.path == path; });
		if (found == images.end()) {
			images.push_back({path, MachOImage::Open(path.wstring(), preferred_cpu_type)});
			found = std::prev(images.end());
		}
		return found->image;
	};
	const auto is_resolver_export = [&image_for](
		const std::filesystem::path& provider, const std::string& symbol) {
		if (provider.empty() || symbol.empty()) return false;
		const auto& image = image_for(provider);
		return std::any_of(image.Symbols().begin(), image.Symbols().end(),
			[&symbol](const MachOSymbol& candidate) {
				return candidate.name == symbol && candidate.has_resolver;
			});
	};
	std::vector<DylibExportResolver> nodes;
	for (const auto& binding : bindings) {
		if (!is_resolver_export(binding.provider, binding.provider_symbol))
			continue;
		const auto provider = binding.provider.lexically_normal();
		const auto found = std::find_if(nodes.begin(), nodes.end(),
			[&provider, &binding](const DylibExportResolver& candidate) {
				return candidate.provider == provider &&
					candidate.symbol == binding.provider_symbol;
			});
		if (found == nodes.end())
			nodes.push_back({provider, binding.provider_symbol});
	}
	std::vector<std::vector<std::size_t>> dependencies(nodes.size());
	for (std::size_t owner_index = 0; owner_index < nodes.size(); ++owner_index) {
		for (const auto& binding : bindings) {
			if (binding.client_image.lexically_normal() !=
				nodes[owner_index].provider || binding.provider.empty() ||
				!is_resolver_export(binding.provider, binding.provider_symbol))
				continue;
			const auto provider = binding.provider.lexically_normal();
			const auto dependency = std::find_if(nodes.begin(), nodes.end(),
				[&provider, &binding](const DylibExportResolver& candidate) {
					return candidate.provider == provider &&
						candidate.symbol == binding.provider_symbol;
				});
			if (dependency != nodes.end()) {
				const auto dependency_index = static_cast<std::size_t>(
					std::distance(nodes.begin(), dependency));
				if (std::find(dependencies[owner_index].begin(),
					dependencies[owner_index].end(), dependency_index) ==
					dependencies[owner_index].end())
					dependencies[owner_index].push_back(dependency_index);
			}
		}
	}
	std::vector<std::uint8_t> state(nodes.size());
	std::vector<DylibExportResolver> order;
	order.reserve(nodes.size());
	std::function<void(std::size_t)> visit = [&](std::size_t index) {
		if (state[index] == 2) return;
		if (state[index] == 1)
			throw std::runtime_error("cyclic Mach-O export resolver dependencies");
		state[index] = 1;
		for (const auto dependency : dependencies[index])
			visit(dependency);
		state[index] = 2;
		order.push_back(nodes[index]);
	};
	for (std::size_t index = 0; index < nodes.size(); ++index)
		visit(index);
	return order;
}

class DarwinDynamicImage final {
public:
	struct Provider final {
		std::filesystem::path path;
		MachOImage image;
		MachOImage::Mapping mapping;
		std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> tlv_registrations;
		std::vector<DyldResolvedBinding> bindings;
	};
	std::filesystem::path path;
	MachOImage image;
	MachOImage::Mapping mapping;
	std::vector<Provider> providers;
	std::vector<DyldResolvedBinding> bindings;
	std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> tlv_registrations;
	std::vector<darling_objc_image_token*> objc_tokens;
	std::vector<std::unique_ptr<MachOLazyBindingSession>> lazy_sessions;
	std::vector<MachOLazyBindingSession::Registration> lazy_registrations;

	~DarwinDynamicImage() noexcept
	{
		for (const auto& registration : tlv_registrations)
			DestroyDarwinTLV(registration.first, registration.second);
		for (auto& provider : providers) {
			for (const auto& registration : provider.tlv_registrations)
				DestroyDarwinTLV(registration.first, registration.second);
		}
	}
};

void SetDynamicImagePreferredCpuType(std::int32_t cpu_type) noexcept
{
	dynamic_image_preferred_cpu_type.store(cpu_type, std::memory_order_release);
}

DarwinDynamicImage* OpenDynamicImage(const std::filesystem::path& path,
	std::int32_t preferred_cpu_type)
{
	if (preferred_cpu_type == 0)
		preferred_cpu_type = dynamic_image_preferred_cpu_type.load(std::memory_order_acquire);
	auto* result = new DarwinDynamicImage{path,
		MachOImage::Open(path.wstring(), preferred_cpu_type),
		{}, {}, {}, {}, {}};
	try {
		result->mapping = result->image.MapSegments();
		const auto guest_cpu_type = result->image.Header().cpu_type;
		const auto graph_nodes = DylibGraph::Load(path, {}, {}, guest_cpu_type);
		const auto graph_bindings = DylibGraph::BindImports(path, {}, {}, guest_cpu_type);
		std::vector<std::uintptr_t> resolved_addresses;
		resolved_addresses.reserve(graph_bindings.size());
		const auto main_path = path.lexically_normal();
		const auto preferred_provider_header = MappedMachOHeader(result->mapping);
		const auto preferred_provider_address = reinterpret_cast<std::uintptr_t>(
			preferred_provider_header);
		auto get_provider = [&](const std::filesystem::path& provider_path)
			-> DarwinDynamicImage::Provider& {
			const auto normalized = provider_path.lexically_normal();
			auto found = std::find_if(result->providers.begin(), result->providers.end(),
				[&normalized](const DarwinDynamicImage::Provider& candidate) {
					return candidate.path == normalized;
				});
			if (found == result->providers.end()) {
				result->providers.push_back({normalized,
				MachOImage::Open(normalized.wstring(), guest_cpu_type), {}, {}, {}});
				result->providers.back().mapping = result->providers.back().image.MapSegments(
					preferred_provider_address);
				found = std::prev(result->providers.end());
			}
			return *found;
		};
		// Load and initialize every dependency in the graph, including dylibs
		// that contribute no undefined symbols to this image.
		for (const auto& node : graph_nodes) {
			if (node.path != main_path) (void)get_provider(node.path);
		}
		for (const auto& binding : graph_bindings) {
			auto append_client_binding = [&](std::uintptr_t address) {
				resolved_addresses.push_back(address);
				DyldResolvedBinding resolved{binding.name, address,
				binding.dylib_ordinal, binding.has_dylib_ordinal};
				if (binding.client_image.lexically_normal() == main_path)
					result->bindings.push_back(std::move(resolved));
				else
					get_provider(binding.client_image).bindings.push_back(
						std::move(resolved));
			};
			if (binding.provider.empty()) {
				append_client_binding(static_cast<std::uintptr_t>(
					binding.provider_value));
				continue;
			}
			std::uintptr_t descriptor_address = 0;
			std::uintptr_t pointer_address = 0;
			if (binding.provider.lexically_normal() == main_path) {
				descriptor_address = result->image.SymbolAddress(
					result->mapping, binding.provider_symbol);
				pointer_address = result->image.TLVPointerAddress(
					result->mapping, descriptor_address);
			} else {
				auto& provider = get_provider(binding.provider);
				descriptor_address = provider.image.SymbolAddress(
					provider.mapping, binding.provider_symbol);
				pointer_address = provider.image.TLVPointerAddress(
					provider.mapping, descriptor_address);
			}
			// A cross-image TLV import addresses the provider's per-thread
			// pointer slot, not the descriptor object itself.
			append_client_binding(pointer_address == 0 ? descriptor_address :
				pointer_address);
		}
		if (resolved_addresses.size() != graph_bindings.size())
			throw std::logic_error("dynamic dyld lost a dependency binding record");
		auto update_client_binding = [&](std::size_t index, std::uintptr_t address) {
			const auto& source = graph_bindings[index];
			auto& client_records = source.client_image.lexically_normal() == main_path ?
				result->bindings : get_provider(source.client_image).bindings;
			for (auto& record : client_records) {
				if (record.symbol == source.name &&
					(!record.has_dylib_ordinal ||
						record.dylib_ordinal == source.dylib_ordinal))
					record.address = address;
			}
		};
		const auto initialization_order = DylibGraph::InitializationOrder(
			path, {}, {}, guest_cpu_type);
		const auto provider_rank = [&initialization_order](
			const std::filesystem::path& provider_path) {
			const auto found = std::find(initialization_order.begin(),
				initialization_order.end(), provider_path.lexically_normal());
			return found == initialization_order.end() ? initialization_order.size() :
				static_cast<std::size_t>(std::distance(initialization_order.begin(), found));
		};
		std::stable_sort(result->providers.begin(), result->providers.end(),
			[&provider_rank](const DarwinDynamicImage::Provider& left,
				const DarwinDynamicImage::Provider& right) {
				return provider_rank(left.path) < provider_rank(right.path);
			});
		for (auto& provider : result->providers) {
			provider.image.ApplyRebaseActions(provider.mapping);
			const auto registrations = InitializeDynamicTLVSections(provider.image,
				provider.mapping);
			provider.tlv_registrations.insert(provider.tlv_registrations.end(),
				registrations.begin(), registrations.end());
			std::vector<std::uintptr_t> provider_symbols(provider.image.Symbols().size());
			for (std::size_t index = 0; index < provider.image.Symbols().size(); ++index) {
				const auto& symbol = provider.image.Symbols()[index];
				if ((symbol.type & 0x0e) != 0)
					provider_symbols[index] = provider.image.SymbolAddress(
						provider.mapping, symbol.name);
				else {
					const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
						MachOLibraryOrdinalFromSymbolDescription(symbol.description);
					const auto found = std::find_if(provider.bindings.begin(), provider.bindings.end(),
						[&symbol, ordinal](const DyldResolvedBinding& candidate) {
							return candidate.symbol == symbol.name &&
								(!candidate.has_dylib_ordinal || candidate.dylib_ordinal ==
									ordinal);
						});
					if (found != provider.bindings.end())
						provider_symbols[index] = found->address;
				}
			}
			if (!provider.image.Relocations().empty())
				provider.image.ApplyRelocations(provider.mapping, provider_symbols, true);
			if (!provider.bindings.empty()) {
				provider.image.ApplyBindActions(provider.mapping, provider.bindings);
				if (BindLazyAtLaunch(provider.image))
					MachOLazyBindingSession(provider.image, provider.mapping,
						provider.bindings).ApplyAll();
				provider.image.ApplyChainedFixups(provider.mapping, provider.bindings);
			}
			provider.image.ApplyIndirectImportBindings(provider.mapping,
				provider.bindings, BindLazyAtLaunch(provider.image));
		}
		result->image.ApplyRebaseActions(result->mapping);
		result->tlv_registrations = InitializeDynamicTLVSections(result->image,
			result->mapping);
		std::vector<std::uintptr_t> symbol_addresses(result->image.Symbols().size());
		for (std::size_t index = 0; index < result->image.Symbols().size(); ++index) {
			const auto& symbol = result->image.Symbols()[index];
			if ((symbol.type & 0x0e) != 0) {
				symbol_addresses[index] = result->image.SymbolAddress(
					result->mapping, symbol.name);
				continue;
			}
			const auto ordinal = MachOImage::IsFlatLookupUndefined(symbol) ? -2 :
				MachOLibraryOrdinalFromSymbolDescription(symbol.description);
			const auto found = std::find_if(result->bindings.begin(), result->bindings.end(),
				[&symbol, ordinal](const DyldResolvedBinding& candidate) {
					return candidate.symbol == symbol.name &&
						(!candidate.has_dylib_ordinal || candidate.dylib_ordinal == ordinal);
				});
			if (found != result->bindings.end())
				symbol_addresses[index] = found->address;
		}
		if (!result->image.Relocations().empty())
			result->image.ApplyRelocations(result->mapping, symbol_addresses, false);
		if (!result->bindings.empty()) {
			result->image.ApplyBindActions(result->mapping, result->bindings);
			if (BindLazyAtLaunch(result->image))
				MachOLazyBindingSession(result->image, result->mapping,
					result->bindings).ApplyAll();
			result->image.ApplyChainedFixups(result->mapping, result->bindings);
		}
		result->image.ApplyIndirectImportBindings(result->mapping,
			result->bindings, BindLazyAtLaunch(result->image));
		const auto reapply_dynamic_client = [](const MachOImage& target_image,
			const MachOImage::Mapping& target_mapping,
			const std::vector<DyldResolvedBinding>& client_bindings,
			bool defer_unreachable_external, bool bind_lazy_at_launch) {
			std::vector<std::uintptr_t> addresses(target_image.Symbols().size());
			for (std::size_t index = 0; index < target_image.Symbols().size(); ++index) {
				const auto& symbol = target_image.Symbols()[index];
				if ((symbol.type & 0x0e) != 0) {
					addresses[index] = target_image.SymbolAddress(target_mapping,
						symbol.name);
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
					addresses[index] = found->address;
			}
			if (!target_image.Relocations().empty())
				target_image.ApplyRelocations(target_mapping, addresses,
					defer_unreachable_external);
			target_image.ApplyBindActions(target_mapping, client_bindings);
			if (bind_lazy_at_launch)
				MachOLazyBindingSession(target_image, target_mapping,
					client_bindings).ApplyAll();
			target_image.ApplyChainedFixups(target_mapping, client_bindings);
			target_image.ApplyIndirectImportBindings(target_mapping, client_bindings,
				bind_lazy_at_launch);
		};
		auto reapply_all_images = [&]() {
			for (auto& provider : result->providers)
				reapply_dynamic_client(provider.image, provider.mapping,
					provider.bindings, true, BindLazyAtLaunch(provider.image));
			reapply_dynamic_client(result->image, result->mapping,
				result->bindings, false, BindLazyAtLaunch(result->image));
		};
		const auto resolve_export = [&](const DylibExportResolver& resolver) {
			const MachOImage* owner_image = nullptr;
			const MachOImage::Mapping* owner_mapping = nullptr;
			if (resolver.provider == main_path) {
				owner_image = &result->image;
				owner_mapping = &result->mapping;
			} else {
				const auto provider = std::find_if(result->providers.begin(),
					result->providers.end(), [&resolver](
						const DarwinDynamicImage::Provider& candidate) {
							return candidate.path == resolver.provider;
						});
				if (provider == result->providers.end())
					throw std::logic_error("dynamic export resolver provider was not mapped");
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
			for (std::size_t index = 0; index < graph_bindings.size(); ++index) {
				if (graph_bindings[index].provider.lexically_normal() == resolver.provider &&
					graph_bindings[index].provider_symbol == resolver.symbol &&
					resolved_addresses[index] == stub_address) {
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
			for (std::size_t index = 0; index < graph_bindings.size(); ++index) {
				if (graph_bindings[index].provider.lexically_normal() == resolver.provider &&
					graph_bindings[index].provider_symbol == resolver.symbol) {
					resolved_addresses[index] = implementation;
					update_client_binding(index, implementation);
				}
			}
			reapply_all_images();
		};
		// Resolver code now sees every image in the dependency graph after its
		// initial rebase and bind pass has completed.
		for (const auto& resolver : DylibGraph::ExportResolverOrder(
			graph_bindings, guest_cpu_type))
			resolve_export(resolver);
		// Keep these image contexts registered until dlclose tears down the
		// mapping. Unlike a bootstrap activation, registration is thread-neutral
		// so guest threads may enter lazy stubs concurrently.
		const auto register_lazy_session = [&](const MachOImage& target_image,
			const MachOImage::Mapping& target_mapping,
			const std::vector<DyldResolvedBinding>& client_bindings) {
			result->lazy_sessions.push_back(std::make_unique<MachOLazyBindingSession>(
				target_image, target_mapping, client_bindings));
			result->lazy_registrations.push_back(
				result->lazy_sessions.back()->Register());
		};
		for (const auto& provider : result->providers)
			register_lazy_session(provider.image, provider.mapping, provider.bindings);
		register_lazy_session(result->image, result->mapping, result->bindings);
		for (auto& provider : result->providers) {
			if (auto* token = RegisterDynamicObjectiveCSections(provider.image,
				provider.mapping))
				result->objc_tokens.push_back(token);
			provider.image.ExecuteInitializers(provider.mapping);
		}
		if (auto* token = RegisterDynamicObjectiveCSections(result->image, result->mapping))
			result->objc_tokens.push_back(token);
		result->image.ExecuteInitializers(result->mapping);
		return result;
	} catch (...) {
		delete result;
		throw;
	}
}

std::uintptr_t DynamicImageSymbol(const DarwinDynamicImage& image, const char* name)
{
	if (name == nullptr || name[0] == '\0')
		return 0;
	const auto lookup = [name](const MachOImage& candidate,
		const MachOImage::Mapping& mapping) {
		try {
			return candidate.SymbolAddress(mapping, name);
		} catch (...) {
			if (name[0] != '_') {
				std::string underscored = "_";
				underscored += name;
				try {
					return candidate.SymbolAddress(mapping, underscored);
				} catch (...) {
				}
			}
			return static_cast<std::uintptr_t>(0);
		}
	};
	if (const auto address = lookup(image.image, image.mapping); address != 0)
		return address;
	for (const auto& provider : image.providers) {
		if (const auto address = lookup(provider.image, provider.mapping); address != 0)
			return address;
	}
	return 0;
}

std::uint32_t DynamicImageDefinedSymbolCount(const DarwinDynamicImage& image) noexcept
{
	std::uint32_t count = 0;
	for (const auto& symbol : image.image.Symbols())
		if ((symbol.type & 0x0e) != 0)
			++count;
	return count;
}

const char* DynamicImageDefinedSymbolName(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept
{
	std::uint32_t current = 0;
	for (const auto& symbol : image.image.Symbols()) {
		if ((symbol.type & 0x0e) == 0)
			continue;
		if (current++ == ordinal)
			return symbol.name.c_str();
	}
	return nullptr;
}

bool DynamicImageHasSymbol(const DarwinDynamicImage& image, const char* name) noexcept
{
	if (name == nullptr || name[0] == '\0')
		return false;
	try { return DynamicImageSymbol(image, name) != 0; }
	catch (...) { return false; }
}

void* DynamicImageSectionData(const DarwinDynamicImage& image,
	const char* segment_name, const char* section_name, std::size_t& size) noexcept
{
	size = 0;
	if (segment_name == nullptr || section_name == nullptr)
		return nullptr;
	try {
		for (const auto& section : image.image.Sections()) {
			if (section.segment_name != segment_name || section.section_name != section_name)
				continue;
			size = static_cast<std::size_t>(section.size);
			return reinterpret_cast<void*>(image.image.SectionAddress(image.mapping,
				section.segment_name, section.section_name));
		}
	} catch (...) {
		size = 0;
	}
	return nullptr;
}

bool DynamicImageHasModuleInitializers(const DarwinDynamicImage& image) noexcept
{
	for (const auto& section : image.image.Sections())
		if (section.section_name == "__mod_init_func" && section.size != 0)
			return true;
	return false;
}

std::uint32_t DynamicImageReferenceCount(const DarwinDynamicImage& image) noexcept
{
	std::uint32_t count = 0;
	for (const auto& symbol : image.image.Symbols())
		if ((symbol.type & 0x0e) == 0)
			++count;
	return count;
}

const char* DynamicImageReferenceName(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept
{
	std::uint32_t current = 0;
	for (const auto& symbol : image.image.Symbols()) {
		if ((symbol.type & 0x0e) != 0)
			continue;
		if (current++ == ordinal)
			return symbol.name.c_str();
	}
	return nullptr;
}

bool DynamicImageReferenceIsTentative(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept
{
	std::uint32_t current = 0;
	for (const auto& symbol : image.image.Symbols()) {
		if ((symbol.type & 0x0e) != 0)
			continue;
		if (current++ == ordinal)
			return symbol.value != 0;
	}
	return false;
}

int DynamicImageExecuteEntry(const DarwinDynamicImage& image,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment,
	DarwinEntryAbi entry_abi)
{
	std::vector<std::pair<const MachOImage*, const MachOImage::Mapping*>> providers;
	providers.reserve(image.providers.size());
	for (const auto& provider : image.providers)
		providers.emplace_back(&provider.image, &provider.mapping);
	return image.image.ExecuteEntry(image.mapping, arguments, environment, {},
		entry_abi, providers);
}

const std::filesystem::path& DynamicImagePath(const DarwinDynamicImage& image) noexcept
{
	return image.path;
}

const void* DynamicImageHeader(const DarwinDynamicImage& image) noexcept
{
	return MappedMachOHeader(image.mapping);
}

std::intptr_t DynamicImageSlide(const DarwinDynamicImage& image) noexcept
{
	return image.mapping.Slide();
}

std::uint32_t DynamicImageProviderCount(const DarwinDynamicImage& image) noexcept
{
	return static_cast<std::uint32_t>((std::min)(image.providers.size(),
		static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
}

const std::filesystem::path& DynamicImageProviderPath(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept
{
	static const std::filesystem::path empty;
	return index < image.providers.size() ? image.providers[index].path : empty;
}

const void* DynamicImageProviderHeader(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept
{
	return index < image.providers.size() ?
		MappedMachOHeader(image.providers[index].mapping) : nullptr;
}

std::intptr_t DynamicImageProviderSlide(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept
{
	return index < image.providers.size() ? image.providers[index].mapping.Slide() : 0;
}

bool DynamicImageContains(const DarwinDynamicImage& image, const void* address) noexcept
{
	const auto value = reinterpret_cast<std::uintptr_t>(address);
	for (const auto& segment : image.mapping.Segments()) {
		if (value >= segment.address && value - segment.address < segment.size)
			return true;
	}
	return false;
}

bool DynamicImageContainsMappedAddress(const DarwinDynamicImage& image,
	const void* address) noexcept
{
	if (DynamicImageContains(image, address)) return true;
	const auto value = reinterpret_cast<std::uintptr_t>(address);
	for (const auto& provider : image.providers) {
		for (const auto& segment : provider.mapping.Segments()) {
			if (value >= segment.address && value - segment.address < segment.size)
				return true;
		}
	}
	return false;
}

void* DynamicImageBase(const DarwinDynamicImage& image) noexcept
{
	const auto& segments = image.mapping.Segments();
	const auto first_loadable = std::find_if(segments.begin(), segments.end(),
		[](const MappedMachOSegment& segment) {
			return segment.address != 0 && segment.size != 0 &&
				segment.name != "__PAGEZERO";
		});
	if (first_loadable == segments.end()) return nullptr;
	const auto lowest = std::min_element(first_loadable, segments.end(),
		[](const MappedMachOSegment& left, const MappedMachOSegment& right) {
			const bool left_loadable = left.address != 0 && left.size != 0 &&
				left.name != "__PAGEZERO";
			const bool right_loadable = right.address != 0 && right.size != 0 &&
				right.name != "__PAGEZERO";
			if (left_loadable != right_loadable) return left_loadable;
			return left_loadable && left.address < right.address;
		});
	return lowest == segments.end() || lowest->address == 0 ? nullptr :
		reinterpret_cast<void*>(lowest->address);
}

bool DynamicImageNearestSymbol(const DarwinDynamicImage& image, const void* address,
	std::string& name, std::uintptr_t& symbol_address) noexcept
{
	const auto target = reinterpret_cast<std::uintptr_t>(address);
	bool found = false;
	const auto visit = [&](const MachOImage& candidate,
		const MachOImage::Mapping& mapping) {
		for (const auto& symbol : candidate.Symbols()) {
			std::uintptr_t current = 0;
			try {
				current = candidate.SymbolAddress(mapping, symbol.name);
			} catch (...) {
				continue;
			}
			if (current <= target && (!found || current > symbol_address)) {
				found = true;
				symbol_address = current;
				name = symbol.name;
			}
		}
	};
	visit(image.image, image.mapping);
	for (const auto& provider : image.providers)
		visit(provider.image, provider.mapping);
	return found;
}

bool DynamicImageSymbolName(const DarwinDynamicImage& image, const void* address,
	std::string& name) noexcept
{
	std::uintptr_t symbol_address = 0;
	return DynamicImageNearestSymbol(image, address, name, symbol_address);
}

void CloseDynamicImage(DarwinDynamicImage* image) noexcept
{
	if (image == nullptr)
		return;
	try {
		image->image.ExecuteTerminators(image->mapping);
		for (auto provider = image->providers.rbegin();
			provider != image->providers.rend(); ++provider) {
			provider->image.ExecuteTerminators(provider->mapping);
		}
		for (auto* token : image->objc_tokens)
			darling_objc_unregister_image(token);
	} catch (...) {
	}
	delete image;
}

} // namespace darling::windows_host
