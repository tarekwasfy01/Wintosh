/* Minimal Win32-backed AppKit bootstrap; GPL-3.0-only. */
#pragma once

#include <cstdint>

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_APPKIT_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_APPKIT_GUEST_ABI
#endif

extern "C" void darling_windows_appkit_initialize();
extern "C" void darling_windows_appkit_wake_main_run_loop();
// Wait for and dispatch one message-queue event on the AppKit main thread.
// Returns 1 for dispatched application input, 0 for timeout/internal wake, -1 on failure.
extern "C" std::int32_t darling_windows_appkit_wait_run_loop_input(
	void* loop, const char* mode, std::uint32_t timeout_milliseconds);
extern "C" std::uintptr_t darling_windows_appkit_symbol(const char* name);
extern "C" DARLING_APPKIT_GUEST_ABI int NSApplicationMain(int argc, char** argv);
