/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_a64_backend.h"

#if defined(WINTOSH_HAS_DYNARMIC)

#include <dynarmic/interface/A64/a64.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace darling::windows_host {
namespace {

class CallbackContext final : public Dynarmic::A64::UserCallbacks {
public:
	explicit CallbackContext(const std::vector<A64GuestMemoryRegion>& memory) :
		m_memory(memory)
	{
	}

	void Attach(Dynarmic::A64::Jit& jit) noexcept { m_jit = &jit; }

	std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A64::VAddr address) override
	{
		const auto* region = Find(address, sizeof(std::uint32_t), Access::Execute);
		if (region == nullptr) {
			RecordFault(address, "AArch64 instruction fetch is outside executable guest memory");
			return std::nullopt;
		}
		return Read<std::uint32_t>(address, *region);
	}

	std::uint8_t MemoryRead8(Dynarmic::A64::VAddr address) override
	{
		return ReadValue<std::uint8_t>(address);
	}
	std::uint16_t MemoryRead16(Dynarmic::A64::VAddr address) override
	{
		return ReadValue<std::uint16_t>(address);
	}
	std::uint32_t MemoryRead32(Dynarmic::A64::VAddr address) override
	{
		return ReadValue<std::uint32_t>(address);
	}
	std::uint64_t MemoryRead64(Dynarmic::A64::VAddr address) override
	{
		return ReadValue<std::uint64_t>(address);
	}
	Dynarmic::A64::Vector MemoryRead128(Dynarmic::A64::VAddr address) override
	{
		return ReadValue<Dynarmic::A64::Vector>(address);
	}

	void MemoryWrite8(Dynarmic::A64::VAddr address, std::uint8_t value) override
	{
		WriteValue(address, value);
	}
	void MemoryWrite16(Dynarmic::A64::VAddr address, std::uint16_t value) override
	{
		WriteValue(address, value);
	}
	void MemoryWrite32(Dynarmic::A64::VAddr address, std::uint32_t value) override
	{
		WriteValue(address, value);
	}
	void MemoryWrite64(Dynarmic::A64::VAddr address, std::uint64_t value) override
	{
		WriteValue(address, value);
	}
	void MemoryWrite128(Dynarmic::A64::VAddr address,
		Dynarmic::A64::Vector value) override
	{
		WriteValue(address, value);
	}

	bool MemoryWriteExclusive8(Dynarmic::A64::VAddr address, std::uint8_t,
		std::uint8_t) override
	{
		return RejectExclusive(address);
	}
	bool MemoryWriteExclusive16(Dynarmic::A64::VAddr address, std::uint16_t,
		std::uint16_t) override
	{
		return RejectExclusive(address);
	}
	bool MemoryWriteExclusive32(Dynarmic::A64::VAddr address, std::uint32_t,
		std::uint32_t) override
	{
		return RejectExclusive(address);
	}
	bool MemoryWriteExclusive64(Dynarmic::A64::VAddr address, std::uint64_t,
		std::uint64_t) override
	{
		return RejectExclusive(address);
	}
	bool MemoryWriteExclusive128(Dynarmic::A64::VAddr address,
		Dynarmic::A64::Vector, Dynarmic::A64::Vector) override
	{
		return RejectExclusive(address);
	}

	void InterpreterFallback(Dynarmic::A64::VAddr pc, std::size_t) override
	{
		RecordFault(pc, "Dynarmic requested an unimplemented AArch64 interpreter fallback");
	}
	void CallSVC(std::uint32_t swi) override
	{
		m_svc = swi;
		if (m_jit != nullptr)
			m_jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
	}
	void ExceptionRaised(Dynarmic::A64::VAddr pc,
		Dynarmic::A64::Exception exception) override
	{
		m_exception = exception;
		if (!m_fault_address)
			m_fault_address = pc;
		if (m_jit != nullptr)
			m_jit->HaltExecution(Dynarmic::HaltReason::UserDefined2);
	}
	void AddTicks(std::uint64_t) override {}
	std::uint64_t GetTicksRemaining() override { return 0; }
	std::uint64_t GetCNTPCT() override
	{
		LARGE_INTEGER counter{};
		LARGE_INTEGER frequency{};
		if (!QueryPerformanceCounter(&counter) ||
			!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
			return GetTickCount64() * (600000000ull / 1000ull);
		}
		const auto scaled = static_cast<long double>(counter.QuadPart) *
			600000000.0L / static_cast<long double>(frequency.QuadPart);
		return scaled >= static_cast<long double>(
			std::numeric_limits<std::uint64_t>::max()) ?
			std::numeric_limits<std::uint64_t>::max() :
			static_cast<std::uint64_t>(scaled);
	}

	[[nodiscard]] const std::optional<std::uint32_t>& Svc() const noexcept { return m_svc; }
	[[nodiscard]] const std::optional<Dynarmic::A64::Exception>& Exception() const noexcept
	{
		return m_exception;
	}
	[[nodiscard]] const std::optional<std::uint64_t>& FaultAddress() const noexcept
	{
		return m_fault_address;
	}
	[[nodiscard]] const std::string& Fault() const noexcept { return m_fault; }

private:
	enum class Access { Read, Write, Execute };

