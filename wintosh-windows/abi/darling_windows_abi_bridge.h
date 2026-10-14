/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */
#pragma once

#include <cstdint>

// Clang can express the System-V x86_64 ABI on a Windows target.  MSVC is
// deliberately kept source-compatible for the default build; its plain
// pointer fallback preserves the existing compatible-main behavior until the
// bridge library is selected for a Clang build.
#if defined(__clang__) && defined(_WIN64)
#define DARLING_SYSV __attribute__((sysv_abi))
#else
#define DARLING_SYSV
#endif

namespace darling::windows_host::abi {

static_assert(sizeof(void*) == 8, "Darling ABI bridge is x86_64-only");

template <typename R, typename... A>
using GuestFn = DARLING_SYSV R (*)(A...);

using GuestVoidFn = GuestFn<void>;

struct DarwinAggregatePair final { std::uint64_t integer; double real; };
struct DarwinAggregateTriple final { std::uint64_t first; std::uint64_t second; std::uint64_t third; };

extern "C" DARLING_SYSV int darling_windows_abi_puts(const char* value);
extern "C" DARLING_SYSV const char* darling_windows_abi_getenv(const char* name);
extern "C" DARLING_SYSV int* darling_windows_abi_error();
extern "C" DARLING_SYSV std::uintptr_t darling_windows_abi_resolve_lazy_binding(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info);
extern "C" DARLING_SYSV int darling_windows_abi_lazy_binding_session_active();
#if defined(__clang__) && defined(_WIN64)
extern "C" int darling_windows_abi_printf_windows_helper(const char*, const void*);
#endif
extern "C" DARLING_SYSV int darling_windows_abi_printf(const char* format, ...);
extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_exit(int status);
extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_normal_exit(int status);
extern "C" DARLING_SYSV int darling_windows_abi_atexit(void (*function)());
extern "C" DARLING_SYSV int darling_windows_abi_cxa_atexit(void (*function)(void*),
	void* argument, void* dso_handle);
// The native trampoline tail-jumps to the resolved guest target; that target
// returns through the original stub, so the binder itself is not noreturn.
extern "C" DARLING_SYSV void darling_windows_abi_dyld_stub_binder();
extern "C" [[noreturn]] void darling_windows_abi_dyld_stub_binder_fail_closed();
extern "C" DARLING_SYSV int darling_windows_darwin_stub_trampoline_probe(
	std::uint64_t first, std::uint64_t second);
extern "C" DARLING_SYSV int darling_windows_darwin_stub_trampoline_fixture(
	std::uint64_t first, std::uint64_t second, std::uintptr_t lazy_binding_info);
extern "C" DARLING_SYSV int darling_windows_darwin_stub_probe_target(
	std::uint64_t first, std::uint64_t second);
extern "C" DARLING_SYSV DarwinAggregatePair darling_windows_abi_aggregate_pair(
	DarwinAggregatePair value);
extern "C" DARLING_SYSV DarwinAggregateTriple darling_windows_abi_aggregate_triple(
	DarwinAggregateTriple value);

} // namespace darling::windows_host::abi

#undef DARLING_SYSV
