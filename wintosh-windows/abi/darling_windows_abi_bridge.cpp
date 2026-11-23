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
#include "darling_windows_errno.h"
#include "darling_windows_tlv.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cinttypes>
#include <cstdlib>
#include <stdexcept>


#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_SYSV __attribute__((sysv_abi))
#else
#define DARLING_SYSV
#endif

extern "C" DARLING_SYSV int darling_windows_abi_puts(const char* value)
{
	return value == nullptr ? EOF : std::puts(value);
}

extern "C" DARLING_SYSV const char* darling_windows_abi_getenv(const char* name)
{
	return darling_windows_getenv(name);
}

extern "C" DARLING_SYSV int* darling_windows_abi_error()
{
	return darling::windows_host::DarwinErrno::Address();
}

extern "C" DARLING_SYSV std::uintptr_t darling_windows_abi_resolve_lazy_binding(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info) noexcept
{
	const bool trace = std::getenv("WINTOSH_TRACE_LAZY_BIND") != nullptr;
	if (trace) {
		std::fprintf(stderr,
			"WINTOSH_LAZY_RESOLVE_ENTER cache=0x%llx info=0x%llx\n",
			static_cast<unsigned long long>(image_cache),
			static_cast<unsigned long long>(lazy_binding_info));
	}
	try {
		const auto target = darling::windows_host::MachOLazyBindingSession::ResolveActiveForImage(
			image_cache, lazy_binding_info);
		if (trace) {
			std::fprintf(stderr,
				"WINTOSH_LAZY_BIND cache=0x%llx info=0x%llx target=0x%llx\n",
				static_cast<unsigned long long>(image_cache),
				static_cast<unsigned long long>(lazy_binding_info),
				static_cast<unsigned long long>(target.value_or(0)));
		}
		return target.value_or(0);
	} catch (...) {
		if (trace)
			std::fputs("WINTOSH_LAZY_RESOLVE_ERROR\n", stderr);
		// Exceptions cannot unwind safely through the hand-written binder frame.
		return 0;
	}
}

extern "C" DARLING_SYSV int darling_windows_abi_lazy_binding_session_active()
{
	return darling::windows_host::MachOLazyBindingSession::HasActiveSessions() ? 1 : 0;
}

extern "C" DARLING_SYSV void* darling_windows_abi_tlv_get_addr(
	darling::windows_host::DarwinTLVDescriptor* descriptor)
{
	const auto result = darling::windows_host::ResolveDarwinTLV(descriptor);
	if (std::getenv("WINTOSH_TRACE_TLV") != nullptr && descriptor != nullptr) {
		std::fprintf(stderr,
			"WINTOSH_TLV_CALL descriptor=0x%llx key=0x%llx offset=0x%llx result=0x%llx\n",
			static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(descriptor)),
			static_cast<unsigned long long>(descriptor->key),
			static_cast<unsigned long long>(descriptor->offset),
			static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(result)));
	}
	return result;
}

extern "C" DARLING_SYSV void darling_windows_abi_tlv_atexit(
	void (*function)(void*), void* argument) noexcept
{
	if (darling::windows_host::darling_windows_tlv_atexit_guest(function, argument) == 0)
		darling_windows_pin_dynamic_image_for_guest_callback(
			reinterpret_cast<const void*>(function));
}

extern "C" DARLING_SYSV int darling_windows_abi_cxa_thread_atexit(
	void (*function)(void*), void* argument, void* dso_handle) noexcept
{
	const auto result = darling::windows_host::darling_windows_cxa_thread_atexit_impl(
		function, argument, dso_handle);
	if (result == 0) {
		darling_windows_pin_dynamic_image_for_guest_callback(dso_handle);
		darling_windows_pin_dynamic_image_for_guest_callback(
			reinterpret_cast<const void*>(function));
	}
	return result;
}

