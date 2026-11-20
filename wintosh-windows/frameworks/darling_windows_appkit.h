/* Minimal Win32-backed AppKit bootstrap; GPL-3.0-only. */
#pragma once

#include <cstdint>

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_APPKIT_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_APPKIT_GUEST_ABI
#endif

extern "C" void darling_windows_appkit_initialize();
extern "C" std::uintptr_t darling_windows_appkit_symbol(const char* name);
extern "C" DARLING_APPKIT_GUEST_ABI int NSApplicationMain(int argc, char** argv);
