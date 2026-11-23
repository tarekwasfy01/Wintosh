/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_tlv.h"
#include "darling_windows_abi_bridge.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace darling::windows_host {

#if defined(_M_IX86)
extern "C" void* darling_windows_tlv_get_addr_i386(DarwinTLVDescriptor* descriptor);
#endif

namespace {

constexpr std::size_t MaxTLVObjectBytes = 1024 * 1024;

struct ThreadStorage;
std::mutex storage_mutex;
std::unordered_set<ThreadStorage*> storages;

struct ThreadStorage final {
	struct Destructor final {
		void (*function)(void*) = nullptr;
		void* argument = nullptr;
		void* dso_handle = nullptr;
		bool guest_abi = false;
	};
	std::unordered_map<std::uintptr_t, std::vector<std::byte>> objects;
	std::vector<Destructor> destructors;

	ThreadStorage()
	{
		std::lock_guard lock(storage_mutex);
		storages.insert(this);
	}

	~ThreadStorage() noexcept
	{
		{
			std::lock_guard lock(storage_mutex);
			storages.erase(this);
		}
		for (auto iterator = destructors.rbegin(); iterator != destructors.rend(); ++iterator) {
			if (iterator->function != nullptr) {
				try {
					if (iterator->guest_abi) {
						using GuestDestructor = darling::windows_host::abi::GuestFn<void, void*>;
						reinterpret_cast<GuestDestructor>(iterator->function)(iterator->argument);
					} else {
						iterator->function(iterator->argument);
					}
				} catch (...) {
				}
			}
		}
	}
};

thread_local ThreadStorage storage;
std::atomic<std::uintptr_t> next_key{1};
std::mutex template_mutex;
std::unordered_map<std::uintptr_t, std::vector<std::byte>> templates;

int RegisterDestructor(void (*function)(void*), void* argument, void* dso_handle,
	bool guest_abi) noexcept
{
	if (function == nullptr) return 0;
	try {
		storage.destructors.push_back({function, argument, dso_handle, guest_abi});
		return 0;
	} catch (...) {
		return -1;
	}
}

} // namespace

void* ResolveDarwinTLV(DarwinTLVDescriptor* descriptor) noexcept
{
	if (descriptor == nullptr || descriptor->key == 0 ||
		descriptor->offset >= MaxTLVObjectBytes)
		return nullptr;
	try {
		auto& object = storage.objects[descriptor->key];
		if (object.empty()) {
			std::lock_guard lock(template_mutex);
			const auto template_it = templates.find(descriptor->key);
			if (template_it == templates.end())
				return nullptr;
			object = template_it->second;
		}
		// Never grow a published TLS block: guest pointers must stay stable.
		if (descriptor->offset >= object.size())
			return nullptr;
		return object.data() + descriptor->offset;
	} catch (...) {
		return nullptr;
	}
}

void InitializeDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count)

{
	InitializeDarwinTLV(descriptors, count, nullptr, 0);
}

void InitializeDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count,
	const void* initial_data, std::size_t initial_bytes)
{
	const DarwinTLVDescriptorRange range{descriptors, count};
	InitializeDarwinTLV(&range, 1, initial_data, initial_bytes);
}

void InitializeDarwinTLV(const DarwinTLVDescriptorRange* ranges, std::size_t range_count,
	const void* initial_data, std::size_t initial_bytes)
{
	if (ranges == nullptr || range_count == 0)
		return;
	if (initial_bytes > MaxTLVObjectBytes ||
		(initial_bytes != 0 && initial_data == nullptr))
		throw std::invalid_argument("invalid Mach-O TLV initial-value template");
	std::size_t object_bytes = initial_bytes;
	for (std::size_t r = 0; r < range_count; ++r) {
		if (ranges[r].descriptors == nullptr || ranges[r].count > std::numeric_limits<std::size_t>::max() / sizeof(*ranges[r].descriptors))
			throw std::invalid_argument("invalid Mach-O TLV descriptor range");
		for (std::size_t i = 0; i < ranges[r].count; ++i) {
			if (ranges[r].descriptors[i].offset >= MaxTLVObjectBytes)
				throw std::invalid_argument("Mach-O TLV descriptor offset exceeds limit");
			object_bytes = (std::max)(object_bytes, static_cast<std::size_t>(ranges[r].descriptors[i].offset) + 1);
		}
	}
	const auto key = next_key.fetch_add(1, std::memory_order_relaxed);
	if (key == 0)
		throw std::overflow_error("Mach-O TLV descriptor key exhausted");
	std::vector<std::byte> template_bytes(object_bytes);
	if (initial_bytes != 0)
		std::memcpy(template_bytes.data(), initial_data, initial_bytes);
	{
		std::lock_guard lock(template_mutex);
		templates.emplace(key, std::move(template_bytes));
	}
	for (std::size_t r = 0; r < range_count; ++r) {
		for (std::size_t i = 0; i < ranges[r].count; ++i) {
#if defined(_M_IX86)
			ranges[r].descriptors[i].thunk = &darling_windows_tlv_get_addr_i386;
#else
			ranges[r].descriptors[i].thunk = &ResolveDarwinTLV;
#endif
			ranges[r].descriptors[i].key = key;
		}
	}
}

void DestroyDarwinTLV(DarwinTLVDescriptor* descriptors, std::size_t count) noexcept
{
	if (descriptors == nullptr || count == 0)
		return;
	try {
		std::vector<std::uintptr_t> keys;
		keys.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			if (descriptors[index].key != 0)
				keys.push_back(descriptors[index].key);
		}
		{
			std::lock_guard lock(template_mutex);
			for (const auto key : keys)
				templates.erase(key);
		}
		{
			std::lock_guard lock(storage_mutex);
			for (auto* thread_storage : storages) {
				for (const auto key : keys)
					thread_storage->objects.erase(key);
			}
		}
		for (std::size_t index = 0; index < count; ++index) {
			descriptors[index].thunk = nullptr;
			descriptors[index].key = 0;
			descriptors[index].offset = 0;
		}
	} catch (...) {
		// dlclose/teardown is noexcept. A partially cleaned registration is
		// safer than propagating through the loader boundary.
	}
}

extern "C" void darling_windows_tlv_atexit(void (*function)(void*),
	void* argument) noexcept
{
	(void)RegisterDestructor(function, argument, nullptr, false);
}

extern "C" void darling_windows_cxa_thread_atexit(void (*function)(void*),
	void* argument) noexcept
{
	(void)RegisterDestructor(function, argument, nullptr, false);
}

extern "C" int darling_windows_cxa_thread_atexit_impl(
	void (*function)(void*), void* argument, void* dso_handle) noexcept
{
	return RegisterDestructor(function, argument, dso_handle, true);
}

extern "C" void darling_windows_tlv_atexit_guest(void (*function)(void*),
	void* argument) noexcept
{
	(void)RegisterDestructor(function, argument, nullptr, true);
}

} // namespace darling::windows_host

