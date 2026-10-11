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
	return 0;
}
