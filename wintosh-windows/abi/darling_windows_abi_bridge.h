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

extern "C" DARLING_SYSV int darling_windows_abi_puts(const char* value);
extern "C" DARLING_SYSV [[noreturn]] void darling_windows_abi_exit(int status);

} // namespace darling::windows_host::abi

#undef DARLING_SYSV