extern "C" std::uintptr_t darling_windows_abi_call_export_resolver(
	std::uintptr_t resolver_address) noexcept
{
	if (resolver_address == 0) return 0;
	try {
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
		using Resolver = darling::windows_host::abi::GuestFn<std::uintptr_t>;
		return reinterpret_cast<Resolver>(resolver_address)();
#elif defined(_M_ARM64) || defined(__aarch64__)
		using Resolver = std::uintptr_t (*)();
		return reinterpret_cast<Resolver>(resolver_address)();
#else
		return 0;
#endif
	} catch (...) {
		return 0;
	}
}

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
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
#elif defined(_M_IX86) || defined(__i386__)
extern "C" int darling_windows_abi_printf(const char* format, ...)
{
	if (format == nullptr)
		return -1;
	std::va_list arguments;
	va_start(arguments, format);
	const int result = std::vfprintf(stdout, format, arguments);
	va_end(arguments);
	std::fflush(stdout);
	return result;
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
	// Darwin _exit is the non-unwinding termination primitive.  It must not
	// run atexit handlers or attempt to unwind through a guest stack.
	darling_windows__exit(status);
}

extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_normal_exit(int status)
{
	darling_windows_exit(status);
}

extern "C" DARLING_SYSV int darling_windows_abi_atexit(void (*function)())
{
	// The guest supplies the callback in the SysV first argument register;
	// this wrapper performs the ABI transition before entering the host
	// atexit registry. The callback remains guest-owned and is invoked only
	// after a normal guest return through the existing finalizer path.
	return darling_windows_atexit_guest(function);
}

extern "C" DARLING_SYSV int darling_windows_abi_cxa_atexit(
	void (*function)(void*), void* argument, void* dso_handle)
{
	return darling_windows_cxa_atexit_guest(function, argument, dso_handle);
}

extern "C" [[noreturn]] void darling_windows_abi_dyld_stub_binder_fail_closed()
{
	std::fputs("DARLING_RUNNER_ERROR=dyld_stub_binder could not resolve the lazy import\n",
		stderr);
	std::fflush(stderr);
	std::abort();
}

#if !(defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__))
extern "C" DARLING_SYSV void darling_windows_abi_dyld_stub_binder()
{
	darling_windows_abi_dyld_stub_binder_fail_closed();
}
#endif

extern "C" DARLING_SYSV int darling_windows_darwin_stub_probe_target(
	std::uint64_t first, std::uint64_t second)
{
	return (first == 0x1122334455667788ULL && second == 0x8877665544332211ULL) ? 73 : 74;
}

#if defined(_WIN64) && (!defined(__clang__) || defined(_M_ARM64) || defined(__aarch64__))
// MSVC cannot assemble the SysV trampoline. Keep the synthetic fixture
// executable and explicit; real foreign-ABI entry transfer remains Clang-only.
extern "C" int darling_windows_darwin_stub_trampoline_fixture(
	std::uint64_t first, std::uint64_t second, std::uintptr_t)
{
	return darling_windows_darwin_stub_probe_target(first, second);
}
#endif

extern "C" DARLING_SYSV darling::windows_host::abi::DarwinAggregatePair
darling_windows_abi_aggregate_pair(darling::windows_host::abi::DarwinAggregatePair value)
{
	return {value.integer ^ 0xfeedfacecafebeefULL, value.real + 0.5};
}

extern "C" DARLING_SYSV darling::windows_host::abi::DarwinAggregateTriple
darling_windows_abi_aggregate_triple(darling::windows_host::abi::DarwinAggregateTriple value)
{
	return {value.first + 1, value.second + 2, value.third + 3};
}

extern "C" DARLING_SYSV std::uintptr_t darling_windows_darwin_stub_probe_resolver(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info)
{
	if (std::getenv("WINTOSH_TRACE_LAZY_BIND") != nullptr) {
		std::fprintf(stderr,
			"WINTOSH_STUB_PROBE cache=0x%llx info=0x%llx\n",
			static_cast<unsigned long long>(image_cache),
			static_cast<unsigned long long>(lazy_binding_info));
	}
	return darling_windows_abi_resolve_lazy_binding(image_cache, lazy_binding_info);
}

#undef DARLING_SYSV
