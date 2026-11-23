/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "darling_windows_macho.h"

namespace darling::windows_host {

class DylibResolver final {
public:
	static std::filesystem::path Resolve(
		const std::filesystem::path& image_path,
		const std::string& dependency,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix,
		const std::filesystem::path& executable_path = {},
		std::int32_t preferred_cpu_type = 0);
};

struct DylibGraphNode final {
	std::filesystem::path path;
	std::vector<std::filesystem::path> dependencies;
	// Keeps one slot per LC_LOAD_*_DYLIB ordinal, including entries omitted
	// from the resolved graph because they are weak or provided by Wintosh.
	std::vector<std::filesystem::path> ordinal_dependencies;
};

struct DylibBinding final {
	std::string name;
	std::filesystem::path provider;
	std::string provider_symbol;
	std::uint64_t provider_value = 0;
	std::filesystem::path client_image;
	std::int32_t dylib_ordinal = 0;
	bool has_dylib_ordinal = false;
};

struct DylibExportResolver final {
	std::filesystem::path provider;
	std::string symbol;
};

class DylibGraph final {
public:
	static std::vector<DylibGraphNode> Load(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix,
		std::int32_t preferred_cpu_type = 0);

	static std::vector<std::filesystem::path> InitializationOrder(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix,
		std::int32_t preferred_cpu_type = 0);

	static std::vector<DylibBinding> BindImports(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix,
		std::int32_t preferred_cpu_type = 0);

	static std::vector<DylibExportResolver> ExportResolverOrder(
		const std::vector<DylibBinding>& bindings,
		std::int32_t preferred_cpu_type = 0);
};

class DarwinDynamicImage;

void SetDynamicImagePreferredCpuType(std::int32_t cpu_type) noexcept;
DarwinDynamicImage* OpenDynamicImage(const std::filesystem::path& path,
	std::int32_t preferred_cpu_type = 0);
std::uintptr_t DynamicImageSymbol(const DarwinDynamicImage& image,
	const char* name);
std::uint32_t DynamicImageDefinedSymbolCount(const DarwinDynamicImage& image) noexcept;
const char* DynamicImageDefinedSymbolName(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept;
bool DynamicImageHasSymbol(const DarwinDynamicImage& image, const char* name) noexcept;
void* DynamicImageSectionData(const DarwinDynamicImage& image,
	const char* segment_name, const char* section_name, std::size_t& size) noexcept;
bool DynamicImageHasModuleInitializers(const DarwinDynamicImage& image) noexcept;
std::uint32_t DynamicImageReferenceCount(const DarwinDynamicImage& image) noexcept;
const char* DynamicImageReferenceName(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept;
bool DynamicImageReferenceIsTentative(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept;
int DynamicImageExecuteEntry(const DarwinDynamicImage& image,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment = {},
	DarwinEntryAbi entry_abi = DarwinEntryAbi::CompatibleMain);
const std::filesystem::path& DynamicImagePath(const DarwinDynamicImage& image) noexcept;
const void* DynamicImageHeader(const DarwinDynamicImage& image) noexcept;
std::intptr_t DynamicImageSlide(const DarwinDynamicImage& image) noexcept;
std::uint32_t DynamicImageProviderCount(const DarwinDynamicImage& image) noexcept;
const std::filesystem::path& DynamicImageProviderPath(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept;
const void* DynamicImageProviderHeader(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept;
std::intptr_t DynamicImageProviderSlide(
	const DarwinDynamicImage& image, std::uint32_t index) noexcept;
bool DynamicImageContains(const DarwinDynamicImage& image, const void* address) noexcept;
bool DynamicImageContainsMappedAddress(const DarwinDynamicImage& image,
	const void* address) noexcept;
bool DynamicImageProviderContains(const DarwinDynamicImage& image,
	std::uint32_t index, const void* address) noexcept;
void* DynamicImageBase(const DarwinDynamicImage& image) noexcept;
bool DynamicImageNearestSymbol(const DarwinDynamicImage& image, const void* address,
	std::string& name, std::uintptr_t& symbol_address) noexcept;
bool DynamicImageSymbolName(const DarwinDynamicImage& image, const void* address,
	std::string& name) noexcept;
void CloseDynamicImage(DarwinDynamicImage* image) noexcept;

} // namespace darling::windows_host
