/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_tlv.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace darling::windows_host {

namespace {

constexpr std::size_t MaxTLVObjectBytes = 1024 * 1024;

struct ThreadStorage;
std::mutex storage_mutex;
std::unordered_set<ThreadStorage*> storages;

struct ThreadStorage final {
	struct Destructor final {
		void (*function)(void*) = nullptr;
		void* argument = nullptr;
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
					iterator->function(iterator->argument);
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

} // namespace

void* ResolveDarwinTLV(DarwinTLVDescriptor* descriptor) noexcept
{
	if (descriptor == nullptr || descriptor->key == 0 ||
		descriptor->offset > MaxTLVObjectBytes)
		return nullptr;
	try {
		auto& object = storage.objects[descriptor->key];
		if (object.empty()) {
			std::lock_guard lock(template_mutex);
			const auto template_it = templates.find(descriptor->key);
			if (template_it != templates.end())
				object = template_it->second;
		}
		const auto required = static_cast<std::size_t>(descriptor->offset) + 1;
		if (object.size() < required)
			object.insert(object.end(), required - object.size(), std::byte{0});
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
	if (descriptors == nullptr || count == 0)
		return;
	if (initial_bytes > MaxTLVObjectBytes ||
		(initial_bytes != 0 && initial_data == nullptr))
		throw std::invalid_argument("invalid Mach-O TLV initial-value template");
	if (count > std::numeric_limits<std::size_t>::max() / sizeof(*descriptors))
		throw std::overflow_error("Mach-O TLV descriptor count overflows");
	for (std::size_t index = 0; index < count; ++index) {
		auto& descriptor = descriptors[index];
		descriptor.thunk = &ResolveDarwinTLV;
		descriptor.key = next_key.fetch_add(1, std::memory_order_relaxed);
		if (descriptor.key == 0)
			throw std::overflow_error("Mach-O TLV descriptor key exhausted");
		std::vector<std::byte> template_bytes(initial_bytes);
		if (initial_bytes != 0)
			std::memcpy(template_bytes.data(), initial_data, initial_bytes);
		std::lock_guard lock(template_mutex);
		templates.emplace(descriptor.key, std::move(template_bytes));
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
	if (function == nullptr)
		return;
	try {
		storage.destructors.push_back({function, argument});
	} catch (...) {
	}
}

extern "C" void darling_windows_cxa_thread_atexit(void (*function)(void*),
	void* argument) noexcept
{
	darling_windows_tlv_atexit(function, argument);
}

} // namespace darling::windows_host

