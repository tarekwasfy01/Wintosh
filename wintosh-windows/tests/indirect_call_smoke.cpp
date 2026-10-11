/*
 * This file is part of the Darling Windows port.
 * Licensed under the GNU General Public License, version 3.
 *
 * Isolates the x64 indirect-call ABI from Mach-O fixture layout.
 */
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
extern "C" std::int32_t Probe()
{
	return 37;
}
}

int main()
{
	// sub rsp,28h; mov rax,[rip+1]; call rax; add rsp,28h; ret
	// followed by the 64-bit slot containing Probe's address.
	constexpr std::uint8_t code[] = {
		0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x05, 0x07,
		0x00, 0x00, 0x00, 0xff, 0xd0, 0x48, 0x83, 0xc4,
		0x28, 0xc3
	};
	constexpr std::size_t slot_offset = sizeof(code);
	auto* executable = static_cast<std::uint8_t*>(VirtualAlloc(
		nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (executable == nullptr) return 2;
	std::memcpy(executable, code, sizeof(code));
	const auto target = reinterpret_cast<std::uintptr_t>(&Probe);
	std::memcpy(executable + slot_offset, &target, sizeof(target));
	DWORD old_protection = 0;
	const bool executable_protection = VirtualProtect(
		executable, 4096, PAGE_EXECUTE_READ, &old_protection) != FALSE;
	FlushInstructionCache(GetCurrentProcess(), executable, 4096);
	const auto result = executable_protection ?
		reinterpret_cast<std::int32_t (*)()>(executable)() : -1;
	VirtualFree(executable, 0, MEM_RELEASE);
	const bool ok = executable_protection && result == 37;
	std::cout << "INDIRECT_CALL_ABI=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
}
