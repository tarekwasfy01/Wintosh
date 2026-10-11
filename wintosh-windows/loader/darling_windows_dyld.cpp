/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_dyld.h"
#include "darling_windows_objc.h"
#include "darling_windows_stdio.h"
#include "darling_windows_tlv.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
#include <utility>

namespace darling::windows_host {

namespace {

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
		AddCandidate(candidates, root / (name + ".framework") / name);
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
		name == "libobjc.A.dylib" || name == "libobjc.dylib";
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
	darling_objc_end_image_registration(token);
	return token;
}

std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>>
InitializeDynamicTLVSections(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
	std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> registrations;
	const MachOSection* thread_data = nullptr;
	std::size_t template_bytes = 0;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_data") {
			thread_data = &section;
			template_bytes += static_cast<std::size_t>(section.size);
		} else if (section.section_name == "__thread_bss") {
			template_bytes += static_cast<std::size_t>(section.size);
		}
	}
	const void* template_data = thread_data == nullptr ? nullptr :
		reinterpret_cast<const void*>(image.SectionAddress(mapping,
			thread_data->segment_name, thread_data->section_name));
	for (const auto& section : image.Sections()) {
		if (section.section_name != "__thread_vars" || section.size == 0)
			continue;
		if (section.size % sizeof(DarwinTLVDescriptor) != 0)
			throw std::runtime_error("Mach-O dynamic __thread_vars section is misaligned");
		auto* descriptors = reinterpret_cast<DarwinTLVDescriptor*>(
			image.SectionAddress(mapping, section.segment_name, section.section_name));
		InitializeDarwinTLV(descriptors,
			static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor)),
			template_data, template_bytes);
		registrations.emplace_back(descriptors,
			static_cast<std::size_t>(section.size / sizeof(DarwinTLVDescriptor)));
	}
	const MachOSection* vars = nullptr;
	const MachOSection* ptrs = nullptr;
	for (const auto& section : image.Sections()) {
		if (section.section_name == "__thread_vars")
			vars = &section;
		else if (section.section_name == "__thread_ptrs")
			ptrs = &section;
	}
	if (vars == nullptr || ptrs == nullptr)
		return registrations;
	if (vars->size % sizeof(DarwinTLVDescriptor) != 0 ||
		ptrs->size % sizeof(std::uintptr_t) != 0)
		throw std::runtime_error("Mach-O dynamic TLV sections are misaligned");
	const auto descriptor_address = image.SectionAddress(mapping,
		vars->segment_name, vars->section_name);
	const auto pointer_address = image.SectionAddress(mapping,
		ptrs->segment_name, ptrs->section_name);
	const auto count = (std::min)(
		static_cast<std::size_t>(vars->size / sizeof(DarwinTLVDescriptor)),
		static_cast<std::size_t>(ptrs->size / sizeof(std::uintptr_t)));
	for (std::size_t index = 0; index < count; ++index) {
		const auto descriptor = descriptor_address + index * sizeof(DarwinTLVDescriptor);
		const auto slot = pointer_address + index * sizeof(std::uintptr_t);
		std::memcpy(reinterpret_cast<void*>(slot), &descriptor, sizeof(descriptor));
	}
	return registrations;
}

} // namespace

std::filesystem::path DylibResolver::Resolve(
	const std::filesystem::path& image_path,
	const std::string& dependency,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix,
	const std::filesystem::path& executable_path)
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
		const auto image = MachOImage::Open(image_path.wstring());
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
			AddFrameworkCandidates(candidates, prefix / rooted_path.parent_path(),
				rooted_path.filename().string());
		else
			AddCandidate(candidates, prefix / rooted);
	} else {
		AddCandidate(candidates, image_directory / dependency);
		AddCandidate(candidates, prefix / "usr" / "lib" / dependency);
		AddFrameworkCandidates(candidates,
			prefix / "System" / "Library" / "Frameworks", dependency);
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
	const std::filesystem::path& prefix)
{
	std::vector<DylibGraphNode> graph;
	std::vector<std::filesystem::path> pending{image_path.lexically_normal()};
	std::set<std::filesystem::path> visited;

	while (!pending.empty()) {
		const auto current = pending.back();
		pending.pop_back();
		const auto normalized = current.lexically_normal();
		if (!visited.insert(normalized).second) {
			continue;
		}

		const auto image = MachOImage::Open(normalized.wstring());
		std::vector<std::filesystem::path> image_rpaths = rpaths;
		for (const auto& rpath : image.RPaths()) {
			image_rpaths.emplace_back(rpath);
		}
		DylibGraphNode node;
		node.path = normalized;
		for (const auto& dependency : image.Dependencies()) {
			try {
				const auto resolved = DylibResolver::Resolve(
					normalized, dependency, image_rpaths, prefix,
					image_path.lexically_normal());
				node.dependencies.push_back(resolved);
				if (visited.find(resolved.lexically_normal()) == visited.end()) {
					pending.push_back(resolved.lexically_normal());
				}
			} catch (const std::exception&) {
				if (!image.IsWeakDependency(dependency) && !IsBuiltinDarwinProvider(dependency)) {
					throw;
				}
			}
		}
		graph.push_back(std::move(node));
	}

	std::sort(graph.begin(), graph.end(), [](const auto& left, const auto& right) {
		return left.path.string() < right.path.string();
	});
	return graph;
}

