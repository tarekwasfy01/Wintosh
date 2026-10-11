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

#if defined(__clang__) && defined(_WIN64)
#define DARLING_SYSV __attribute__((sysv_abi))
#else
#define DARLING_SYSV
#endif

extern "C" DARLING_SYSV int darling_windows_abi_puts(const char* value)
{
	return value == nullptr ? EOF : std::puts(value);
}

extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_exit(int status)
{
	darling_windows_exit(status);
}

#undef DARLING_SYSV
