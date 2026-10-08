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

class DarwinClock final {
public:
	[[nodiscard]] static std::uint64_t MonotonicNanoseconds() noexcept;
	[[nodiscard]] static std::uint64_t WallClockNanoseconds() noexcept;
};

} // namespace darling::windows_host