std::vector<std::filesystem::path> DylibGraph::InitializationOrder(
	const std::filesystem::path& image_path,
	const std::vector<std::filesystem::path>& rpaths,
	const std::filesystem::path& prefix)
{
	const auto graph = Load(image_path, rpaths, prefix);
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
	const std::filesystem::path& prefix)
{
	const auto graph = Load(image_path, rpaths, prefix);
	struct Definition final {
		std::string name;
		std::filesystem::path provider;
		std::uint64_t value;
	};
	std::vector<Definition> definitions;
	for (const auto& node : graph) {
		const auto image = MachOImage::Open(node.path.wstring());
		for (const auto& symbol : image.Symbols()) {
			if (!symbol.name.empty() && (symbol.type & 0x0e) != 0) {
				definitions.push_back({symbol.name, node.path, symbol.value});
			}
		}
	}
	std::function<bool(const std::filesystem::path&, const std::string&,
		std::set<std::string>&, Definition&)> resolve_export =
		[&](const std::filesystem::path& owner, const std::string& name,
			std::set<std::string>& visiting, Definition& result) {
			const auto key = owner.lexically_normal().string() + "\n" + name;
			if (!visiting.insert(key).second)
				return false;
			const auto image = MachOImage::Open(owner.wstring());
			for (const auto& symbol : image.Symbols()) {
				if (!symbol.name.empty() && symbol.name == name &&
					(symbol.type & 0x0e) != 0) {
					result = {name, owner, symbol.value};
					visiting.erase(key);
					return true;
				}
			}
			for (const auto& reexport : image.Reexports()) {
				if (reexport.name != name || reexport.dependency_ordinal == 0 ||
					reexport.dependency_ordinal > image.Dependencies().size())
					continue;
				const auto dependency = image.Dependencies()[reexport.dependency_ordinal - 1];
				std::vector<std::filesystem::path> image_rpaths = rpaths;
				for (const auto& rpath : image.RPaths())
					image_rpaths.emplace_back(rpath);
				try {
					const auto resolved = DylibResolver::Resolve(owner, dependency,
						image_rpaths, prefix, image_path.lexically_normal());
					const auto target = reexport.target.empty() ? name : reexport.target;
					if (resolve_export(resolved, target, visiting, result)) {
						visiting.erase(key);
						return true;
					}
				} catch (const std::exception&) {
				}
			}
			visiting.erase(key);
			return false;
		};

	std::vector<DylibBinding> bindings;
	for (const auto& node : graph) {
		const auto image = MachOImage::Open(node.path.wstring());
		for (const auto& symbol : image.UndefinedSymbols()) {
			const auto match = std::find_if(definitions.begin(), definitions.end(),
				[&symbol](const auto& definition) {
					return definition.name == symbol.name;
				});
			if (match == definitions.end()) {
				Definition forwarded;
				std::set<std::string> visiting;
				if (resolve_export(node.path, symbol.name, visiting, forwarded)) {
					bindings.push_back({symbol.name, forwarded.provider,
						forwarded.value});
					continue;
				}
				const auto host_address = darling_windows_host_symbol(symbol.name.c_str());
				if (host_address != 0) {
					bindings.push_back({symbol.name, {}, host_address});
					continue;
				}
				if (MachOImage::IsWeakUndefined(symbol)) {
					// Darwin leaves an unresolved weak reference as NULL.
					bindings.push_back({symbol.name, {}, 0});
					continue;
				}
				throw std::runtime_error("unresolved Mach-O import: " + symbol.name);
			}
			bindings.push_back({symbol.name, match->provider, match->value});
		}
	}
	return bindings;
}

