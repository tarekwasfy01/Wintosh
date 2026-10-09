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
const std::filesystem::path& DynamicImagePath(const DarwinDynamicImage& image) noexcept;
const void* DynamicImageHeader(const DarwinDynamicImage& image) noexcept;
std::intptr_t DynamicImageSlide(const DarwinDynamicImage& image) noexcept;
void CloseDynamicImage(DarwinDynamicImage* image) noexcept;

} // namespace darling::windows_host
