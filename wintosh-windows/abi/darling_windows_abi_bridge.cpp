/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_abi_bridge.h"
#include "darling_windows_macho.h"
#include "darling_windows_stdio.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cinttypes>

#if defined(__clang__) && defined(_WIN64)
#define DARLING_SYSV __attribute__((sysv_abi))
#else
#define DARLING_SYSV
#endif

extern "C" DARLING_SYSV int darling_windows_abi_puts(const char* value)
{
	return value == nullptr ? EOF : std::puts(value);
}

extern "C" DARLING_SYSV std::uintptr_t darling_windows_abi_resolve_lazy_binding(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info)
{
	(void)image_cache;
	const auto* session = darling::windows_host::MachOLazyBindingSession::Current();
	if (session == nullptr)
		return 0;
	const auto target = session->ResolveForLazyInfo(lazy_binding_info);
	return target.value_or(0);
}

#if defined(__clang__) && defined(_WIN64)
namespace {
struct SysVVaList final { std::uint32_t gp_offset, fp_offset; const std::uint8_t* overflow; const std::uint8_t* save; };
template <typename T> bool ReadGp(SysVVaList& state, T& value) {
	if (state.gp_offset > 40) return false;
	std::memcpy(&value, state.save + state.gp_offset, sizeof(T)); state.gp_offset += 8; return true;
}
template <typename T> bool ReadFp(SysVVaList& state, T& value) {
	if (state.fp_offset > 160) return false;
	std::memcpy(&value, state.save + state.fp_offset, sizeof(T)); state.fp_offset += 16; return true;
}
}
extern "C" int darling_windows_abi_printf_windows_helper(const char* format, const void* raw)
{
	if (format == nullptr || raw == nullptr) return -1;
	auto state = *static_cast<const SysVVaList*>(raw); int written = 0;
	for (const char* p = format; *p; ++p) {
		if (*p != '%') { std::fputc(*p, stdout); ++written; continue; }
		++p;
		if (*p == '%') { std::fputc('%', stdout); ++written; continue; }
		bool long_value = false;
		if (*p == 'l') { long_value = true; ++p; if (*p == 'l') ++p; }
		if (*p == 'd' || *p == 'i') {
			if (long_value) { std::int64_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, "%" PRId64, v); }
			else { std::int32_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, "%" PRId32, v); }
			continue;
		}
		if (*p == 'u') {
			if (long_value) { std::uint64_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, "%" PRIu64, v); }
			else { std::uint32_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, "%" PRIu32, v); }
			continue;
		}
		if (*p == 'x' || *p == 'X') {
			if (long_value) { std::uint64_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, *p == 'x' ? "%" PRIx64 : "%" PRIX64, v); }
			else { std::uint32_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, *p == 'x' ? "%" PRIx32 : "%" PRIX32, v); }
			continue;
		}
		if (*p == 'c' && !long_value) { std::uint32_t v{}; if (!ReadGp(state, v)) return -1; std::fputc(static_cast<int>(v), stdout); ++written; continue; }
		if (*p == 'f' || *p == 'g') { double v{}; if (!ReadFp(state, v)) return -1; written += std::fprintf(stdout, *p == 'f' ? "%.6f" : "%.6g", v); continue; }
		if (*p == 's') { const char* v{}; if (!ReadGp(state, v)) return -1; const char* s = v ? v : "(null)"; if (std::fputs(s, stdout) < 0) return -1; written += static_cast<int>(std::strlen(s)); continue; }
		return -1;
	}
	std::fflush(stdout); return written;
}
#else
extern "C" int darling_windows_abi_printf(const char* format, ...)
{
	(void)format;
	std::fputs("DARLING_RUNNER_ERROR=SysV variadic printf bridge is not implemented\n",
		stderr);
	std::fflush(stderr);
	return -1;
}
#endif

extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_exit(int status)
{
	darling_windows_exit(status);
}

extern "C" [[noreturn]] void darling_windows_abi_dyld_stub_binder_fail_closed()
{
	std::fputs("DARLING_RUNNER_ERROR=dyld_stub_binder was invoked; lazy binding is not supported\n",
		stderr);
	std::fflush(stderr);
	std::abort();
}

#if !(defined(__clang__) && defined(_WIN64))
extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_dyld_stub_binder()
{
	darling_windows_abi_dyld_stub_binder_fail_closed();
}
#endif

extern "C" DARLING_SYSV int darling_windows_darwin_stub_probe_target(
	std::uint64_t first, std::uint64_t second)
{
	return (first == 0x1122334455667788ULL && second == 0x8877665544332211ULL) ? 73 : 74;
}

extern "C" DARLING_SYSV std::uintptr_t darling_windows_darwin_stub_probe_resolver(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info)
{
	return darling_windows_abi_resolve_lazy_binding(image_cache, lazy_binding_info);
}

#undef DARLING_SYSV
