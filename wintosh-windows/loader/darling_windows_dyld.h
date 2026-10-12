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
		const std::filesystem::path& executable_path = {});
};

struct DylibGraphNode final {
	std::filesystem::path path;
	std::vector<std::filesystem::path> dependencies;
};

struct DylibBinding final {
	std::string name;
	std::filesystem::path provider;
	std::uint64_t provider_value = 0;
};

class DylibGraph final {
public:
	static std::vector<DylibGraphNode> Load(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix);

	static std::vector<std::filesystem::path> InitializationOrder(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix);

	static std::vector<DylibBinding> BindImports(
		const std::filesystem::path& image_path,
		const std::vector<std::filesystem::path>& rpaths,
		const std::filesystem::path& prefix);
};

class DarwinDynamicImage;

DarwinDynamicImage* OpenDynamicImage(const std::filesystem::path& path);
std::uintptr_t DynamicImageSymbol(const DarwinDynamicImage& image,
	const char* name);
std::uint32_t DynamicImageDefinedSymbolCount(const DarwinDynamicImage& image) noexcept;
const char* DynamicImageDefinedSymbolName(const DarwinDynamicImage& image,
	std::uint32_t ordinal) noexcept;
bool DynamicImageHasSymbol(const DarwinDynamicImage& image, const char* name) noexcept;
void* DynamicImageSectionData(const DarwinDynamicImage& image,
	const char* segment_name, const char* section_name, std::size_t& size) noexcept;
int DynamicImageExecuteEntry(const DarwinDynamicImage& image,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment = {},
	DarwinEntryAbi entry_abi = DarwinEntryAbi::CompatibleMain);
const std::filesystem::path& DynamicImagePath(const DarwinDynamicImage& image) noexcept;
const void* DynamicImageHeader(const DarwinDynamicImage& image) noexcept;
std::intptr_t DynamicImageSlide(const DarwinDynamicImage& image) noexcept;
bool DynamicImageContains(const DarwinDynamicImage& image, const void* address) noexcept;
void* DynamicImageBase(const DarwinDynamicImage& image) noexcept;
bool DynamicImageNearestSymbol(const DarwinDynamicImage& image, const void* address,
	std::string& name, std::uintptr_t& symbol_address) noexcept;
bool DynamicImageSymbolName(const DarwinDynamicImage& image, const void* address,
	std::string& name) noexcept;
void CloseDynamicImage(DarwinDynamicImage* image) noexcept;

} // namespace darling::windows_host