	[[nodiscard]] const A64GuestMemoryRegion* Find(std::uint64_t address,
		std::size_t bytes, Access access) const noexcept
	{
		for (const auto& region : m_memory) {
			if (address < region.address)
				continue;
			const auto offset = address - region.address;
			if (offset > region.bytes || bytes > region.bytes -
				static_cast<std::size_t>(offset)) {
				continue;
			}
			const bool allowed = access == Access::Read ? region.readable :
				access == Access::Write ? region.writable : region.executable;
			if (allowed)
				return &region;
		}
		return nullptr;
	}

	void RecordFault(std::uint64_t address, const char* message)
	{
		if (!m_fault_address)
			m_fault_address = address;
		if (m_fault.empty())
			m_fault = message;
		if (m_jit != nullptr)
			m_jit->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
	}

	template<typename T>
	T Read(std::uint64_t address, const A64GuestMemoryRegion& region) const noexcept
	{
		T value{};
		const auto* source = reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(address));
		(void)region;
		std::memcpy(&value, source, sizeof(value));
		return value;
	}

	template<typename T>
	T ReadValue(std::uint64_t address)
	{
		const auto* region = Find(address, sizeof(T), Access::Read);
		if (region == nullptr) {
			RecordFault(address, "AArch64 data read is outside readable guest memory");
			return {};
		}
		return Read<T>(address, *region);
	}

	template<typename T>
	void WriteValue(std::uint64_t address, const T& value)
	{
		if (Find(address, sizeof(T), Access::Write) == nullptr) {
			RecordFault(address, "AArch64 data write is outside writable guest memory");
			return;
		}
		std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
			&value, sizeof(value));
	}

	bool RejectExclusive(std::uint64_t address)
	{
		RecordFault(address,
			"AArch64 exclusive store requires a shared exclusive-monitor implementation");
		return false;
	}

	const std::vector<A64GuestMemoryRegion>& m_memory;
	Dynarmic::A64::Jit* m_jit = nullptr;
	std::optional<std::uint32_t> m_svc;
	std::optional<Dynarmic::A64::Exception> m_exception;
	std::optional<std::uint64_t> m_fault_address;
	std::string m_fault;
};

} // namespace

DarlingWindowsA64Backend::DarlingWindowsA64Backend(
	std::vector<A64GuestMemoryRegion> memory) : m_memory(std::move(memory))
{
	for (const auto& region : m_memory) {
		if (region.bytes == 0 || region.address == 0 ||
			region.address > std::numeric_limits<std::uintptr_t>::max() - region.bytes) {
			throw std::invalid_argument("AArch64 guest memory region is empty or overflows");
		}
	}
}

A64ExecutionResult DarlingWindowsA64Backend::Run(std::uint64_t entry,
	std::uint64_t stack_pointer,
	const std::array<std::uint64_t, 31>& registers) const
	{
	A64CpuState initial_state{};
	initial_state.x = registers;
	initial_state.sp = stack_pointer;
	initial_state.pc = entry;
	return Run(initial_state);
}

A64ExecutionResult DarlingWindowsA64Backend::Run(
	const A64CpuState& initial_state) const
{
	CallbackContext callbacks(m_memory);
	Dynarmic::A64::UserConfig config{};
	config.callbacks = &callbacks;
	config.enable_cycle_counting = false;
	config.check_halt_on_memory_access = true;
	config.wall_clock_cntpct = true;
	Dynarmic::A64::Jit jit(config);
	callbacks.Attach(jit);
	jit.SetRegisters(initial_state.x);
	jit.SetVectors(initial_state.v);
	jit.SetSP(initial_state.sp);
	jit.SetPC(initial_state.pc);
	jit.SetPstate(initial_state.pstate);
	jit.SetFpcr(initial_state.fpcr);
	jit.SetFpsr(initial_state.fpsr);

	A64ExecutionResult result{};
	result.halt_reason = jit.Run();
	result.state.pc = jit.GetPC();
	result.state.sp = jit.GetSP();
	result.state.x = jit.GetRegisters();
	result.state.v = jit.GetVectors();
	result.state.pstate = jit.GetPstate();
	result.state.fpcr = jit.GetFpcr();
	result.state.fpsr = jit.GetFpsr();
	result.svc = callbacks.Svc();
	result.exception = callbacks.Exception();
	result.fault_address = callbacks.FaultAddress();
	result.fault = callbacks.Fault();
	return result;
}

bool DarlingWindowsA64Backend::ContainsGuestRange(std::uint64_t address,
	std::size_t bytes, bool guest_writes) const noexcept
{
	for (const auto& region : m_memory) {
		if (address < region.address)
			continue;
		const auto offset = address - region.address;
		if (offset > region.bytes || bytes > region.bytes -
			static_cast<std::size_t>(offset)) {
			continue;
		}
		if (guest_writes ? region.writable : region.readable)
			return true;
	}
	return bytes == 0;
}

