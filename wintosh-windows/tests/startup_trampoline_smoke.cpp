/*
 * Isolated Darwin initial-stack trampoline proof.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 * Licensed under the GNU General Public License, version 3.
 */

#include <windows.h>

#include <cstdint>

extern "C" void darling_windows_enter_darwin_start(
	void* entry, void* darwin_stack);
extern "C" void darling_windows_startup_probe();

int main()
{
#if defined(_M_X64)
	// Layout: argc, argv[0], argv[1], argv terminator, envp[0].
	char argv0[] = "fixture-argv";
	char argv1[] = "argument";
	char env0[] = "fixture-env";
	std::uintptr_t stack[8]{};
	stack[0] = 2;
	stack[1] = reinterpret_cast<std::uintptr_t>(argv0);
	stack[2] = reinterpret_cast<std::uintptr_t>(argv1);
	stack[3] = 0;
	stack[4] = reinterpret_cast<std::uintptr_t>(env0);
	stack[5] = 0;
	// The probe is intentionally minimal; the trampoline proof is that the
	// entry receives the supplied stack rather than the Windows call frame.
	darling_windows_enter_darwin_start(
		reinterpret_cast<void*>(&darling_windows_startup_probe), stack);
	return 92;
#else
	return 0;
#endif
}
