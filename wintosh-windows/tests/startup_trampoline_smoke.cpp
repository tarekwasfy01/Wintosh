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
	// Layout: argc, argv, envp, apple parameters, followed by a null marker.
	std::uintptr_t stack[8]{};
	stack[0] = 2;
	stack[1] = 0x1111;
	stack[2] = 0x2222;
	stack[3] = 0x3333;
	stack[4] = 0;
	// The probe is intentionally minimal; the trampoline proof is that the
	// entry receives the supplied stack rather than the Windows call frame.
	darling_windows_enter_darwin_start(
		reinterpret_cast<void*>(&darling_windows_startup_probe), stack);
	return 92;
#else
	return 0;
#endif
}
