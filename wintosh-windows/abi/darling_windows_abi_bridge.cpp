/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_abi_bridge.h"
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

#if defined(__clang__) && defined(_WIN64)
namespace {
struct SysVVaList final { std::uint32_t gp_offset, fp_offset; const std::uint8_t* overflow; const std::uint8_t* save; };
template <typename T> bool ReadGp(SysVVaList& state, T& value) {
	if (state.gp_offset > 40) return false;
	std::memcpy(&value, state.save + state.gp_offset, sizeof(T)); state.gp_offset += 8; return true;
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
		if (*p == 'd' || *p == 'i') { std::int32_t v{}; if (!ReadGp(state, v)) return -1; written += std::fprintf(stdout, "%" PRId32, v); continue; }
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

extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_dyld_stub_binder()
{
	std::fputs("DARLING_RUNNER_ERROR=dyld_stub_binder was invoked; lazy binding is not supported\n",
		stderr);
	std::fflush(stderr);
	std::abort();
}

#undef DARLING_SYSV
