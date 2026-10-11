/*
 * Native MIG-style routine registration and bounds proof.
 * This file is part of the Darling Windows port.
 * Licensed under the GNU General Public License, version 3.
 */
#include "darling_windows_mig.h"
#include <iostream>
int main() {
	darling::windows_host::MigRoutineRegistry registry;
	const bool invalid_rejected = !registry.Register({0, 0, 1, 1, [](std::span<const std::uint8_t>) { return std::vector<std::uint8_t>{}; }}) &&
		!registry.Register({0x1002, 8, 4, 1, [](std::span<const std::uint8_t>) { return std::vector<std::uint8_t>{}; }}) &&
		!registry.Register({0x1003, 0, 8, 0, [](std::span<const std::uint8_t>) { return std::vector<std::uint8_t>{}; }});
	const bool registered = registry.Register({0x1001, 4, 8, 4, [](std::span<const std::uint8_t> r) { return std::vector<std::uint8_t>{r[0], r[1], 0xaa, 0x55}; }});
	const bool duplicate_rejected = !registry.Register({0x1001, 0, 8, 4, [](std::span<const std::uint8_t>) { return std::vector<std::uint8_t>{}; }});
	const bool oversized_handler_registered = registry.Register({0x1004, 0, 8, 2, [](std::span<const std::uint8_t>) { return std::vector<std::uint8_t>{1, 2, 3}; }});
	const std::vector<std::uint8_t> request{1, 2, 3, 4};
	const auto reply = registry.Dispatch(0x1001, request);
	const auto short_reply = registry.Dispatch(0x1001, std::vector<std::uint8_t>{1, 2});
	const bool ok = invalid_rejected && registered && duplicate_rejected && oversized_handler_registered && registry.Contains(0x1001) && reply == std::vector<std::uint8_t>({1, 2, 0xaa, 0x55}) && short_reply.empty() && registry.Dispatch(0x9999, request).empty() && registry.Dispatch(0x1004, request).empty() && registry.Unregister(0x1001) && !registry.Contains(0x1001);
	std::cout << "MIG_DISPATCH=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
}
