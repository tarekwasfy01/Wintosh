/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace darling::windows_host {

enum class DarwinMemoryProtection : std::uint8_t { None = 0, Read = 1, Write = 2, Execute = 4 };

constexpr DarwinMemoryProtection operator|(DarwinMemoryProtection left,
	DarwinMemoryProtection right) noexcept
{
	return static_cast<DarwinMemoryProtection>(static_cast<std::uint8_t>(left) |
		static_cast<std::uint8_t>(right));
}

class DarwinMemory final {
public:
	[[nodiscard]] static void* Mmap(std::size_t bytes, DarwinMemoryProtection protection);
	static void Mprotect(void* address, std::size_t bytes,
		DarwinMemoryProtection protection);
	static void Munmap(void* address, std::size_t bytes);
};

} // namespace darling::windows_host

extern "C" void* darling_windows_mmap_anonymous(void* address, std::size_t length,
	int protection, int flags, int descriptor, std::int64_t offset);
extern "C" int darling_windows_mprotect(void* address, std::size_t length,
	int protection);
extern "C" int darling_windows_munmap_anonymous(void* address, std::size_t length);
