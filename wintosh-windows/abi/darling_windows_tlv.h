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

struct DarwinTLVDescriptor final {
	void* (*thunk)(DarwinTLVDescriptor*);
	std::uintptr_t key;
	std::uintptr_t offset;
};
struct DarwinTLVDescriptorRange final { DarwinTLVDescriptor* descriptors; std::size_t count; };

// Descriptors in one registration share per-thread storage. Supply the full
// template extent (including zero-fill) to preserve space for each variable.
void InitializeDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count);
void InitializeDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count,
	const void* initial_data, std::size_t initial_bytes);
void InitializeDarwinTLV(const DarwinTLVDescriptorRange* ranges, std::size_t range_count,
	const void* initial_data, std::size_t initial_bytes);
void DestroyDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count) noexcept;
void* ResolveDarwinTLV(DarwinTLVDescriptor* descriptor) noexcept;

extern "C" void darling_windows_tlv_atexit(void (*function)(void*),
	void* argument) noexcept;
extern "C" void darling_windows_cxa_thread_atexit(void (*function)(void*),
	void* argument) noexcept;
extern "C" int darling_windows_cxa_thread_atexit_impl(
	void (*function)(void*), void* argument, void* dso_handle) noexcept;
extern "C" void darling_windows_tlv_atexit_guest(void (*function)(void*),
	void* argument) noexcept;

} // namespace darling::windows_host

