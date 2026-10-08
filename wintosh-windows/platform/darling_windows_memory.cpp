/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_memory.h"
#include "darling_windows_errno.h"

#include <windows.h>

#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace darling::windows_host {

namespace {

std::mutex anonymous_mapping_mutex;
std::unordered_map<void*, std::size_t> anonymous_mappings;

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

bool ReleaseOwnedAnonymousMappingImpl(void* address)
{
	std::lock_guard lock(anonymous_mapping_mutex);
	const auto found = anonymous_mappings.find(address);
	if (found == anonymous_mappings.end()) {
		return false;
	}
	if (!VirtualFree(address, 0, MEM_RELEASE)) {
		ThrowLastError("VirtualFree(MAP_FIXED)");
	}
	anonymous_mappings.erase(found);
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
	anonymous_mappings.emplace(address, length);
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
		length, MEM_RESERVE | MEM_COMMIT, page_protection);
	if (mapped == nullptr) {
		darling::windows_host::DarwinErrno::SetFromWin32(GetLastError());
		return reinterpret_cast<void*>(-1);
	}
	{
		darling::windows_host::RegisterOwnedAnonymousMapping(mapped, length);
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
	try {
		if (darling::windows_host::ReleaseOwnedAnonymousMapping(address)) {
			return 0;
		}
		darling::windows_host::DarwinMemory::Munmap(address, length);
		return 0;
	} catch (...) {
		return -1;
	}
}