bool DarlingWindowsA64Backend::ContainsGuestCString(std::uint64_t address,
	std::size_t maximum_bytes) const noexcept
{
	if (address == 0 || maximum_bytes == 0)
		return false;
	for (const auto& region : m_memory) {
		if (!region.readable || address < region.address)
			continue;
		const auto offset = address - region.address;
		if (offset >= region.bytes)
			continue;
		const auto available = region.bytes - static_cast<std::size_t>(offset);
		const auto bytes = std::min(available, maximum_bytes);
		const auto* source = reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(address));
		if (std::memchr(source, 0, bytes) != nullptr)
			return true;
	}
	return false;
}

bool DarlingWindowsA64Backend::RegisterDynamicGuestMapping(
	A64GuestMemoryRegion region)
{
	if (region.address == 0 || region.bytes == 0 ||
		region.address > std::numeric_limits<std::uintptr_t>::max() - region.bytes) {
		return false;
	}
	const auto end = region.address + region.bytes;
	for (const auto& existing : m_memory) {
		const auto existing_end = existing.address + existing.bytes;
		if (region.address < existing_end && existing.address < end)
			return false;
	}
	region.dynamically_allocated = true;
	region.allocation_base = region.address;
	if (region.allocation_size == 0)
		region.allocation_size = region.bytes;
	region.guest_protectable = true;
	m_memory.push_back(region);
	return true;
}

bool DarlingWindowsA64Backend::HasDynamicGuestMapping(
	std::uint64_t address) const noexcept
{
	return std::any_of(m_memory.begin(), m_memory.end(), [address](const auto& region) {
		return region.dynamically_allocated && region.allocation_base == address;
	});
}

std::optional<std::size_t> DarlingWindowsA64Backend::DynamicGuestMappingSize(
	std::uint64_t address) const noexcept
{
	const auto found = std::find_if(m_memory.begin(), m_memory.end(),
		[address](const auto& region) {
			return region.dynamically_allocated && region.allocation_base == address;
		});
	if (found == m_memory.end())
		return std::nullopt;
	return found->allocation_size;
}

bool DarlingWindowsA64Backend::RemoveDynamicGuestMapping(
	std::uint64_t address) noexcept
{
	const auto previous_size = m_memory.size();
	std::erase_if(m_memory, [address](const auto& region) {
		return region.dynamically_allocated && region.allocation_base == address;
	});
	return m_memory.size() != previous_size;
}

bool DarlingWindowsA64Backend::CanProtectGuestRange(std::uint64_t address,
	std::size_t bytes) const noexcept
{
	if (bytes == 0 || address == 0 ||
		address > std::numeric_limits<std::uintptr_t>::max() - bytes) {
		return false;
	}
	const auto end = address + bytes;
	auto cursor = address;
	while (cursor < end) {
		std::uint64_t furthest = cursor;
		for (const auto& region : m_memory) {
			if (!region.guest_protectable)
				continue;
			const auto region_end = region.address + region.bytes;
			if (region.address <= cursor && cursor < region_end)
				furthest = (std::max)(furthest, static_cast<std::uint64_t>(region_end));
		}
		if (furthest == cursor)
			return false;
		cursor = (std::min)(furthest, end);
	}
	return true;
}

bool DarlingWindowsA64Backend::ProtectGuestRange(std::uint64_t address,
	std::size_t bytes, bool readable, bool writable, bool executable)
{
	if (!CanProtectGuestRange(address, bytes))
		return false;
	const auto end = address + bytes;
	std::vector<A64GuestMemoryRegion> updated;
	updated.reserve(m_memory.size() + 4);
	for (const auto& region : m_memory) {
		const auto region_start = static_cast<std::uint64_t>(region.address);
		const auto region_end = static_cast<std::uint64_t>(region.address + region.bytes);
		const auto overlap_start = (std::max)(region_start, address);
		const auto overlap_end = (std::min)(region_end, end);
		if (overlap_start >= overlap_end) {
			updated.push_back(region);
			continue;
		}
		if (region_start < overlap_start) {
			auto before = region;
			before.bytes = static_cast<std::size_t>(overlap_start - region_start);
			updated.push_back(before);
		}
		auto middle = region;
		middle.address = static_cast<std::uintptr_t>(overlap_start);
		middle.bytes = static_cast<std::size_t>(overlap_end - overlap_start);
		middle.readable = readable;
		middle.writable = writable;
		middle.executable = executable;
		updated.push_back(middle);
		if (overlap_end < region_end) {
			auto after = region;
			after.address = static_cast<std::uintptr_t>(overlap_end);
			after.bytes = static_cast<std::size_t>(region_end - overlap_end);
			updated.push_back(after);
		}
	}
	m_memory.swap(updated);
	return true;
}

} // namespace darling::windows_host

#endif // WINTOSH_HAS_DYNARMIC
