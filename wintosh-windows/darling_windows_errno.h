/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <cstdint>

namespace darling::windows_host {

class DarwinErrno final {
public:
	[[nodiscard]] static int Get() noexcept;
	[[nodiscard]] static int* Address() noexcept;
	static void Clear() noexcept;
	static void Set(int value) noexcept;
	static void SetFromWin32(std::uint32_t value) noexcept;
	static void SetFromWinsock(std::uint32_t value) noexcept;
};

} // namespace darling::windows_host
