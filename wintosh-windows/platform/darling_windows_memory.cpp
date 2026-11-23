/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_memory.h"
#include "darling_windows_errno.h"
#include "darling_windows_stdio.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace darling::windows_host {

namespace {

std::mutex anonymous_mapping_mutex;
struct AnonymousMappingSegment final {
	std::size_t bytes = 0;
	void* reservation = nullptr;
	std::size_t reservation_bytes = 0;
};
std::map<std::uintptr_t, AnonymousMappingSegment> anonymous_mappings;

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

bool ReleaseOwnedAnonymousMappingImpl(void* address)
{
	std::lock_guard lock(anonymous_mapping_mutex);
	const auto found = anonymous_mappings.find(reinterpret_cast<std::uintptr_t>(address));
	if (found == anonymous_mappings.end()) {
		return false;
	}
	const auto reservation = found->second.reservation;
	if (!VirtualFree(reservation, 0, MEM_RELEASE)) {
		ThrowLastError("VirtualFree(MAP_FIXED)");
	}
	std::erase_if(anonymous_mappings, [reservation](const auto& item) {
		return item.second.reservation == reservation;
	});
	return true;
}

bool UnmapOwnedAnonymousRange(void* address, std::size_t length)
{
	const auto start = reinterpret_cast<std::uintptr_t>(address);
	if (length == 0 || start > (std::numeric_limits<std::uintptr_t>::max)() - length)
		return false;
	const auto end = start + length;
	std::lock_guard lock(anonymous_mapping_mutex);
	auto found = anonymous_mappings.upper_bound(start);
	if (found == anonymous_mappings.begin())
		return false;
	--found;
	const auto reservation = found->second.reservation;
	const auto reservation_bytes = found->second.reservation_bytes;
	auto cursor = start;
	for (auto it = found; it != anonymous_mappings.end() && cursor < end; ++it) {
		const auto segment_start = it->first;
		const auto segment_end = segment_start + it->second.bytes;
		if (it->second.reservation != reservation || segment_start > cursor || segment_end <= cursor)
			return false;
		cursor = (std::min)(end, segment_end);
	}
	if (cursor != end)
		return false;

	auto updated = anonymous_mappings;
	for (auto it = updated.begin(); it != updated.end();) {
		const auto segment_start = it->first;
		const auto segment_end = segment_start + it->second.bytes;
		if (it->second.reservation != reservation || segment_end <= start || segment_start >= end) {
			++it;
			continue;
		}
		it = updated.erase(it);
		if (segment_start < start)
			updated.emplace(segment_start, AnonymousMappingSegment{start - segment_start,
				reservation, reservation_bytes});
		if (segment_end > end)
			updated.emplace(end, AnonymousMappingSegment{segment_end - end,
				reservation, reservation_bytes});
	}
	const bool reservation_empty = std::none_of(updated.begin(), updated.end(),
		[reservation](const auto& item) { return item.second.reservation == reservation; });
	if (reservation_empty) {
		if (!VirtualFree(reservation, 0, MEM_RELEASE))
			ThrowLastError("VirtualFree(MEM_RELEASE)");
	} else if (!VirtualFree(address, length, MEM_DECOMMIT)) {
		ThrowLastError("VirtualFree(MEM_DECOMMIT)");
	}
	anonymous_mappings.swap(updated);
	return true;
}

DWORD Protection(DarwinMemoryProtection value)
{
	const auto bits = static_cast<std::uint8_t>(value);
	const bool read = (bits & 1) != 0;
	const bool write = (bits & 2) != 0;
	const bool execute = (bits & 4) != 0;
	if (execute) {
		return write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	}
	return write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS);
}

} // namespace

void RegisterOwnedAnonymousMapping(void* address, std::size_t length)
{
	std::lock_guard lock(anonymous_mapping_mutex);
	const auto start = reinterpret_cast<std::uintptr_t>(address);
	anonymous_mappings.emplace(start, AnonymousMappingSegment{length, address, length});
}

bool ReleaseOwnedAnonymousMapping(void* address)
{
	return ReleaseOwnedAnonymousMappingImpl(address);
}