class DarwinDynamicImage final {
public:
	struct Provider final {
		std::filesystem::path path;
		MachOImage image;
		MachOImage::Mapping mapping;
		std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> tlv_registrations;
	};
	std::filesystem::path path;
	MachOImage image;
	MachOImage::Mapping mapping;
	std::vector<Provider> providers;
	std::vector<DyldResolvedBinding> bindings;
	std::vector<std::pair<DarwinTLVDescriptor*, std::size_t>> tlv_registrations;
	std::vector<darling_objc_image_token*> objc_tokens;

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

DarwinDynamicImage* OpenDynamicImage(const std::filesystem::path& path)
{
	auto* result = new DarwinDynamicImage{path, MachOImage::Open(path.wstring()), {}, {}, {}, {}};
	try {
		result->mapping = result->image.MapSegments();
		const auto graph_bindings = DylibGraph::BindImports(path, {}, {});
		const auto preferred_provider_address = result->mapping.Segments().empty() ? 0 :
			result->mapping.Segments().front().address;
		for (const auto& binding : graph_bindings) {
			if (binding.provider.empty()) {
				result->bindings.push_back({binding.name,
					static_cast<std::uintptr_t>(binding.provider_value)});
				continue;
			}
			const auto normalized = binding.provider.lexically_normal();
			auto provider = std::find_if(result->providers.begin(), result->providers.end(),
				[&normalized](const DarwinDynamicImage::Provider& candidate) {
					return candidate.path == normalized;
				});
			if (provider == result->providers.end()) {
				result->providers.push_back({normalized,
					MachOImage::Open(normalized.wstring()), {}, {}});
				result->providers.back().mapping = result->providers.back().image.MapSegments(
					preferred_provider_address);
				provider = std::prev(result->providers.end());
			}
			const auto descriptor_address = provider->image.SymbolAddress(
				provider->mapping, binding.name);
			const auto pointer_address = provider->image.TLVPointerAddress(
				provider->mapping, descriptor_address);
			// A cross-image TLV import addresses the provider's per-thread
			// pointer slot, not the descriptor object itself.
			result->bindings.push_back({binding.name,
				pointer_address == 0 ? descriptor_address : pointer_address});
		}
		const auto initialization_order = DylibGraph::InitializationOrder(path, {}, {});
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
					const auto found = std::find_if(result->bindings.begin(), result->bindings.end(),
						[&symbol](const DyldResolvedBinding& candidate) {
							return candidate.symbol == symbol.name;
						});
					if (found != result->bindings.end())
						provider_symbols[index] = found->address;
				}
			}
			if (!provider.image.Relocations().empty())
				provider.image.ApplyRelocations(provider.mapping, provider_symbols, true);
			if (!result->bindings.empty()) {
				provider.image.ApplyBindActions(provider.mapping, result->bindings);
				provider.image.ApplyLazyBindActions(provider.mapping, result->bindings);
				provider.image.ApplyChainedFixups(provider.mapping, result->bindings);
			}
			provider.image.ApplyIndirectImportBindings(provider.mapping, result->bindings);
			provider.image.ExecuteInitializers(provider.mapping);
			if (auto* token = RegisterDynamicObjectiveCSections(provider.image,
				provider.mapping))
				result->objc_tokens.push_back(token);
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
			const auto found = std::find_if(result->bindings.begin(), result->bindings.end(),
				[&symbol](const DyldResolvedBinding& candidate) {
					return candidate.symbol == symbol.name;
				});
			if (found != result->bindings.end())
				symbol_addresses[index] = found->address;
		}
		if (!result->image.Relocations().empty())
			result->image.ApplyRelocations(result->mapping, symbol_addresses, false);
		if (!result->bindings.empty()) {
			result->image.ApplyBindActions(result->mapping, result->bindings);
			result->image.ApplyLazyBindActions(result->mapping, result->bindings);
			result->image.ApplyChainedFixups(result->mapping, result->bindings);
		}
		result->image.ApplyIndirectImportBindings(result->mapping, result->bindings);
		result->image.ExecuteInitializers(result->mapping);
		if (auto* token = RegisterDynamicObjectiveCSections(result->image, result->mapping))
			result->objc_tokens.push_back(token);
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

int DynamicImageExecuteEntry(const DarwinDynamicImage& image,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment)
{
	return image.image.ExecuteEntry(image.mapping, arguments, environment);
}

const std::filesystem::path& DynamicImagePath(const DarwinDynamicImage& image) noexcept
{
	return image.path;
}

const void* DynamicImageHeader(const DarwinDynamicImage& image) noexcept
{
	if (image.mapping.Segments().empty()) return nullptr;
	return reinterpret_cast<const void*>(image.mapping.Segments().front().address);
}

std::intptr_t DynamicImageSlide(const DarwinDynamicImage& image) noexcept
{
	return image.mapping.Slide();
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

void* DynamicImageBase(const DarwinDynamicImage& image) noexcept
{
	return image.mapping.Segments().empty() ? nullptr :
		reinterpret_cast<void*>(image.mapping.Segments().front().address);
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
