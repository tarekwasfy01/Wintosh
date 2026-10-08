/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_time.h"

#include <windows.h>

#include <limits>

namespace darling::windows_host {

std::uint64_t DarwinClock::MonotonicNanoseconds() noexcept
{
	LARGE_INTEGER counter{};
	LARGE_INTEGER frequency{};
	if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) ||
		frequency.QuadPart <= 0 || counter.QuadPart < 0) {
		return 0;
	}
	const auto ticks = static_cast<std::uint64_t>(counter.QuadPart);
	const auto hz = static_cast<std::uint64_t>(frequency.QuadPart);
	const auto seconds = ticks / hz;
	const auto remainder = ticks % hz;
	if (seconds > std::numeric_limits<std::uint64_t>::max() / 1'000'000'000ull) {
		return std::numeric_limits<std::uint64_t>::max();
	}
	const auto whole = seconds * 1'000'000'000ull;
	const auto fraction = (remainder * 1'000'000'000ull) / hz;
	return whole > std::numeric_limits<std::uint64_t>::max() - fraction ?
		std::numeric_limits<std::uint64_t>::max() : whole + fraction;
}

std::uint64_t DarwinClock::WallClockNanoseconds() noexcept
{
	FILETIME file_time{};
	GetSystemTimePreciseAsFileTime(&file_time);
	ULARGE_INTEGER value{};
	value.LowPart = file_time.dwLowDateTime;
	value.HighPart = file_time.dwHighDateTime;
	// FILETIME is 100 ns since 1601-01-01; return Unix epoch nanoseconds.
	constexpr std::uint64_t epoch_offset = 11644473600ull * 10'000'000ull;
	if (value.QuadPart < epoch_offset) {
		return 0;
	}
	return (value.QuadPart - epoch_offset) * 100ull;
}

} // namespace darling::windows_host