void* DarwinMemory::Mmap(std::size_t bytes, DarwinMemoryProtection protection)
{
	if (bytes == 0 || bytes > std::numeric_limits<SIZE_T>::max()) {
		throw std::invalid_argument("Darwin mmap size is invalid");
	}
	void* address = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, Protection(protection));
	if (address == nullptr) {
		ThrowLastError("VirtualAlloc");
	}
	return address;
}

void DarwinMemory::Mprotect(void* address, std::size_t bytes,
	DarwinMemoryProtection protection)
{
	if (address == nullptr || bytes == 0 || bytes > std::numeric_limits<SIZE_T>::max()) {
		throw std::invalid_argument("Darwin mprotect range is invalid");
	}
	DWORD old_protection = PAGE_NOACCESS;
	if (!VirtualProtect(address, bytes, Protection(protection), &old_protection)) {
		ThrowLastError("VirtualProtect");
	}
}

void DarwinMemory::Munmap(void* address, std::size_t bytes)
{
	(void)bytes;
	if (address == nullptr || !VirtualFree(address, 0, MEM_RELEASE)) {
		ThrowLastError("VirtualFree");
	}
}

} // namespace darling::windows_host

extern "C" void* darling_windows_mmap_anonymous(void* address, std::size_t length,
	int protection, int flags, int descriptor, std::int64_t offset)
{
	constexpr int map_fixed = 0x0010;
	constexpr int map_anon = 0x1000;
	if (length == 0 || (protection & ~0x0007) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return reinterpret_cast<void*>(-1);
	}
	if ((flags & map_anon) == 0 || descriptor != -1 || offset != 0) {
		darling::windows_host::DarwinErrno::Set(95);
		return reinterpret_cast<void*>(-1);
	}
	const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
	if (page_size == 0 ||
		((flags & map_fixed) != 0 && address != nullptr &&
		 reinterpret_cast<std::uintptr_t>(address) % page_size != 0) ||
		length > (std::numeric_limits<std::size_t>::max)() - (page_size - 1)) {
		darling::windows_host::DarwinErrno::Set(22);
		return reinterpret_cast<void*>(-1);
	}
	const auto allocation_length = ((length + page_size - 1) / page_size) * page_size;
	const auto bits = static_cast<std::uint8_t>(protection);
	const bool read = (bits & 1) != 0;
	const bool write = (bits & 2) != 0;
	const bool execute = (bits & 4) != 0;
	const DWORD page_protection = execute ?
		(write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE)) :
		(write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS));
	if ((flags & map_fixed) != 0 && address != nullptr) {
		try {
			(void)darling::windows_host::ReleaseOwnedAnonymousMapping(address);
		} catch (...) {
			darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
			return reinterpret_cast<void*>(-1);
		}
	}
	void* mapped = VirtualAlloc((flags & map_fixed) != 0 ? address : nullptr,
		allocation_length, MEM_RESERVE | MEM_COMMIT, page_protection);
	if (mapped == nullptr) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return reinterpret_cast<void*>(-1);
	}
	{
		darling::windows_host::RegisterOwnedAnonymousMapping(mapped, allocation_length);
	}
	return mapped;
}

extern "C" int darling_windows_mprotect(void* address, std::size_t length,
	int protection)
{
	if (address == nullptr || length == 0 || (protection & ~0x0007) != 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
	if (page_size == 0 || reinterpret_cast<std::uintptr_t>(address) % page_size != 0 ||
		length > (std::numeric_limits<std::size_t>::max)() - (page_size - 1)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	length = ((length + page_size - 1) / page_size) * page_size;
	try {
		darling::windows_host::DarwinMemory::Mprotect(address, length,
		static_cast<darling::windows_host::DarwinMemoryProtection>(protection));
		return 0;
	} catch (...) {
		return -1;
	}
}

extern "C" int darling_windows_munmap_anonymous(void* address, std::size_t length)
{
	if (address == nullptr || address == reinterpret_cast<void*>(-1) || length == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
	if (page_size == 0 || reinterpret_cast<std::uintptr_t>(address) % page_size != 0 ||
		length > (std::numeric_limits<std::size_t>::max)() - (page_size - 1)) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	length = ((length + page_size - 1) / page_size) * page_size;
	try {
		if (darling::windows_host::UnmapOwnedAnonymousRange(address, length)) {
			return 0;
		}
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	} catch (...) {
		return -1;
	}
}
