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

int main()
{
	const auto direct = darling::windows_host::abi::darling_windows_abi_puts(
		"ABI_BRIDGE_FIXED_EXPORT=PASS");
	const auto resolved = darling_windows_host_symbol("_puts");
	const auto expected = reinterpret_cast<std::uintptr_t>(
		&darling::windows_host::abi::darling_windows_abi_puts);
	if (direct < 0 || resolved != expected) {
		std::fprintf(stderr, "ABI_BRIDGE_FIXED_EXPORT=FAIL\n");
		return 1;
	}
	std::puts("ABI_BRIDGE_SYMBOL_LOOKUP=PASS");
	const auto printf_address = darling_windows_host_symbol("_printf");
	const auto printf_expected = reinterpret_cast<std::uintptr_t>(
		&darling::windows_host::abi::darling_windows_abi_printf);
	if (printf_address != printf_expected) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_LOOKUP=FAIL\n");
		return 2;
	}
	std::puts("ABI_BRIDGE_VARIADIC_LOOKUP=PASS");
#if defined(__clang__) && defined(_WIN64)
	if (darling::windows_host::abi::darling_windows_abi_printf("v=%d\n", 7) != 4) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_EXECUTION=FAIL\n");
		return 3;
	}
	if (darling::windows_host::abi::darling_windows_abi_printf("s=%s\n", "ok") != 5) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_STRING=FAIL\n");
		return 4;
	}
	if (darling::windows_host::abi::darling_windows_abi_printf("u=%u x=%x c=%c %%\n", 9u, 0x2au, 'Q') != 15) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_INTEGER=FAIL\n");
		return 5;
	}
	if (darling::windows_host::abi::darling_windows_abi_printf("f=%f g=%g\n", 1.5, 2.5) != 17) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_FLOAT=FAIL\n");
		return 6;
	}
	if (darling::windows_host::abi::darling_windows_abi_printf("ld=%ld ll=%lld lf=%lf\n", 1234567890L, 1234567890123LL, 3.25) != 43) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_LENGTH=FAIL\n");
		return 7;
	}
	std::puts("ABI_BRIDGE_VARIADIC_EXECUTION=PASS");
#else
	if (darling::windows_host::abi::darling_windows_abi_printf("unused %d", 7) != -1) {
		std::fprintf(stderr, "ABI_BRIDGE_VARIADIC_FAIL_CLOSED=FAIL\n");
		return 3;
	}
	std::puts("ABI_BRIDGE_VARIADIC_FAIL_CLOSED=PASS");
#endif
	if (darling::windows_host::abi::darling_windows_abi_lazy_binding_session_active() != 0 ||
		darling_windows_host_symbol("_wintosh_lazy_binding_session_active") == 0) {
		std::fprintf(stderr, "ABI_BRIDGE_LAZY_SESSION_PRIMITIVE=FAIL\n");
		return 8;
	}
	std::puts("ABI_BRIDGE_LAZY_SESSION_PRIMITIVE=PASS");
	if (darling_windows_host_symbol("_dyld_stub_binder") != reinterpret_cast<std::uintptr_t>(
		&darling::windows_host::abi::darling_windows_abi_dyld_stub_binder)) {
		std::fprintf(stderr, "ABI_BRIDGE_DYLD_BINDER_EXPORT=FAIL\n");
		return 9;
	}
	std::puts("ABI_BRIDGE_DYLD_BINDER_EXPORT=PASS");
	return 0;
}
