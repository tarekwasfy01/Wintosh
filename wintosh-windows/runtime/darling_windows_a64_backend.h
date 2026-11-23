/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#if defined(WINTOSH_HAS_DYNARMIC)

#include <dynarmic/interface/A64/config.h>
#include <dynarmic/interface/halt_reason.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace darling::windows_host {

struct A64GuestMemoryRegion final {
	std::uintptr_t address = 0;
	std::size_t bytes = 0;
	bool readable = false;
	bool writable = false;
	bool executable = false;
	bool dynamically_allocated = false;
	std::uintptr_t allocation_base = 0;
	std::size_t allocation_size = 0;
	bool guest_protectable = false;
};

struct A64CpuState final {
	std::array<std::uint64_t, 31> x{};
	std::array<Dynarmic::A64::Vector, 32> v{};
	std::uint64_t sp = 0;
	std::uint64_t pc = 0;
	std::uint32_t pstate = 0;
	std::uint32_t fpcr = 0;
	std::uint32_t fpsr = 0;
};

struct A64ExecutionResult final {
	Dynarmic::HaltReason halt_reason = Dynarmic::HaltReason::Step;
	A64CpuState state{};
	std::optional<std::uint32_t> svc;
	std::optional<Dynarmic::A64::Exception> exception;
	std::optional<std::uint64_t> fault_address;
	std::string fault;
};

// A bounded-memory AArch64 user-mode CPU adapter. This executes instructions
// only; the owner must dispatch SVCs and implement Darwin imports/services.
class DarlingWindowsA64Backend final {
public:
	explicit DarlingWindowsA64Backend(std::vector<A64GuestMemoryRegion> memory);

	[[nodiscard]] A64ExecutionResult Run(std::uint64_t entry,
		std::uint64_t stack_pointer,
		const std::array<std::uint64_t, 31>& registers = {}) const;
	[[nodiscard]] A64ExecutionResult Run(const A64CpuState& initial_state) const;
	[[nodiscard]] bool ContainsGuestRange(std::uint64_t address,
		std::size_t bytes, bool guest_writes) const noexcept;
	[[nodiscard]] bool ContainsGuestCString(std::uint64_t address,
		std::size_t maximum_bytes) const noexcept;
	[[nodiscard]] bool RegisterDynamicGuestMapping(
		A64GuestMemoryRegion region);
	[[nodiscard]] bool HasDynamicGuestMapping(std::uint64_t address) const noexcept;
	[[nodiscard]] std::optional<std::size_t> DynamicGuestMappingSize(
		std::uint64_t address) const noexcept;
	[[nodiscard]] bool RemoveDynamicGuestMapping(std::uint64_t address) noexcept;
	[[nodiscard]] bool CanProtectGuestRange(std::uint64_t address,
		std::size_t bytes) const noexcept;
	[[nodiscard]] bool ProtectGuestRange(std::uint64_t address,
		std::size_t bytes, bool readable, bool writable,
		bool executable);

private:
	std::vector<A64GuestMemoryRegion> m_memory;
};

} // namespace darling::windows_host

#endif // WINTOSH_HAS_DYNARMIC
