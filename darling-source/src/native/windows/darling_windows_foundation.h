/* Minimal Foundation ABI primitives; GPL-3.0-only, see source manifests. */
#pragma once

#include <cstddef>

extern "C" const char* darling_windows_NSGetSizeAndAlignment(
	const char* type, std::size_t* size, std::size_t* alignment);
