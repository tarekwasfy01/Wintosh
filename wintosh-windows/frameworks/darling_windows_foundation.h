/* Minimal Foundation ABI primitives; GPL-3.0-only, see source manifests. */
#pragma once

#include <cstddef>

struct darling_windows_NSRange {
	std::size_t location;
	std::size_t length;
};

extern "C" darling_windows_NSRange darling_windows_NSMakeRange(
	std::size_t location, std::size_t length);
extern "C" std::size_t darling_windows_NSMaxRange(darling_windows_NSRange range);
extern "C" bool darling_windows_NSLocationInRange(
	std::size_t location, darling_windows_NSRange range);
extern "C" bool darling_windows_NSEqualRanges(
	darling_windows_NSRange left, darling_windows_NSRange right);
extern "C" darling_windows_NSRange darling_windows_NSIntersectionRange(
	darling_windows_NSRange left, darling_windows_NSRange right);
extern "C" darling_windows_NSRange darling_windows_NSUnionRange(
	darling_windows_NSRange left, darling_windows_NSRange right);

extern "C" const char* darling_windows_NSGetSizeAndAlignment(
	const char* type, std::size_t* size, std::size_t* alignment);
