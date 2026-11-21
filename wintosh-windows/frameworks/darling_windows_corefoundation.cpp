/* Minimal CoreFoundation-compatible primitives; GPL-3.0-only. */
#include "darling_windows_corefoundation.h"
#include "../loader/darling_windows_macho.h"
#include "darling_windows_appkit.h"
#include "../abi/darling_windows_objc.h"
#include "../abi/darling_windows_mach.h"
#include "../platform/darling_windows_stdio.h"

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <cwchar>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>

namespace {
enum class Kind { String, Data, Array, Number, Real, Dictionary, Set, Date, URL, Boolean, Null, ImageSource, DataProvider, ImageDestination, DataConsumer, Bundle, PropertyListUID, RunLoopSource, RunLoopTimer, RunLoopObserver, RunLoop, MachPort, Allocator };
constexpr darling_windows_CFTypeID TypeId(Kind kind)
{
	return static_cast<darling_windows_CFTypeID>(kind) + 1;
}
struct RunLoopState;

extern "C" int darling_windows_CFStringGetSystemEncoding()
{
	return 1;
}
struct Object {
	std::atomic<std::size_t> references{1};
	bool immortal = false;
	bool mutable_dictionary = false;
	bool mutable_set = false;
	bool mutable_array = false;
	bool has_array_callback_table = false;
	DarlingCFArrayCallbacks array_callbacks{};
	const void* array_allocator = nullptr;
	bool has_dictionary_callback_tables = false;
	DarlingCFDictionaryKeyCallbacks dictionary_key_callbacks{};
	DarlingCFDictionaryValueCallbacks dictionary_value_callbacks{};
	const void* dictionary_allocator = nullptr;
	bool has_set_callback_table = false;
	DarlingCFDictionaryKeyCallbacks set_callbacks{};
	const void* set_allocator = nullptr;
	Kind kind;
	std::string string;
	mutable std::mutex url_string_mutex;
	mutable const void* url_string_cache = nullptr;
	std::string bundle_path;
	mutable const void* bundle_info_dictionary = nullptr;
	std::vector<const void*> bundle_localized_info_values;
	void* bundle_executable_handle = nullptr;
	darling_windows_CFBundleReleaseExecutableCallback bundle_release_executable = nullptr;
	mutable std::mutex bundle_info_mutex;
	std::vector<unsigned char> data;
	std::vector<const void*> array;
	std::unordered_multimap<std::uintptr_t, const void*> set_entries;
	std::int64_t number = 0;
	std::uint64_t property_list_uid = 0;
	double real_number = 0;
	std::vector<std::pair<const void*, const void*>> dictionary;
	DarlingCFRunLoopSourceContext run_loop_source_context{};
	DarlingCFRunLoopSourceContext1 run_loop_source_context1{};
	const void* run_loop_source_info = nullptr;
	const void* run_loop_source_allocator = nullptr;
	darling_windows_CFIndex run_loop_source_order = 0;
	darling_mach_port_name_t run_loop_source_port = 0;
	bool run_loop_source_valid = false;
	bool run_loop_source_is_v1 = false;
	bool run_loop_source_signaled = false;
	std::uint64_t run_loop_source_generation = 0;
	std::mutex run_loop_source_mutex;
	struct SourceSchedule final {
		std::weak_ptr<RunLoopState> state;
		void* loop{};
		std::string mode;
	};
	std::vector<SourceSchedule> run_loop_source_schedules;
	Object* run_loop_source_cfmachport = nullptr;
	DarlingCFMachPortContext mach_port_context{};
	const void* mach_port_info = nullptr;
	const void* mach_port_allocator = nullptr;
	DarlingCFMachPortCallout mach_port_callout{};
	DarlingCFMachPortInvalidationCallout mach_port_invalidation_callout{};
	darling_mach_port_name_t mach_port_name = 0;
	bool mach_port_valid = false;
	bool mach_port_owns_name = false;
	bool mach_port_context_released = false;
	std::mutex mach_port_mutex;
	std::vector<Object*> mach_port_sources;
	DarlingCFAllocatorContext allocator_context{};
	const void* allocator_info = nullptr;
	const void* object_backing_allocator = nullptr;
	const void* allocator_backing_allocator = nullptr;
	bool allocator_storage_uses_context = false;
	bool allocator_context_release_deferred = false;
	DarlingCFRunLoopTimerContext run_loop_timer_context{};
	const void* run_loop_timer_info = nullptr;
	const void* run_loop_timer_allocator = nullptr;
	DarlingCFRunLoopTimerCallout run_loop_timer_callout{};
	double run_loop_timer_fire_date = 0;
	double run_loop_timer_interval = 0;
	darling_windows_CFIndex run_loop_timer_order = 0;
	bool run_loop_timer_valid = false;
	std::mutex run_loop_timer_mutex;
	struct TimerSchedule final {
		std::weak_ptr<RunLoopState> state;
		void* loop{};
		std::string mode;
	};
	std::vector<TimerSchedule> run_loop_timer_schedules;
	DarlingCFRunLoopObserverContext run_loop_observer_context{};
	const void* run_loop_observer_info = nullptr;
	const void* run_loop_observer_allocator = nullptr;
	DarlingCFRunLoopObserverCallout run_loop_observer_callout{};
	std::uint64_t run_loop_observer_activities = 0;
	darling_windows_CFIndex run_loop_observer_order = 0;
	bool run_loop_observer_repeats = false;
	bool run_loop_observer_valid = false;
	std::mutex run_loop_observer_mutex;
	struct ObserverSchedule final {
		std::weak_ptr<RunLoopState> state;
		void* loop{};
		std::string mode;
	};
	std::vector<ObserverSchedule> run_loop_observer_schedules;
	double date = 0;
	bool boolean = false;
	void* external_data_info = nullptr;
	const void* external_data = nullptr;
	std::size_t external_data_size = 0;
	darling_windows_CFDataProviderReleaseCallback external_data_release = nullptr;
	bool is_external_data_provider = false;
	void* sequential_info = nullptr;
	darling_windows_CFDataProviderGetBytesCallback sequential_get_bytes = nullptr;
	darling_windows_CFDataProviderSkipForwardCallback sequential_skip_forward = nullptr;
	darling_windows_CFDataProviderRewindCallback sequential_rewind = nullptr;
	darling_windows_CFDataProviderReleaseInfoCallback sequential_release_info = nullptr;
	std::int64_t sequential_position = 0;
	std::mutex sequential_mutex;
	bool is_sequential_data_provider = false;
	void* direct_info = nullptr;
	darling_windows_CFIndex direct_length = 0;
	darling_windows_CFDataProviderGetBytePointerCallback direct_get_byte_pointer = nullptr;
	darling_windows_CFDataProviderReleaseBytePointerCallback direct_release_byte_pointer = nullptr;
	darling_windows_CFDataProviderGetBytesAtPositionCallback direct_get_bytes_at_position = nullptr;
	darling_windows_CFDataProviderReleaseInfoCallback direct_release_info = nullptr;
	bool is_direct_callback_provider = false;
	void* opaque_payload = nullptr;
	darling_windows_CFOpaqueDestroyCallback opaque_destroy = nullptr;
	~Object();
};
std::mutex run_loop_source1_port_registry_mutex;
std::unordered_map<darling_mach_port_name_t, Object*> run_loop_source1_port_registry;
std::mutex cfmachport_registry_mutex;
std::unordered_map<darling_mach_port_name_t, Object*> cfmachport_registry;
std::uint8_t allocator_use_context_sentinel{};
Object* As(const void* value) { return const_cast<Object*>(static_cast<const Object*>(value)); }
Object* AsCFAllocator(const void* value);
Object* CreateObjectWithAllocator(const void* allocator, Kind kind);
std::mutex bundle_state_mutex;
std::string main_executable_path;
Object* main_bundle_object = nullptr;
std::mutex bundle_registry_mutex;
std::unordered_map<std::string, Object*> bundle_registry;
darling_windows_CFIndex UTF16Length(const std::string& value)
{
	std::size_t index = 0;
	darling_windows_CFIndex length = 0;
	while (index < value.size()) {
		const auto first = static_cast<unsigned char>(value[index]);
		std::uint32_t codepoint = 0xfffd;
		std::size_t width = 1;
		if (first < 0x80) { codepoint = first; }
		else if (first >= 0xc2 && first <= 0xdf && index + 1 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x1f) << 6) |
				(static_cast<unsigned char>(value[index + 1]) & 0x3f); width = 2;
		} else if (first >= 0xe0 && first <= 0xef && index + 2 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 2]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x0f) << 12) |
				((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 2]) & 0x3f); width = 3;
		} else if (first >= 0xf0 && first <= 0xf4 && index + 3 < value.size() &&
			(static_cast<unsigned char>(value[index + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 2]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(value[index + 3]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x07) << 18) |
				((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(value[index + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 3]) & 0x3f); width = 4;
		}
		length += codepoint > 0xffff ? 2 : 1;
		index += width;
	}
	return length;
}
std::size_t UTF8OffsetForUTF16(const std::string& value, darling_windows_CFIndex target)
{
	if (target < 0) return std::string::npos;
	std::size_t index = 0;
	darling_windows_CFIndex units = 0;
	while (index < value.size() && units < target) {
		const auto first = static_cast<unsigned char>(value[index]);
		std::size_t width = 1;
		std::uint32_t codepoint = 0xfffd;
		if (first < 0x80) codepoint = first;
		else if (first >= 0xc2 && first <= 0xdf && index + 1 < value.size()) {
			codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(value[index + 1]) & 0x3f); width = 2;
		} else if (first >= 0xe0 && first <= 0xef && index + 2 < value.size()) {
			codepoint = ((first & 0x0f) << 12) | ((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 2]) & 0x3f); width = 3;
		} else if (first >= 0xf0 && first <= 0xf4 && index + 3 < value.size()) {
			codepoint = ((first & 0x07) << 18) | ((static_cast<unsigned char>(value[index + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(value[index + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(value[index + 3]) & 0x3f); width = 4;
		}
		const auto consumed = codepoint > 0xffff ? 2 : 1;
		if (units + consumed > target) return std::string::npos;
		units += consumed;
		index += width;
	}
	return units == target ? index : std::string::npos;
}
const void* RetainArrayValue(Object* array, const void* value);
void ReleaseArrayValue(Object* array, const void* value);
bool EqualArrayValues(const Object* array, const void* left, const void* right);
const void* RetainDictionaryKey(Object* dictionary, const void* key);
const void* RetainDictionaryValue(Object* dictionary, const void* value);
void ReleaseDictionaryKey(Object* dictionary, const void* key);
void ReleaseDictionaryValue(Object* dictionary, const void* value);
bool EqualDictionaryKeys(const Object* dictionary, const void* left, const void* right);
const void* RetainSetValue(Object* set, const void* value);
void ReleaseSetValue(Object* set, const void* value);
bool EqualSetValues(const Object* set, const void* left, const void* right);
void ReleaseOwned(Object* object)
{
	if (object == nullptr || object->immortal) return;
	if (object->references.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
	if (object->object_backing_allocator != nullptr) {
		auto* backing = AsCFAllocator(object->object_backing_allocator);
		object->object_backing_allocator = nullptr;
		auto* cached_url_string = object->kind == Kind::URL
			? const_cast<Object*>(static_cast<const Object*>(object->url_string_cache)) : nullptr;
		object->url_string_cache = nullptr;
		object->~Object();
		if (backing != nullptr) {
			CFAllocatorDeallocate(backing, object);
			ReleaseOwned(backing);
		} else ::operator delete(object);
		ReleaseOwned(cached_url_string);
		return;
	}
	if (object->kind == Kind::Allocator && object->allocator_backing_allocator != nullptr) {
		auto* backing = AsCFAllocator(object->allocator_backing_allocator);
		object->allocator_backing_allocator = nullptr;
		object->~Object();
		if (backing != nullptr) {
			CFAllocatorDeallocate(backing, object);
			ReleaseOwned(backing);
		} else ::operator delete(object);
		return;
	}
	if (object->kind == Kind::Allocator && object->allocator_storage_uses_context) {
		const auto deallocate = object->allocator_context.deallocate;
		const auto release = object->allocator_context.release;
		const auto info = object->allocator_info;
		object->allocator_context_release_deferred = true;
		object->~Object();
		if (deallocate != nullptr) {
			try { deallocate(object, const_cast<void*>(info)); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		if (info != nullptr && release != nullptr) {
			try { release(info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		return;
	}
	delete object;
}
struct ThreadCFAllocator final {
	Object* value{};
	~ThreadCFAllocator() { ReleaseOwned(value); }
};
thread_local ThreadCFAllocator thread_cf_allocator;
void* DARLING_CF_GUEST_ABI CFSystemAllocatorAllocate(
	darling_windows_CFIndex size, std::uint64_t, void*)
{
	return size <= 0 ? nullptr : darling_windows_malloc(static_cast<std::size_t>(size));
}
void* DARLING_CF_GUEST_ABI CFSystemAllocatorReallocate(
	void* address, darling_windows_CFIndex size, std::uint64_t, void*)
{
	if (size < 0) return nullptr;
	if (size == 0) { darling_windows_free(address); return nullptr; }
	return darling_windows_realloc(address, static_cast<std::size_t>(size));
}
void DARLING_CF_GUEST_ABI CFSystemAllocatorDeallocate(void* address, void*)
{
	darling_windows_free(address);
}
darling_windows_CFIndex DARLING_CF_GUEST_ABI CFSystemAllocatorPreferredSize(
	darling_windows_CFIndex size, std::uint64_t, void*)
{
	return (std::max)(size, darling_windows_CFIndex{0});
}
Object* SystemCFAllocator()
{
	static Object allocator{};
	static const bool initialized = [&] {
		allocator.kind = Kind::Allocator;
		allocator.immortal = true;
		allocator.allocator_context.allocate = &CFSystemAllocatorAllocate;
		allocator.allocator_context.reallocate = &CFSystemAllocatorReallocate;
		allocator.allocator_context.deallocate = &CFSystemAllocatorDeallocate;
		allocator.allocator_context.preferred_size = &CFSystemAllocatorPreferredSize;
		return true;
	}();
	(void)initialized;
	return &allocator;
}
Object::~Object()
{
	if (kind == Kind::RunLoopSource && run_loop_source_cfmachport != nullptr) {
		std::lock_guard port_lock(run_loop_source_cfmachport->mach_port_mutex);
		std::erase(run_loop_source_cfmachport->mach_port_sources, this);
	}
	if (kind == Kind::RunLoopSource && run_loop_source_port != 0) {
		std::lock_guard registry_lock(run_loop_source1_port_registry_mutex);
		const auto found = run_loop_source1_port_registry.find(run_loop_source_port);
		if (found != run_loop_source1_port_registry.end() && found->second == this)
			run_loop_source1_port_registry.erase(found);
	}
	if (kind == Kind::RunLoopSource && run_loop_source_info != nullptr) {
		const auto release = run_loop_source_is_v1 ? run_loop_source_context1.release :
			run_loop_source_context.release;
		try { if (release != nullptr) release(run_loop_source_info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (kind == Kind::RunLoopTimer && run_loop_timer_context.release != nullptr &&
		run_loop_timer_info != nullptr) {
		try { run_loop_timer_context.release(run_loop_timer_info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (kind == Kind::RunLoopObserver && run_loop_observer_context.release != nullptr &&
		run_loop_observer_info != nullptr) {
		try { run_loop_observer_context.release(run_loop_observer_info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (kind == Kind::MachPort) {
		{
			std::lock_guard registry_lock(cfmachport_registry_mutex);
			const auto found = cfmachport_registry.find(mach_port_name);
			if (found != cfmachport_registry.end() && found->second == this)
				cfmachport_registry.erase(found);
		}
		if (mach_port_owns_name && mach_port_name != 0)
			(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(), mach_port_name);
		if (!mach_port_context_released && mach_port_info != nullptr &&
			mach_port_context.release != nullptr) {
			try { mach_port_context.release(mach_port_info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
	}
	if (kind == Kind::Allocator && !allocator_context_release_deferred && allocator_info != nullptr &&
		allocator_context.release != nullptr) {
		try { allocator_context.release(allocator_info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (opaque_destroy != nullptr) {
		try { opaque_destroy(opaque_payload); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
	if (is_direct_callback_provider && direct_release_info != nullptr) {
		try { direct_release_info(direct_info); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
	if (is_sequential_data_provider && sequential_release_info != nullptr) {
		try { sequential_release_info(sequential_info); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
	if (external_data_release != nullptr) {
		try { external_data_release(external_data_info, external_data, external_data_size); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
	if (kind == Kind::Set) {
		for (const auto& entry : set_entries) ReleaseSetValue(this, entry.second);
	} else {
		for (const auto* value : array) ReleaseArrayValue(this, value);
	}
	ReleaseOwned(As(bundle_info_dictionary));
	for (const auto* value : bundle_localized_info_values) ReleaseOwned(As(value));
	if (bundle_executable_handle != nullptr && bundle_release_executable != nullptr) {
		try { bundle_release_executable(bundle_executable_handle); }
		catch (...) { /* Loader cleanup callbacks must not escape CFRelease. */ }
	}
	for (const auto& entry : dictionary) {
		ReleaseDictionaryKey(this, entry.first);
		ReleaseDictionaryValue(this, entry.second);
	}
}
void RetainOwned(const void* value)
{
	auto* object = As(value);
	if (object != nullptr && !object->immortal)
		object->references.fetch_add(1, std::memory_order_relaxed);
}
const void* RetainArrayValue(Object* array, const void* value)
{
	if (array == nullptr || !array->has_array_callback_table) {
		RetainOwned(value);
		return value;
	}
	if (array->array_callbacks.retain == nullptr) return value;
	try { return array->array_callbacks.retain(array->array_allocator, value); }
	catch (...) { return nullptr; }
}
void ReleaseArrayValue(Object* array, const void* value)
{
	if (array == nullptr || !array->has_array_callback_table) {
		ReleaseOwned(As(value));
		return;
	}
	if (array->array_callbacks.release != nullptr) {
		try { array->array_callbacks.release(array->array_allocator, value); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
}
bool EqualArrayValues(const Object* array, const void* left, const void* right)
{
	if (array != nullptr && array->has_array_callback_table) {
		if (array->array_callbacks.equal == nullptr) return left == right;
		try { return array->array_callbacks.equal(left, right) != 0; }
		catch (...) { return false; }
	}
	return darling_windows_CFEqual(left, right);
}
const void* RetainDictionaryKey(Object* dictionary, const void* key)
{
	if (dictionary == nullptr || !dictionary->has_dictionary_callback_tables) {
		RetainOwned(key);
		return key;
	}
	if (dictionary->dictionary_key_callbacks.retain == nullptr) return key;
	try { return dictionary->dictionary_key_callbacks.retain(dictionary->dictionary_allocator, key); }
	catch (...) { return nullptr; }
}
const void* RetainDictionaryValue(Object* dictionary, const void* value)
{
	if (dictionary == nullptr || !dictionary->has_dictionary_callback_tables) {
		RetainOwned(value);
		return value;
	}
	if (dictionary->dictionary_value_callbacks.retain == nullptr) return value;
	try { return dictionary->dictionary_value_callbacks.retain(dictionary->dictionary_allocator, value); }
	catch (...) { return nullptr; }
}
void ReleaseDictionaryKey(Object* dictionary, const void* key)
{
	if (dictionary == nullptr || !dictionary->has_dictionary_callback_tables) {
		ReleaseOwned(As(key));
		return;
	}
	if (dictionary->dictionary_key_callbacks.release != nullptr) {
		try { dictionary->dictionary_key_callbacks.release(dictionary->dictionary_allocator, key); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
}
void ReleaseDictionaryValue(Object* dictionary, const void* value)
{
	if (dictionary == nullptr || !dictionary->has_dictionary_callback_tables) {
		ReleaseOwned(As(value));
		return;
	}
	if (dictionary->dictionary_value_callbacks.release != nullptr) {
		try { dictionary->dictionary_value_callbacks.release(dictionary->dictionary_allocator, value); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
}
bool EqualDictionaryKeys(const Object* dictionary, const void* left, const void* right)
{
	if (dictionary != nullptr && dictionary->has_dictionary_callback_tables) {
		if (dictionary->dictionary_key_callbacks.equal == nullptr) return left == right;
		try { return dictionary->dictionary_key_callbacks.equal(left, right) != 0; }
		catch (...) { return false; }
	}
	return darling_windows_CFEqual(left, right);
}
const void* RetainSetValue(Object* set, const void* value)
{
	if (set == nullptr || !set->has_set_callback_table) {
		RetainOwned(value);
		return value;
	}
	if (set->set_callbacks.retain == nullptr) return value;
	try { return set->set_callbacks.retain(set->set_allocator, value); }
	catch (...) { return nullptr; }
}
void ReleaseSetValue(Object* set, const void* value)
{
	if (set == nullptr || !set->has_set_callback_table) {
		ReleaseOwned(As(value));
		return;
	}
	if (set->set_callbacks.release != nullptr) {
		try { set->set_callbacks.release(set->set_allocator, value); }
		catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
	}
}
bool EqualSetValues(const Object* set, const void* left, const void* right)
{
	if (set != nullptr && set->has_set_callback_table) {
		if (set->set_callbacks.equal == nullptr) return left == right;
		try { return set->set_callbacks.equal(left, right) != 0; }
		catch (...) { return false; }
	}
	return darling_windows_CFEqual(left, right);
}
std::uintptr_t HashSetValue(const Object* set, const void* value)
{
	if (set != nullptr && set->has_set_callback_table) {
		if (set->set_callbacks.hash == nullptr)
			return reinterpret_cast<std::uintptr_t>(value);
		try { return set->set_callbacks.hash(value); }
		catch (...) { return 0; }
	}
	return static_cast<std::uintptr_t>(darling_windows_CFHash(value));
}
auto FindSetEntry(Object* set, const void* candidate, std::uintptr_t hash)
{
	auto [first, last] = set->set_entries.equal_range(hash);
	for (; first != last; ++first)
		if (EqualSetValues(set, first->second, candidate)) return first;
	return set->set_entries.end();
}
auto FindSetEntry(const Object* set, const void* candidate, std::uintptr_t hash)
{
	auto [first, last] = set->set_entries.equal_range(hash);
	for (; first != last; ++first)
		if (EqualSetValues(set, first->second, candidate)) return first;
	return set->set_entries.end();
}
std::string EscapeXML(const std::string& value)
{
	std::string escaped;
	for (const char character : value) {
		switch (character) {
		case '&': escaped += "&amp;"; break;
		case '<': escaped += "&lt;"; break;
		case '>': escaped += "&gt;"; break;
		case '\"': escaped += "&quot;"; break;
		case '\'': escaped += "&apos;"; break;
		default: escaped += character; break;
		}
	}
	return escaped;
}
std::string Base64(const std::vector<unsigned char>& bytes)
{
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	for (std::size_t index = 0; index < bytes.size(); index += 3) {
		const std::size_t remaining = bytes.size() - index;
		const std::uint32_t value = (static_cast<std::uint32_t>(bytes[index]) << 16) |
			(remaining > 1 ? static_cast<std::uint32_t>(bytes[index + 1]) << 8 : 0) |
			(remaining > 2 ? bytes[index + 2] : 0);
		result += alphabet[(value >> 18) & 0x3f];
		result += alphabet[(value >> 12) & 0x3f];
		result += remaining > 1 ? alphabet[(value >> 6) & 0x3f] : '=';
		result += remaining > 2 ? alphabet[value & 0x3f] : '=';
	}
	return result;
}
std::vector<unsigned char> DecodeBase64(const std::string& input)
{
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector<unsigned char> output;
	int value = 0;
	int bits = -8;
	for (const unsigned char character : input) {
		if (character == '=') break;
		const char* found = std::strchr(alphabet, character);
		if (found == nullptr) continue;
		value = (value << 6) | static_cast<int>(found - alphabet);
		bits += 6;
		if (bits >= 0) {
			output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return output;
}
std::string PercentDecode(const std::string& input)
{
	std::string output;
	for (std::size_t index = 0; index < input.size(); ++index) {
		if (input[index] == '%' && index + 2 < input.size()) {
			const auto hex = [](char value) -> int {
				if (value >= '0' && value <= '9') return value - '0';
				if (value >= 'a' && value <= 'f') return value - 'a' + 10;
				if (value >= 'A' && value <= 'F') return value - 'A' + 10;
				return -1;
			};
			const auto high = hex(input[index + 1]);
			const auto low = hex(input[index + 2]);
			if (high >= 0 && low >= 0) {
				output.push_back(static_cast<char>((high << 4) | low));
				index += 2;
				continue;
			}
		}
		output.push_back(input[index]);
	}
	return output;
}
std::string PercentDecodeLeaving(const std::string& input, const std::string* leave_escaped)
{
	if (leave_escaped == nullptr) return input;
	std::string output;
	for (std::size_t index = 0; index < input.size(); ++index) {
		if (input[index] == '%' && index + 2 < input.size()) {
			const auto hex = [](char value) -> int {
				if (value >= '0' && value <= '9') return value - '0';
				if (value >= 'a' && value <= 'f') return value - 'a' + 10;
				if (value >= 'A' && value <= 'F') return value - 'A' + 10;
				return -1;
			};
			const auto high = hex(input[index + 1]);
			const auto low = hex(input[index + 2]);
			if (high >= 0 && low >= 0) {
				const auto decoded = static_cast<char>((high << 4) | low);
				if (leave_escaped->find(decoded) != std::string::npos)
					output.append(input, index, 3);
				else output.push_back(decoded);
				index += 2;
				continue;
			}
		}
		output.push_back(input[index]);
	}
	return output;
}
bool IsWellFormedUTF8(const std::string& value)
{
	for (std::size_t index = 0; index < value.size();) {
		const auto first = static_cast<unsigned char>(value[index]);
		if (first <= 0x7f) { ++index; continue; }
		std::size_t continuation_count = 0;
		unsigned char second_min = 0x80;
		unsigned char second_max = 0xbf;
		if (first >= 0xc2 && first <= 0xdf) continuation_count = 1;
		else if (first >= 0xe0 && first <= 0xef) {
			continuation_count = 2;
			if (first == 0xe0) second_min = 0xa0;
			if (first == 0xed) second_max = 0x9f;
		} else if (first >= 0xf0 && first <= 0xf4) {
			continuation_count = 3;
			if (first == 0xf0) second_min = 0x90;
			if (first == 0xf4) second_max = 0x8f;
		} else return false;
		if (index + continuation_count >= value.size()) return false;
		const auto second = static_cast<unsigned char>(value[index + 1]);
		if (second < second_min || second > second_max) return false;
		for (std::size_t offset = 2; offset <= continuation_count; ++offset) {
			const auto next = static_cast<unsigned char>(value[index + offset]);
			if (next < 0x80 || next > 0xbf) return false;
		}
		index += continuation_count + 1;
	}
	return true;
}
std::string PercentEncodeURLString(const std::string& input,
	const std::string* characters_to_leave_unescaped,
	const std::string* legal_characters_to_escape)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string output;
	for (std::size_t index = 0; index < input.size();) {
		const auto first = static_cast<unsigned char>(input[index]);
		const auto length = first <= 0x7f ? std::size_t{1} :
			(first <= 0xdf ? std::size_t{2} : (first <= 0xef ? std::size_t{3} : std::size_t{4}));
		const auto character = input.substr(index, length);
		const bool leave = characters_to_leave_unescaped != nullptr &&
			characters_to_leave_unescaped->find(character) != std::string::npos;
		const bool force_escape = legal_characters_to_escape != nullptr &&
			legal_characters_to_escape->find(character) != std::string::npos;
		const bool alphanumeric = (first >= 'A' && first <= 'Z') ||
			(first >= 'a' && first <= 'z') || (first >= '0' && first <= '9');
		const bool mark = first == '-' || first == '_' || first == '.' || first == '!' ||
			first == '~' || first == '*' || first == '\'' || first == '(' || first == ')';
		const bool reserved = first == ';' || first == '/' || first == '?' || first == ':' ||
			first == '@' || first == '&' || first == '=' || first == '+' || first == '$' ||
			first == ',';
		const bool legal = length == 1 && (alphanumeric || mark || reserved);
		if (leave || (legal && !force_escape)) output.append(character);
		else {
			for (const auto byte : character) {
				const auto value = static_cast<unsigned char>(byte);
				output.push_back('%');
				output.push_back(hex[value >> 4]);
				output.push_back(hex[value & 0x0f]);
			}
		}
		index += length;
	}
	return output;
}
struct URLPathParts final {
	std::size_t path_start{};
	std::size_t path_end{};
	std::size_t parameter_start{std::string::npos};
	std::size_t resource_end{};
	bool absolute{};
};
URLPathParts ParseURLPathParts(const std::string& url)
{
	URLPathParts parts{};
	parts.resource_end = url.find_first_of("?#");
	if (parts.resource_end == std::string::npos) parts.resource_end = url.size();
	parts.path_end = parts.resource_end;
	const auto colon = url.find(':');
	const auto first_separator = url.find_first_of("/?#");
	const bool windows_drive = colon == 1 && url.size() > 2 &&
		((url[0] >= 'A' && url[0] <= 'Z') || (url[0] >= 'a' && url[0] <= 'z')) &&
		(url[2] == '/' || url[2] == '\\');
	if (colon != std::string::npos && !windows_drive &&
		(first_separator == std::string::npos || colon < first_separator)) {
		parts.path_start = colon + 1;
		if (url.compare(parts.path_start, 2, "//") == 0) {
			const auto authority_end = url.find_first_of("/?#", parts.path_start + 2);
			if (authority_end == std::string::npos || url[authority_end] != '/')
				parts.path_start = parts.resource_end;
			else parts.path_start = authority_end;
		}
	}
	parts.absolute = parts.path_start < parts.path_end && url[parts.path_start] == '/';
	if (windows_drive) parts.absolute = true;
	const auto slash = url.find_last_of("/\\", parts.path_end == 0 ? 0 : parts.path_end - 1);
	const auto semicolon = url.rfind(';', parts.path_end == 0 ? 0 : parts.path_end - 1);
	if (semicolon != std::string::npos && semicolon >= parts.path_start &&
		(slash == std::string::npos || semicolon > slash)) {
		parts.parameter_start = semicolon + 1;
		parts.path_end = semicolon;
	}
	return parts;
}
std::string DecodeUTF16BE(const unsigned char* bytes, std::size_t units)
{
	std::string output;
	for (std::size_t index = 0; index < units; ++index) {
		std::uint32_t code = static_cast<std::uint16_t>((bytes[index * 2] << 8) | bytes[index * 2 + 1]);
		if (code >= 0xd800 && code <= 0xdbff && index + 1 < units) {
			const auto low = static_cast<std::uint16_t>((bytes[(index + 1) * 2] << 8) | bytes[(index + 1) * 2 + 1]);
			if (low >= 0xdc00 && low <= 0xdfff) {
				code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00); ++index;
			}
		}
		if (code < 0x80) output.push_back(static_cast<char>(code));
		else if (code < 0x800) {
			output.push_back(static_cast<char>(0xc0 | (code >> 6)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else if (code < 0x10000) {
			output.push_back(static_cast<char>(0xe0 | (code >> 12)));
			output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else {
			output.push_back(static_cast<char>(0xf0 | (code >> 18)));
			output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		}
	}
	return output;
}
std::string PropertyListDate(double absolute_time)
{
	constexpr std::int64_t apple_epoch_unix = 978307200;
	const auto whole_seconds = static_cast<std::int64_t>(std::floor(absolute_time));
	const auto fraction = absolute_time - static_cast<double>(whole_seconds);
	auto unix_seconds = apple_epoch_unix + whole_seconds;
	auto milliseconds = static_cast<int>(std::llround(fraction * 1000.0));
	if (milliseconds >= 1000) { ++unix_seconds; milliseconds -= 1000; }
	const std::time_t timestamp = static_cast<std::time_t>(unix_seconds);
	const std::tm* utc = std::gmtime(&timestamp);
	if (utc == nullptr) return {};
	std::ostringstream result;
	result << std::put_time(utc, "%Y-%m-%dT%H:%M:%S");
	if (milliseconds != 0) result << '.' << std::setw(3) << std::setfill('0') << milliseconds;
	result << 'Z';
	return result.str();
}
void AppendXML(const Object* object, std::ostringstream& output)
{
	if (object == nullptr) { output << "<null/>"; return; }
	switch (object->kind) {
	case Kind::String: output << "<string>" << EscapeXML(object->string) << "</string>"; break;
	case Kind::URL: output << "<string>" << EscapeXML(object->string) << "</string>"; break;
	case Kind::Number: output << "<integer>" << object->number << "</integer>"; break;
	case Kind::Real: output << "<real>" << object->real_number << "</real>"; break;
	case Kind::Data: output << "<data>" << Base64(object->data) << "</data>"; break;
	case Kind::Date: output << "<date>" << PropertyListDate(object->date) << "</date>"; break;
	case Kind::Boolean: output << (object->boolean ? "<true/>" : "<false/>"); break;
	case Kind::Null: output << "<null/>"; break;
	case Kind::Array:
		output << "<array>";
		for (const auto* entry : object->array) AppendXML(static_cast<const Object*>(entry), output);
		output << "</array>";
		break;
	case Kind::Dictionary:
		output << "<dict>";
		for (const auto& entry : object->dictionary) {
			const auto* key = static_cast<const Object*>(entry.first);
			output << "<key>" << (key == nullptr ? "" : EscapeXML(key->string)) << "</key>";
			AppendXML(static_cast<const Object*>(entry.second), output);
		}
		output << "</dict>";
		break;
	default: output << "<unsupported/>"; break;
	}
}
std::string UnescapeXML(std::string value)
{
	const std::pair<const char*, const char*> entities[] = {
		{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
		{"&apos;", "'"}, {"&amp;", "&"}};
	for (const auto& entity : entities) {
		std::size_t position = 0;
		while ((position = value.find(entity.first, position)) != std::string::npos) {
			value.replace(position, std::strlen(entity.first), entity.second);
			position += std::strlen(entity.second);
		}
	}
	return value;
}
Object* ParsePropertyValue(const std::string& xml, std::size_t& position)
{
	while (position < xml.size() && xml[position] != '<') ++position;
	if (position >= xml.size()) return nullptr;
	const auto close = xml.find('>', position);
	if (close == std::string::npos) return nullptr;
	const std::string tag = xml.substr(position + 1, close - position - 1);
	position = close + 1;
	if (tag == "true/") { auto* object = new Object{}; object->kind = Kind::Boolean; object->boolean = true; return object; }
	if (tag == "false/") { auto* object = new Object{}; object->kind = Kind::Boolean; return object; }
	if (tag == "null/") { auto* object = new Object{}; object->kind = Kind::Null; return object; }
	if (tag == "array") {
		auto* object = new Object{}; object->kind = Kind::Array;
		while (position < xml.size() && xml.compare(position, 8, "</array>") != 0) {
			auto* child = ParsePropertyValue(xml, position);
			if (child == nullptr) { delete object; return nullptr; }
			object->array.push_back(child);
		}
		if (xml.compare(position, 8, "</array>") != 0) { delete object; return nullptr; }
		position += 8; return object;
	}
	if (tag == "dict") {
		auto* object = new Object{}; object->kind = Kind::Dictionary;
		while (position < xml.size() && xml.compare(position, 7, "</dict>") != 0) {
			while (position < xml.size() && xml[position] != '<') ++position;
			if (xml.compare(position, 5, "<key>") != 0) { delete object; return nullptr; }
			position += 5;
			const auto key_end = xml.find("</key>", position);
			if (key_end == std::string::npos) { delete object; return nullptr; }
			auto* key = new Object{}; key->kind = Kind::String;
			key->string = UnescapeXML(xml.substr(position, key_end - position));
			position = key_end + 6;
			auto* child = ParsePropertyValue(xml, position);
			if (child == nullptr) { delete key; delete object; return nullptr; }
			object->dictionary.emplace_back(key, child);
		}
		if (xml.compare(position, 7, "</dict>") != 0) { delete object; return nullptr; }
		position += 7; return object;
	}
	const std::string end_tag = "</" + tag + ">";
	const auto end = xml.find(end_tag, position);
	if (end == std::string::npos) return nullptr;
	const auto text = UnescapeXML(xml.substr(position, end - position));
	position = end + end_tag.size();
	auto* object = new Object{};
	try {
		if (tag == "string") { object->kind = Kind::String; object->string = text; }
		else if (tag == "integer") { object->kind = Kind::Number; object->number = std::stoll(text); }
		else if (tag == "real") { object->kind = Kind::Real; object->real_number = std::stod(text); }
		else if (tag == "data") { object->kind = Kind::Data; object->data = DecodeBase64(text); }
		else if (tag == "date") {
			std::tm calendar{};
			std::istringstream input(text);
			input >> std::get_time(&calendar, "%Y-%m-%dT%H:%M:%S");
			if (input.fail()) { delete object; return nullptr; }
			auto unix_seconds = static_cast<std::int64_t>(_mkgmtime(&calendar));
			if (text.size() > 19 && text[19] != 'Z') {
				const auto sign = text[19] == '-' ? -1 : text[19] == '+' ? 1 : 0;
				if (sign != 0 && text.size() >= 25 && text[22] == ':') {
					const auto hours = std::stoi(text.substr(20, 2));
					const auto minutes = std::stoi(text.substr(23, 2));
					unix_seconds -= sign * (hours * 3600 + minutes * 60);
				}
			}
			object->kind = Kind::Date;
			double fraction = 0.0;
			const auto decimal = text.find('.', 19);
			if (decimal != std::string::npos) {
				const auto end = text.find('Z', decimal);
				const auto digits = text.substr(decimal + 1, end == std::string::npos ? std::string::npos : end - decimal - 1);
				if (!digits.empty()) fraction = std::stod("0." + digits);
			}
			object->date = static_cast<double>(unix_seconds - 978307200) + fraction;
		}
		else { delete object; return nullptr; }
	} catch (...) { delete object; return nullptr; }
	return object;
}
struct BinaryPlistContext {
	const unsigned char* bytes;
	std::size_t length;
	std::size_t offset_size;
	std::size_t reference_size;
	std::size_t object_count;
	std::size_t object_table;
	std::size_t offsets_table;
};
std::uint64_t ReadBigEndian(const unsigned char* bytes, std::size_t count)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < count; ++index) value = (value << 8) | bytes[index];
	return value;
}
bool ReadBinaryLength(const BinaryPlistContext& context, std::size_t offset, unsigned char info,
	std::size_t& length, std::size_t& payload)
{
	if (info < 0x0f) { length = info; payload = offset + 1; return payload <= context.length; }
	if (offset + 2 > context.length || (context.bytes[offset + 1] >> 4) != 0x1) return false;
	const auto width = std::size_t{1} << (context.bytes[offset + 1] & 0x0f);
	if (width == 0 || width > sizeof(std::size_t) || offset + 2 + width > context.length) return false;
	const auto encoded = ReadBigEndian(context.bytes + offset + 2, width);
	if (encoded > context.length) return false;
	length = static_cast<std::size_t>(encoded);
	payload = offset + 2 + width;
	return payload <= context.length;
}
Object* ParseBinaryObject(const BinaryPlistContext& context, std::size_t index,
	std::vector<bool>& active)
{
	if (index >= context.object_count || active[index]) return nullptr;
	const auto offset_position = context.offsets_table + index * context.offset_size;
	if (offset_position + context.offset_size > context.length) return nullptr;
	const auto offset = ReadBigEndian(context.bytes + offset_position, context.offset_size);
	if (offset >= context.length) return nullptr;
	const auto descriptor = context.bytes[offset];
	const auto type = descriptor >> 4;
	const auto info = descriptor & 0x0f;
	if (type == 0x0) {
		auto* object = new Object{};
		if (info == 0x0) object->kind = Kind::Null;
		else if (info == 0x8 || info == 0x9) { object->kind = Kind::Boolean; object->boolean = info == 0x9; }
		else { delete object; return nullptr; }
		return object;
	}
	if (type == 0x1) {
		const auto width = std::size_t{1} << info;
		if (width == 0 || width > 8 || offset + 1 + width > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::Number;
		object->number = static_cast<std::int64_t>(ReadBigEndian(context.bytes + offset + 1, width));
		return object;
	}
	if (type == 0x2) {
		const auto width = std::size_t{1} << info;
		if ((width != 4 && width != 8) || offset + 1 + width > context.length) return nullptr;
		const auto bits = ReadBigEndian(context.bytes + offset + 1, width);
		auto* object = new Object{}; object->kind = Kind::Real;
		if (width == 4) {
			const auto narrow_bits = static_cast<std::uint32_t>(bits);
			float narrow = 0;
			std::memcpy(&narrow, &narrow_bits, sizeof(narrow));
			object->real_number = narrow;
		} else {
			std::uint64_t host_bits = bits;
			double value = 0;
			std::memcpy(&value, &host_bits, sizeof(value));
			object->real_number = value;
		}
		return object;
	}
	if (type == 0x3 && offset + 9 <= context.length) {
		// Binary plists store an IEEE-754 double in big-endian byte order.
		// ReadBigEndian yields the host integer bit pattern which can then be
		// copied into the little-endian Windows double representation.
		const std::uint64_t bits = ReadBigEndian(context.bytes + offset + 1, 8);
		double value = 0;
		std::memcpy(&value, &bits, sizeof(value));
		auto* object = new Object{}; object->kind = Kind::Date; object->date = value;
		return object;
	}
	if (type == 0x8) {
		const auto width = static_cast<std::size_t>(info) + 1;
		if (width > 8 || offset + 1 + width > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::PropertyListUID;
		object->property_list_uid = ReadBigEndian(context.bytes + offset + 1, width);
		return object;
	}
	if (type == 0x4) {
		std::size_t length = 0;
		std::size_t payload = 0;
		if (!ReadBinaryLength(context, offset, info, length, payload) || payload + length > context.length) return nullptr;
		auto* object = new Object{}; object->kind = Kind::Data;
		object->data.assign(context.bytes + payload, context.bytes + payload + length);
		return object;
	}
	std::size_t length = 0;
	std::size_t payload = 0;
	if (type == 0x5 && ReadBinaryLength(context, offset, info, length, payload) &&
		payload + length <= context.length) {
		auto* object = new Object{}; object->kind = Kind::String;
		object->string.assign(reinterpret_cast<const char*>(context.bytes + payload), length);
		return object;
	}
	if (type == 0x6 && ReadBinaryLength(context, offset, info, length, payload) &&
		length <= (context.length - payload) / 2) {
		auto* object = new Object{}; object->kind = Kind::String;
		object->string = DecodeUTF16BE(context.bytes + payload, length);
		return object;
	}
	if (type == 0xa) {
		std::size_t count = 0;
		std::size_t refs_start = 0;
		if (!ReadBinaryLength(context, offset, info, count, refs_start) ||
			count > (context.length - refs_start) / context.reference_size) return nullptr;
		active[index] = true;
		auto* object = new Object{}; object->kind = Kind::Array;
		for (std::size_t child = 0; child < count; ++child) {
			const auto ref = ReadBigEndian(context.bytes + refs_start + child * context.reference_size, context.reference_size);
			auto* value = ParseBinaryObject(context, static_cast<std::size_t>(ref), active);
			if (value == nullptr) { delete object; active[index] = false; return nullptr; }
			object->array.push_back(value);
		}
		active[index] = false;
		return object;
	}
	if (type == 0xd) {
		std::size_t count = 0;
		std::size_t refs_start = 0;
		if (!ReadBinaryLength(context, offset, info, count, refs_start) || count >
			(context.length - refs_start) / (context.reference_size * 2)) return nullptr;
		const auto refs_count = count * 2;
		if (refs_start + refs_count * context.reference_size > context.length) return nullptr;
		active[index] = true;
		auto* object = new Object{}; object->kind = Kind::Dictionary;
		for (std::size_t entry = 0; entry < count; ++entry) {
			const auto key_ref = ReadBigEndian(context.bytes + refs_start + entry * context.reference_size, context.reference_size);
			const auto value_ref = ReadBigEndian(context.bytes + refs_start + (entry + count) * context.reference_size, context.reference_size);
			auto* key = ParseBinaryObject(context, static_cast<std::size_t>(key_ref), active);
			auto* value = ParseBinaryObject(context, static_cast<std::size_t>(value_ref), active);
			if (key == nullptr || value == nullptr) { delete key; delete value; delete object; active[index] = false; return nullptr; }
			object->dictionary.emplace_back(key, value);
		}
		active[index] = false;
		return object;
	}
	return nullptr;
}
struct RunLoopState {
	std::mutex mutex;
	std::condition_variable condition;
	bool running = false;
	bool waiting = false;
	std::thread::id running_thread;
	std::size_t activation_depth = 0;
	bool stopped = false;
	std::string current_mode;
	std::vector<std::string> mode_stack;
	std::vector<std::string> common_modes{"kCFRunLoopDefaultMode"};
	struct Block final {
		darling_windows_CFRunLoopBlock callback{};
		void* context{};
		std::vector<std::string> modes;
		std::uint64_t order{};
	};
	struct Source final {
		Object* object{};
		std::string mode;
	};
	struct Timer final {
		Object* object{};
		std::string mode;
	};
	struct Observer final {
		Object* object{};
		std::string mode;
	};
	std::vector<Block> blocks;
	std::vector<Source> sources;
	std::vector<Timer> timers;
	std::vector<Observer> observers;
	~RunLoopState()
	{
		for (const auto& source : sources) ReleaseOwned(source.object);
		for (const auto& timer : timers) ReleaseOwned(timer.object);
		for (const auto& observer : observers) ReleaseOwned(observer.object);
	}
};
struct RunLoop : Object {
	std::shared_ptr<RunLoopState> state = std::make_shared<RunLoopState>();
	RunLoop()
	{
		kind = Kind::RunLoop;
		immortal = true;
	}
};
extern RunLoop main_run_loop;
struct RunLoopSourceInvocation final {
	Object* source{};
	std::uint64_t generation{};
	std::weak_ptr<RunLoopState> loop_state;
	~RunLoopSourceInvocation() { ReleaseOwned(source); }
};
const void* RunLoopModeObject(const std::string& mode)
{
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	try { object->kind = Kind::String; object->string = mode; }
	catch (...) { delete object; return nullptr; }
	return object;
}
bool DispatchSignaledRunLoopSource(Object* source)
{
	if (source == nullptr || source->kind != Kind::RunLoopSource ||
		source->run_loop_source_is_v1) return false;
	std::vector<std::pair<std::shared_ptr<RunLoopState>, std::vector<std::string>>> targets;
	std::uint64_t generation{};
	{
		std::lock_guard lock(source->run_loop_source_mutex);
		if (!source->run_loop_source_valid || !source->run_loop_source_signaled) return false;
		generation = source->run_loop_source_generation;
		try {
			for (const auto& schedule : source->run_loop_source_schedules) {
				auto state = schedule.state.lock();
				if (state == nullptr) continue;
				auto found = std::find_if(targets.begin(), targets.end(),
					[&](const auto& target) { return target.first == state; });
				if (found == targets.end()) {
					targets.emplace_back(std::move(state), std::vector<std::string>{schedule.mode});
				} else if (std::find(found->second.begin(), found->second.end(), schedule.mode) ==
					found->second.end()) found->second.push_back(schedule.mode);
			}
		} catch (...) { return false; }
	}
	bool queued = false;
	for (auto& target : targets) {
		auto* invocation = new (std::nothrow) RunLoopSourceInvocation{};
		if (invocation == nullptr) continue;
		invocation->source = source;
		invocation->generation = generation;
		invocation->loop_state = target.first;
		RetainOwned(source);
		const auto order = static_cast<std::uint64_t>(source->run_loop_source_order);
		{
			std::lock_guard lock(target.first->mutex);
			try {
				target.first->blocks.push_back({
					[](void* context) {
						std::unique_ptr<RunLoopSourceInvocation> invocation(
							static_cast<RunLoopSourceInvocation*>(context));
						auto state = invocation->loop_state.lock();
						if (state == nullptr) return;
						bool still_scheduled = false;
						{
							std::lock_guard lock(state->mutex);
							still_scheduled = std::any_of(state->sources.begin(), state->sources.end(),
								[&](const RunLoopState::Source& entry) {
									return entry.object == invocation->source;
								});
						}
						if (!still_scheduled) return;
						auto* source = invocation->source;
						DarlingCFRunLoopSourcePerformCallback callback{};
						void* info{};
						{
							std::lock_guard lock(source->run_loop_source_mutex);
							if (!source->run_loop_source_valid || !source->run_loop_source_signaled ||
								source->run_loop_source_generation != invocation->generation) return;
							source->run_loop_source_signaled = false;
							callback = source->run_loop_source_context.perform;
							info = const_cast<void*>(source->run_loop_source_info);
						}
						if (callback != nullptr) {
							try { callback(info); }
							catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
						}
					}, invocation, std::move(target.second), order});
				queued = true;
			} catch (...) { delete invocation; }
		}
		if (queued) {
			target.first->condition.notify_all();
			if (target.first == main_run_loop.state)
				darling_windows_appkit_wake_main_run_loop();
		}
	}
	return queued;
}
// CoreFoundation associates the current run loop with the calling thread.
// Keeping this object thread-local also prevents callbacks posted by one
// thread from being consumed by an unrelated thread's run loop.
thread_local RunLoop current_run_loop;
RunLoop main_run_loop;
bool HasRunLoopBlockForMode(const RunLoopState& state, const std::string& mode)
{
	return std::any_of(state.blocks.begin(), state.blocks.end(),
		[&state, &mode](const RunLoopState::Block& block) {
			return std::any_of(block.modes.begin(), block.modes.end(),
				[&state, &mode](const std::string& scheduled_mode) {
					return scheduled_mode == mode ||
						(scheduled_mode == "kCFRunLoopCommonModes" &&
						std::find(state.common_modes.begin(), state.common_modes.end(), mode) !=
							state.common_modes.end());
				});
		});
}
bool RunLoopModeHasWorkSource(const RunLoopState& state, const std::string& mode,
	const std::shared_ptr<RunLoopState>& state_reference)
{
	if (state_reference == main_run_loop.state || HasRunLoopBlockForMode(state, mode))
		return true;
	for (const auto& binding : state.sources) {
		if (binding.mode != mode) continue;
		std::lock_guard source_lock(binding.object->run_loop_source_mutex);
		if (binding.object->run_loop_source_valid) return true;
	}
	for (const auto& binding : state.timers) {
		if (binding.mode != mode) continue;
		std::lock_guard timer_lock(binding.object->run_loop_timer_mutex);
		if (binding.object->run_loop_timer_valid) return true;
	}
	return false;
}
bool RunLoopBlockMatchesMode(const RunLoopState& state,
	const RunLoopState::Block& block, const std::string& mode)
{
	return std::any_of(block.modes.begin(), block.modes.end(),
		[&state, &mode](const std::string& scheduled_mode) {
			return scheduled_mode == mode ||
				(scheduled_mode == "kCFRunLoopCommonModes" &&
				std::find(state.common_modes.begin(), state.common_modes.end(), mode) !=
					state.common_modes.end());
		});
}
struct NotificationObserver {
	const void* observer;
	darling_windows_CFNotificationCallback callback;
};
std::mutex notification_mutex;
std::unordered_map<std::string, std::vector<NotificationObserver>> notifications;
constexpr const char* notification_wildcard = "\x01wildcard";
}

extern "C" darling_windows_CFRange darling_windows_CFRangeMake(
	darling_windows_CFIndex location, darling_windows_CFIndex length)
{
	return {location, length};
}

extern "C" darling_windows_CFIndex darling_windows_CFRangeGetMax(
	darling_windows_CFRange range)
{
	const auto maximum = (std::numeric_limits<darling_windows_CFIndex>::max)();
	const auto minimum = (std::numeric_limits<darling_windows_CFIndex>::min)();
	if (range.length > 0 && range.location > maximum - range.length)
		return maximum;
	if (range.length < 0 && range.location < minimum - range.length)
		return minimum;
	return range.location + range.length;
}

extern "C" void* darling_windows_CFRetain(const void* value)
{
	RetainOwned(value);
	return const_cast<void*>(value);
}

extern "C" void darling_windows_CFRelease(const void* value)
{
	ReleaseOwned(As(value));
}

extern "C" darling_windows_CFTypeID darling_windows_CFGetTypeID(const void* value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr) return 0;
	return object->kind == Kind::Real ? TypeId(Kind::Number) : TypeId(object->kind);
}

extern "C" darling_windows_CFTypeRef darling_windows_CFImageDestinationCreateOpaque(
	void* payload, darling_windows_CFOpaqueDestroyCallback destroy_payload)
{
	if (payload == nullptr || destroy_payload == nullptr) return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::ImageDestination;
	object->opaque_payload = payload;
	object->opaque_destroy = destroy_payload;
	return object;
}

extern "C" void* darling_windows_CFImageDestinationGetOpaque(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::ImageDestination
		? object->opaque_payload : nullptr;
}

extern "C" darling_windows_CFTypeID darling_windows_CFImageDestinationGetTypeID()
{
	return TypeId(Kind::ImageDestination);
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDataConsumerCreateOpaque(
	void* payload, darling_windows_CFOpaqueDestroyCallback destroy_payload)
{
	if (payload == nullptr || destroy_payload == nullptr) return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::DataConsumer;
	object->opaque_payload = payload;
	object->opaque_destroy = destroy_payload;
	return object;
}

extern "C" void* darling_windows_CFDataConsumerGetOpaque(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::DataConsumer
		? object->opaque_payload : nullptr;
}

extern "C" darling_windows_CFTypeID darling_windows_CFDataConsumerGetTypeID()
{
	return TypeId(Kind::DataConsumer);
}

extern "C" std::uint64_t darling_windows_CFHash(const void* value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr) return 0;
	const auto seed = static_cast<std::uint64_t>(TypeId(object->kind)) * 0x9e3779b97f4a7c15ULL;
	const auto mix = [seed](std::uint64_t hash) {
		hash += seed + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
		return hash;
	};
	switch (object->kind) {
	case Kind::String:
	case Kind::URL: return mix(static_cast<std::uint64_t>(std::hash<std::string>{}(object->string)));
	case Kind::Data: return mix(static_cast<std::uint64_t>(std::hash<std::string_view>{}(
		std::string_view(reinterpret_cast<const char*>(object->data.data()), object->data.size()))));
	case Kind::Number:
	case Kind::Real:
		// CFEqual deliberately treats numerically equal integer and real values as
		// equal, so both representations must share one hash domain.
		return 0x4e554d455249435full + static_cast<std::uint64_t>(std::hash<double>{}(
			object->kind == Kind::Number ? static_cast<double>(object->number) : object->real_number));
	case Kind::PropertyListUID:
		return mix(object->property_list_uid);
	case Kind::Date: return mix(static_cast<std::uint64_t>(std::hash<double>{}(object->date)));
	case Kind::Boolean: return mix(object->boolean ? 1 : 0);
	case Kind::Null: return mix(0);
	case Kind::Array: {
		std::uint64_t hash = 0x4152524159ULL;
		for (const auto* element : object->array)
			hash = hash * 0x100000001b3ULL ^ darling_windows_CFHash(element);
		return mix(hash);
	}
	case Kind::Dictionary: {
		std::uint64_t hash = 0x44494354ULL;
		// Dictionary equality is order-independent, so combine entries
		// commutatively rather than hashing vector order.
		for (const auto& [key, element] : object->dictionary)
			hash ^= darling_windows_CFHash(key) +
				0x9e3779b97f4a7c15ULL + (darling_windows_CFHash(element) << 6) +
				(darling_windows_CFHash(element) >> 2);
		return mix(hash);
	}
	case Kind::Set: {
		std::uint64_t hash = 0x534554ULL;
		for (const auto& entry : object->set_entries)
			hash ^= HashSetValue(object, entry.second);
		return mix(hash);
	}
	default: return mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value)));
	}
}

extern "C" bool darling_windows_CFEqual(const void* left, const void* right)
{
	if (left == right) return true;
	if (left == nullptr || right == nullptr) return false;
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	const bool lhs_number = lhs->kind == Kind::Number || lhs->kind == Kind::Real;
	const bool rhs_number = rhs->kind == Kind::Number || rhs->kind == Kind::Real;
	if (lhs_number && rhs_number) {
		const auto left_value = lhs->kind == Kind::Number ? static_cast<long double>(lhs->number) : lhs->real_number;
		const auto right_value = rhs->kind == Kind::Number ? static_cast<long double>(rhs->number) : rhs->real_number;
		return left_value == right_value;
	}
	if (lhs->kind != rhs->kind) return false;
	switch (lhs->kind) {
	case Kind::String:
	case Kind::URL: return lhs->string == rhs->string;
	case Kind::Data: return lhs->data == rhs->data;
	case Kind::Number: return lhs->number == rhs->number;
	case Kind::Real: return lhs->real_number == rhs->real_number;
	case Kind::PropertyListUID:
		return lhs->property_list_uid == rhs->property_list_uid;
	case Kind::Date: return lhs->date == rhs->date;
	case Kind::Boolean: return lhs->boolean == rhs->boolean;
	case Kind::Null: return true;
	case Kind::Array:
		if (lhs->array.size() != rhs->array.size()) return false;
		for (std::size_t index = 0; index < lhs->array.size(); ++index)
			if (!darling_windows_CFEqual(lhs->array[index], rhs->array[index])) return false;
		return true;
	case Kind::Set:
		if (lhs->set_entries.size() != rhs->set_entries.size()) return false;
		for (const auto& left_entry : lhs->set_entries) {
			bool found = false;
			for (const auto& right_entry : rhs->set_entries)
				if (EqualSetValues(lhs, left_entry.second, right_entry.second) &&
					EqualSetValues(rhs, right_entry.second, left_entry.second)) {
					found = true; break;
				}
			if (!found) return false;
		}
		return true;
	case Kind::Dictionary:
		if (lhs->dictionary.size() != rhs->dictionary.size()) return false;
		for (const auto& left_entry : lhs->dictionary) {
			bool found = false;
			for (const auto& right_entry : rhs->dictionary)
				if (darling_windows_CFEqual(left_entry.first, right_entry.first) &&
					darling_windows_CFEqual(left_entry.second, right_entry.second)) {
					found = true; break;
				}
			if (!found) return false;
		}
		return true;
	default: return false;
	}
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCString(const char* value)
{
	if (value == nullptr) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::String;
	object->string = value;
	return object;
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCStringNoCopy(
	const char* value, int encoding)
{
	if (encoding != 0 && encoding != 1 && encoding != 4) return nullptr;
	return darling_windows_CFStringCreateWithCString(value);
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithFileSystemRepresentation(
	const unsigned char* bytes, darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	std::string path(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length));
	if (path.find('\0') != std::string::npos) return nullptr;
	return darling_windows_CFStringCreateWithCString(path.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCharacters(
	const std::uint16_t* characters, darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && characters == nullptr)) return nullptr;
	std::string text;
	for (darling_windows_CFIndex index = 0; index < length; ++index) {
		std::uint32_t codepoint = characters[index];
		if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
			if (index + 1 >= length || characters[index + 1] < 0xdc00 || characters[index + 1] > 0xdfff)
				return nullptr;
			codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
				(characters[++index] - 0xdc00);
		} else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return nullptr;
		if (codepoint < 0x80) text.push_back(static_cast<char>(codepoint));
		else if (codepoint < 0x800) {
			text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else if (codepoint < 0x10000) {
			text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		} else {
			text.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
	}
	return darling_windows_CFStringCreateWithCString(text.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithPascalString(
	const std::uint8_t* value, int encoding)
{
	if (value == nullptr || (encoding != 0 && encoding != 1 && encoding != 4)) return nullptr;
	const auto length = static_cast<std::size_t>(value[0]);
	std::string text(reinterpret_cast<const char*>(value + 1), length);
	return darling_windows_CFStringCreateWithCString(text.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithBytes(
	const unsigned char* bytes, darling_windows_CFIndex length, int encoding,
	bool is_external_representation)
{
	(void)is_external_representation;
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	// kCFStringEncodingASCII and kCFStringEncodingUTF8 are accepted here;
	// the internal representation is UTF-8 and validates the byte sequence
	// through the existing string operations.
	if (encoding != 0 && encoding != 1 && encoding != 4) return nullptr;
	std::string value(reinterpret_cast<const char*>(bytes),
		static_cast<std::size_t>(length));
	return darling_windows_CFStringCreateWithCString(value.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithFormat(
	const char* format, ...)
{
	if (format == nullptr) return nullptr;
	va_list arguments;
	va_start(arguments, format);
	va_list sizing;
	va_copy(sizing, arguments);
	const int required = std::vsnprintf(nullptr, 0, format, sizing);
	va_end(sizing);
	if (required < 0) {
		va_end(arguments);
		return nullptr;
	}
	std::string result(static_cast<std::size_t>(required) + 1, '\0');
	std::vsnprintf(result.data(), result.size(), format, arguments);
	va_end(arguments);
	return darling_windows_CFStringCreateWithCString(result.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateCopy(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateMutableCopy(
	darling_windows_CFStringRef value, darling_windows_CFIndex capacity)
{
	(void)capacity;
	return darling_windows_CFStringCreateCopy(value);
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateMutable(
	darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = new Object;
	object->kind = Kind::String;
	if (capacity > 0) object->string.reserve(static_cast<std::size_t>(capacity));
	return object;
}

extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithSubstring(
	darling_windows_CFStringRef value, darling_windows_CFRange range)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || range.location < 0 ||
		range.length < 0) return nullptr;
	const auto& source = object->string;
	const auto utf16_to_utf8 = [&source](darling_windows_CFIndex target) -> std::size_t {
		if (target < 0) return std::string::npos;
		std::size_t index = 0;
		darling_windows_CFIndex units = 0;
		while (index < source.size() && units < target) {
			const auto first = static_cast<unsigned char>(source[index]);
			std::size_t width = 1;
			std::uint32_t codepoint = 0xfffd;
			if (first < 0x80) codepoint = first;
			else if (first >= 0xc2 && first <= 0xdf && index + 1 < source.size()) {
				codepoint = ((first & 0x1f) << 6) |
					(static_cast<unsigned char>(source[index + 1]) & 0x3f); width = 2;
			} else if (first >= 0xe0 && first <= 0xef && index + 2 < source.size()) {
				codepoint = ((first & 0x0f) << 12) |
					((static_cast<unsigned char>(source[index + 1]) & 0x3f) << 6) |
					(static_cast<unsigned char>(source[index + 2]) & 0x3f); width = 3;
			} else if (first >= 0xf0 && first <= 0xf4 && index + 3 < source.size()) {
				codepoint = ((first & 0x07) << 18) |
					((static_cast<unsigned char>(source[index + 1]) & 0x3f) << 12) |
					((static_cast<unsigned char>(source[index + 2]) & 0x3f) << 6) |
					(static_cast<unsigned char>(source[index + 3]) & 0x3f); width = 4;
			}
			const auto consumed = codepoint > 0xffff ? 2 : 1;
			if (units + consumed > target) return std::string::npos;
			units += consumed;
			index += width;
		}
		return units == target ? index : std::string::npos;
	};
	const auto start = utf16_to_utf8(range.location);
	const auto end = utf16_to_utf8(range.location + range.length);
	if (start == std::string::npos || end == std::string::npos || start > end) return nullptr;
	auto* result = new Object;
	result->kind = Kind::String;
	result->string = source.substr(start, end - start);
	return result;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetLength(darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::String ?
		UTF16Length(object->string) : 0;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetMaximumSizeForEncoding(
	darling_windows_CFIndex length, int encoding)
{
	if (length < 0 || (encoding != 0 && encoding != 1 && encoding != 4)) return 0;
	const auto multiplier = encoding == 1 ? 4 : 1;
	const auto maximum = (std::numeric_limits<darling_windows_CFIndex>::max)();
	return length > maximum / multiplier ? maximum : length * multiplier;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetMaximumSizeOfFileSystemRepresentation(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String) return 0;
	return static_cast<darling_windows_CFIndex>(object->string.size() + 1);
}

extern "C" bool darling_windows_CFStringGetFileSystemRepresentation(
	darling_windows_CFStringRef value, unsigned char* buffer, darling_windows_CFIndex buffer_capacity)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || buffer == nullptr || buffer_capacity <= 0 ||
		object->string.size() + 1 > static_cast<std::size_t>(buffer_capacity)) return false;
	std::memcpy(buffer, object->string.c_str(), object->string.size() + 1);
	return true;
}

extern "C" bool darling_windows_CFStringFindAndReplace(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement,
	darling_windows_CFRange range, int flags)
{
	auto* object = As(value);
	const auto* needle = static_cast<const Object*>(target);
	const auto* substitute = static_cast<const Object*>(replacement);
	if (object == nullptr || needle == nullptr || substitute == nullptr || object->kind != Kind::String ||
		needle->kind != Kind::String || substitute->kind != Kind::String || range.location < 0 || range.length < 0 ||
		range.location + range.length > UTF16Length(object->string)) return false;
	const auto scoped = darling_windows_CFStringCreateWithSubstring(value, range);
	if (scoped == nullptr) return false;
	const auto found = darling_windows_CFStringFind(scoped, target, flags);
	darling_windows_CFRelease(scoped);
	if (found.location < 0) return false;
	return darling_windows_CFStringReplaceRange(value, range.location + found.location,
		found.length, substitute->string.c_str());
}

extern "C" bool darling_windows_CFStringGetCharacters(
	darling_windows_CFStringRef value, darling_windows_CFRange range, std::uint16_t* output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || range.location < 0 ||
		range.length < 0 || (range.length != 0 && output == nullptr) ||
		range.location + range.length > UTF16Length(object->string)) return false;
	std::size_t byte_index = 0;
	darling_windows_CFIndex unit_index = 0;
	darling_windows_CFIndex output_index = 0;
	while (byte_index < object->string.size() && output_index < range.length) {
		const auto first = static_cast<unsigned char>(object->string[byte_index]);
		std::size_t width = 1;
		std::uint32_t codepoint = 0xfffd;
		if (first < 0x80) codepoint = first;
		else if (first >= 0xc2 && first <= 0xdf && byte_index + 1 < object->string.size()) {
			codepoint = ((first & 0x1f) << 6) |
				(static_cast<unsigned char>(object->string[byte_index + 1]) & 0x3f); width = 2;
		} else if (first >= 0xe0 && first <= 0xef && byte_index + 2 < object->string.size()) {
			codepoint = ((first & 0x0f) << 12) |
				((static_cast<unsigned char>(object->string[byte_index + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(object->string[byte_index + 2]) & 0x3f); width = 3;
		} else if (first >= 0xf0 && first <= 0xf4 && byte_index + 3 < object->string.size()) {
			codepoint = ((first & 0x07) << 18) |
				((static_cast<unsigned char>(object->string[byte_index + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(object->string[byte_index + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(object->string[byte_index + 3]) & 0x3f); width = 4;
		}
		const auto units = codepoint > 0xffff ? 2 : 1;
		if (unit_index + units > range.location && unit_index < range.location + range.length) {
			if (unit_index < range.location) {
				if (units != 1) return false;
			} else if (units == 1) output[output_index++] = static_cast<std::uint16_t>(codepoint);
			else {
				output[output_index++] = static_cast<std::uint16_t>(0xd800 + ((codepoint - 0x10000) >> 10));
				if (output_index < range.length)
					output[output_index++] = static_cast<std::uint16_t>(0xdc00 + ((codepoint - 0x10000) & 0x3ff));
			}
		}
		unit_index += units;
		byte_index += width;
	}
	return output_index == range.length;
}

extern "C" std::uint16_t darling_windows_CFStringGetCharacterAtIndex(
	darling_windows_CFStringRef value, darling_windows_CFIndex index)
{
	if (index > 0) {
		std::uint16_t pair[2]{};
		if (darling_windows_CFStringGetCharacters(value, {index - 1, 2}, pair) &&
			pair[0] >= 0xd800 && pair[0] <= 0xdbff) return pair[1];
	}
	std::uint16_t pair[2]{};
	if (darling_windows_CFStringGetCharacters(value, {index, 2}, pair)) return pair[0];
	std::uint16_t character = 0;
	return darling_windows_CFStringGetCharacters(value, {index, 1}, &character) ? character : 0;
}

extern "C" std::int32_t darling_windows_CFStringGetIntValue(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String) return 0;
	const auto* start = object->string.c_str();
	char* end = nullptr;
	const auto parsed = std::strtoll(start, &end, 10);
	if (end == start) return 0;
	while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
	if (*end != '\0') return 0;
	if (parsed > (std::numeric_limits<std::int32_t>::max)())
		return (std::numeric_limits<std::int32_t>::max)();
	if (parsed < (std::numeric_limits<std::int32_t>::min)())
		return (std::numeric_limits<std::int32_t>::min)();
	return static_cast<std::int32_t>(parsed);
}

extern "C" double darling_windows_CFStringGetDoubleValue(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String) return 0.0;
	const auto* start = object->string.c_str();
	char* end = nullptr;
	const auto parsed = std::strtod(start, &end);
	if (end == start) return 0.0;
	while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
	return *end == '\0' && std::isfinite(parsed) ? parsed : 0.0;
}

extern "C" bool darling_windows_CFStringGetCString(darling_windows_CFStringRef value,
	char* buffer, darling_windows_CFIndex capacity)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || buffer == nullptr || capacity <= 0 ||
		static_cast<darling_windows_CFIndex>(object->string.size() + 1) > capacity) return false;
	std::memcpy(buffer, object->string.c_str(), object->string.size() + 1);
	return true;
}

extern "C" const char* darling_windows_CFStringGetCStringPtr(
	darling_windows_CFStringRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::String ? object->string.c_str() : nullptr;
}

extern "C" bool darling_windows_CFStringHasPrefix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef prefix)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(prefix);
	return object != nullptr && candidate != nullptr && object->kind == Kind::String &&
		candidate->kind == Kind::String && object->string.rfind(candidate->string, 0) == 0;
}

extern "C" bool darling_windows_CFStringHasSuffix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef suffix)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(suffix);
	return object != nullptr && candidate != nullptr && object->kind == Kind::String &&
		candidate->kind == Kind::String && object->string.size() >= candidate->string.size() &&
		object->string.compare(object->string.size() - candidate->string.size(),
			candidate->string.size(), candidate->string) == 0;
}

extern "C" bool darling_windows_CFStringHasPrefixWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFStringRef prefix, int flags)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(prefix);
	if (object == nullptr || candidate == nullptr || object->kind != Kind::String ||
		candidate->kind != Kind::String || candidate->string.size() > object->string.size()) return false;
	Object scoped;
	scoped.kind = Kind::String;
	scoped.string = object->string.substr(0, candidate->string.size());
	return darling_windows_CFStringCompareWithOptions(&scoped, candidate, flags) == 0;
}

extern "C" bool darling_windows_CFStringHasSuffixWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFStringRef suffix, int flags)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(suffix);
	if (object == nullptr || candidate == nullptr || object->kind != Kind::String ||
		candidate->kind != Kind::String || candidate->string.size() > object->string.size()) return false;
	Object scoped;
	scoped.kind = Kind::String;
	scoped.string = object->string.substr(object->string.size() - candidate->string.size());
	return darling_windows_CFStringCompareWithOptions(&scoped, candidate, flags) == 0;
}

extern "C" int darling_windows_CFStringCompare(darling_windows_CFStringRef left,
	darling_windows_CFStringRef right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr || lhs->kind != Kind::String ||
		rhs->kind != Kind::String) return 0;
	const int comparison = lhs->string.compare(rhs->string);
	return comparison < 0 ? -1 : comparison > 0 ? 1 : 0;
}

extern "C" int darling_windows_CFStringCompareWithOptions(
	darling_windows_CFStringRef left, darling_windows_CFStringRef right, int flags)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr || lhs->kind != Kind::String ||
		rhs->kind != Kind::String) return 0;
	const auto& a = lhs->string;
	const auto& b = rhs->string;
	if ((flags & darling_windows_CFCompareNumerically) != 0 &&
		(flags & darling_windows_CFCompareBackwards) == 0) {
		std::size_t ai = 0, bi = 0;
		while (ai < a.size() && bi < b.size()) {
			const bool adigit = a[ai] >= '0' && a[ai] <= '9';
			const bool bdigit = b[bi] >= '0' && b[bi] <= '9';
			if (adigit && bdigit) {
				const auto astart = ai, bstart = bi;
				while (ai < a.size() && a[ai] >= '0' && a[ai] <= '9') ++ai;
				while (bi < b.size() && b[bi] >= '0' && b[bi] <= '9') ++bi;
				const auto acontent = a.substr(astart, ai - astart);
				const auto bcontent = b.substr(bstart, bi - bstart);
				const auto atrim = acontent.find_first_not_of('0');
				const auto btrim = bcontent.find_first_not_of('0');
				const auto anormal = atrim == std::string::npos ? std::string("0") : acontent.substr(atrim);
				const auto bnormal = btrim == std::string::npos ? std::string("0") : bcontent.substr(btrim);
				if (anormal.size() != bnormal.size()) return anormal.size() < bnormal.size() ? -1 : 1;
				if (anormal != bnormal) return anormal < bnormal ? -1 : 1;
				continue;
			}
			const auto ca = static_cast<unsigned char>(a[ai++]);
			const auto cb = static_cast<unsigned char>(b[bi++]);
			const auto la = (flags & darling_windows_CFCompareCaseInsensitive) != 0 ?
				static_cast<unsigned char>(std::tolower(ca)) : ca;
			const auto lb = (flags & darling_windows_CFCompareCaseInsensitive) != 0 ?
				static_cast<unsigned char>(std::tolower(cb)) : cb;
			if (la != lb) return la < lb ? -1 : 1;
		}
		if (ai != a.size() || bi != b.size()) return ai == a.size() ? -1 : 1;
		return 0;
	}
	const auto limit = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < limit; ++i) {
		const auto offset = (flags & darling_windows_CFCompareBackwards) != 0 ?
			(limit - i - 1) : i;
		const auto ca = static_cast<unsigned char>(a[offset]);
		const auto cb = static_cast<unsigned char>(b[offset]);
		const auto la = (flags & darling_windows_CFCompareCaseInsensitive) != 0 ?
			static_cast<unsigned char>(std::tolower(ca)) : ca;
		const auto lb = (flags & darling_windows_CFCompareCaseInsensitive) != 0 ?
			static_cast<unsigned char>(std::tolower(cb)) : cb;
		if (la != lb) return la < lb ? -1 : 1;
	}
	if (a.size() == b.size()) return 0;
	return a.size() < b.size() ? -1 : 1;
}

extern "C" darling_windows_CFRange darling_windows_CFStringFind(
	darling_windows_CFStringRef value, darling_windows_CFStringRef needle, int flags)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* candidate = static_cast<const Object*>(needle);
	if (object == nullptr || candidate == nullptr || object->kind != Kind::String || candidate->kind != Kind::String)
		return { -1, 0 };
	const bool backwards = (flags & darling_windows_CFCompareBackwards) != 0;
	if ((flags & darling_windows_CFCompareCaseInsensitive) == 0) {
		const auto position = backwards ? object->string.rfind(candidate->string) : object->string.find(candidate->string);
		return position == std::string::npos ? darling_windows_CFRange{ -1, 0 } : darling_windows_CFRange{
			UTF16Length(object->string.substr(0, position)), UTF16Length(candidate->string) };
	}
	if (candidate->string.empty()) return { backwards ? UTF16Length(object->string) : 0, 0 };
	if (candidate->string.size() > object->string.size()) return { -1, 0 };
	const auto begin = backwards ? object->string.size() - candidate->string.size() : 0;
	for (std::size_t step = 0; step <= object->string.size() - candidate->string.size(); ++step) {
		const auto position = backwards ? begin - step : step;
		bool match = true;
		for (std::size_t offset = 0; offset < candidate->string.size(); ++offset) {
			const auto lhs = static_cast<unsigned char>(object->string[position + offset]);
			const auto rhs = static_cast<unsigned char>(candidate->string[offset]);
			if (std::tolower(lhs) != std::tolower(rhs)) { match = false; break; }
		}
		if (match) return { UTF16Length(object->string.substr(0, position)),
			UTF16Length(candidate->string) };
	}
	return { -1, 0 };
}

extern "C" darling_windows_CFRange darling_windows_CFStringFindWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFRange range,
	darling_windows_CFStringRef needle, int flags)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || range.location < 0 ||
		range.length < 0 || range.location > UTF16Length(object->string) ||
		range.length > UTF16Length(object->string) - range.location)
		return { -1, 0 };
	const auto start = UTF8OffsetForUTF16(object->string, range.location);
	const auto end = UTF8OffsetForUTF16(object->string, range.location + range.length);
	if (start == std::string::npos || end == std::string::npos || end < start)
		return { -1, 0 };
	Object scoped;
	scoped.kind = Kind::String;
	scoped.string = object->string.substr(start, end - start);
	const auto found = darling_windows_CFStringFind(&scoped, needle, flags);
	return found.location < 0 ? found : darling_windows_CFRange{
		range.location + found.location, found.length };
}

extern "C" bool darling_windows_CFStringAppendCString(darling_windows_CFStringRef value,
	const char* suffix)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String || suffix == nullptr) return false;
	object->string += suffix; return true;
}

extern "C" bool darling_windows_CFStringAppend(darling_windows_CFStringRef value,
	darling_windows_CFStringRef suffix)
{
	auto* object = As(value);
	const auto* appended = static_cast<const Object*>(suffix);
	if (object == nullptr || appended == nullptr || object->kind != Kind::String ||
		appended->kind != Kind::String) return false;
	object->string += appended->string;
	return true;
}

extern "C" bool darling_windows_CFStringReplaceAll(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement)
{
	auto* object = As(value);
	const auto* needle = static_cast<const Object*>(target);
	const auto* substitute = static_cast<const Object*>(replacement);
	if (object == nullptr || object->kind != Kind::String || needle == nullptr ||
		needle->kind != Kind::String || needle->string.empty() || substitute == nullptr ||
		substitute->kind != Kind::String) return false;
	std::size_t position = 0;
	while ((position = object->string.find(needle->string, position)) != std::string::npos) {
		object->string.replace(position, needle->string.size(), substitute->string);
		position += substitute->string.size();
	}
	return true;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFStringCreateArrayBySeparatingStrings(
	darling_windows_CFStringRef value, darling_windows_CFStringRef separator)
{
	const auto* object = static_cast<const Object*>(value);
	const auto* delimiter = static_cast<const Object*>(separator);
	if (object == nullptr || delimiter == nullptr || object->kind != Kind::String ||
		delimiter->kind != Kind::String || delimiter->string.empty()) return nullptr;
	std::vector<const void*> parts;
	std::size_t start = 0;
	while (true) {
		const auto end = object->string.find(delimiter->string, start);
		const auto length = end == std::string::npos ? object->string.size() - start : end - start;
		parts.push_back(darling_windows_CFStringCreateWithCString(
			object->string.substr(start, length).c_str()));
		if (end == std::string::npos) break;
		start = end + delimiter->string.size();
	}
	const auto result = darling_windows_CFArrayCreate(parts.data(),
		static_cast<darling_windows_CFIndex>(parts.size()));
	for (const auto part : parts) darling_windows_CFRelease(part);
	return result;
}

extern "C" bool darling_windows_CFStringReplaceRange(darling_windows_CFStringRef value,
	darling_windows_CFIndex location, darling_windows_CFIndex length, const char* replacement)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String || location < 0 || length < 0 || replacement == nullptr) return false;
	const auto start = UTF8OffsetForUTF16(object->string, location);
	const auto end = UTF8OffsetForUTF16(object->string, location + length);
	if (start == std::string::npos || end == std::string::npos || start > end) return false;
	object->string.replace(start, end - start, replacement);
	return true;
}

extern "C" darling_windows_CFIndex darling_windows_CFStringGetBytes(
	darling_windows_CFStringRef value, darling_windows_CFIndex location,
	darling_windows_CFIndex length, char* output, darling_windows_CFIndex capacity)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::String || location < 0 || length < 0 || capacity < 0 ||
		(static_cast<std::size_t>(capacity) != 0 && output == nullptr)) return -1;
	const auto start = UTF8OffsetForUTF16(object->string, location);
	const auto end = UTF8OffsetForUTF16(object->string, location + length);
	if (start == std::string::npos || end == std::string::npos || start > end) return -1;
	const auto copied = std::min<std::size_t>(end - start, static_cast<std::size_t>(capacity));
	if (copied != 0) std::memcpy(output, object->string.data() + start, copied);
	return static_cast<darling_windows_CFIndex>(copied);
}

extern "C" bool darling_windows_CFStringLowercase(darling_windows_CFStringRef value,
	const void* locale)
{
	(void)locale;
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String) return false;
	for (auto& byte : object->string)
		if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte - 'A' + 'a');
	return true;
}

extern "C" bool darling_windows_CFStringUppercase(darling_windows_CFStringRef value,
	const void* locale)
{
	(void)locale;
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String) return false;
	for (auto& byte : object->string)
		if (byte >= 'a' && byte <= 'z') byte = static_cast<char>(byte - 'a' + 'A');
	return true;
}

extern "C" bool darling_windows_CFStringCapitalize(darling_windows_CFStringRef value,
	const void* locale)
{
	(void)locale;
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String) return false;
	bool word_start = true;
	for (auto& byte : object->string) {
		if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')) {
			if (word_start) {
				if (byte >= 'a' && byte <= 'z') byte = static_cast<char>(byte - 'a' + 'A');
				word_start = false;
			} else if (byte >= 'A' && byte <= 'Z') {
				byte = static_cast<char>(byte - 'A' + 'a');
			}
		} else {
			word_start = true;
		}
	}
	return true;
}

extern "C" bool darling_windows_CFStringTrimWhitespace(darling_windows_CFStringRef value)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::String) return false;
	const auto decode = [](const std::string& input, std::size_t offset, std::uint32_t& codepoint) {
		const auto first = static_cast<unsigned char>(input[offset]);
		if (first < 0x80) { codepoint = first; return std::size_t{1}; }
		if (first >= 0xc2 && first <= 0xdf && offset + 1 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(input[offset + 1]) & 0x3f); return std::size_t{2};
		}
		if (first >= 0xe0 && first <= 0xef && offset + 2 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 2]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x0f) << 12) |
				((static_cast<unsigned char>(input[offset + 1]) & 0x3f) << 6) |
				(static_cast<unsigned char>(input[offset + 2]) & 0x3f); return std::size_t{3};
		}
		if (first >= 0xf0 && first <= 0xf4 && offset + 3 < input.size() &&
			(static_cast<unsigned char>(input[offset + 1]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 2]) & 0xc0) == 0x80 &&
			(static_cast<unsigned char>(input[offset + 3]) & 0xc0) == 0x80) {
			codepoint = ((first & 0x07) << 18) |
				((static_cast<unsigned char>(input[offset + 1]) & 0x3f) << 12) |
				((static_cast<unsigned char>(input[offset + 2]) & 0x3f) << 6) |
				(static_cast<unsigned char>(input[offset + 3]) & 0x3f); return std::size_t{4};
		}
		codepoint = first; return std::size_t{1};
	};
	const auto is_space = [](std::uint32_t codepoint) {
		return (codepoint >= 0x0009 && codepoint <= 0x000d) || codepoint == 0x0020 ||
			codepoint == 0x0085 || codepoint == 0x00a0 || codepoint == 0x1680 ||
			(codepoint >= 0x2000 && codepoint <= 0x200a) || codepoint == 0x2028 ||
			codepoint == 0x2029 || codepoint == 0x202f || codepoint == 0x205f ||
			codepoint == 0x3000;
	};
	std::size_t first = 0;
	while (first < object->string.size()) {
		std::uint32_t codepoint = 0;
		const auto width = decode(object->string, first, codepoint);
		if (!is_space(codepoint)) break;
		first += width;
	}
	std::size_t last = object->string.size();
	while (last > first) {
		std::size_t start = last - 1;
		while (start > first && (static_cast<unsigned char>(object->string[start]) & 0xc0) == 0x80) --start;
		std::uint32_t codepoint = 0;
		const auto width = decode(object->string, start, codepoint);
		if (start + width != last || !is_space(codepoint)) break;
		last = start;
	}
	object->string = object->string.substr(first, last - first);
	return true;
}

extern "C" darling_windows_CFDataRef darling_windows_CFDataCreate(const void* bytes,
	darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Data;
	if (length != 0) {
		const auto* begin = static_cast<const unsigned char*>(bytes);
		object->data.assign(begin, begin + length);
	}
	return object;
}

extern "C" darling_windows_CFDataRef darling_windows_CFDataCreateMutable(
	darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::Data;
	try { object->data.reserve(static_cast<std::size_t>(capacity)); }
	catch (...) { delete object; return nullptr; }
	return object;
}

extern "C" darling_windows_CFDataRef darling_windows_CFDataCreateCopy(
	darling_windows_CFDataRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Data) return nullptr;
	return darling_windows_CFDataCreate(object->data.data(),
		static_cast<darling_windows_CFIndex>(object->data.size()));
}

extern "C" const unsigned char* darling_windows_CFDataGetBytePtr(darling_windows_CFDataRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Data && !object->data.empty() ?
		object->data.data() : nullptr;
}

extern "C" unsigned char* darling_windows_CFDataGetMutableBytePtr(darling_windows_CFDataRef value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::Data && !object->data.empty() ?
		object->data.data() : nullptr;
}

extern "C" darling_windows_CFIndex darling_windows_CFDataGetLength(darling_windows_CFDataRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Data ?
		static_cast<darling_windows_CFIndex>(object->data.size()) : 0;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFImageSourceCreate(
	const void* bytes, darling_windows_CFIndex length, bool is_final)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	try {
		auto* object = new Object{};
		object->kind = Kind::ImageSource;
		object->boolean = is_final;
		if (length != 0) {
			const auto* begin = static_cast<const unsigned char*>(bytes);
			object->data.assign(begin, begin + length);
		}
		return object;
	} catch (...) { return nullptr; }
}

extern "C" const unsigned char* darling_windows_CFImageSourceGetBytePtr(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::ImageSource && !object->data.empty()
		? object->data.data() : nullptr;
}

extern "C" darling_windows_CFIndex darling_windows_CFImageSourceGetLength(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::ImageSource
		? static_cast<darling_windows_CFIndex>(object->data.size()) : 0;
}

extern "C" darling_windows_CFTypeID darling_windows_CFImageSourceGetTypeID()
{
	return TypeId(Kind::ImageSource);
}

extern "C" bool darling_windows_CFImageSourceSetData(darling_windows_CFTypeRef value,
	const void* bytes, darling_windows_CFIndex length, bool is_final)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::ImageSource || length < 0 ||
		(length != 0 && bytes == nullptr)) return false;
	try {
		std::vector<unsigned char> replacement;
		if (length != 0) {
			const auto* begin = static_cast<const unsigned char*>(bytes);
			replacement.assign(begin, begin + length);
		}
		object->data.swap(replacement);
		object->boolean = is_final;
		return true;
	} catch (...) { return false; }
}

extern "C" bool darling_windows_CFImageSourceIsFinal(darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::ImageSource && object->boolean;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreate(
	const void* bytes, darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	try {
		auto* object = new Object{};
		object->kind = Kind::DataProvider;
		if (length != 0) {
			const auto* begin = static_cast<const unsigned char*>(bytes);
			object->data.assign(begin, begin + length);
		}
		return object;
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateExternal(
	void* info, const void* bytes, darling_windows_CFIndex length,
	darling_windows_CFDataProviderReleaseCallback release_callback)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::DataProvider;
	object->external_data_info = info;
	object->external_data = bytes;
	object->external_data_size = static_cast<std::size_t>(length);
	object->external_data_release = release_callback;
	object->is_external_data_provider = true;
	return object;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateSequential(
	void* info, darling_windows_CFDataProviderGetBytesCallback get_bytes,
	darling_windows_CFDataProviderSkipForwardCallback skip_forward,
	darling_windows_CFDataProviderRewindCallback rewind,
	darling_windows_CFDataProviderReleaseInfoCallback release_info)
{
	if (get_bytes == nullptr) return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::DataProvider;
	object->is_sequential_data_provider = true;
	object->sequential_info = info;
	object->sequential_get_bytes = get_bytes;
	object->sequential_skip_forward = skip_forward;
	object->sequential_rewind = rewind;
	object->sequential_release_info = release_info;
	return object;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateDirect(
	void* info, darling_windows_CFIndex length,
	darling_windows_CFDataProviderGetBytePointerCallback get_byte_pointer,
	darling_windows_CFDataProviderReleaseBytePointerCallback release_byte_pointer,
	darling_windows_CFDataProviderGetBytesAtPositionCallback get_bytes_at_position,
	darling_windows_CFDataProviderReleaseInfoCallback release_info)
{
	if (length < 0 || (get_byte_pointer == nullptr && get_bytes_at_position == nullptr))
		return nullptr;
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	object->kind = Kind::DataProvider;
	object->is_direct_callback_provider = true;
	object->direct_info = info;
	object->direct_length = length;
	object->direct_get_byte_pointer = get_byte_pointer;
	object->direct_release_byte_pointer = release_byte_pointer;
	object->direct_get_bytes_at_position = get_bytes_at_position;
	object->direct_release_info = release_info;
	return object;
}

extern "C" std::size_t darling_windows_CFDataProviderReadAtPosition(
	darling_windows_CFTypeRef value, void* buffer, std::int64_t position,
	std::size_t count)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::DataProvider ||
		buffer == nullptr || position < 0) return 0;
	if (!object->is_sequential_data_provider) {
		const auto length = darling_windows_CFDataProviderGetLength(value);
		if (position >= length) return 0;
		const auto available = static_cast<std::size_t>(length - position);
		const auto copied = (std::min)(available, count);
		if (copied == 0) return 0;
		if (object->is_direct_callback_provider) {
			try {
				if (object->direct_get_byte_pointer != nullptr) {
					const auto* bytes = object->direct_get_byte_pointer(object->direct_info);
					if (bytes == nullptr) return 0;
					std::memcpy(buffer, static_cast<const unsigned char*>(bytes) +
						static_cast<std::size_t>(position), copied);
					if (object->direct_release_byte_pointer != nullptr)
						object->direct_release_byte_pointer(object->direct_info, bytes);
					return copied;
				}
				const auto read = object->direct_get_bytes_at_position(
					object->direct_info, position, buffer, copied);
				return read <= copied ? read : 0;
			} catch (...) { return 0; }
		}
		const auto* bytes = darling_windows_CFDataProviderGetBytePtr(value);
		if (copied != 0 && bytes != nullptr)
			std::memcpy(buffer, bytes + static_cast<std::size_t>(position), copied);
		return copied;
	}
	std::lock_guard<std::mutex> lock(object->sequential_mutex);
	if (position < object->sequential_position) {
		if (object->sequential_rewind == nullptr) return 0;
		try { object->sequential_rewind(object->sequential_info); }
		catch (...) { return 0; }
		object->sequential_position = 0;
	}
	auto remaining = position - object->sequential_position;
	if (remaining > 0 && object->sequential_skip_forward != nullptr) {
		std::int64_t skipped = 0;
		try { skipped = object->sequential_skip_forward(object->sequential_info, remaining); }
		catch (...) { return 0; }
		if (skipped < 0 || skipped > remaining) return 0;
		object->sequential_position += skipped;
		remaining -= skipped;
	}
	std::array<unsigned char, 4096> discard{};
	while (remaining > 0) {
		const auto amount = static_cast<std::size_t>((std::min<std::int64_t>)(
			remaining, static_cast<std::int64_t>(discard.size())));
		std::size_t read = 0;
		try { read = object->sequential_get_bytes(object->sequential_info, discard.data(), amount); }
		catch (...) { return 0; }
		if (read == 0 || read > amount) return 0;
		object->sequential_position += static_cast<std::int64_t>(read);
		remaining -= static_cast<std::int64_t>(read);
	}
	if (count == 0) return 0;
	std::size_t read = 0;
	try { read = object->sequential_get_bytes(object->sequential_info, buffer, count); }
	catch (...) { return 0; }
	if (read > count) return 0;
	if (read <= static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)() - object->sequential_position))
		object->sequential_position += static_cast<std::int64_t>(read);
	else return 0;
	return read;
}

extern "C" const unsigned char* darling_windows_CFDataProviderGetBytePtr(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::DataProvider) return nullptr;
	if (object->is_sequential_data_provider) return nullptr;
	if (object->is_direct_callback_provider) {
		try { return object->direct_get_byte_pointer == nullptr ? nullptr
			: static_cast<const unsigned char*>(
				object->direct_get_byte_pointer(object->direct_info)); }
		catch (...) { return nullptr; }
	}
	if (object->is_external_data_provider)
		return static_cast<const unsigned char*>(object->external_data);
	return object->data.empty() ? nullptr : object->data.data();
}

extern "C" darling_windows_CFIndex darling_windows_CFDataProviderGetLength(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::DataProvider) return 0;
	if (object->is_sequential_data_provider) return 0;
	if (object->is_direct_callback_provider) return object->direct_length;
	const auto length = object->is_external_data_provider
		? object->external_data_size : object->data.size();
	if (length > static_cast<std::size_t>((std::numeric_limits<darling_windows_CFIndex>::max)()))
		return 0;
	return static_cast<darling_windows_CFIndex>(length);
}

extern "C" bool darling_windows_CFDataProviderIsSequential(
	darling_windows_CFTypeRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::DataProvider &&
		object->is_sequential_data_provider;
}

extern "C" void darling_windows_CFDataProviderReleaseBytePointer(
	darling_windows_CFTypeRef value, const void* pointer)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::DataProvider || pointer == nullptr ||
		!object->is_direct_callback_provider || object->direct_release_byte_pointer == nullptr)
		return;
	try { object->direct_release_byte_pointer(object->direct_info, pointer); }
	catch (...) { /* C callbacks must not unwind through CoreFoundation. */ }
}

extern "C" darling_windows_CFTypeID darling_windows_CFDataProviderGetTypeID()
{
	return TypeId(Kind::DataProvider);
}

extern "C" bool darling_windows_CFDataGetBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	unsigned char* output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Data || range_start < 0 ||
		range_count < 0 || (range_count != 0 && output == nullptr) ||
		static_cast<std::size_t>(range_start) > object->data.size() ||
		static_cast<std::size_t>(range_count) > object->data.size() -
			static_cast<std::size_t>(range_start)) return false;
	if (range_count != 0) std::memmove(output, object->data.data() + range_start,
		static_cast<std::size_t>(range_count));
	return true;
}

extern "C" bool darling_windows_CFDataAppendBytes(darling_windows_CFDataRef value,
	const void* bytes, darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || length < 0 ||
		(length != 0 && bytes == nullptr)) return false;
	const auto* input = static_cast<const unsigned char*>(bytes);
	std::vector<unsigned char> copy;
	if (length != 0) copy.assign(input, input + length);
	object->data.insert(object->data.end(), copy.begin(), copy.end()); return true;
}

extern "C" bool darling_windows_CFDataReplaceBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* bytes, darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || range_start < 0 || range_count < 0 || length < 0 ||
		(length != 0 && bytes == nullptr) || static_cast<std::size_t>(range_start) > object->data.size() ||
		static_cast<std::size_t>(range_count) > object->data.size() - static_cast<std::size_t>(range_start)) return false;
	const auto* input = static_cast<const unsigned char*>(bytes);
	std::vector<unsigned char> copy;
	if (length != 0) copy.assign(input, input + length);
	const auto begin = object->data.begin() + range_start;
	object->data.erase(begin, begin + range_count);
	object->data.insert(object->data.begin() + range_start, copy.begin(), copy.end()); return true;
}

extern "C" bool darling_windows_CFDataSetLength(darling_windows_CFDataRef value,
	darling_windows_CFIndex length)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data || length < 0) return false;
	object->data.resize(static_cast<std::size_t>(length)); return true;
}

extern "C" bool darling_windows_CFDataClear(darling_windows_CFDataRef value)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Data) return false;
	object->data.clear(); return true;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreate(const void* const* values,
	darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && values == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Array;
	if (count != 0) {
		object->array.assign(values, values + count);
		for (const auto* value : object->array) RetainOwned(value);
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCount(darling_windows_CFArrayRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Array ?
		static_cast<darling_windows_CFIndex>(object->array.size()) : 0;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateMutable(
	darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFArrayCreate(nullptr, 0));
	if (object != nullptr) object->mutable_array = true;
	return object;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateCopy(
	darling_windows_CFArrayRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return nullptr;
	return darling_windows_CFArrayCreate(object->array.data(),
		static_cast<darling_windows_CFIndex>(object->array.size()));
}

extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFArrayRef value)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFArrayCreateCopy(value));
	if (object != nullptr) object->mutable_array = true;
	return object;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array || index < 0 ||
		static_cast<std::size_t>(index) >= object->array.size()) return nullptr;
	return object->array[static_cast<std::size_t>(index)];
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return -1;
	for (std::size_t index = 0; index < object->array.size(); ++index)
		if (EqualArrayValues(object, object->array[index], candidate))
			return static_cast<darling_windows_CFIndex>(index);
	return -1;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return 0;
	darling_windows_CFIndex count = 0;
	for (const auto* entry : object->array)
		if (EqualArrayValues(object, entry, candidate)) ++count;
	return count;
}

extern "C" darling_windows_CFIndex darling_windows_CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array) return -1;
	for (std::size_t index = object->array.size(); index > 0; --index)
		if (EqualArrayValues(object, object->array[index - 1], candidate))
			return static_cast<darling_windows_CFIndex>(index - 1);
	return -1;
}

extern "C" void darling_windows_CFArrayApplyFunction(darling_windows_CFArrayRef value,
	darling_windows_CFRange range, darling_windows_CFArrayApplierFunction function,
	void* context)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array || function == nullptr ||
		range.location < 0 || range.length < 0 ||
		static_cast<std::size_t>(range.location) > object->array.size() ||
		static_cast<std::size_t>(range.length) > object->array.size() -
			static_cast<std::size_t>(range.location)) return;
	std::vector<const void*> snapshot;
	snapshot.reserve(static_cast<std::size_t>(range.length));
	for (darling_windows_CFIndex index = 0; index < range.length; ++index) {
		const auto* element = object->array[static_cast<std::size_t>(range.location + index)];
		snapshot.push_back(RetainArrayValue(const_cast<Object*>(object), element));
	}
	for (const auto* element : snapshot) function(element, context);
	for (const auto* element : snapshot) ReleaseArrayValue(const_cast<Object*>(object), element);
}

extern "C" void darling_windows_CFArrayGetValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count, const void** output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array ||
		range_start < 0 || range_count < 0 ||
		(range_count != 0 && output == nullptr) ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() -
			static_cast<std::size_t>(range_start)) return;
	if (range_count != 0) std::memmove(output, object->array.data() + range_start,
		static_cast<std::size_t>(range_count) * sizeof(const void*));
}

extern "C" bool darling_windows_CFArrayAppendValue(darling_windows_CFArrayRef value, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || element == nullptr) return false;
	try { object->array.reserve(object->array.size() + 1); }
	catch (...) { return false; }
	const auto* retained = RetainArrayValue(object, element);
	if (retained == nullptr) return false;
	object->array.push_back(retained); return true;
}

extern "C" bool darling_windows_CFArrayInsertValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || element == nullptr || index < 0 ||
		static_cast<std::size_t>(index) > object->array.size()) return false;
	try { object->array.reserve(object->array.size() + 1); }
	catch (...) { return false; }
	const auto* retained = RetainArrayValue(object, element);
	if (retained == nullptr) return false;
	object->array.insert(object->array.begin() + index, retained); return true;
}

extern "C" bool darling_windows_CFArrayRemoveValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || index < 0 ||
		static_cast<std::size_t>(index) >= object->array.size()) return false;
	const auto it = object->array.begin() + index; ReleaseArrayValue(object, *it); object->array.erase(it); return true;
}

extern "C" bool darling_windows_CFArrayReplaceValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* const* replacement, darling_windows_CFIndex replacement_count)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || range_start < 0 || range_count < 0 ||
		replacement_count < 0 || (replacement_count != 0 && replacement == nullptr) ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() - static_cast<std::size_t>(range_start)) return false;
	for (darling_windows_CFIndex index = 0; index < replacement_count; ++index)
		if (replacement[index] == nullptr) return false;
	const auto retained_size = object->array.size() - static_cast<std::size_t>(range_count) +
		static_cast<std::size_t>(replacement_count);
	std::vector<const void*> replacement_values;
	try {
		object->array.reserve(retained_size);
		replacement_values.reserve(static_cast<std::size_t>(replacement_count));
		for (darling_windows_CFIndex index = 0; index < replacement_count; ++index) {
			const auto* retained = RetainArrayValue(object, replacement[index]);
			if (retained == nullptr) {
				for (const auto* previous : replacement_values) ReleaseArrayValue(object, previous);
				return false;
			}
			replacement_values.push_back(retained);
		}
	} catch (...) {
		for (const auto* retained : replacement_values) ReleaseArrayValue(object, retained);
		return false;
	}
	const auto begin = object->array.begin() + range_start;
	for (darling_windows_CFIndex index = 0; index < range_count; ++index) ReleaseArrayValue(object, object->array[static_cast<std::size_t>(range_start + index)]);
	object->array.erase(begin, begin + range_count);
	object->array.insert(object->array.begin() + range_start,
		replacement_values.begin(), replacement_values.end());
	return true;
}

extern "C" bool darling_windows_CFArrayRemoveValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Array || range_start < 0 || range_count < 0 ||
		static_cast<std::size_t>(range_start) > object->array.size() ||
		static_cast<std::size_t>(range_count) > object->array.size() - static_cast<std::size_t>(range_start)) return false;
	for (darling_windows_CFIndex index = 0; index < range_count; ++index)
		ReleaseArrayValue(object, object->array[static_cast<std::size_t>(range_start + index)]);
	object->array.erase(object->array.begin() + range_start, object->array.begin() + range_start + range_count);
	return true;
}

extern "C" bool darling_windows_CFArrayAppendArray(darling_windows_CFArrayRef value,
	darling_windows_CFArrayRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Array || source_object == nullptr ||
		source_object->kind != Kind::Array) return false;
	std::vector<const void*> source_values;
	try { source_values = source_object->array; }
	catch (...) { return false; }
	return darling_windows_CFArrayReplaceValues(value,
		static_cast<darling_windows_CFIndex>(object->array.size()), 0,
		source_values.data(), static_cast<darling_windows_CFIndex>(source_values.size()));
}

extern "C" void darling_windows_CFArrayRemoveAllValues(darling_windows_CFArrayRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Array) return;
	for (const auto* element : object->array) ReleaseArrayValue(object, element); object->array.clear();
}

extern "C" const void* darling_windows_CFNumberCreateInteger(std::int64_t value)
{
	auto* object = new Object{};
	object->kind = Kind::Number;
	object->number = value;
	return object;
}

extern "C" const void* darling_windows_CFNumberCreateCopy(const void* value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr) return nullptr;
	if (object->kind == Kind::Number)
		return darling_windows_CFNumberCreateInteger(object->number);
	if (object->kind == Kind::Real)
		return darling_windows_CFNumberCreateDouble(object->real_number);
	return nullptr;
}

extern "C" bool darling_windows_CFNumberGetInteger(const void* value, std::int64_t* result)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || result == nullptr ||
		(object->kind != Kind::Number && object->kind != Kind::Real)) return false;
	if (object->kind == Kind::Number) {
		*result = object->number;
		return true;
	}
	if (!std::isfinite(object->real_number)) return false;
	const auto wide = static_cast<long double>(object->real_number);
	if (wide < static_cast<long double>((std::numeric_limits<std::int64_t>::min)()) ||
		wide > static_cast<long double>((std::numeric_limits<std::int64_t>::max)())) return false;
	*result = static_cast<std::int64_t>(object->real_number);
	return true;
}

extern "C" const void* darling_windows_CFNumberCreateDouble(double value)
{
	auto* object = new Object{};
	object->kind = Kind::Real;
	object->real_number = value;
	return object;
}

extern "C" bool darling_windows_CFNumberGetDouble(const void* value, double* result)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || result == nullptr ||
		(object->kind != Kind::Real && object->kind != Kind::Number)) return false;
	*result = object->kind == Kind::Real ? object->real_number : static_cast<double>(object->number);
	return true;
}

extern "C" int darling_windows_CFNumberCompare(const void* left, const void* right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr ||
		(lhs->kind != Kind::Number && lhs->kind != Kind::Real) ||
		(rhs->kind != Kind::Number && rhs->kind != Kind::Real)) return 0;
	const auto lhs_value = lhs->kind == Kind::Number ? static_cast<double>(lhs->number) : lhs->real_number;
	const auto rhs_value = rhs->kind == Kind::Number ? static_cast<double>(rhs->number) : rhs->real_number;
	if (std::isnan(lhs_value) || std::isnan(rhs_value)) return 0;
	return lhs_value < rhs_value ? -1 : lhs_value > rhs_value ? 1 : 0;
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreate(
	const void* const* keys, const void* const* values, darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && (keys == nullptr || values == nullptr))) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Dictionary;
	for (darling_windows_CFIndex index = 0; index < count; ++index) {
		const auto found = std::find_if(object->dictionary.begin(), object->dictionary.end(),
			[&](const auto& entry) { return EqualDictionaryKeys(object, entry.first, keys[index]); });
		if (found != object->dictionary.end()) {
			RetainOwned(values[index]);
			ReleaseOwned(As(found->second));
			found->second = values[index];
			continue;
		}
		object->dictionary.emplace_back(keys[index], values[index]);
		RetainOwned(keys[index]);
		RetainOwned(values[index]);
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Dictionary ?
		static_cast<darling_windows_CFIndex>(object->dictionary.size()) : 0;
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateMutable(
	darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFDictionaryCreate(nullptr, nullptr, 0));
	if (object != nullptr) object->mutable_dictionary = true;
	return object;
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateCopy(
	darling_windows_CFDictionaryRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return nullptr;
	std::vector<const void*> keys;
	std::vector<const void*> values;
	keys.reserve(object->dictionary.size());
	values.reserve(object->dictionary.size());
	for (const auto& entry : object->dictionary) {
		keys.push_back(entry.first);
		values.push_back(entry.second);
	}
	return darling_windows_CFDictionaryCreate(keys.data(), values.data(),
		static_cast<darling_windows_CFIndex>(keys.size()));
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFDictionaryRef value)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFDictionaryCreateCopy(value));
	if (object != nullptr) object->mutable_dictionary = true;
	return object;
}

extern "C" darling_windows_CFTypeRef darling_windows_CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, const void* key)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return nullptr;
	for (const auto& entry : object->dictionary)
		if (EqualDictionaryKeys(object, entry.first, key)) return entry.second;
	return nullptr;
}

extern "C" bool darling_windows_CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, const void* key)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return false;
	for (const auto& entry : object->dictionary)
		if (EqualDictionaryKeys(object, entry.first, key)) return true;
	return false;
}

extern "C" void darling_windows_CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary) return;
	for (std::size_t index = 0; index < object->dictionary.size(); ++index) {
		if (keys != nullptr) keys[index] = object->dictionary[index].first;
		if (values != nullptr) values[index] = object->dictionary[index].second;
	}
}

extern "C" void darling_windows_CFDictionaryApplyFunction(
	darling_windows_CFDictionaryRef value, darling_windows_CFDictionaryApplierFunction function,
	void* context)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Dictionary || function == nullptr) return;
	std::vector<std::pair<const void*, const void*>> snapshot;
	try {
		snapshot.reserve(object->dictionary.size());
		for (const auto& entry : object->dictionary) {
			const auto* key = RetainDictionaryKey(const_cast<Object*>(object), entry.first);
			if (key == nullptr) throw std::bad_alloc{};
			const auto* element = RetainDictionaryValue(const_cast<Object*>(object), entry.second);
			if (element == nullptr) {
				ReleaseDictionaryKey(const_cast<Object*>(object), key);
				throw std::bad_alloc{};
			}
			snapshot.emplace_back(key, element);
		}
	} catch (...) {
		for (const auto& entry : snapshot) {
			ReleaseDictionaryKey(const_cast<Object*>(object), entry.first);
			ReleaseDictionaryValue(const_cast<Object*>(object), entry.second);
		}
		return;
	}
	for (const auto& entry : snapshot) function(entry.first, entry.second, context);
	for (const auto& entry : snapshot) {
		ReleaseDictionaryKey(const_cast<Object*>(object), entry.first);
		ReleaseDictionaryValue(const_cast<Object*>(object), entry.second);
	}
}

extern "C" bool darling_windows_CFDictionarySetValue(darling_windows_CFDictionaryRef value,
	const void* key, const void* element)
{
	auto* object = As(value);
	if (object == nullptr || object->kind != Kind::Dictionary || key == nullptr || element == nullptr) return false;
	for (auto& entry : object->dictionary) if (EqualDictionaryKeys(object, entry.first, key)) {
		const auto* retained = RetainDictionaryValue(object, element);
		if (retained == nullptr) return false;
		ReleaseDictionaryValue(object, entry.second); entry.second = retained; return true;
	}
	const auto* retained_key = RetainDictionaryKey(object, key);
	if (retained_key == nullptr) return false;
	const auto* retained_value = RetainDictionaryValue(object, element);
	if (retained_value == nullptr) { ReleaseDictionaryKey(object, retained_key); return false; }
	try { object->dictionary.emplace_back(retained_key, retained_value); }
	catch (...) {
		ReleaseDictionaryKey(object, retained_key);
		ReleaseDictionaryValue(object, retained_value);
		return false;
	}
	return true;
}

extern "C" bool darling_windows_CFDictionaryRemoveValue(darling_windows_CFDictionaryRef value, const void* key)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Dictionary || key == nullptr) return false;
	for (auto it = object->dictionary.begin(); it != object->dictionary.end(); ++it) if (EqualDictionaryKeys(object, it->first, key)) {
		ReleaseDictionaryKey(object, it->first); ReleaseDictionaryValue(object, it->second); object->dictionary.erase(it); return true;
	}
	return false;
}

extern "C" void darling_windows_CFDictionaryRemoveAllValues(darling_windows_CFDictionaryRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Dictionary) return;
	for (const auto& entry : object->dictionary) {
		ReleaseDictionaryKey(object, entry.first);
		ReleaseDictionaryValue(object, entry.second);
	}
	object->dictionary.clear();
}

extern "C" bool darling_windows_CFDictionaryMerge(darling_windows_CFDictionaryRef value,
	darling_windows_CFDictionaryRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Dictionary || source_object == nullptr ||
		source_object->kind != Kind::Dictionary) return false;
	if (object == source_object) return true;
	const auto source_entries = source_object->dictionary;
	for (const auto& source_entry : source_entries) {
		bool replaced = false;
		for (auto& entry : object->dictionary) if (EqualDictionaryKeys(object, entry.first, source_entry.first)) {
			const auto* retained = RetainDictionaryValue(object, source_entry.second);
			if (retained == nullptr) return false;
			ReleaseDictionaryValue(object, entry.second); entry.second = retained;
			replaced = true; break;
		}
		if (!replaced) {
			if (!darling_windows_CFDictionarySetValue(object, source_entry.first,
				source_entry.second)) return false;
		}
	}
	return true;
}

extern "C" darling_windows_CFSetRef darling_windows_CFSetCreate(
	const void* const* values, darling_windows_CFIndex count)
{
	if (count < 0 || (count != 0 && values == nullptr)) return nullptr;
	auto* object = new Object{};
	object->kind = Kind::Set;
	try { object->set_entries.reserve(static_cast<std::size_t>(count)); }
	catch (...) { delete object; return nullptr; }
	for (darling_windows_CFIndex index = 0; index < count; ++index) {
		if (values[index] == nullptr) { delete object; return nullptr; }
		const auto hash = HashSetValue(object, values[index]);
		if (FindSetEntry(object, values[index], hash) == object->set_entries.end()) {
			const auto* retained = RetainSetValue(object, values[index]);
			if (retained == nullptr) { delete object; return nullptr; }
			try { object->set_entries.emplace(hash, retained); }
			catch (...) { ReleaseSetValue(object, retained); delete object; return nullptr; }
		}
	}
	return object;
}

extern "C" darling_windows_CFIndex darling_windows_CFSetGetCount(darling_windows_CFSetRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Set ?
		static_cast<darling_windows_CFIndex>(object->set_entries.size()) : 0;
}

extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateMutable(
	darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFSetCreate(nullptr, 0));
	if (object != nullptr) {
		try { object->set_entries.reserve(static_cast<std::size_t>(capacity)); }
		catch (...) { darling_windows_CFRelease(object); return nullptr; }
		object->mutable_set = true;
	}
	return object;
}

extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateCopy(
	darling_windows_CFSetRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set) return nullptr;
	std::vector<const void*> values;
	try {
		values.reserve(object->set_entries.size());
		for (const auto& entry : object->set_entries) values.push_back(entry.second);
	} catch (...) { return nullptr; }
	return darling_windows_CFSetCreate(values.data(),
		static_cast<darling_windows_CFIndex>(values.size()));
}

extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFSetRef value)
{
	if (capacity < 0) return nullptr;
	auto* object = As(darling_windows_CFSetCreateCopy(value));
	if (object != nullptr) {
		try { object->set_entries.reserve((std::max)(object->set_entries.size(),
			static_cast<std::size_t>(capacity))); }
		catch (...) { darling_windows_CFRelease(object); return nullptr; }
		object->mutable_set = true;
	}
	return object;
}

extern "C" bool darling_windows_CFSetContainsValue(darling_windows_CFSetRef value,
	const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set) return false;
	return FindSetEntry(object, candidate, HashSetValue(object, candidate)) !=
		object->set_entries.end();
}

extern "C" darling_windows_CFTypeRef darling_windows_CFSetGetValue(
	darling_windows_CFSetRef value, const void* candidate)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set) return nullptr;
	const auto found = FindSetEntry(object, candidate, HashSetValue(object, candidate));
	return found == object->set_entries.end() ? nullptr : found->second;
}

extern "C" void darling_windows_CFSetGetValues(darling_windows_CFSetRef value,
	const void** output)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set || output == nullptr) return;
	std::size_t index = 0;
	for (const auto& entry : object->set_entries) output[index++] = entry.second;
}

extern "C" void darling_windows_CFSetApplyFunction(darling_windows_CFSetRef value,
	darling_windows_CFArrayApplierFunction function, void* context)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Set || function == nullptr) return;
	std::vector<const void*> snapshot;
	try {
		snapshot.reserve(object->set_entries.size());
		for (const auto& entry : object->set_entries) {
			const auto* element = entry.second;
			const auto* retained = RetainSetValue(const_cast<Object*>(object), element);
			if (retained == nullptr) throw std::bad_alloc{};
			snapshot.push_back(retained);
		}
	} catch (...) {
		for (const auto* element : snapshot) ReleaseSetValue(const_cast<Object*>(object), element);
		return;
	}
	for (const auto* element : snapshot) function(element, context);
	for (const auto* element : snapshot) ReleaseSetValue(const_cast<Object*>(object), element);
}

extern "C" bool darling_windows_CFSetAddValue(darling_windows_CFSetRef value, const void* element)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set || element == nullptr) return false;
	const auto hash = HashSetValue(object, element);
	if (FindSetEntry(object, element, hash) != object->set_entries.end()) return false;
	const auto* retained = RetainSetValue(object, element);
	if (retained == nullptr) return false;
	try { object->set_entries.emplace(hash, retained); }
	catch (...) { ReleaseSetValue(object, retained); return false; }
	return true;
}

extern "C" bool darling_windows_CFSetRemoveValue(darling_windows_CFSetRef value, const void* element)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set || element == nullptr) return false;
	const auto found = FindSetEntry(object, element, HashSetValue(object, element));
	if (found == object->set_entries.end()) return false;
	ReleaseSetValue(object, found->second);
	object->set_entries.erase(found);
	return true;
}

extern "C" void darling_windows_CFSetRemoveAllValues(darling_windows_CFSetRef value)
{
	auto* object = As(value); if (object == nullptr || object->kind != Kind::Set) return;
	for (const auto& entry : object->set_entries) ReleaseSetValue(object, entry.second);
	object->set_entries.clear();
}

extern "C" bool darling_windows_CFSetUnion(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	if (object == source_object) return true;
	for (const auto& source_entry : source_object->set_entries) {
		const auto* candidate = source_entry.second;
		if (FindSetEntry(object, candidate, HashSetValue(object, candidate)) ==
			object->set_entries.end() && !darling_windows_CFSetAddValue(object, candidate)) return false;
	}
	return true;
}

extern "C" bool darling_windows_CFSetIntersect(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	if (object == source_object) return true;
	for (auto it = object->set_entries.begin(); it != object->set_entries.end();) {
		bool found = false;
		for (const auto& candidate_entry : source_object->set_entries) {
			const auto* candidate = candidate_entry.second;
			if (it->first == HashSetValue(object, candidate) &&
				EqualSetValues(object, it->second, candidate)) { found = true; break; }
		}
		if (found) { ++it; continue; }
		ReleaseSetValue(object, it->second);
		it = object->set_entries.erase(it);
	}
	return true;
}

extern "C" bool darling_windows_CFSetSubtract(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source)
{
	auto* object = As(value);
	const auto* source_object = static_cast<const Object*>(source);
	if (object == nullptr || object->kind != Kind::Set || source_object == nullptr ||
		source_object->kind != Kind::Set) return false;
	if (object == source_object) {
		darling_windows_CFSetRemoveAllValues(value);
		return true;
	}
	for (auto it = object->set_entries.begin(); it != object->set_entries.end();) {
		bool found = false;
		for (const auto& candidate_entry : source_object->set_entries) {
			const auto* candidate = candidate_entry.second;
			if (it->first == HashSetValue(object, candidate) &&
				EqualSetValues(object, it->second, candidate)) { found = true; break; }
		}
		if (!found) { ++it; continue; }
		ReleaseSetValue(object, it->second);
		it = object->set_entries.erase(it);
	}
	return true;
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreate(double absolute_time)
{
	auto* object = new Object{};
	object->kind = Kind::Date;
	object->date = absolute_time;
	return object;
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateWithTimeIntervalSinceNow(
	double seconds)
{
	using namespace std::chrono;
	const auto now = system_clock::now().time_since_epoch();
	const double unix_seconds = duration<double>(now).count();
	return darling_windows_CFDateCreate(unix_seconds - 978307200.0 + seconds);
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateCopy(
	darling_windows_CFDateRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Date) return nullptr;
	return darling_windows_CFDateCreate(object->date);
}

extern "C" double darling_windows_CFDateGetAbsoluteTime(darling_windows_CFDateRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Date ? object->date : 0;
}

extern "C" double darling_windows_CFDateGetTimeIntervalSinceReferenceDate(
	darling_windows_CFDateRef value)
{
	return darling_windows_CFDateGetAbsoluteTime(value);
}

extern "C" int darling_windows_CFDateCompare(darling_windows_CFDateRef left,
	darling_windows_CFDateRef right)
{
	const auto* lhs = static_cast<const Object*>(left);
	const auto* rhs = static_cast<const Object*>(right);
	if (lhs == nullptr || rhs == nullptr || lhs->kind != Kind::Date || rhs->kind != Kind::Date) return 0;
	return lhs->date < rhs->date ? -1 : lhs->date > rhs->date ? 1 : 0;
}

extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateByAddingTimeInterval(
    darling_windows_CFDateRef value, double seconds)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Date) return nullptr;
    return darling_windows_CFDateCreate(object->date + seconds);
}

extern "C" double darling_windows_CFDateGetTimeIntervalSinceDate(
    darling_windows_CFDateRef value, darling_windows_CFDateRef reference)
{
    const auto* object = static_cast<const Object*>(value);
    const auto* base = static_cast<const Object*>(reference);
    if (object == nullptr || base == nullptr || object->kind != Kind::Date || base->kind != Kind::Date) {
        return 0.0;
    }
    return object->date - base->date;
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithFileSystemPath(const char* path)
{
	if (path == nullptr || path[0] == '\0') return nullptr;
	auto* object = CreateObjectWithAllocator(nullptr, Kind::URL);
	if (object == nullptr) return nullptr;
	try { object->string = path; }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithString(
	darling_windows_CFStringRef string, darling_windows_CFURLRef base_url)
{
	(void)base_url;
	const auto* source = static_cast<const Object*>(string);
	if (source == nullptr || source->kind != Kind::String || source->string.empty()) return nullptr;
	std::string resolved = source->string;
	if (resolved.find(':') == std::string::npos && base_url != nullptr) {
		const auto* base = static_cast<const Object*>(base_url);
		if (base->kind != Kind::URL || base->string.empty()) return nullptr;
		const auto query = base->string.find_first_of("?#");
		const auto base_value = query == std::string::npos ? base->string : base->string.substr(0, query);
		const auto slash = base_value.find_last_of('/');
		if (slash == std::string::npos) return nullptr;
		std::string directory = base_value.substr(0, slash + 1);
		while (resolved.rfind("../", 0) == 0) {
			resolved.erase(0, 3);
			if (directory.size() > 1) {
				directory.erase(directory.size() - 1);
				const auto parent_slash = directory.find_last_of('/');
				directory = parent_slash == std::string::npos ? std::string{} :
					directory.substr(0, parent_slash + 1);
			}
		}
		resolved = directory + resolved;
	}
	for (std::size_t marker = resolved.find("/./"); marker != std::string::npos;
		marker = resolved.find("/./")) {
		resolved.replace(marker, 2, "");
	}
	if (resolved.size() >= 2 && resolved.ends_with("/.")) resolved.resize(resolved.size() - 2);
	return darling_windows_CFURLCreateWithFileSystemPath(resolved.c_str());
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateFromFileSystemRepresentation(
	const unsigned char* bytes, darling_windows_CFIndex length, bool is_directory)
{
	(void)is_directory;
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	std::string path(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length));
	return darling_windows_CFURLCreateWithFileSystemPath(path.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPath(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(object->string);
	return darling_windows_CFStringCreateWithCString(
		object->string.substr(parts.path_start, parts.path_end - parts.path_start).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyParameterString(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(object->string);
	if (parts.parameter_start == std::string::npos) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.substr(
		parts.parameter_start, parts.resource_end - parts.parameter_start).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPathDecoded(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(object->string);
	const auto path = object->string.substr(parts.path_start, parts.path_end - parts.path_start);
	const auto decoded = PercentDecode(path);
	return darling_windows_CFStringCreateWithCString(decoded.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyQueryString(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto query = object->string.find('?');
	if (query == std::string::npos) return nullptr;
	const auto end = object->string.find('#', query + 1);
	return darling_windows_CFStringCreateWithCString(object->string.substr(
		query + 1, end == std::string::npos ? std::string::npos : end - query - 1).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyQueryStringDecoded(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto query = object->string.find('?');
	if (query == std::string::npos) return nullptr;
	const auto end = object->string.find('#', query + 1);
	const auto raw = object->string.substr(
		query + 1, end == std::string::npos ? std::string::npos : end - query - 1);
	return darling_windows_CFStringCreateWithCString(PercentDecode(raw).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFragment(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto fragment = object->string.find('#');
	if (fragment == std::string::npos) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.substr(fragment + 1).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFragmentDecoded(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto fragment = object->string.find('#');
	if (fragment == std::string::npos) return nullptr;
	return darling_windows_CFStringCreateWithCString(
		PercentDecode(object->string.substr(fragment + 1)).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyScheme(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto colon = object->string.find(':');
	if (colon == std::string::npos || colon == 0) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.substr(0, colon).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyResourceSpecifier(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto colon = object->string.find(':');
	if (colon == std::string::npos || colon + 1 >= object->string.size()) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.substr(colon + 1).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyHostName(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto scheme = object->string.find("://");
	if (scheme == std::string::npos) return nullptr;
	const auto start = scheme + 3;
	const auto end = object->string[start] == '[' ? object->string.find(']', start) :
		object->string.find_first_of("/:?#", start);
	if (end == start) return nullptr;
	const auto offset = object->string[start] == '[' ? 1 : 0;
	return darling_windows_CFStringCreateWithCString(object->string.substr(
		start + offset, end == std::string::npos ? std::string::npos : end - start - offset).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyNetLocation(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto scheme = object->string.find("://");
	if (scheme == std::string::npos) return nullptr;
	const auto start = scheme + 3;
	const auto end = object->string.find_first_of("/?#", start);
	if (end == start) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.substr(
		start, end == std::string::npos ? std::string::npos : end - start).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyUserName(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto scheme = object->string.find("://");
	if (scheme == std::string::npos) return nullptr;
	const auto start = scheme + 3;
	const auto end = object->string.find_first_of("/?#", start);
	const auto authority = object->string.substr(start, end == std::string::npos ? std::string::npos : end - start);
	const auto at = authority.rfind('@');
	if (at == std::string::npos) return nullptr;
	const auto colon = authority.find(':');
	return darling_windows_CFStringCreateWithCString(PercentDecode(authority.substr(0,
		colon == std::string::npos || colon > at ? at : colon)).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPassword(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto scheme = object->string.find("://");
	if (scheme == std::string::npos) return nullptr;
	const auto start = scheme + 3;
	const auto end = object->string.find_first_of("/?#", start);
	const auto authority = object->string.substr(start, end == std::string::npos ? std::string::npos : end - start);
	const auto at = authority.rfind('@');
	if (at == std::string::npos) return nullptr;
	const auto colon = authority.find(':');
	if (colon == std::string::npos || colon > at) return nullptr;
	return darling_windows_CFStringCreateWithCString(PercentDecode(authority.substr(
		colon + 1, at - colon - 1)).c_str());
}

extern "C" int darling_windows_CFURLGetPortNumber(darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return -1;
	const auto scheme = object->string.find("://");
	if (scheme == std::string::npos) return -1;
	const auto start = scheme + 3;
	const auto colon = object->string[start] == '[' ? object->string.find(':', object->string.find(']', start)) :
		object->string.find(':', start);
	if (colon == std::string::npos) return -1;
	const auto end = object->string.find_first_of("/?#", colon + 1);
	const auto text = object->string.substr(colon + 1,
		end == std::string::npos ? std::string::npos : end - colon - 1);
	if (text.empty()) return -1;
	try {
		const auto port = std::stoi(text);
		return port >= 0 && port <= 65535 ? port : -1;
	} catch (...) {
		return -1;
	}
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyLastPathComponent(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(object->string);
	const auto path = object->string.substr(parts.path_start, parts.path_end - parts.path_start);
	const auto end = path.find_last_not_of("/\\");
	if (end == std::string::npos) return darling_windows_CFStringCreateWithCString("");
	const auto separator = path.find_last_of("/\\", end);
	const auto start = separator == std::string::npos ? 0 : separator + 1;
	return darling_windows_CFStringCreateWithCString(path.substr(start, end - start + 1).c_str());
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateCopyDeletingLastPathComponent(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto end = object->string.find_last_not_of("/\\");
	if (end == std::string::npos) return darling_windows_CFURLCreateWithFileSystemPath(object->string.c_str());
	const auto separator = object->string.find_last_of("/\\", end);
	if (separator == std::string::npos) return darling_windows_CFURLCreateWithFileSystemPath("");
	return darling_windows_CFURLCreateWithFileSystemPath(object->string.substr(0, separator + 1).c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPathExtension(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(object->string);
	const auto path = object->string.substr(parts.path_start, parts.path_end - parts.path_start);
	const auto end = path.find_last_not_of("/\\");
	if (end == std::string::npos) return darling_windows_CFStringCreateWithCString("");
	const auto separator = path.find_last_of("/\\", end);
	const auto dot = path.find_last_of('.', end);
	if (dot == std::string::npos || dot <= (separator == std::string::npos ? 0 : separator + 1) || dot == end)
		return darling_windows_CFStringCreateWithCString("");
	return darling_windows_CFStringCreateWithCString(path.substr(dot + 1, end - dot).c_str());
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateCopyDeletingPathExtension(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	const auto end = object->string.find_last_not_of("/\\");
	if (end == std::string::npos) return darling_windows_CFURLCreateWithFileSystemPath(object->string.c_str());
	const auto separator = object->string.find_last_of("/\\", end);
	const auto dot = object->string.find_last_of('.', end);
	if (dot == std::string::npos || dot <= (separator == std::string::npos ? 0 : separator + 1) || dot == end)
		return darling_windows_CFURLCreateWithFileSystemPath(object->string.c_str());
	return darling_windows_CFURLCreateWithFileSystemPath(object->string.substr(0, dot).c_str());
}

extern "C" darling_windows_CFURLRef darling_windows_CFURLCopyAbsoluteURL(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	return darling_windows_CFURLCreateWithFileSystemPath(object->string.c_str());
}

extern "C" darling_windows_CFStringRef darling_windows_CFURLGetString(
	darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	return darling_windows_CFStringCreateWithCString(object->string.c_str());
}

namespace {
darling_windows_CFStringRef BorrowURLString(darling_windows_CFURLRef value)
{
	auto* object = const_cast<Object*>(static_cast<const Object*>(value));
	if (object == nullptr || object->kind != Kind::URL) return nullptr;
	std::lock_guard lock(object->url_string_mutex);
	if (object->url_string_cache == nullptr)
		object->url_string_cache = darling_windows_CFStringCreateWithCString(object->string.c_str());
	return object->url_string_cache;
}
}

extern "C" bool darling_windows_CFURLHasDirectoryPath(darling_windows_CFURLRef value)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL || object->string.empty()) return false;
	const char last = object->string.back();
	return last == '/' || last == '\\';
}

extern "C" darling_windows_CFTypeID darling_windows_CFURLGetTypeID()
{
	return TypeId(Kind::URL);
}

extern "C" bool darling_windows_CFURLGetFileSystemRepresentation(
	darling_windows_CFURLRef value, bool resolve_against_base, unsigned char* buffer,
	darling_windows_CFIndex buffer_capacity)
{
	(void)resolve_against_base;
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::URL || buffer == nullptr ||
		buffer_capacity <= 0 || object->string.size() + 1 >
		static_cast<std::size_t>(buffer_capacity)) return false;
	std::memcpy(buffer, object->string.c_str(), object->string.size() + 1);
	return true;
}

extern "C" darling_windows_CFTypeID darling_windows_CFStringGetTypeID() { return TypeId(Kind::String); }
extern "C" darling_windows_CFTypeID darling_windows_CFDataGetTypeID() { return TypeId(Kind::Data); }
extern "C" darling_windows_CFTypeID darling_windows_CFArrayGetTypeID() { return TypeId(Kind::Array); }
extern "C" darling_windows_CFTypeID darling_windows_CFDictionaryGetTypeID() { return TypeId(Kind::Dictionary); }
extern "C" darling_windows_CFTypeID darling_windows_CFSetGetTypeID() { return TypeId(Kind::Set); }
extern "C" darling_windows_CFTypeID darling_windows_CFNumberGetTypeID() { return TypeId(Kind::Number); }
extern "C" darling_windows_CFTypeID darling_windows_CFDateGetTypeID() { return TypeId(Kind::Date); }

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRetain(const void* value)
{
	return darling_windows_CFRetain(value);
}

extern "C" DARLING_CF_GUEST_ABI void CFRelease(const void* value)
{
	darling_windows_CFRelease(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFGetTypeID(const void* value)
{
	return darling_windows_CFGetTypeID(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDataRef CFDataCreate(
	const void* allocator, const unsigned char* bytes, darling_windows_CFIndex length)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Data);
	if (object == nullptr) return nullptr;
	try {
		if (length != 0) object->data.assign(bytes, bytes + length);
	} catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableDataRef CFDataCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity)
{
	if (capacity < 0) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Data);
	if (object == nullptr) return nullptr;
	try { object->data.reserve(static_cast<std::size_t>(capacity)); }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDataRef CFDataCreateCopy(
	const void* allocator, darling_windows_CFDataRef data)
{
	const auto* source = static_cast<const Object*>(data);
	if (source == nullptr || source->kind != Kind::Data) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Data);
	if (object == nullptr) return nullptr;
	try { object->data = source->data; }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI const unsigned char* CFDataGetBytePtr(
	darling_windows_CFDataRef data)
{
	return darling_windows_CFDataGetBytePtr(data);
}

extern "C" DARLING_CF_GUEST_ABI unsigned char* CFDataGetMutableBytePtr(
	darling_windows_CFDataRef data)
{
	return darling_windows_CFDataGetMutableBytePtr(data);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFDataGetLength(
	darling_windows_CFDataRef data)
{
	return darling_windows_CFDataGetLength(data);
}

extern "C" DARLING_CF_GUEST_ABI void CFDataGetBytes(darling_windows_CFDataRef data,
	darling_windows_CFRange range, unsigned char* buffer)
{
	(void)darling_windows_CFDataGetBytes(data, range.location, range.length, buffer);
}

extern "C" DARLING_CF_GUEST_ABI void CFDataAppendBytes(darling_windows_CFDataRef data,
	const unsigned char* bytes, darling_windows_CFIndex length)
{
	(void)darling_windows_CFDataAppendBytes(data, bytes, length);
}

extern "C" DARLING_CF_GUEST_ABI void CFDataReplaceBytes(darling_windows_CFDataRef data,
	darling_windows_CFRange range, const unsigned char* bytes,
	darling_windows_CFIndex length)
{
	(void)darling_windows_CFDataReplaceBytes(data, range.location, range.length, bytes, length);
}

extern "C" DARLING_CF_GUEST_ABI void CFDataSetLength(darling_windows_CFDataRef data,
	darling_windows_CFIndex length)
{
	(void)darling_windows_CFDataSetLength(data, length);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDataGetTypeID()
{
	return darling_windows_CFDataGetTypeID();
}

extern "C" darling_windows_CFBooleanRef darling_windows_CFBooleanGetValue(bool value)
{
	static Object false_value{};
	static Object true_value{};
	static const bool initialized = ((false_value.kind = Kind::Boolean),
		(true_value.kind = Kind::Boolean), (true_value.boolean = true),
		(false_value.immortal = true), (true_value.immortal = true), true);
	(void)initialized;
	return value ? &true_value : &false_value;
}

extern "C" bool darling_windows_CFBooleanIsTrue(darling_windows_CFBooleanRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Boolean && object->boolean;
}

extern "C" darling_windows_CFTypeID darling_windows_CFBooleanGetTypeID() { return TypeId(Kind::Boolean); }

extern "C" darling_windows_CFTypeRef darling_windows_CFNullGetValue()
{
	static Object null_value{};
	static const bool initialized = (null_value.kind = Kind::Null, null_value.immortal = true, true);
	(void)initialized;
	return &null_value;
}

extern "C" darling_windows_CFTypeID darling_windows_CFNullGetTypeID() { return TypeId(Kind::Null); }

extern "C" darling_windows_CFTypeID
darling_windows_CFPropertyListUIDGetTypeID()
{
	return TypeId(Kind::PropertyListUID);
}

extern "C" bool darling_windows_CFPropertyListUIDGetValue(
	darling_windows_CFTypeRef value, std::uint64_t* uid)
{
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::PropertyListUID || uid == nullptr)
		return false;
	*uid = object->property_list_uid;
	return true;
}

extern "C" darling_windows_CFStringRef darling_windows_CFPropertyListCreateXML(
	darling_windows_CFTypeRef value)
{
	std::ostringstream payload;
	payload << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\">";
	AppendXML(static_cast<const Object*>(value), payload);
	payload << "</plist>";
	return darling_windows_CFStringCreateWithCString(payload.str().c_str());
}

extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromXML(const char* xml)
{
	if (xml == nullptr) return nullptr;
	std::string document(xml);
	const auto plist = document.find("<plist");
	std::size_t position = plist == std::string::npos ? 0 : document.find('>', plist);
	if (position == std::string::npos) return nullptr;
	++position;
	return ParsePropertyValue(document, position);
}

extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromBinary(
	const void* bytes, darling_windows_CFIndex length)
{
	if (bytes == nullptr || length < 40) return nullptr;
	const auto* input = static_cast<const unsigned char*>(bytes);
	if (std::memcmp(input, "bplist00", 8) != 0) return nullptr;
	const auto trailer = input + length - 32;
	BinaryPlistContext context{input, static_cast<std::size_t>(length), trailer[6], trailer[7],
		static_cast<std::size_t>(ReadBigEndian(trailer + 8, 8)), 8,
		static_cast<std::size_t>(ReadBigEndian(trailer + 24, 8))};
	if (context.offset_size == 0 || context.reference_size == 0 || context.object_count == 0 ||
		context.object_count > context.length || context.offsets_table >= context.length ||
		context.offsets_table < context.object_table ||
		context.offsets_table + context.object_count * context.offset_size > context.length) return nullptr;
	std::vector<bool> active(context.object_count, false);
	return ParseBinaryObject(context, static_cast<std::size_t>(ReadBigEndian(trailer + 16, 8)), active);
}

extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetCurrent()
{
	return &current_run_loop;
}

extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetMain()
{
	return &main_run_loop;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopGetTypeID()
{
	return TypeId(Kind::RunLoop);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFRunLoopCopyCurrentMode(
	darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return nullptr;
	auto* loop = static_cast<RunLoop*>(value);
	std::string mode;
	{
		std::lock_guard lock(loop->state->mutex);
		try { mode = loop->state->current_mode; }
		catch (...) { return nullptr; }
	}
	if (mode.empty()) return nullptr;
	auto* result = new (std::nothrow) Object{};
	if (result == nullptr) return nullptr;
	result->kind = Kind::String;
	try { result->string = std::move(mode); }
	catch (...) { delete result; return nullptr; }
	return result;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFRunLoopCopyAllModes(
	darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return nullptr;
	auto* loop = static_cast<RunLoop*>(value);
	std::vector<std::string> modes;
	try {
		std::lock_guard lock(loop->state->mutex);
		modes = loop->state->common_modes;
		auto append_mode = [&modes](const std::string& mode) {
			if (!mode.empty() && std::find(modes.begin(), modes.end(), mode) == modes.end())
				modes.push_back(mode);
		};
		for (const auto& mode : loop->state->mode_stack) append_mode(mode);
		for (const auto& entry : loop->state->blocks)
			for (const auto& mode : entry.modes) append_mode(mode);
		for (const auto& entry : loop->state->sources) append_mode(entry.mode);
		for (const auto& entry : loop->state->timers) append_mode(entry.mode);
		for (const auto& entry : loop->state->observers) append_mode(entry.mode);
	} catch (...) { return nullptr; }
	std::vector<const void*> strings;
	try { strings.reserve(modes.size()); }
	catch (...) { return nullptr; }
	for (const auto& mode : modes) {
		auto* string = new (std::nothrow) Object{};
		if (string == nullptr) {
			for (const auto* item : strings)
				ReleaseOwned(const_cast<Object*>(static_cast<const Object*>(item)));
			return nullptr;
		}
		string->kind = Kind::String;
		try { string->string = mode; }
		catch (...) {
			delete string;
			for (const auto* item : strings)
				ReleaseOwned(const_cast<Object*>(static_cast<const Object*>(item)));
			return nullptr;
		}
		strings.push_back(string);
	}
	darling_windows_CFArrayRef result = nullptr;
	try {
		result = darling_windows_CFArrayCreate(strings.data(),
			static_cast<darling_windows_CFIndex>(strings.size()));
	} catch (...) { result = nullptr; }
	for (const auto* item : strings)
		ReleaseOwned(const_cast<Object*>(static_cast<const Object*>(item)));
	return result;
}

extern "C" void darling_windows_CFRunLoopUseCurrentAsMain()
{
	current_run_loop.state = main_run_loop.state;
}

namespace {
constexpr double kCFAbsoluteTimeUnixOffset = 978307200.0;
constexpr std::uint64_t kCFRunLoopActivityEntry = 1ull << 0;
constexpr std::uint64_t kCFRunLoopActivityBeforeTimers = 1ull << 1;
constexpr std::uint64_t kCFRunLoopActivityBeforeSources = 1ull << 2;
constexpr std::uint64_t kCFRunLoopActivityBeforeWaiting = 1ull << 5;
constexpr std::uint64_t kCFRunLoopActivityAfterWaiting = 1ull << 6;
constexpr std::uint64_t kCFRunLoopActivityExit = 1ull << 7;
constexpr std::uint64_t kCFRunLoopAllActivities = 0x0fffffffULL;
double CFAbsoluteTimeNow()
{
	return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() -
		kCFAbsoluteTimeUnixOffset;
}
bool RunLoopTimerMatchesMode(const RunLoopState::Timer& timer, const std::string& mode)
{
	return timer.mode == mode;
}
bool HasDueRunLoopTimer(const RunLoopState& state, const std::string& mode)
{
	const double now = CFAbsoluteTimeNow();
	for (const auto& binding : state.timers) {
		if (!RunLoopTimerMatchesMode(binding, mode)) continue;
		std::lock_guard lock(binding.object->run_loop_timer_mutex);
		if (binding.object->run_loop_timer_valid &&
			binding.object->run_loop_timer_fire_date <= now) return true;
	}
	return false;
}
bool HasRunLoopMachSource(const RunLoopState& state, const std::string& mode)
{
	for (const auto& binding : state.sources) {
		if (binding.mode != mode) continue;
		std::lock_guard source_lock(binding.object->run_loop_source_mutex);
		if (binding.object->run_loop_source_valid && binding.object->run_loop_source_is_v1)
			return true;
	}
	return false;
}
struct RunLoopMachMessage final {
	Object* source{};
	DarlingCFRunLoopSource1Perform perform{};
	void* info{};
	const void* allocator{};
	std::vector<std::uint8_t> bytes;
};
std::vector<RunLoopMachMessage> CollectRunLoopMachMessages(
	const std::shared_ptr<RunLoopState>& state, const std::string& mode)
{
	std::vector<Object*> sources;
	{
		std::lock_guard state_lock(state->mutex);
		for (const auto& binding : state->sources) {
			if (binding.mode != mode) continue;
			auto* source = binding.object;
			std::lock_guard source_lock(source->run_loop_source_mutex);
			if (!source->run_loop_source_valid || !source->run_loop_source_is_v1) continue;
			RetainOwned(source);
			try { sources.push_back(source); }
			catch (...) { ReleaseOwned(source); break; }
		}
	}
	std::stable_sort(sources.begin(), sources.end(), [](const Object* left, const Object* right) {
		return left->run_loop_source_order < right->run_loop_source_order;
	});
	std::vector<RunLoopMachMessage> messages;
	for (auto* source : sources) {
		DarlingCFRunLoopSource1GetPort get_port = nullptr;
		DarlingCFRunLoopSource1Perform perform = nullptr;
		void* info = nullptr;
		const void* allocator = nullptr;
		darling_mach_port_name_t registered_port = 0;
		{
			std::lock_guard source_lock(source->run_loop_source_mutex);
			if (source->run_loop_source_valid && source->run_loop_source_is_v1) {
				get_port = source->run_loop_source_context1.get_port;
				perform = source->run_loop_source_context1.perform;
				info = const_cast<void*>(source->run_loop_source_info);
				allocator = source->run_loop_source_allocator;
				registered_port = source->run_loop_source_port;
			}
		}
		if (get_port == nullptr || perform == nullptr) { ReleaseOwned(source); continue; }
		const auto port = get_port(info);
		if (port == 0 || port != registered_port) { ReleaseOwned(source); continue; }
		std::uint32_t sequence = 0, message_size = 0, trailer_size = 0;
		darling_mach_msg_id_t message_id = 0;
		const auto peek = darling_windows_mach_port_peek(darling_windows_mach_task_self(),
			port, 0, &sequence, &message_size, &message_id, nullptr, &trailer_size);
		if (peek != 0 || message_size < sizeof(darling_mach_msg_header)) {
			ReleaseOwned(source);
			continue;
		}
		RunLoopMachMessage pending;
		pending.source = source;
		pending.perform = perform;
		pending.info = info;
		pending.allocator = allocator;
		try { pending.bytes.resize(message_size); }
		catch (...) { ReleaseOwned(source); continue; }
		std::uint32_t received_size = 0;
		if (darling_windows_mach_port_receive(port, pending.bytes.data(), message_size,
			&received_size, 0) != 0 || received_size < sizeof(darling_mach_msg_header) ||
			received_size > message_size) {
			ReleaseOwned(source);
			continue;
		}
		pending.bytes.resize(received_size);
		try { messages.push_back(std::move(pending)); }
		catch (...) { ReleaseOwned(source); }
	}
	return messages;
}
std::optional<std::chrono::steady_clock::time_point> NextRunLoopTimerDeadline(
	const RunLoopState& state, const std::string& mode)
{
	const double now_absolute = CFAbsoluteTimeNow();
	const auto now_steady = std::chrono::steady_clock::now();
	std::optional<std::chrono::steady_clock::time_point> deadline;
	for (const auto& binding : state.timers) {
		if (!RunLoopTimerMatchesMode(binding, mode)) continue;
		std::lock_guard lock(binding.object->run_loop_timer_mutex);
		if (!binding.object->run_loop_timer_valid) continue;
		const double remaining = (std::max)(0.0,
			binding.object->run_loop_timer_fire_date - now_absolute);
		const auto bounded = (std::min)(remaining, 315360000.0);
		const auto candidate = now_steady + std::chrono::duration_cast<
			std::chrono::steady_clock::duration>(std::chrono::duration<double>(bounded));
		if (!deadline || candidate < *deadline) deadline = candidate;
	}
	return deadline;
}
std::vector<Object*> CollectDueRunLoopTimers(RunLoopState& state, const std::string& mode)
{
	std::vector<Object*> due;
	try { due.reserve(state.timers.size()); }
	catch (...) { return due; }
	const double now = CFAbsoluteTimeNow();
	for (const auto& binding : state.timers) {
		if (!RunLoopTimerMatchesMode(binding, mode)) continue;
		auto* timer = binding.object;
		std::lock_guard lock(timer->run_loop_timer_mutex);
		if (!timer->run_loop_timer_valid || timer->run_loop_timer_fire_date > now) continue;
		RetainOwned(timer);
		try { due.push_back(timer); }
		catch (...) { ReleaseOwned(timer); break; }
		if (timer->run_loop_timer_interval <= 0) {
			timer->run_loop_timer_valid = false;
		} else {
			const double missed = (std::floor)((now - timer->run_loop_timer_fire_date) /
				timer->run_loop_timer_interval);
			timer->run_loop_timer_fire_date += (missed + 1.0) * timer->run_loop_timer_interval;
			if (!std::isfinite(timer->run_loop_timer_fire_date))
				timer->run_loop_timer_valid = false;
		}
	}
	std::stable_sort(due.begin(), due.end(), [](const Object* left, const Object* right) {
		return left->run_loop_timer_order < right->run_loop_timer_order;
	});
	return due;
}
void FireRunLoopTimer(Object* timer)
{
	if (timer == nullptr || timer->kind != Kind::RunLoopTimer) return;
	const auto callout = timer->run_loop_timer_callout;
	void* info = const_cast<void*>(timer->run_loop_timer_info);
	if (callout != nullptr) {
		try { callout(timer, info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (timer->run_loop_timer_interval <= 0 || !CFRunLoopTimerIsValid(timer))
		CFRunLoopTimerInvalidate(timer);
	ReleaseOwned(timer);
}
void NotifyRunLoopObservers(const std::shared_ptr<RunLoopState>& state,
	const std::string& mode, std::uint64_t activity,
	std::unique_lock<std::mutex>& state_lock)
{
	std::vector<Object*> pending;
	try { pending.reserve(state->observers.size()); }
	catch (...) { return; }
	for (const auto& binding : state->observers) {
		if (binding.mode != mode) continue;
		auto* observer = binding.object;
		std::lock_guard observer_lock(observer->run_loop_observer_mutex);
		if (!observer->run_loop_observer_valid ||
			(observer->run_loop_observer_activities & activity) == 0) continue;
		RetainOwned(observer);
		try { pending.push_back(observer); }
		catch (...) { ReleaseOwned(observer); break; }
		if (!observer->run_loop_observer_repeats)
			observer->run_loop_observer_valid = false;
	}
	std::stable_sort(pending.begin(), pending.end(), [](const Object* left, const Object* right) {
		return left->run_loop_observer_order < right->run_loop_observer_order;
	});
	state_lock.unlock();
	for (auto* observer : pending) {
		const auto callout = observer->run_loop_observer_callout;
		void* info = const_cast<void*>(observer->run_loop_observer_info);
		const bool repeats = observer->run_loop_observer_repeats;
		if (callout != nullptr) {
			try { callout(observer, activity, info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		if (!repeats) CFRunLoopObserverInvalidate(observer);
		ReleaseOwned(observer);
	}
	state_lock.lock();
}
}

extern "C" int darling_windows_CFRunLoopRunInMode(double seconds, bool return_after_source)
{
	return darling_windows_CFRunLoopRunInModeForLoop(&current_run_loop,
		"kCFRunLoopDefaultMode", seconds, return_after_source);
}

extern "C" int darling_windows_CFRunLoopRunInModeForLoop(
	darling_windows_CFRunLoopRef value, const char* mode, double seconds,
	bool return_after_source)
{
	if (value == nullptr || mode == nullptr || *mode == '\0' ||
		std::strcmp(mode, "kCFRunLoopCommonModes") == 0 ||
		!std::isfinite(seconds) || seconds < 0) return -1;
	auto& loop = *static_cast<RunLoop*>(value);
	const auto state = loop.state;
	const std::string active_mode(mode);
	std::unique_lock lock(state->mutex);
	try { state->mode_stack.push_back(active_mode); }
	catch (...) { return -1; }
	state->activation_depth = state->mode_stack.size();
	state->running = true;
	state->running_thread = std::this_thread::get_id();
	state->stopped = false;
	state->current_mode = active_mode;
	constexpr double max_wait_seconds = 315360000.0;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>((std::min)(seconds, max_wait_seconds)));
	bool returned_after_source = false;
	NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityEntry, lock);
	while (!state->stopped) {
		NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityBeforeTimers, lock);
		auto due_timers = CollectDueRunLoopTimers(*state, active_mode);
		lock.unlock();
		for (auto* timer : due_timers) FireRunLoopTimer(timer);
		lock.lock();
		if (state->stopped) break;
		NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityBeforeSources, lock);
		lock.unlock();
		auto mach_messages = CollectRunLoopMachMessages(state, active_mode);
		lock.lock();
		if (state->stopped) break;
		if (!mach_messages.empty()) {
			lock.unlock();
			for (auto& pending : mach_messages) {
				bool valid = false;
				{
					std::lock_guard source_lock(pending.source->run_loop_source_mutex);
					valid = pending.source->run_loop_source_valid &&
						pending.source->run_loop_source_is_v1;
				}
				if (valid) {
					try {
						auto* reply = static_cast<darling_mach_msg_header*>(pending.perform(pending.bytes.data(),
						static_cast<darling_windows_CFIndex>(pending.bytes.size()),
						pending.allocator, pending.info));
						if (reply != nullptr) {
							if (reply->msgh_size >= sizeof(darling_mach_msg_header))
								(void)darling_windows_mach_msg(reply, darling_mach_send_msg,
									reply->msgh_size, 0, 0, 0, 0);
							CFAllocatorDeallocate(pending.allocator, reply);
						}
					} catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
				}
				ReleaseOwned(pending.source);
			}
			lock.lock();
			if (return_after_source) {
				returned_after_source = true;
				break;
			}
			continue;
		}
		const bool has_blocks = HasRunLoopBlockForMode(*state, active_mode);
		if (!has_blocks) {
			auto wait_deadline = deadline;
			if (const auto timer_deadline = NextRunLoopTimerDeadline(*state, active_mode);
				timer_deadline && *timer_deadline < wait_deadline)
				wait_deadline = *timer_deadline;
			if (HasRunLoopMachSource(*state, active_mode)) {
				const auto poll_deadline = std::chrono::steady_clock::now() +
					std::chrono::milliseconds(10);
				if (poll_deadline < wait_deadline) wait_deadline = poll_deadline;
			}
			const bool will_wait = seconds > 0 &&
				wait_deadline > std::chrono::steady_clock::now();
			if (will_wait)
				NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityBeforeWaiting, lock);
			bool woke = false;
			bool dispatched_input = false;
			bool used_native_wait = false;
			if (will_wait && state == main_run_loop.state) {
				const auto remaining = wait_deadline - std::chrono::steady_clock::now();
				auto timeout = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
				timeout = (std::clamp)(timeout, std::int64_t{0},
					static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()));
				lock.unlock();
				const auto input = darling_windows_appkit_wait_run_loop_input(
					value, active_mode.c_str(), static_cast<std::uint32_t>(timeout));
				lock.lock();
				if (input >= 0) {
					used_native_wait = true;
					woke = input > 0;
					dispatched_input = input > 0;
				}
			}
			if (!used_native_wait) {
				state->waiting = will_wait;
				woke = state->condition.wait_until(lock, wait_deadline,
					[&state, &active_mode] {
						return state->stopped || HasRunLoopBlockForMode(*state, active_mode) ||
							HasDueRunLoopTimer(*state, active_mode);
					});
				state->waiting = false;
			}
			if (will_wait)
				NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityAfterWaiting, lock);
			if (return_after_source && dispatched_input) {
				returned_after_source = true;
				break;
			}
			if (!woke && std::chrono::steady_clock::now() >= deadline) break;
			continue;
		}
		std::vector<RunLoopState::Block> pending;
		if (has_blocks && return_after_source) {
			auto found = std::find_if(state->blocks.begin(), state->blocks.end(),
				[&state, &active_mode](const RunLoopState::Block& block) {
					return RunLoopBlockMatchesMode(*state, block, active_mode);
				});
			if (found == state->blocks.end()) continue;
			for (auto candidate = std::next(found); candidate != state->blocks.end(); ++candidate) {
				if (RunLoopBlockMatchesMode(*state, *candidate, active_mode) &&
					candidate->order < found->order) found = candidate;
			}
			pending.push_back(std::move(*found));
			state->blocks.erase(found);
			returned_after_source = true;
		} else if (has_blocks) {
			for (auto iterator = state->blocks.begin(); iterator != state->blocks.end();) {
				if (RunLoopBlockMatchesMode(*state, *iterator, active_mode)) {
					pending.push_back(std::move(*iterator));
					iterator = state->blocks.erase(iterator);
				} else {
					++iterator;
				}
			}
			std::stable_sort(pending.begin(), pending.end(),
				[](const RunLoopState::Block& left, const RunLoopState::Block& right) {
					return left.order < right.order;
				});
		}
		lock.unlock();
		for (const auto& entry : pending) {
			if (entry.callback == nullptr) continue;
			try { entry.callback(entry.context); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		lock.lock();
		if (return_after_source && returned_after_source) break;
		if (std::chrono::steady_clock::now() >= deadline &&
			!HasRunLoopBlockForMode(*state, active_mode) &&
			!HasDueRunLoopTimer(*state, active_mode)) break;
	}
	NotifyRunLoopObservers(state, active_mode, kCFRunLoopActivityExit, lock);
	if (!state->mode_stack.empty()) state->mode_stack.pop_back();
	state->activation_depth = state->mode_stack.size();
	state->running = state->activation_depth != 0;
	state->waiting = false;
	if (!state->running) state->running_thread = {};
	state->current_mode = state->mode_stack.empty() ? std::string{} :
		state->mode_stack.back();
	return state->stopped || returned_after_source ? 0 : 1;
}

extern "C" void darling_windows_CFRunLoopStop(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return;
	auto* loop = static_cast<RunLoop*>(value);
	{
		std::lock_guard lock(loop->state->mutex);
		loop->state->stopped = true;
	}
	loop->state->condition.notify_all();
	if (loop->state == main_run_loop.state)
		darling_windows_appkit_wake_main_run_loop();
}

extern "C" bool darling_windows_CFRunLoopIsRunning(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return false;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	return loop->state->running;
}

extern "C" bool darling_windows_CFRunLoopIsWaiting(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return false;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	return loop->state->waiting && loop->state->running &&
		loop->state->running_thread != std::this_thread::get_id();
}

extern "C" bool darling_windows_CFRunLoopIsCommonMode(
	darling_windows_CFRunLoopRef value, const char* mode)
{
	if (value == nullptr || mode == nullptr || *mode == '\0') return false;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	return std::find(loop->state->common_modes.begin(),
		loop->state->common_modes.end(), mode) != loop->state->common_modes.end();
}

extern "C" void darling_windows_CFRunLoopSetWaiting(
	darling_windows_CFRunLoopRef value, bool waiting)
{
	if (value == nullptr) return;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	loop->state->waiting = waiting;
}

extern "C" bool darling_windows_CFRunLoopContainsPendingBlockForMode(
	darling_windows_CFRunLoopRef value, const char* mode)
{
	if (value == nullptr || mode == nullptr || *mode == '\0') return false;
	auto* loop = static_cast<RunLoop*>(value);
	std::lock_guard lock(loop->state->mutex);
	return HasRunLoopBlockForMode(*loop->state, mode);
}

extern "C" bool darling_windows_CFRunLoopPerformBlock(darling_windows_CFRunLoopRef value,
	darling_windows_CFRunLoopBlock block, void* context)
{
	return darling_windows_CFRunLoopPerformBlockInMode(value,
		"kCFRunLoopDefaultMode", block, context);
}

extern "C" bool darling_windows_CFRunLoopPerformBlockInMode(
	darling_windows_CFRunLoopRef value, const char* mode,
	darling_windows_CFRunLoopBlock block, void* context)
{
	return mode == nullptr ? false : darling_windows_CFRunLoopPerformBlockInModesWithOrder(
		value, &mode, 1, 0, block, context);
}

extern "C" bool darling_windows_CFRunLoopPerformBlockInModesWithOrder(
	darling_windows_CFRunLoopRef value, const char* const* modes,
	std::size_t mode_count, std::uint64_t order,
	darling_windows_CFRunLoopBlock block, void* context)
{
	if (value == nullptr || modes == nullptr || mode_count == 0 || mode_count > 256 ||
		block == nullptr) return false;
	RunLoopState::Block pending{block, context, {}, order};
	try {
		pending.modes.reserve(mode_count);
		for (std::size_t index = 0; index < mode_count; ++index) {
			if (modes[index] == nullptr || *modes[index] == '\0') return false;
			pending.modes.emplace_back(modes[index]);
		}
	} catch (...) {
		return false;
	}
	auto* loop = static_cast<RunLoop*>(value);
	{
		std::lock_guard lock(loop->state->mutex);
		try { loop->state->blocks.push_back(std::move(pending)); }
		catch (...) { return false; }
	}
	loop->state->condition.notify_all();
	if (loop->state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	return true;
}

extern "C" void darling_windows_CFRunLoopAddCommonMode(
	darling_windows_CFRunLoopRef value, const char* mode)
{
	if (value == nullptr || mode == nullptr || *mode == '\0' ||
		std::strcmp(mode, "kCFRunLoopCommonModes") == 0) return;
	auto* loop = static_cast<RunLoop*>(value);
	{
		std::lock_guard lock(loop->state->mutex);
		if (std::find(loop->state->common_modes.begin(),
			loop->state->common_modes.end(), mode) == loop->state->common_modes.end()) {
			try { loop->state->common_modes.emplace_back(mode); }
			catch (...) { return; }
		}
	}
	loop->state->condition.notify_all();
	if (loop->state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
}

namespace {
Object* AsRunLoopSource(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::RunLoopSource ? object : nullptr;
}
Object* AsRunLoopTimer(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::RunLoopTimer ? object : nullptr;
}
Object* AsRunLoopObserver(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::RunLoopObserver ? object : nullptr;
}
Object* AsCFMachPort(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::MachPort ? object : nullptr;
}
Object* AsCFAllocator(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::Allocator ? object : nullptr;
}
Object* EffectiveCFAllocator(const void* value)
{
	if (value != nullptr) return AsCFAllocator(value);
	return thread_cf_allocator.value != nullptr ? thread_cf_allocator.value : SystemCFAllocator();
}
Object* CreateObjectWithAllocator(const void* allocator_value, Kind kind)
{
	auto* backing = allocator_value == nullptr ? EffectiveCFAllocator(nullptr) :
		AsCFAllocator(allocator_value);
	if (backing == nullptr) return nullptr;
	void* storage = CFAllocatorAllocate(backing,
		static_cast<darling_windows_CFIndex>(sizeof(Object)), 0);
	if (storage == nullptr) return nullptr;
	Object* object = nullptr;
	try { object = new (storage) Object{}; }
	catch (...) {
		CFAllocatorDeallocate(backing, storage);
		return nullptr;
	}
	object->kind = kind;
	RetainOwned(backing);
	object->object_backing_allocator = backing;
	return object;
}
const void* DARLING_CF_GUEST_ABI CFMachPortRetainInfo(const void* info)
{
	if (auto* port = AsCFMachPort(info)) RetainOwned(port);
	return info;
}
void DARLING_CF_GUEST_ABI CFMachPortReleaseInfo(const void* info)
{
	if (auto* port = AsCFMachPort(info)) ReleaseOwned(port);
}
std::uint32_t DARLING_CF_GUEST_ABI CFMachPortSourceGetPort(void* info)
{
	auto* port = AsCFMachPort(info);
	if (port == nullptr) return 0;
	std::lock_guard lock(port->mach_port_mutex);
	return port->mach_port_valid ? port->mach_port_name : 0;
}
void* DARLING_CF_GUEST_ABI CFMachPortSourcePerform(void* message, darling_windows_CFIndex size,
	const void*, void* info)
{
	auto* port = AsCFMachPort(info);
	if (port == nullptr || message == nullptr || size < 0) return nullptr;
	DarlingCFMachPortCallout callout = nullptr;
	void* callback_info = nullptr;
	{
		std::lock_guard lock(port->mach_port_mutex);
		if (!port->mach_port_valid) return nullptr;
		callout = port->mach_port_callout;
		callback_info = const_cast<void*>(port->mach_port_info);
	}
	if (callout != nullptr) {
		try { callout(port, message, size, callback_info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	return nullptr;
}
std::vector<std::string> RunLoopConcreteModes(const std::shared_ptr<RunLoopState>& state,
	const std::string& mode)
{
	if (mode != "kCFRunLoopCommonModes") return {mode};
	std::lock_guard lock(state->mutex);
	return state->common_modes;
}
void InvokeRunLoopSourceModeCallback(Object* source, bool scheduling,
	void* loop, const std::string& mode)
{
	auto callback = scheduling ? source->run_loop_source_context.schedule :
		source->run_loop_source_context.cancel;
	if (callback == nullptr) return;
	const auto mode_object = RunLoopModeObject(mode);
	if (mode_object == nullptr) return;
	try { callback(const_cast<void*>(source->run_loop_source_info), loop, mode_object); }
	catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	ReleaseOwned(As(mode_object));
}
bool AddRunLoopSourceMode(void* loop, const std::shared_ptr<RunLoopState>& state,
	Object* source, const std::string& mode)
{
	if (source->run_loop_source_is_v1) {
		DarlingCFRunLoopSource1GetPort get_port = nullptr;
		void* info = nullptr;
		{
			std::lock_guard source_lock(source->run_loop_source_mutex);
			if (!source->run_loop_source_valid) return false;
			get_port = source->run_loop_source_context1.get_port;
			info = const_cast<void*>(source->run_loop_source_info);
		}
		if (get_port == nullptr) return false;
		const auto port = get_port(info);
		if (port == 0) return false;
		std::lock_guard registry_lock(run_loop_source1_port_registry_mutex);
		const auto owner = run_loop_source1_port_registry.find(port);
		if (owner != run_loop_source1_port_registry.end() && owner->second != source)
			return false;
		std::lock_guard source_lock(source->run_loop_source_mutex);
		if (source->run_loop_source_port != 0 && source->run_loop_source_port != port)
			return false;
		try { run_loop_source1_port_registry.emplace(port, source); }
		catch (...) { return false; }
		source->run_loop_source_port = port;
	}
	{
		std::lock_guard source_lock(source->run_loop_source_mutex);
		if (!source->run_loop_source_valid) return false;
		const auto found = std::find_if(source->run_loop_source_schedules.begin(),
			source->run_loop_source_schedules.end(), [&](const Object::SourceSchedule& entry) {
				return entry.state.lock() == state && entry.mode == mode;
			});
		if (found != source->run_loop_source_schedules.end()) return true;
		try { source->run_loop_source_schedules.push_back({state, loop, mode}); }
		catch (...) { return false; }
	}
	bool inserted = false;
	{
		std::lock_guard state_lock(state->mutex);
		const auto found = std::find_if(state->sources.begin(), state->sources.end(),
			[&](const RunLoopState::Source& entry) {
				return entry.object == source && entry.mode == mode;
			});
		if (found == state->sources.end()) {
			try {
				RetainOwned(source);
				state->sources.push_back({source, mode});
				inserted = true;
			} catch (...) { ReleaseOwned(source); }
		}
	}
	if (!inserted) {
		std::lock_guard source_lock(source->run_loop_source_mutex);
		std::erase_if(source->run_loop_source_schedules,
			[&](const Object::SourceSchedule& entry) {
				return entry.state.lock() == state && entry.mode == mode;
			});
		return false;
	}
	InvokeRunLoopSourceModeCallback(source, true, loop, mode);
	state->condition.notify_all();
	if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	DispatchSignaledRunLoopSource(source);
	return true;
}
void RemoveRunLoopSourceMode(void* loop, const std::shared_ptr<RunLoopState>& state,
	Object* source, const std::string& mode)
{
	bool removed = false;
	{
		std::lock_guard state_lock(state->mutex);
		for (auto it = state->sources.begin(); it != state->sources.end(); ++it) {
			if (it->object == source && it->mode == mode) {
				state->sources.erase(it);
				removed = true;
				break;
			}
		}
	}
	{
		std::lock_guard source_lock(source->run_loop_source_mutex);
		std::erase_if(source->run_loop_source_schedules,
			[&](const Object::SourceSchedule& entry) {
				return entry.state.lock() == state && entry.mode == mode;
			});
	}
	if (!removed) return;
	InvokeRunLoopSourceModeCallback(source, false, loop, mode);
	ReleaseOwned(source);
	state->condition.notify_all();
	if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopSourceGetTypeID()
{
	return TypeId(Kind::RunLoopSource);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopSourceCreate(
	const void* allocator, darling_windows_CFIndex order,
	const DarlingCFRunLoopSourceContext* context)
{
	if (context == nullptr || (context->version != 0 && context->version != 1)) return nullptr;
	const bool is_v1 = context->version == 1;
	const auto* context1 = reinterpret_cast<const DarlingCFRunLoopSourceContext1*>(context);
	if ((!is_v1 && context->perform == nullptr) ||
		(is_v1 && (context1->get_port == nullptr || context1->perform == nullptr))) return nullptr;
	auto* source = new (std::nothrow) Object{};
	if (source == nullptr) return nullptr;
	source->kind = Kind::RunLoopSource;
	source->run_loop_source_is_v1 = is_v1;
	if (is_v1) source->run_loop_source_context1 = *context1;
	else source->run_loop_source_context = *context;
	source->run_loop_source_allocator = allocator;
	source->run_loop_source_order = order;
	source->run_loop_source_valid = true;
	const auto info = is_v1 ? context1->info : context->info;
	const auto retain = is_v1 ? context1->retain : context->retain;
	if (info != nullptr && retain != nullptr) {
		try { source->run_loop_source_info = retain(info); }
		catch (...) { delete source; return nullptr; }
		if (source->run_loop_source_info == nullptr) { delete source; return nullptr; }
	} else source->run_loop_source_info = info;
	if (is_v1) source->run_loop_source_context1.info = const_cast<void*>(source->run_loop_source_info);
	else source->run_loop_source_context.info = const_cast<void*>(source->run_loop_source_info);
	return source;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopSourceGetOrder(
	darling_windows_CFTypeRef value)
{
	const auto* source = AsRunLoopSource(value);
	return source == nullptr ? 0 : source->run_loop_source_order;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceGetContext(
	darling_windows_CFTypeRef value, DarlingCFRunLoopSourceContext* context)
{
	const auto* source = AsRunLoopSource(value);
	if (source != nullptr && context != nullptr) {
		if (source->run_loop_source_is_v1) {
			*context = {};
			context->version = 1;
			context->info = const_cast<void*>(source->run_loop_source_info);
			context->retain = source->run_loop_source_context1.retain;
			context->release = source->run_loop_source_context1.release;
			context->copy_description = source->run_loop_source_context1.copy_description;
			context->equal = source->run_loop_source_context1.equal;
			context->hash = source->run_loop_source_context1.hash;
		} else {
			*context = source->run_loop_source_context;
			context->info = const_cast<void*>(source->run_loop_source_info);
		}
	}
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopSourceIsValid(
	darling_windows_CFTypeRef value)
{
	auto* source = AsRunLoopSource(value);
	if (source == nullptr) return 0;
	std::lock_guard lock(source->run_loop_source_mutex);
	return source->run_loop_source_valid ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceSignal(darling_windows_CFTypeRef value)
{
	auto* source = AsRunLoopSource(value);
	if (source == nullptr) return;
	{
		std::lock_guard lock(source->run_loop_source_mutex);
		if (!source->run_loop_source_valid) return;
		if (source->run_loop_source_is_v1) return;
		source->run_loop_source_signaled = true;
		++source->run_loop_source_generation;
	}
	DispatchSignaledRunLoopSource(source);
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceInvalidate(
	darling_windows_CFTypeRef value)
{
	auto* source = AsRunLoopSource(value);
	if (source == nullptr) return;
	std::vector<Object::SourceSchedule> schedules;
	{
		std::lock_guard lock(source->run_loop_source_mutex);
		if (!source->run_loop_source_valid) return;
		source->run_loop_source_valid = false;
		source->run_loop_source_signaled = false;
		++source->run_loop_source_generation;
		schedules.swap(source->run_loop_source_schedules);
	}
	for (const auto& schedule : schedules) {
		auto state = schedule.state.lock();
		if (state == nullptr) continue;
		bool removed = false;
		{
			std::lock_guard lock(state->mutex);
			for (auto it = state->sources.begin(); it != state->sources.end(); ++it) {
				if (it->object == source && it->mode == schedule.mode) {
					state->sources.erase(it); removed = true; break;
				}
			}
		}
		InvokeRunLoopSourceModeCallback(source, false, schedule.loop, schedule.mode);
		if (removed) ReleaseOwned(source);
		state->condition.notify_all();
		if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFAllocatorGetTypeID()
{
	return TypeId(Kind::Allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFAllocatorRef CFAllocatorCreate(
	const void* object_allocator, const DarlingCFAllocatorContext* context)
{
	if (context == nullptr || context->version != 0 || context->allocate == nullptr)
		return nullptr;
	const bool use_context = object_allocator == &allocator_use_context_sentinel;
	const void* retained_info = context->info;
	if (retained_info != nullptr && context->retain != nullptr) {
		try { retained_info = context->retain(retained_info); }
		catch (...) { return nullptr; }
	}
	Object* backing = nullptr;
	void* storage = nullptr;
	if (use_context) {
		try { storage = context->allocate(static_cast<darling_windows_CFIndex>(sizeof(Object)),
			0, const_cast<void*>(retained_info)); }
		catch (...) { storage = nullptr; }
	} else {
		backing = object_allocator == nullptr ? EffectiveCFAllocator(nullptr) :
			AsCFAllocator(object_allocator);
		if (backing != nullptr)
			storage = CFAllocatorAllocate(backing,
				static_cast<darling_windows_CFIndex>(sizeof(Object)), 0);
	}
	if (storage == nullptr) {
		if (retained_info != nullptr && context->release != nullptr) {
			try { context->release(retained_info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		return nullptr;
	}
	Object* allocator = nullptr;
	try { allocator = new (storage) Object{}; }
	catch (...) {
		if (use_context) {
			try { context->deallocate(storage, const_cast<void*>(retained_info)); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		} else if (backing != nullptr) CFAllocatorDeallocate(backing, storage);
		if (retained_info != nullptr && context->release != nullptr) {
			try { context->release(retained_info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		return nullptr;
	}
	allocator->kind = Kind::Allocator;
	allocator->allocator_context = *context;
	allocator->allocator_info = retained_info;
	allocator->allocator_context.info = const_cast<void*>(allocator->allocator_info);
	allocator->allocator_storage_uses_context = use_context;
	if (!use_context) {
		RetainOwned(backing);
		allocator->allocator_backing_allocator = backing;
	}
	return allocator;
}

extern "C" DARLING_CF_GUEST_ABI void CFAllocatorGetContext(
	darling_windows_CFAllocatorRef value, DarlingCFAllocatorContext* context)
{
	if (context == nullptr) return;
	const auto* allocator = value == nullptr ? EffectiveCFAllocator(nullptr) : AsCFAllocator(value);
	if (allocator == nullptr) { *context = {}; return; }
	*context = allocator->allocator_context;
	context->info = const_cast<void*>(allocator->allocator_info);
}

extern "C" DARLING_CF_GUEST_ABI void* CFAllocatorAllocate(
	darling_windows_CFAllocatorRef value, darling_windows_CFIndex size, std::uint64_t hint)
{
	if (size <= 0) return nullptr;
	auto* allocator = EffectiveCFAllocator(value);
	if (value != nullptr && allocator == nullptr) return nullptr;
	if (allocator == nullptr) return darling_windows_malloc(static_cast<std::size_t>(size));
	if (allocator->allocator_context.allocate == nullptr) return nullptr;
	try { return allocator->allocator_context.allocate(size, hint,
		const_cast<void*>(allocator->allocator_info)); }
	catch (...) { return nullptr; }
}

extern "C" DARLING_CF_GUEST_ABI void CFAllocatorDeallocate(
	darling_windows_CFAllocatorRef value, void* address)
{
	if (address == nullptr) return;
	auto* allocator = EffectiveCFAllocator(value);
	if (value != nullptr && allocator == nullptr) return;
	if (allocator == nullptr) { darling_windows_free(address); return; }
	if (allocator->allocator_context.deallocate == nullptr) return;
	try { allocator->allocator_context.deallocate(address,
		const_cast<void*>(allocator->allocator_info)); }
	catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
}

extern "C" DARLING_CF_GUEST_ABI void* CFAllocatorReallocate(
	darling_windows_CFAllocatorRef value, void* address,
	darling_windows_CFIndex size, std::uint64_t hint)
{
	if (size < 0) return nullptr;
	auto* allocator = EffectiveCFAllocator(value);
	if (value != nullptr && allocator == nullptr) return nullptr;
	if (allocator == nullptr) {
		if (size == 0) { darling_windows_free(address); return nullptr; }
		return darling_windows_realloc(address, static_cast<std::size_t>(size));
	}
	const auto info = const_cast<void*>(allocator->allocator_info);
	if (address == nullptr) {
		if (size == 0 || allocator->allocator_context.allocate == nullptr) return nullptr;
		try { return allocator->allocator_context.allocate(size, hint, info); }
		catch (...) { return nullptr; }
	}
	if (size == 0) {
		if (allocator->allocator_context.deallocate != nullptr) {
			try { allocator->allocator_context.deallocate(address, info); }
			catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
		}
		return nullptr;
	}
	if (allocator->allocator_context.reallocate == nullptr) return nullptr;
	try { return allocator->allocator_context.reallocate(address, size, hint, info); }
	catch (...) { return nullptr; }
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFAllocatorGetPreferredSizeForSize(
	darling_windows_CFAllocatorRef value, darling_windows_CFIndex size, std::uint64_t hint)
{
	if (size < 0) return 0;
	auto* allocator = EffectiveCFAllocator(value);
	if (value != nullptr && allocator == nullptr) return 0;
	if (allocator == nullptr || allocator->allocator_context.preferred_size == nullptr)
		return size;
	try { return allocator->allocator_context.preferred_size(size, hint,
		const_cast<void*>(allocator->allocator_info)); }
	catch (...) { return 0; }
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFAllocatorRef CFAllocatorGetDefault()
{
	return thread_cf_allocator.value != nullptr ? thread_cf_allocator.value : SystemCFAllocator();
}

extern "C" DARLING_CF_GUEST_ABI void CFAllocatorSetDefault(
	darling_windows_CFAllocatorRef value)
{
	auto* allocator = value == nullptr ? SystemCFAllocator() : AsCFAllocator(value);
	if (value != nullptr && allocator == nullptr) return;
	if (thread_cf_allocator.value == allocator) return;
	if (allocator != nullptr) RetainOwned(allocator);
	ReleaseOwned(thread_cf_allocator.value);
	thread_cf_allocator.value = allocator;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFMachPortGetTypeID()
{
	return TypeId(Kind::MachPort);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMachPortRef CFMachPortCreateWithPort(
	const void* allocator, std::uint32_t port_name, DarlingCFMachPortCallout callout,
	const DarlingCFMachPortContext* context, std::uint8_t* should_free_info)
{
	if (should_free_info != nullptr) *should_free_info = 1;
	if (port_name == 0 || callout == nullptr ||
		(context != nullptr && context->version != 0)) return nullptr;
	std::uint32_t receive_refs = 0;
	if (darling_windows_mach_port_get_refs(darling_windows_mach_task_self(), port_name,
		darling_mach_port_type_receive, &receive_refs) != 0 || receive_refs == 0) return nullptr;
	{
		std::lock_guard registry_lock(cfmachport_registry_mutex);
		const auto found = cfmachport_registry.find(port_name);
		if (found != cfmachport_registry.end()) {
			RetainOwned(found->second);
			return found->second;
		}
	}
	auto* object = CreateObjectWithAllocator(allocator, Kind::MachPort);
	if (object == nullptr) return nullptr;
	object->mach_port_name = port_name;
	object->mach_port_allocator = allocator;
	object->mach_port_callout = callout;
	object->mach_port_context = context == nullptr ? DarlingCFMachPortContext{} : *context;
	object->mach_port_valid = true;
	const void* info = context == nullptr ? nullptr : context->info;
	if (info != nullptr && object->mach_port_context.retain != nullptr) {
		try { object->mach_port_info = object->mach_port_context.retain(info); }
		catch (...) { ReleaseOwned(object); return nullptr; }
		if (object->mach_port_info == nullptr) { ReleaseOwned(object); return nullptr; }
	} else object->mach_port_info = info;
	object->mach_port_context.info = const_cast<void*>(object->mach_port_info);
	Object* existing = nullptr;
	bool inserted = false;
	try {
		std::lock_guard registry_lock(cfmachport_registry_mutex);
		const auto [found, was_inserted] = cfmachport_registry.emplace(port_name, object);
		inserted = was_inserted;
		if (!was_inserted) {
			existing = found->second;
			RetainOwned(existing);
		}
	} catch (...) {
		ReleaseOwned(object);
		return nullptr;
	}
	if (!inserted) {
		ReleaseOwned(object);
		return existing;
	}
	if (should_free_info != nullptr) *should_free_info = 0;
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMachPortRef CFMachPortCreate(
	const void* allocator, DarlingCFMachPortCallout callout,
	const DarlingCFMachPortContext* context, std::uint8_t* should_free_info)
{
	if (should_free_info != nullptr) *should_free_info = 1;
	if (callout == nullptr) return nullptr;
	darling_mach_port_name_t port_name = 0;
	if (darling_windows_mach_port_allocate(darling_windows_mach_task_self(), &port_name) != 0 ||
		port_name == 0) return nullptr;
	if (darling_windows_mach_port_insert_right(darling_windows_mach_task_self(),
		port_name, port_name, darling_mach_make_send) != 0) {
		(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(), port_name);
		return nullptr;
	}
	auto* result = const_cast<Object*>(AsCFMachPort(CFMachPortCreateWithPort(
		allocator, port_name, callout, context, should_free_info)));
	if (result == nullptr) {
		(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(), port_name);
		return nullptr;
	}
	{
		std::lock_guard lock(result->mach_port_mutex);
		result->mach_port_owns_name = true;
	}
	return result;
}

extern "C" DARLING_CF_GUEST_ABI void CFMachPortGetContext(
	darling_windows_CFMachPortRef value, DarlingCFMachPortContext* context)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr || context == nullptr) return;
	std::lock_guard lock(port->mach_port_mutex);
	*context = port->mach_port_context;
	context->info = const_cast<void*>(port->mach_port_info);
}

extern "C" DARLING_CF_GUEST_ABI std::uint32_t CFMachPortGetPort(
	darling_windows_CFMachPortRef value)
{
	auto* port = AsCFMachPort(value);
	return port == nullptr ? 0 : port->mach_port_name;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFMachPortIsValid(
	darling_windows_CFMachPortRef value)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr) return 0;
	std::lock_guard lock(port->mach_port_mutex);
	return port->mach_port_valid ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFMachPortInvalidate(
	darling_windows_CFMachPortRef value)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr) return;
	std::vector<Object*> sources;
	DarlingCFMachPortInvalidationCallout invalidation_callout = nullptr;
	DarlingCFMachPortReleaseCallback release_context = nullptr;
	const void* info = nullptr;
	const bool destroy_port = [&] {
		std::lock_guard lock(port->mach_port_mutex);
		if (!port->mach_port_valid) return false;
		try { sources.reserve(port->mach_port_sources.size()); }
		catch (...) { return false; }
		port->mach_port_valid = false;
		invalidation_callout = port->mach_port_invalidation_callout;
		release_context = port->mach_port_context.release;
		info = port->mach_port_info;
		port->mach_port_info = nullptr;
		port->mach_port_context.info = nullptr;
		port->mach_port_context_released = true;
		for (auto* source : port->mach_port_sources) {
			RetainOwned(source);
			try { sources.push_back(source); }
			catch (...) { ReleaseOwned(source); break; }
		}
		const bool owns = port->mach_port_owns_name;
		port->mach_port_owns_name = false;
		return owns;
	}();
	if (port->mach_port_name != 0 && destroy_port)
		(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(),
			port->mach_port_name);
	for (auto* source : sources) {
		CFRunLoopSourceInvalidate(source);
		ReleaseOwned(source);
	}
	if (invalidation_callout != nullptr) {
		try { invalidation_callout(port, const_cast<void*>(info)); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
	if (info != nullptr && release_context != nullptr) {
		try { release_context(info); }
		catch (...) { /* Guest callbacks must not unwind through CoreFoundation. */ }
	}
}

extern "C" DARLING_CF_GUEST_ABI void CFMachPortSetInvalidationCallBack(
	darling_windows_CFMachPortRef value, DarlingCFMachPortInvalidationCallout callout)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr) return;
	std::lock_guard lock(port->mach_port_mutex);
	if (port->mach_port_valid) port->mach_port_invalidation_callout = callout;
}

extern "C" DARLING_CF_GUEST_ABI DarlingCFMachPortInvalidationCallout
CFMachPortGetInvalidationCallBack(darling_windows_CFMachPortRef value)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr) return nullptr;
	std::lock_guard lock(port->mach_port_mutex);
	return port->mach_port_invalidation_callout;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFMachPortCreateRunLoopSource(
	const void* allocator, darling_windows_CFMachPortRef value,
	darling_windows_CFIndex order)
{
	auto* port = AsCFMachPort(value);
	if (port == nullptr) return nullptr;
	{
		std::lock_guard lock(port->mach_port_mutex);
		if (!port->mach_port_valid) return nullptr;
	}
	DarlingCFRunLoopSourceContext1 context{};
	context.version = 1;
	context.info = port;
	context.retain = &CFMachPortRetainInfo;
	context.release = &CFMachPortReleaseInfo;
	context.get_port = &CFMachPortSourceGetPort;
	context.perform = &CFMachPortSourcePerform;
	auto* source = AsRunLoopSource(CFRunLoopSourceCreate(allocator, 0,
		reinterpret_cast<const DarlingCFRunLoopSourceContext*>(&context)));
	if (source == nullptr) return nullptr;
	bool registered = false;
	{
		std::lock_guard lock(port->mach_port_mutex);
		if (port->mach_port_valid) {
			try {
				port->mach_port_sources.push_back(source);
				source->run_loop_source_cfmachport = port;
				registered = true;
			} catch (...) { /* Release the source after dropping the port lock. */ }
		}
	}
	if (!registered) {
		CFRunLoopSourceInvalidate(source);
		ReleaseOwned(source);
		return nullptr;
	}
	(void)order; // Apple's CFMachPort sources currently ignore source order.
	return source;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsSource(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef source_value,
	darling_windows_CFStringRef mode_value)
{
	auto* source = AsRunLoopSource(source_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || source == nullptr || mode == nullptr || mode->kind != Kind::String)
		return 0;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	std::lock_guard lock(loop->state->mutex);
	return std::any_of(loop->state->sources.begin(), loop->state->sources.end(),
		[&](const RunLoopState::Source& entry) {
			return entry.object == source && std::find(modes.begin(), modes.end(), entry.mode) != modes.end();
		}) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddSource(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef source_value,
	darling_windows_CFStringRef mode_value)
{
	auto* source = AsRunLoopSource(source_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || source == nullptr || mode == nullptr || mode->kind != Kind::String) return;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	for (const auto& concrete_mode : modes)
		(void)AddRunLoopSourceMode(value, loop->state, source, concrete_mode);
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveSource(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef source_value,
	darling_windows_CFStringRef mode_value)
{
	auto* source = AsRunLoopSource(source_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || source == nullptr || mode == nullptr || mode->kind != Kind::String) return;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	for (const auto& concrete_mode : modes)
		RemoveRunLoopSourceMode(value, loop->state, source, concrete_mode);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopTimerGetTypeID()
{
	return TypeId(Kind::RunLoopTimer);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopTimerCreate(
	const void* allocator, double fire_date, double interval, std::uint64_t flags,
	darling_windows_CFIndex order, DarlingCFRunLoopTimerCallout callout,
	const DarlingCFRunLoopTimerContext* context)
{
	if (!std::isfinite(fire_date) || !std::isfinite(interval) || flags != 0 ||
		callout == nullptr || context == nullptr || context->version != 0) return nullptr;
	auto* timer = new (std::nothrow) Object{};
	if (timer == nullptr) return nullptr;
	timer->kind = Kind::RunLoopTimer;
	timer->run_loop_timer_context = *context;
	timer->run_loop_timer_allocator = allocator;
	timer->run_loop_timer_callout = callout;
	timer->run_loop_timer_fire_date = fire_date;
	timer->run_loop_timer_interval = interval;
	timer->run_loop_timer_order = order;
	timer->run_loop_timer_valid = true;
	if (context->info != nullptr && context->retain != nullptr) {
		try { timer->run_loop_timer_info = context->retain(context->info); }
		catch (...) { delete timer; return nullptr; }
		if (timer->run_loop_timer_info == nullptr) { delete timer; return nullptr; }
	} else timer->run_loop_timer_info = context->info;
	timer->run_loop_timer_context.info = const_cast<void*>(timer->run_loop_timer_info);
	return timer;
}

extern "C" DARLING_CF_GUEST_ABI double CFRunLoopTimerGetNextFireDate(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	if (timer == nullptr) return 0;
	std::lock_guard lock(timer->run_loop_timer_mutex);
	return timer->run_loop_timer_fire_date;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerSetNextFireDate(
	darling_windows_CFTypeRef value, double fire_date)
{
	auto* timer = AsRunLoopTimer(value);
	if (timer == nullptr || !std::isfinite(fire_date)) return;
	std::vector<std::shared_ptr<RunLoopState>> states;
	{
		std::lock_guard lock(timer->run_loop_timer_mutex);
		if (!timer->run_loop_timer_valid) return;
		timer->run_loop_timer_fire_date = fire_date;
		try {
			for (const auto& schedule : timer->run_loop_timer_schedules) {
				auto state = schedule.state.lock();
				if (state != nullptr && std::find(states.begin(), states.end(), state) == states.end())
					states.push_back(std::move(state));
			}
		} catch (...) { /* Existing schedules remain valid if wake-list allocation fails. */ }
	}
	for (const auto& state : states) {
		state->condition.notify_all();
		if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	}
}

extern "C" DARLING_CF_GUEST_ABI double CFRunLoopTimerGetInterval(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	return timer == nullptr ? 0 : timer->run_loop_timer_interval;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopTimerDoesRepeat(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	return timer != nullptr && timer->run_loop_timer_interval > 0 ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopTimerGetOrder(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	return timer == nullptr ? 0 : timer->run_loop_timer_order;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopTimerIsValid(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	if (timer == nullptr) return 0;
	std::lock_guard lock(timer->run_loop_timer_mutex);
	return timer->run_loop_timer_valid ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerGetContext(
	darling_windows_CFTypeRef value, DarlingCFRunLoopTimerContext* context)
{
	auto* timer = AsRunLoopTimer(value);
	if (timer == nullptr || context == nullptr) return;
	std::lock_guard lock(timer->run_loop_timer_mutex);
	*context = timer->run_loop_timer_context;
	context->info = const_cast<void*>(timer->run_loop_timer_info);
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerInvalidate(
	darling_windows_CFTypeRef value)
{
	auto* timer = AsRunLoopTimer(value);
	if (timer == nullptr) return;
	std::vector<Object::TimerSchedule> schedules;
	{
		std::lock_guard lock(timer->run_loop_timer_mutex);
		timer->run_loop_timer_valid = false;
		schedules.swap(timer->run_loop_timer_schedules);
	}
	for (const auto& schedule : schedules) {
		auto state = schedule.state.lock();
		if (state == nullptr) continue;
		bool removed = false;
		{
			std::lock_guard lock(state->mutex);
			for (auto it = state->timers.begin(); it != state->timers.end(); ++it) {
				if (it->object == timer && it->mode == schedule.mode) {
					state->timers.erase(it); removed = true; break;
				}
			}
		}
		state->condition.notify_all();
		if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
		if (removed) ReleaseOwned(timer);
	}
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsTimer(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef timer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* timer = AsRunLoopTimer(timer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || timer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return 0;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	std::lock_guard lock(loop->state->mutex);
	return std::any_of(loop->state->timers.begin(), loop->state->timers.end(),
		[&](const RunLoopState::Timer& entry) {
			return entry.object == timer && std::find(modes.begin(), modes.end(), entry.mode) != modes.end();
		}) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddTimer(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef timer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* timer = AsRunLoopTimer(timer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || timer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	for (const auto& concrete_mode : modes) {
		bool scheduled = false;
		{
			std::lock_guard lock(timer->run_loop_timer_mutex);
			if (!timer->run_loop_timer_valid) break;
			auto found = std::find_if(timer->run_loop_timer_schedules.begin(),
				timer->run_loop_timer_schedules.end(), [&](const Object::TimerSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
			if (found != timer->run_loop_timer_schedules.end()) continue;
			try { timer->run_loop_timer_schedules.push_back({loop->state, value, concrete_mode}); }
			catch (...) { continue; }
			scheduled = true;
		}
		if (!scheduled) continue;
		bool inserted = false;
		{
			std::lock_guard lock(loop->state->mutex);
			auto found = std::find_if(loop->state->timers.begin(), loop->state->timers.end(),
				[&](const RunLoopState::Timer& entry) {
					return entry.object == timer && entry.mode == concrete_mode;
				});
			if (found == loop->state->timers.end()) {
				try { RetainOwned(timer); loop->state->timers.push_back({timer, concrete_mode}); inserted = true; }
				catch (...) { ReleaseOwned(timer); }
			}
		}
		if (!inserted) {
			std::lock_guard lock(timer->run_loop_timer_mutex);
			std::erase_if(timer->run_loop_timer_schedules,
				[&](const Object::TimerSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
			continue;
		}
		loop->state->condition.notify_all();
		if (loop->state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	}
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveTimer(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef timer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* timer = AsRunLoopTimer(timer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || timer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	for (const auto& concrete_mode : modes) {
		bool removed = false;
		{
			std::lock_guard lock(loop->state->mutex);
			for (auto it = loop->state->timers.begin(); it != loop->state->timers.end(); ++it) {
				if (it->object == timer && it->mode == concrete_mode) {
					loop->state->timers.erase(it); removed = true; break;
				}
			}
		}
		{
			std::lock_guard lock(timer->run_loop_timer_mutex);
			std::erase_if(timer->run_loop_timer_schedules,
				[&](const Object::TimerSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
		}
		if (removed) ReleaseOwned(timer);
	}
	loop->state->condition.notify_all();
	if (loop->state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
}

extern "C" DARLING_CF_GUEST_ABI double CFRunLoopGetNextTimerFireDate(
	darling_windows_CFRunLoopRef value, darling_windows_CFStringRef mode_value)
{
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || mode == nullptr || mode->kind != Kind::String) return 0;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	std::optional<double> next;
	std::lock_guard loop_lock(loop->state->mutex);
	for (const auto& binding : loop->state->timers) {
		if (std::find(modes.begin(), modes.end(), binding.mode) == modes.end()) continue;
		std::lock_guard timer_lock(binding.object->run_loop_timer_mutex);
		if (binding.object->run_loop_timer_valid &&
			(!next || binding.object->run_loop_timer_fire_date < *next))
			next = binding.object->run_loop_timer_fire_date;
	}
	return next.value_or(0.0);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopObserverGetTypeID()
{
	return TypeId(Kind::RunLoopObserver);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopObserverCreate(
	const void* allocator, std::uint64_t activities, std::uint8_t repeats,
	darling_windows_CFIndex order, DarlingCFRunLoopObserverCallout callout,
	const DarlingCFRunLoopObserverContext* context)
{
	if (activities == 0 || (activities & ~kCFRunLoopAllActivities) != 0 ||
		callout == nullptr || (context != nullptr && context->version != 0)) return nullptr;
	auto* observer = new (std::nothrow) Object{};
	if (observer == nullptr) return nullptr;
	observer->kind = Kind::RunLoopObserver;
	observer->run_loop_observer_context = context == nullptr
		? DarlingCFRunLoopObserverContext{} : *context;
	observer->run_loop_observer_allocator = allocator;
	observer->run_loop_observer_callout = callout;
	observer->run_loop_observer_activities = activities;
	observer->run_loop_observer_order = order;
	observer->run_loop_observer_repeats = repeats != 0;
	observer->run_loop_observer_valid = true;
	if (context != nullptr && context->info != nullptr && context->retain != nullptr) {
		try { observer->run_loop_observer_info = context->retain(context->info); }
		catch (...) { delete observer; return nullptr; }
		if (observer->run_loop_observer_info == nullptr) { delete observer; return nullptr; }
	} else if (context != nullptr) observer->run_loop_observer_info = context->info;
	observer->run_loop_observer_context.info = const_cast<void*>(observer->run_loop_observer_info);
	return observer;
}

extern "C" DARLING_CF_GUEST_ABI std::uint64_t CFRunLoopObserverGetActivities(
	darling_windows_CFTypeRef value)
{
	auto* observer = AsRunLoopObserver(value);
	return observer == nullptr ? 0 : observer->run_loop_observer_activities;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopObserverDoesRepeat(
	darling_windows_CFTypeRef value)
{
	auto* observer = AsRunLoopObserver(value);
	return observer != nullptr && observer->run_loop_observer_repeats ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopObserverGetOrder(
	darling_windows_CFTypeRef value)
{
	auto* observer = AsRunLoopObserver(value);
	return observer == nullptr ? 0 : observer->run_loop_observer_order;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopObserverGetContext(
	darling_windows_CFTypeRef value, DarlingCFRunLoopObserverContext* context)
{
	auto* observer = AsRunLoopObserver(value);
	if (observer == nullptr || context == nullptr) return;
	std::lock_guard lock(observer->run_loop_observer_mutex);
	*context = observer->run_loop_observer_context;
	context->info = const_cast<void*>(observer->run_loop_observer_info);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopObserverIsValid(
	darling_windows_CFTypeRef value)
{
	auto* observer = AsRunLoopObserver(value);
	if (observer == nullptr) return 0;
	std::lock_guard lock(observer->run_loop_observer_mutex);
	return observer->run_loop_observer_valid ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopObserverInvalidate(
	darling_windows_CFTypeRef value)
{
	auto* observer = AsRunLoopObserver(value);
	if (observer == nullptr) return;
	std::vector<Object::ObserverSchedule> schedules;
	{
		std::lock_guard lock(observer->run_loop_observer_mutex);
		observer->run_loop_observer_valid = false;
		schedules.swap(observer->run_loop_observer_schedules);
	}
	for (const auto& schedule : schedules) {
		auto state = schedule.state.lock();
		if (state == nullptr) continue;
		bool removed = false;
		{
			std::lock_guard lock(state->mutex);
			for (auto it = state->observers.begin(); it != state->observers.end(); ++it) {
				if (it->object == observer && it->mode == schedule.mode) {
					state->observers.erase(it); removed = true; break;
				}
			}
		}
		state->condition.notify_all();
		if (state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
		if (removed) ReleaseOwned(observer);
	}
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsObserver(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef observer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* observer = AsRunLoopObserver(observer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || observer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return 0;
	auto* loop = static_cast<RunLoop*>(value);
	const auto modes = RunLoopConcreteModes(loop->state, mode->string);
	std::lock_guard lock(loop->state->mutex);
	return std::any_of(loop->state->observers.begin(), loop->state->observers.end(),
		[&](const RunLoopState::Observer& entry) {
			return entry.object == observer &&
				std::find(modes.begin(), modes.end(), entry.mode) != modes.end();
		}) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddObserver(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef observer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* observer = AsRunLoopObserver(observer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || observer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return;
	auto* loop = static_cast<RunLoop*>(value);
	for (const auto& concrete_mode : RunLoopConcreteModes(loop->state, mode->string)) {
		bool registered = false;
		{
			std::lock_guard lock(observer->run_loop_observer_mutex);
			if (!observer->run_loop_observer_valid) break;
			auto found = std::find_if(observer->run_loop_observer_schedules.begin(),
				observer->run_loop_observer_schedules.end(),
				[&](const Object::ObserverSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
			if (found != observer->run_loop_observer_schedules.end()) continue;
			try { observer->run_loop_observer_schedules.push_back(
				{loop->state, value, concrete_mode}); registered = true; }
			catch (...) { continue; }
		}
		if (!registered) continue;
		bool inserted = false;
		{
			std::lock_guard lock(loop->state->mutex);
			auto found = std::find_if(loop->state->observers.begin(), loop->state->observers.end(),
				[&](const RunLoopState::Observer& entry) {
					return entry.object == observer && entry.mode == concrete_mode;
				});
			if (found == loop->state->observers.end()) {
				try { RetainOwned(observer); loop->state->observers.push_back(
					{observer, concrete_mode}); inserted = true; }
				catch (...) { ReleaseOwned(observer); }
			}
		}
		if (!inserted) {
			std::lock_guard lock(observer->run_loop_observer_mutex);
			std::erase_if(observer->run_loop_observer_schedules,
				[&](const Object::ObserverSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
			continue;
		}
		loop->state->condition.notify_all();
		if (loop->state == main_run_loop.state) darling_windows_appkit_wake_main_run_loop();
	}
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveObserver(
	darling_windows_CFRunLoopRef value, darling_windows_CFTypeRef observer_value,
	darling_windows_CFStringRef mode_value)
{
	auto* observer = AsRunLoopObserver(observer_value);
	const auto* mode = static_cast<const Object*>(mode_value);
	if (value == nullptr || observer == nullptr || mode == nullptr || mode->kind != Kind::String)
		return;
	auto* loop = static_cast<RunLoop*>(value);
	for (const auto& concrete_mode : RunLoopConcreteModes(loop->state, mode->string)) {
		bool removed = false;
		{
			std::lock_guard lock(loop->state->mutex);
			for (auto it = loop->state->observers.begin(); it != loop->state->observers.end(); ++it) {
				if (it->object == observer && it->mode == concrete_mode) {
					loop->state->observers.erase(it); removed = true; break;
				}
			}
		}
		{
			std::lock_guard lock(observer->run_loop_observer_mutex);
			std::erase_if(observer->run_loop_observer_schedules,
				[&](const Object::ObserverSchedule& entry) {
					return entry.state.lock() == loop->state && entry.mode == concrete_mode;
				});
		}
		if (removed) ReleaseOwned(observer);
	}
	loop->state->condition.notify_all();
}

extern "C" void darling_windows_CFRunLoopWakeUp(darling_windows_CFRunLoopRef value)
{
	if (value == nullptr) return;
	auto* loop = static_cast<RunLoop*>(value);
	loop->state->condition.notify_all();
	if (loop->state == main_run_loop.state)
		darling_windows_appkit_wake_main_run_loop();
}

extern "C" bool darling_windows_CFRunLoopPerformOneShotTimer(
	darling_windows_CFRunLoopRef value, double seconds,
	darling_windows_CFRunLoopBlock block, void* context)
{
	if (value == nullptr || block == nullptr || !std::isfinite(seconds) || seconds < 0) return false;
	auto* loop = static_cast<RunLoop*>(value);
	const auto state = loop->state;
	std::thread([state, seconds, block, context] {
		std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
		{
			std::lock_guard lock(state->mutex);
			try {
				state->blocks.push_back({block, context,
					{"kCFRunLoopDefaultMode"}, 0});
			} catch (...) { return; }
		}
		state->condition.notify_all();
	}).detach();
	return true;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFRunLoopRef CFRunLoopGetCurrent()
{
	return darling_windows_CFRunLoopGetCurrent();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFRunLoopRef CFRunLoopGetMain()
{
	return darling_windows_CFRunLoopGetMain();
}

extern "C" DARLING_CF_GUEST_ABI std::int32_t CFRunLoopRunInMode(
	darling_windows_CFStringRef mode, double seconds, std::uint8_t return_after_source)
{
	char mode_name[512]{};
	if (mode == nullptr || !darling_windows_CFStringGetCString(mode, mode_name,
		static_cast<darling_windows_CFIndex>(sizeof(mode_name))) ||
		std::strcmp(mode_name, "kCFRunLoopCommonModes") == 0 ||
		!std::isfinite(seconds) || seconds < 0)
		return DarlingCFRunLoopRunFinished;
	const auto loop_value = darling_windows_CFRunLoopGetCurrent();
	auto* loop = static_cast<RunLoop*>(loop_value);
	bool has_work_source = false;
	{
		std::lock_guard lock(loop->state->mutex);
		has_work_source = RunLoopModeHasWorkSource(
			*loop->state, mode_name, loop->state);
	}
	if (!has_work_source) {
		// Preserve entry/exit observer delivery for an empty mode without waiting.
		(void)darling_windows_CFRunLoopRunInModeForLoop(loop_value, mode_name, 0.0, false);
		std::lock_guard lock(loop->state->mutex);
		return loop->state->stopped ? DarlingCFRunLoopRunStopped :
			DarlingCFRunLoopRunFinished;
	}
	const int result = darling_windows_CFRunLoopRunInModeForLoop(
		loop_value, mode_name, seconds, return_after_source != 0);
	if (result < 0) return DarlingCFRunLoopRunFinished;
	std::lock_guard lock(loop->state->mutex);
	if (loop->state->stopped) return DarlingCFRunLoopRunStopped;
	return result == 0 ? DarlingCFRunLoopRunHandledSource :
		DarlingCFRunLoopRunTimedOut;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRun()
{
	const auto loop_value = darling_windows_CFRunLoopGetCurrent();
	auto* loop = static_cast<RunLoop*>(loop_value);
	for (;;) {
		{
			std::lock_guard lock(loop->state->mutex);
			if (!RunLoopModeHasWorkSource(*loop->state,
				"kCFRunLoopDefaultMode", loop->state)) return;
		}
		const int result = darling_windows_CFRunLoopRunInModeForLoop(loop_value,
			"kCFRunLoopDefaultMode", 315360000.0, false);
		if (result != 1) return;
	}
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopStop(darling_windows_CFRunLoopRef loop)
{
	darling_windows_CFRunLoopStop(loop);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopIsWaiting(
	darling_windows_CFRunLoopRef loop)
{
	return darling_windows_CFRunLoopIsWaiting(loop) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopWakeUp(darling_windows_CFRunLoopRef loop)
{
	darling_windows_CFRunLoopWakeUp(loop);
}

namespace {
struct DarlingRunLoopGuestBlock final {
	void* block{};
	~DarlingRunLoopGuestBlock() { if (block != nullptr) objc_releaseBlock(block); }
};

void DarlingRunLoopInvokeGuestBlock(void* context)
{
	std::unique_ptr<DarlingRunLoopGuestBlock> retained(
		static_cast<DarlingRunLoopGuestBlock*>(context));
	try { darling_objc_apple_block_invoke0(retained->block); }
	catch (...) { /* Never unwind a guest block through CoreFoundation. */ }
}

bool DarlingRunLoopCopyModeName(darling_windows_CFStringRef mode, std::string& output)
{
	char buffer[4096]{};
	if (mode == nullptr || !darling_windows_CFStringGetCString(mode, buffer,
		static_cast<darling_windows_CFIndex>(sizeof(buffer)))) return false;
	output.assign(buffer);
	return !output.empty();
}
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopPerformBlock(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef modes, void* block)
{
	if (loop == nullptr || modes == nullptr || block == nullptr) return;
	std::vector<std::string> mode_names;
	try {
		const auto mode_type = darling_windows_CFGetTypeID(modes);
		if (mode_type == darling_windows_CFStringGetTypeID()) {
			mode_names.emplace_back();
			if (!DarlingRunLoopCopyModeName(modes, mode_names.back())) return;
		} else if (mode_type == darling_windows_CFArrayGetTypeID()) {
			const auto count = darling_windows_CFArrayGetCount(modes);
			if (count <= 0 || count > 256) return;
			mode_names.reserve(static_cast<std::size_t>(count));
			for (darling_windows_CFIndex index = 0; index < count; ++index) {
				mode_names.emplace_back();
				if (!DarlingRunLoopCopyModeName(
					darling_windows_CFArrayGetValueAtIndex(modes, index), mode_names.back()))
					return;
			}
		} else return;
	} catch (...) { return; }
	std::vector<const char*> mode_pointers;
	try {
		mode_pointers.reserve(mode_names.size());
		for (const auto& mode : mode_names) mode_pointers.push_back(mode.c_str());
	} catch (...) { return; }
	void* retained_block = objc_retainBlock(block);
	if (retained_block == nullptr) return;
	std::unique_ptr<DarlingRunLoopGuestBlock> context(
		new (std::nothrow) DarlingRunLoopGuestBlock{retained_block});
	if (context == nullptr) {
		objc_releaseBlock(retained_block);
		return;
	}
	if (!darling_windows_CFRunLoopPerformBlockInModesWithOrder(loop,
		mode_pointers.data(), mode_pointers.size(), 0,
		&DarlingRunLoopInvokeGuestBlock, context.get())) return;
	context.release();
	darling_windows_CFRunLoopWakeUp(loop);
}

extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddCommonMode(
	darling_windows_CFRunLoopRef loop, darling_windows_CFStringRef mode)
{
	char mode_name[512]{};
	if (mode != nullptr && darling_windows_CFStringGetCString(mode, mode_name,
		static_cast<darling_windows_CFIndex>(sizeof(mode_name))))
		darling_windows_CFRunLoopAddCommonMode(loop, mode_name);
}

// CFString stores UTF-8 internally. Accept the runtime's reported system
// encoding and Darwin's explicit UTF-8 encoding; other conversions are not
// silently treated as UTF-8.
namespace {
const void* DARLING_CF_GUEST_ABI DarlingCFTypeRetain(const void*, const void* value)
{
	return darling_windows_CFRetain(value);
}
void DARLING_CF_GUEST_ABI DarlingCFTypeRelease(const void*, const void* value)
{
	darling_windows_CFRelease(value);
}
darling_windows_CFStringRef DARLING_CF_GUEST_ABI DarlingCFTypeCopyDescription(
	const void*)
{
	return darling_windows_CFStringCreateWithCString("<CFType>");
}
std::uint8_t DARLING_CF_GUEST_ABI DarlingCFTypeEqual(const void* left, const void* right)
{
	return darling_windows_CFEqual(left, right) ? 1 : 0;
}
std::uintptr_t DARLING_CF_GUEST_ABI DarlingCFTypeHash(const void* value)
{
	return static_cast<std::uintptr_t>(darling_windows_CFHash(value));
}

constexpr std::uint32_t kDarlingCfUtf8Encoding = 0x08000100;
bool DarlingCfSupportsStringEncoding(std::uint32_t encoding)
{
	return encoding == static_cast<std::uint32_t>(darling_windows_CFStringGetSystemEncoding()) ||
		encoding == kDarlingCfUtf8Encoding;
}
}

extern "C" const DarlingCFDictionaryKeyCallbacks kCFTypeDictionaryKeyCallBacks = {
	0, &DarlingCFTypeRetain, &DarlingCFTypeRelease,
	&DarlingCFTypeCopyDescription, &DarlingCFTypeEqual, &DarlingCFTypeHash};
extern "C" const DarlingCFDictionaryValueCallbacks kCFTypeDictionaryValueCallBacks = {
	0, &DarlingCFTypeRetain, &DarlingCFTypeRelease, &DarlingCFTypeCopyDescription};
extern "C" const DarlingCFDictionaryKeyCallbacks kCFTypeSetCallBacks = {
	0, &DarlingCFTypeRetain, &DarlingCFTypeRelease,
	&DarlingCFTypeCopyDescription, &DarlingCFTypeEqual, &DarlingCFTypeHash};
extern "C" const DarlingCFArrayCallbacks kCFTypeArrayCallBacks = {
	0, &DarlingCFTypeRetain, &DarlingCFTypeRelease,
	&DarlingCFTypeCopyDescription, &DarlingCFTypeEqual};

namespace {
Object* NullCFAllocator()
{
	static Object allocator{};
	static const bool initialized = [&] {
		allocator.kind = Kind::Allocator;
		allocator.immortal = true;
		return true;
	}();
	(void)initialized;
	return &allocator;
}
}
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorDefault = nullptr;
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorSystemDefault = SystemCFAllocator();
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorMalloc = SystemCFAllocator();
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorMallocZone = SystemCFAllocator();
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorNull = NullCFAllocator();
extern "C" const darling_windows_CFAllocatorRef kCFAllocatorUseContext =
	&allocator_use_context_sentinel;

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFStringGetTypeID()
{
	return darling_windows_CFStringGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateWithCString(
	const void* allocator, const char* bytes, std::uint32_t encoding)
{
	if (!DarlingCfSupportsStringEncoding(encoding) || bytes == nullptr) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try { object->string = bytes; }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFStringGetLength(
	darling_windows_CFStringRef value)
{
	return darling_windows_CFStringGetLength(value);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetCString(
	darling_windows_CFStringRef value, char* buffer, darling_windows_CFIndex capacity,
	std::uint32_t encoding)
{
	return DarlingCfSupportsStringEncoding(encoding) &&
		darling_windows_CFStringGetCString(value, buffer, capacity) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateWithCharacters(
	const void* allocator, const std::uint16_t* characters, darling_windows_CFIndex length)
{
	const auto converted = As(darling_windows_CFStringCreateWithCharacters(characters, length));
	if (converted == nullptr || converted->kind != Kind::String) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) { ReleaseOwned(converted); return nullptr; }
	try { object->string = converted->string; }
	catch (...) { ReleaseOwned(converted); ReleaseOwned(object); return nullptr; }
	ReleaseOwned(converted);
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateCopy(
	const void* allocator, darling_windows_CFStringRef value)
{
	const auto* source = As(value);
	if (source == nullptr || source->kind != Kind::String) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try { object->string = source->string; }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableStringRef CFStringCreateMutable(
	const void* allocator, darling_windows_CFIndex max_length)
{
	if (max_length < 0) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try {
		if (max_length > 0) object->string.reserve(static_cast<std::size_t>(max_length));
	} catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableStringRef CFStringCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex max_length,
	darling_windows_CFStringRef value)
{
	if (max_length < 0) return nullptr;
	const auto* source = As(value);
	if (source == nullptr || source->kind != Kind::String) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try {
		object->string = source->string;
		if (max_length > 0 && static_cast<std::size_t>(max_length) > object->string.capacity())
			object->string.reserve(static_cast<std::size_t>(max_length));
	} catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateWithString(
	const void* allocator, darling_windows_CFStringRef string,
	darling_windows_CFURLRef base_url)
{
	const auto temporary = darling_windows_CFURLCreateWithString(string, base_url);
	const auto* source = As(temporary);
	if (source == nullptr || source->kind != Kind::URL) {
		if (temporary != nullptr) ReleaseOwned(const_cast<Object*>(source));
		return nullptr;
	}
	auto* object = CreateObjectWithAllocator(allocator, Kind::URL);
	if (object != nullptr) {
		try { object->string = source->string; }
		catch (...) { ReleaseOwned(object); object = nullptr; }
	}
	ReleaseOwned(const_cast<Object*>(source));
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateWithFileSystemPath(
	const void* allocator, darling_windows_CFStringRef file_path, std::int32_t path_style,
	std::uint8_t is_directory)
{
	if (path_style != 0 && path_style != 2) return nullptr;
	const auto* source = As(file_path);
	if (source == nullptr || source->kind != Kind::String) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::URL);
	if (object == nullptr) return nullptr;
	try {
		object->string = source->string;
		if (path_style == 2)
			std::replace(object->string.begin(), object->string.end(), '\\', '/');
		if (is_directory != 0 && !object->string.empty() && object->string.back() != '/')
			object->string.push_back('/');
	}
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFURLCreateWithFileSystemPathRelativeToBase(const void* allocator,
	darling_windows_CFStringRef file_path, std::int32_t path_style,
	std::uint8_t is_directory, darling_windows_CFURLRef base_url)
{
	if (path_style != 0 && path_style != 2) return nullptr;
	const auto* source = As(file_path);
	if (source == nullptr || source->kind != Kind::String) return nullptr;
	std::string path;
	try {
		path = source->string;
		if (path_style == 2) std::replace(path.begin(), path.end(), '\\', '/');
		if (is_directory != 0 && !path.empty() && path.back() != '/') path.push_back('/');
	} catch (...) { return nullptr; }
	const bool absolute = (!path.empty() && path.front() == '/') ||
		(path.size() >= 3 && path[1] == ':' && path[2] == '/');
	if (absolute || base_url == nullptr) {
		auto* object = CreateObjectWithAllocator(allocator, Kind::URL);
		if (object == nullptr) return nullptr;
		try { object->string = std::move(path); }
		catch (...) { ReleaseOwned(object); return nullptr; }
		return object;
	}
	const auto* base = As(base_url);
	if (base == nullptr || base->kind != Kind::URL) return nullptr;
	const auto relative = darling_windows_CFStringCreateWithCString(path.c_str());
	if (relative == nullptr) return nullptr;
	const auto resolved = darling_windows_CFURLCreateWithString(relative, base_url);
	ReleaseOwned(const_cast<Object*>(As(relative)));
	const auto* resolved_object = As(resolved);
	if (resolved_object == nullptr || resolved_object->kind != Kind::URL) {
		if (resolved != nullptr) ReleaseOwned(const_cast<Object*>(resolved_object));
		return nullptr;
	}
	auto* object = CreateObjectWithAllocator(allocator, Kind::URL);
	if (object != nullptr) {
		try { object->string = resolved_object->string; }
		catch (...) { ReleaseOwned(object); object = nullptr; }
	}
	ReleaseOwned(const_cast<Object*>(resolved_object));
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFURLCreateFromFileSystemRepresentation(const void* allocator, const std::uint8_t* bytes,
	darling_windows_CFIndex length, std::uint8_t is_directory)
{
	if (length < 0 || (length != 0 && bytes == nullptr)) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::URL);
	if (object == nullptr) return nullptr;
	try {
		if (length != 0) object->string.assign(reinterpret_cast<const char*>(bytes),
			static_cast<std::size_t>(length));
		if (is_directory != 0 && !object->string.empty() && object->string.back() != '/')
			object->string.push_back('/');
	} catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByReplacingPercentEscapes(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_escaped)
{
	const auto* source = As(original_string);
	if (source == nullptr || source->kind != Kind::String) return nullptr;
	const std::string* leave_escaped = nullptr;
	if (characters_to_leave_escaped != nullptr) {
		const auto* leave = As(characters_to_leave_escaped);
		if (leave == nullptr || leave->kind != Kind::String) return nullptr;
		leave_escaped = &leave->string;
	}
	std::string decoded;
	try { decoded = PercentDecodeLeaving(source->string, leave_escaped); }
	catch (...) { return nullptr; }
	if (decoded == source->string) {
		RetainOwned(const_cast<Object*>(source));
		return source;
	}
	if (!IsWellFormedUTF8(decoded)) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try { object->string = std::move(decoded); }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByReplacingPercentEscapesUsingEncoding(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_escaped,
	std::uint32_t encoding)
{
	if (encoding != kDarlingCfUtf8Encoding) return nullptr;
	return CFURLCreateStringByReplacingPercentEscapes(allocator, original_string,
		characters_to_leave_escaped);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByAddingPercentEscapes(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_unescaped,
	darling_windows_CFStringRef legal_url_characters_to_escape,
	std::uint32_t encoding)
{
	if (encoding != kDarlingCfUtf8Encoding) return nullptr;
	const auto* source = As(original_string);
	if (source == nullptr || source->kind != Kind::String || !IsWellFormedUTF8(source->string))
		return nullptr;
	const std::string* leave = nullptr;
	const std::string* force_escape = nullptr;
	if (characters_to_leave_unescaped != nullptr) {
		const auto* object = As(characters_to_leave_unescaped);
		if (object == nullptr || object->kind != Kind::String) return nullptr;
		leave = &object->string;
	}
	if (legal_url_characters_to_escape != nullptr) {
		const auto* object = As(legal_url_characters_to_escape);
		if (object == nullptr || object->kind != Kind::String) return nullptr;
		force_escape = &object->string;
	}
	std::string escaped;
	try { escaped = PercentEncodeURLString(source->string, leave, force_escape); }
	catch (...) { return nullptr; }
	if (escaped == source->string) {
		RetainOwned(const_cast<Object*>(source));
		return source;
	}
	auto* object = CreateObjectWithAllocator(allocator, Kind::String);
	if (object == nullptr) return nullptr;
	try { object->string = std::move(escaped); }
	catch (...) { ReleaseOwned(object); return nullptr; }
	return object;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFURLGetTypeID()
{
	return darling_windows_CFURLGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLCanBeDecomposed(
	darling_windows_CFURLRef value)
{
	const auto* object = As(value);
	if (object == nullptr || object->kind != Kind::URL || object->string.empty() ||
		!IsWellFormedUTF8(object->string)) return 0;
	const auto colon = object->string.find(':');
	const auto first_delimiter = object->string.find_first_of("/?#");
	const bool windows_drive = colon == 1 && object->string.size() > 2 &&
		((object->string[0] >= 'A' && object->string[0] <= 'Z') ||
		 (object->string[0] >= 'a' && object->string[0] <= 'z')) &&
		(object->string[2] == '/' || object->string[2] == '\\');
	if (colon != std::string::npos && !windows_drive &&
		(first_delimiter == std::string::npos || colon < first_delimiter)) {
		if (colon == 0 || !((object->string[0] >= 'A' && object->string[0] <= 'Z') ||
			(object->string[0] >= 'a' && object->string[0] <= 'z'))) return 0;
		for (std::size_t index = 1; index < colon; ++index) {
			const auto character = static_cast<unsigned char>(object->string[index]);
			if (!((character >= 'A' && character <= 'Z') ||
				(character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9') || character == '+' ||
				character == '-' || character == '.')) return 0;
		}
	}
	for (std::size_t index = 0; index < object->string.size(); ++index) {
		if (object->string[index] == '%' && (index + 2 >= object->string.size() ||
			!std::isxdigit(static_cast<unsigned char>(object->string[index + 1])) ||
			!std::isxdigit(static_cast<unsigned char>(object->string[index + 2])))) return 0;
		if (object->string[index] == '%') index += 2;
	}
	return 1;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLGetString(
	darling_windows_CFURLRef value)
{
	return BorrowURLString(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value, std::int32_t path_style)
{
	if (path_style != 0 && path_style != 2) return nullptr;
	const auto copied = darling_windows_CFURLCopyFileSystemPath(value);
	auto* object = As(copied);
	if (object == nullptr || object->kind != Kind::String) return copied;
	if (path_style == 2)
		std::replace(object->string.begin(), object->string.end(), '/', '\\');
	return copied;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPath(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyPath(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyStrictPath(
	darling_windows_CFURLRef value, std::uint8_t* is_absolute)
{
	const auto* url = As(value);
	if (url == nullptr || url->kind != Kind::URL) return nullptr;
	const auto parts = ParseURLPathParts(url->string);
	const auto copied = darling_windows_CFURLCopyPath(value);
	auto* path = As(copied);
	if (path == nullptr || path->kind != Kind::String) return copied;
	if (!path->string.empty() && path->string.front() == '/') path->string.erase(0, 1);
	if (is_absolute != nullptr) *is_absolute = parts.absolute ? 1 : 0;
	return copied;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyLastPathComponent(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyLastPathComponent(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPathExtension(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyPathExtension(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyScheme(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyScheme(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyResourceSpecifier(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyResourceSpecifier(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyHostName(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyHostName(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyNetLocation(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyNetLocation(value);
}

namespace {
using DarlingCFURLStringCopy = darling_windows_CFStringRef (*)(darling_windows_CFURLRef);
darling_windows_CFStringRef CopyURLComponentWithEscapeSet(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped,
	DarlingCFURLStringCopy copy)
{
	const auto result = copy(value);
	auto* copied = As(result);
	if (copied == nullptr || copied->kind != Kind::String) return result;
	const std::string* leave_escaped = nullptr;
	if (characters_to_leave_escaped != nullptr) {
		const auto* leave = As(characters_to_leave_escaped);
		if (leave == nullptr || leave->kind != Kind::String) {
			ReleaseOwned(copied);
			return nullptr;
		}
		leave_escaped = &leave->string;
	}
	try { copied->string = PercentDecodeLeaving(copied->string, leave_escaped); }
	catch (...) { ReleaseOwned(copied); return nullptr; }
	return copied;
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyParameterString(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped)
{
	return CopyURLComponentWithEscapeSet(value, characters_to_leave_escaped,
		&darling_windows_CFURLCopyParameterString);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyQueryString(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped)
{
	return CopyURLComponentWithEscapeSet(value, characters_to_leave_escaped,
		&darling_windows_CFURLCopyQueryString);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyFragment(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped)
{
	return CopyURLComponentWithEscapeSet(value, characters_to_leave_escaped,
		&darling_windows_CFURLCopyFragment);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyUserName(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyUserName(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPassword(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyPassword(value);
}

extern "C" DARLING_CF_GUEST_ABI std::int32_t CFURLGetPortNumber(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLGetPortNumber(value);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLHasDirectoryPath(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLHasDirectoryPath(value) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLGetFileSystemRepresentation(
	darling_windows_CFURLRef value, std::uint8_t resolve_against_base,
	std::uint8_t* buffer, darling_windows_CFIndex buffer_capacity)
{
	return darling_windows_CFURLGetFileSystemRepresentation(value,
		resolve_against_base != 0, buffer, buffer_capacity) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCopyAbsoluteURL(
	darling_windows_CFURLRef value)
{
	return darling_windows_CFURLCopyAbsoluteURL(value);
}

extern "C" DARLING_CF_GUEST_ABI const char* CFStringGetCStringPtr(
	darling_windows_CFStringRef value, std::uint32_t encoding)
{
	return DarlingCfSupportsStringEncoding(encoding) ?
		darling_windows_CFStringGetCStringPtr(value) : nullptr;
}

extern "C" DARLING_CF_GUEST_ABI std::uint16_t CFStringGetCharacterAtIndex(
	darling_windows_CFStringRef value, darling_windows_CFIndex index)
{
	return darling_windows_CFStringGetCharacterAtIndex(value, index);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetCharacters(
	darling_windows_CFStringRef value, darling_windows_CFRange range, std::uint16_t* output)
{
	return darling_windows_CFStringGetCharacters(value, range, output) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFStringGetMaximumSizeForEncoding(
	darling_windows_CFIndex length, std::uint32_t encoding)
{
	return DarlingCfSupportsStringEncoding(encoding) ?
		darling_windows_CFStringGetMaximumSizeForEncoding(length,
			darling_windows_CFStringGetSystemEncoding()) : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex
CFStringGetMaximumSizeOfFileSystemRepresentation(darling_windows_CFStringRef value)
{
	return darling_windows_CFStringGetMaximumSizeOfFileSystemRepresentation(value);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetFileSystemRepresentation(
	darling_windows_CFStringRef value, char* buffer, darling_windows_CFIndex capacity)
{
	return darling_windows_CFStringGetFileSystemRepresentation(value,
		reinterpret_cast<unsigned char*>(buffer), capacity) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFArrayGetTypeID()
{
	return darling_windows_CFArrayGetTypeID();
}

namespace {
struct DarlingArrayApplyContext {
	darling_windows_CFArrayGuestApplierFunction callback;
	void* context;
};
void DarlingArrayApplyThunk(const void* value, void* opaque)
{
	auto* state = static_cast<DarlingArrayApplyContext*>(opaque);
	if (state != nullptr && state->callback != nullptr)
		state->callback(value, state->context);
}
bool DarlingArrayIsMutable(darling_windows_CFArrayRef value)
{
	const auto* object = static_cast<const Object*>(value);
	return object != nullptr && object->kind == Kind::Array && object->mutable_array;
}
darling_windows_CFArrayRef DarlingCreateGuestArray(const void* const* values,
	darling_windows_CFIndex count, const DarlingCFArrayCallbacks* callbacks,
	bool mutable_array, darling_windows_CFIndex capacity, const void* allocator)
{
	if (count < 0 || capacity < 0 || (count != 0 && values == nullptr)) return nullptr;
	DarlingCFArrayCallbacks copied_callbacks{};
	if (callbacks != nullptr) copied_callbacks = *callbacks;
	if (copied_callbacks.version != 0) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Array);
	if (object == nullptr) return nullptr;
	object->mutable_array = mutable_array;
	object->has_array_callback_table = true;
	object->array_callbacks = copied_callbacks;
	object->array_allocator = allocator;
	try {
		object->array.reserve(static_cast<std::size_t>((std::max)(count, capacity)));
		for (darling_windows_CFIndex index = 0; index < count; ++index) {
			if (values[index] == nullptr) { ReleaseOwned(object); return nullptr; }
			const auto* retained = RetainArrayValue(object, values[index]);
			if (retained == nullptr) { ReleaseOwned(object); return nullptr; }
			object->array.push_back(retained);
		}
	} catch (...) {
		ReleaseOwned(object);
		return nullptr;
	}
	return object;
}
DarlingCFArrayCallbacks DarlingGetCallbacksForArray(const Object* object)
{
	if (object != nullptr && object->has_array_callback_table)
		return object->array_callbacks;
	return kCFTypeArrayCallBacks;
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreate(
	const void* allocator, const void* const* values, darling_windows_CFIndex count,
	const DarlingCFArrayCallbacks* callbacks)
{
	return DarlingCreateGuestArray(values, count, callbacks, false, 0, allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity,
	const DarlingCFArrayCallbacks* callbacks)
{
	return DarlingCreateGuestArray(nullptr, 0, callbacks, true, capacity, allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateCopy(
	const void* allocator, darling_windows_CFArrayRef value)
{
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Array) return nullptr;
	const auto callbacks = DarlingGetCallbacksForArray(source);
	return DarlingCreateGuestArray(source->array.data(),
		static_cast<darling_windows_CFIndex>(source->array.size()), &callbacks, false, 0,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity, darling_windows_CFArrayRef value)
{
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Array || capacity < 0) return nullptr;
	const auto callbacks = DarlingGetCallbacksForArray(source);
	return DarlingCreateGuestArray(source->array.data(),
		static_cast<darling_windows_CFIndex>(source->array.size()), &callbacks, true, capacity,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayAppendValue(
	darling_windows_CFArrayRef value, darling_windows_CFTypeRef element)
{
	if (DarlingArrayIsMutable(value)) darling_windows_CFArrayAppendValue(value, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayInsertValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index,
	darling_windows_CFTypeRef element)
{
	if (DarlingArrayIsMutable(value)) darling_windows_CFArrayInsertValueAtIndex(value, index, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayRemoveValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index)
{
	if (DarlingArrayIsMutable(value)) darling_windows_CFArrayRemoveValueAtIndex(value, index);
}

extern "C" DARLING_CF_GUEST_ABI void CFArraySetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index,
	darling_windows_CFTypeRef element)
{
	if (!DarlingArrayIsMutable(value) || index < 0 ||
		index >= darling_windows_CFArrayGetCount(value)) return;
	darling_windows_CFArrayReplaceValues(value, index, 1, &element, 1);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayReplaceValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	const void* const* replacement, darling_windows_CFIndex replacement_count)
{
	if (DarlingArrayIsMutable(value))
		darling_windows_CFArrayReplaceValues(value, range.location, range.length,
			replacement, replacement_count);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayRemoveAllValues(darling_windows_CFArrayRef value)
{
	if (DarlingArrayIsMutable(value)) darling_windows_CFArrayRemoveAllValues(value);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayApplyFunction(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFArrayGuestApplierFunction callback, void* context)
{
	if (callback == nullptr) return;
	DarlingArrayApplyContext state{callback, context};
	darling_windows_CFArrayApplyFunction(value, range, &DarlingArrayApplyThunk, &state);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayAppendArray(
	darling_windows_CFArrayRef value, darling_windows_CFArrayRef source,
	darling_windows_CFRange range)
{
	if (!DarlingArrayIsMutable(value)) return;
	const auto count = darling_windows_CFArrayGetCount(source);
	if (range.location < 0 || range.length < 0 || range.location > count ||
		range.length > count - range.location) return;
	std::vector<const void*> values;
	try { values.resize(static_cast<std::size_t>(range.length)); }
	catch (...) { return; }
	if (!values.empty()) darling_windows_CFArrayGetValues(source, range.location,
		range.length, values.data());
	const auto* destination = static_cast<const Object*>(value);
	darling_windows_CFArrayReplaceValues(value,
		static_cast<darling_windows_CFIndex>(destination->array.size()), 0,
		values.data(), static_cast<darling_windows_CFIndex>(values.size()));
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayExchangeValuesAtIndices(
	darling_windows_CFArrayRef value, darling_windows_CFIndex first,
	darling_windows_CFIndex second)
{
	if (!DarlingArrayIsMutable(value)) return;
	auto* object = const_cast<Object*>(static_cast<const Object*>(value));
	if (first < 0 || second < 0 ||
		static_cast<std::size_t>(first) >= object->array.size() ||
		static_cast<std::size_t>(second) >= object->array.size()) return;
	std::swap(object->array[static_cast<std::size_t>(first)],
		object->array[static_cast<std::size_t>(second)]);
}

extern "C" DARLING_CF_GUEST_ABI void CFArraySortValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFComparatorFunction comparator, void* context)
{
	if (!DarlingArrayIsMutable(value) || comparator == nullptr) return;
	auto* object = const_cast<Object*>(static_cast<const Object*>(value));
	if (range.location < 0 || range.length < 0 ||
		static_cast<std::size_t>(range.location) > object->array.size() ||
		static_cast<std::size_t>(range.length) > object->array.size() -
			static_cast<std::size_t>(range.location)) return;
	auto begin = object->array.begin() + range.location;
	auto end = begin + range.length;
	try {
		std::stable_sort(begin, end, [comparator, context](const void* left, const void* right) {
			return comparator(left, right, context) < 0;
		});
	} catch (...) { /* Comparator failures must not unwind through the guest ABI. */ }
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayBSearchValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate, darling_windows_CFComparatorFunction comparator,
	void* context)
{
	if (comparator == nullptr) return -1;
	const auto* object = static_cast<const Object*>(value);
	if (object == nullptr || object->kind != Kind::Array || range.location < 0 ||
		range.length < 0 || static_cast<std::size_t>(range.location) > object->array.size() ||
		static_cast<std::size_t>(range.length) > object->array.size() -
			static_cast<std::size_t>(range.location)) return -1;
	darling_windows_CFIndex low = range.location;
	darling_windows_CFIndex high = range.location + range.length;
	try {
		while (low < high) {
			const auto middle = low + (high - low) / 2;
			if (comparator(object->array[static_cast<std::size_t>(middle)], candidate,
				context) < 0) low = middle + 1;
			else high = middle;
		}
		if (low < range.location + range.length &&
			comparator(object->array[static_cast<std::size_t>(low)], candidate, context) == 0) {
			return low;
		}
		return low;
	} catch (...) { return -1; }
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetCount(
	darling_windows_CFArrayRef value)
{
	return darling_windows_CFArrayGetCount(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index)
{
	return darling_windows_CFArrayGetValueAtIndex(value, index);
}

extern "C" DARLING_CF_GUEST_ABI void CFArrayGetValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range, const void** output)
{
	darling_windows_CFArrayGetValues(value, range.location, range.length, output);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFArrayContainsValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate)
{
	const auto count = darling_windows_CFArrayGetCount(value);
	if (range.location < 0 || range.length < 0 || range.location > count ||
		range.length > count - range.location) return 0;
	const auto* object = static_cast<const Object*>(value);
	for (darling_windows_CFIndex index = range.location;
		index < range.location + range.length; ++index) {
		if (EqualArrayValues(object,
			darling_windows_CFArrayGetValueAtIndex(value, index), candidate)) return 1;
	}
	return 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate)
{
	const auto count = darling_windows_CFArrayGetCount(value);
	if (range.location < 0 || range.length < 0 || range.location > count ||
		range.length > count - range.location) return 0;
	const auto* object = static_cast<const Object*>(value);
	darling_windows_CFIndex matches = 0;
	for (darling_windows_CFIndex index = range.location;
		index < range.location + range.length; ++index)
		if (EqualArrayValues(object, object->array[static_cast<std::size_t>(index)], candidate))
			++matches;
	return matches;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate)
{
	const auto count = darling_windows_CFArrayGetCount(value);
	if (range.location < 0 || range.length < 0 || range.location > count ||
		range.length > count - range.location) return -1;
	const auto* object = static_cast<const Object*>(value);
	for (darling_windows_CFIndex index = range.location;
		index < range.location + range.length; ++index)
		if (EqualArrayValues(object, object->array[static_cast<std::size_t>(index)], candidate))
			return index;
	return -1;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate)
{
	const auto count = darling_windows_CFArrayGetCount(value);
	if (range.location < 0 || range.length < 0 || range.location > count ||
		range.length > count - range.location) return -1;
	const auto* object = static_cast<const Object*>(value);
	for (darling_windows_CFIndex index = range.location + range.length;
		index > range.location; --index)
		if (EqualArrayValues(object, object->array[static_cast<std::size_t>(index - 1)], candidate))
			return index - 1;
	return -1;
}

namespace {
template<class T> T DarlingCfReadScalar(const void* value)
{
	T result{};
	if (value != nullptr) std::memcpy(&result, value, sizeof(result));
	return result;
}

template<class T> bool DarlingCfWriteSigned(const Object* object, void* output)
{
	if (object == nullptr || output == nullptr ||
		(object->kind != Kind::Number && object->kind != Kind::Real)) return false;
	T converted{};
	if (object->kind == Kind::Number) {
		if (object->number < static_cast<std::int64_t>((std::numeric_limits<T>::min)()) ||
			object->number > static_cast<std::int64_t>((std::numeric_limits<T>::max)())) return false;
		converted = static_cast<T>(object->number);
	} else {
		const double value = object->real_number;
		if (!std::isfinite(value)) return false;
		if constexpr (std::numeric_limits<T>::digits == 63) {
			constexpr double signed_64_limit = 9223372036854775808.0;
			if (value < -signed_64_limit || value >= signed_64_limit) return false;
		} else if (value < static_cast<double>((std::numeric_limits<T>::min)()) ||
			value > static_cast<double>((std::numeric_limits<T>::max)())) return false;
		converted = static_cast<T>(value);
	}
	std::memcpy(output, &converted, sizeof(converted));
	return true;
}

template<class T> bool DarlingCfWriteFloat(const Object* object, void* output)
{
	if (object == nullptr || output == nullptr ||
		(object->kind != Kind::Number && object->kind != Kind::Real)) return false;
	const double value = object->kind == Kind::Number ?
		static_cast<double>(object->number) : object->real_number;
	if constexpr (sizeof(T) < sizeof(double)) {
		if (std::isfinite(value) && std::abs(value) >
			static_cast<double>((std::numeric_limits<T>::max)())) return false;
	}
	const T converted = static_cast<T>(value);
	std::memcpy(output, &converted, sizeof(converted));
	return true;
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFNumberGetTypeID()
{
	return darling_windows_CFNumberGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFNumberCreate(
	const void* allocator, darling_windows_CFNumberType type, const void* value)
{
	if (value == nullptr) return nullptr;
	const void* converted = nullptr;
	switch (type) {
	case 1: case 7: converted = darling_windows_CFNumberCreateInteger(DarlingCfReadScalar<std::int8_t>(value)); break;
	case 2: case 8: converted = darling_windows_CFNumberCreateInteger(DarlingCfReadScalar<std::int16_t>(value)); break;
	case 3: case 9: converted = darling_windows_CFNumberCreateInteger(DarlingCfReadScalar<std::int32_t>(value)); break;
	case 4: case 10: case 11: case 14: case 15:
		converted = darling_windows_CFNumberCreateInteger(DarlingCfReadScalar<std::int64_t>(value)); break;
	case 5: case 12: converted = darling_windows_CFNumberCreateDouble(DarlingCfReadScalar<float>(value)); break;
	case 6: case 13: case 16: converted = darling_windows_CFNumberCreateDouble(DarlingCfReadScalar<double>(value)); break;
	default: return nullptr;
	}
	auto* source = As(converted);
	if (source == nullptr) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, source->kind);
	if (object != nullptr) {
		object->number = source->number;
		object->real_number = source->real_number;
	}
	ReleaseOwned(source);
	return object;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFNumberGetValue(
	darling_windows_CFTypeRef number, darling_windows_CFNumberType type, void* output)
{
	const auto* object = static_cast<const Object*>(number);
	switch (type) {
	case 1: case 7: return DarlingCfWriteSigned<std::int8_t>(object, output);
	case 2: case 8: return DarlingCfWriteSigned<std::int16_t>(object, output);
	case 3: case 9: return DarlingCfWriteSigned<std::int32_t>(object, output);
	case 4: case 10: case 11: case 14: case 15: return DarlingCfWriteSigned<std::int64_t>(object, output);
	case 5: case 12: return DarlingCfWriteFloat<float>(object, output);
	case 6: case 13: case 16: return DarlingCfWriteFloat<double>(object, output);
	default: return 0;
	}
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFNumberIsFloatType(darling_windows_CFTypeRef number)
{
	const auto* object = static_cast<const Object*>(number);
	return object != nullptr && object->kind == Kind::Real;
}

extern "C" DARLING_CF_GUEST_ABI std::intptr_t CFNumberCompare(
	darling_windows_CFTypeRef left, darling_windows_CFTypeRef right, const void*)
{
	return darling_windows_CFNumberCompare(left, right);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDateGetTypeID()
{
	return darling_windows_CFDateGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDateRef CFDateCreate(
	const void* allocator, double absolute_time)
{
	if (!std::isfinite(absolute_time)) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Date);
	if (object == nullptr) return nullptr;
	object->date = absolute_time;
	return object;
}

extern "C" DARLING_CF_GUEST_ABI double CFDateGetAbsoluteTime(darling_windows_CFDateRef value)
{
	return darling_windows_CFDateGetAbsoluteTime(value);
}

extern "C" DARLING_CF_GUEST_ABI std::intptr_t CFDateCompare(
	darling_windows_CFDateRef left, darling_windows_CFDateRef right, const void*)
{
	return darling_windows_CFDateCompare(left, right);
}

extern "C" DARLING_CF_GUEST_ABI double CFDateGetTimeIntervalSinceDate(
	darling_windows_CFDateRef value, darling_windows_CFDateRef reference)
{
	return darling_windows_CFDateGetTimeIntervalSinceDate(value, reference);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDictionaryGetTypeID()
{
	return darling_windows_CFDictionaryGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value)
{
	return darling_windows_CFDictionaryGetCount(value);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key)
{
	return darling_windows_CFDictionaryGetValue(value, key);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryGetValueIfPresent(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key, const void** output)
{
	if (!darling_windows_CFDictionaryContainsKey(value, key)) return 0;
	if (output != nullptr) *output = darling_windows_CFDictionaryGetValue(value, key);
	return 1;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key)
{
	return darling_windows_CFDictionaryContainsKey(value, key) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryContainsValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef candidate)
{
	const auto count = darling_windows_CFDictionaryGetCount(value);
	if (count < 0 || static_cast<std::uint64_t>(count) >
		std::numeric_limits<std::size_t>::max() / sizeof(void*)) return 0;
	try {
		std::vector<const void*> keys(static_cast<std::size_t>(count));
		std::vector<const void*> values(static_cast<std::size_t>(count));
		darling_windows_CFDictionaryGetKeysAndValues(value, keys.data(), values.data());
		return std::any_of(values.begin(), values.end(), [candidate](const void* current) {
			return darling_windows_CFEqual(current, candidate);
		}) ? 1 : 0;
	} catch (...) { return 0; }
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values)
{
	darling_windows_CFDictionaryGetKeysAndValues(value, keys, values);
}

namespace {
std::pair<DarlingCFDictionaryKeyCallbacks, DarlingCFDictionaryValueCallbacks>
DarlingGetCallbacksForDictionary(const Object* object)
{
	if (object != nullptr && object->has_dictionary_callback_tables)
		return {object->dictionary_key_callbacks, object->dictionary_value_callbacks};
	return {kCFTypeDictionaryKeyCallBacks, kCFTypeDictionaryValueCallBacks};
}

darling_windows_CFDictionaryRef DarlingCreateGuestDictionary(
	const void* const* keys, const void* const* values, darling_windows_CFIndex count,
	const DarlingCFDictionaryKeyCallbacks* key_callbacks,
	const DarlingCFDictionaryValueCallbacks* value_callbacks,
	bool mutable_dictionary, darling_windows_CFIndex capacity, const void* allocator)
{
	if (count < 0 || capacity < 0 || (count != 0 && (keys == nullptr || values == nullptr)))
		return nullptr;
	DarlingCFDictionaryKeyCallbacks copied_key_callbacks{};
	DarlingCFDictionaryValueCallbacks copied_value_callbacks{};
	if (key_callbacks != nullptr) copied_key_callbacks = *key_callbacks;
	if (value_callbacks != nullptr) copied_value_callbacks = *value_callbacks;
	if (copied_key_callbacks.version != 0 || copied_value_callbacks.version != 0) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Dictionary);
	if (object == nullptr) return nullptr;
	object->mutable_dictionary = mutable_dictionary;
	object->has_dictionary_callback_tables = true;
	object->dictionary_key_callbacks = copied_key_callbacks;
	object->dictionary_value_callbacks = copied_value_callbacks;
	object->dictionary_allocator = allocator;
	try {
		object->dictionary.reserve(static_cast<std::size_t>((std::max)(count, capacity)));
		for (darling_windows_CFIndex index = 0; index < count; ++index) {
			if (keys[index] == nullptr || values[index] == nullptr) { ReleaseOwned(object); return nullptr; }
			auto found = std::find_if(object->dictionary.begin(), object->dictionary.end(),
				[&](const auto& entry) { return EqualDictionaryKeys(object, entry.first, keys[index]); });
			const auto* retained_value = RetainDictionaryValue(object, values[index]);
			if (retained_value == nullptr) { ReleaseOwned(object); return nullptr; }
			if (found != object->dictionary.end()) {
				ReleaseDictionaryValue(object, found->second);
				found->second = retained_value;
				continue;
			}
			const auto* retained_key = RetainDictionaryKey(object, keys[index]);
			if (retained_key == nullptr) {
				ReleaseDictionaryValue(object, retained_value);
				ReleaseOwned(object);
				return nullptr;
			}
			try { object->dictionary.emplace_back(retained_key, retained_value); }
			catch (...) {
				ReleaseDictionaryKey(object, retained_key);
				ReleaseDictionaryValue(object, retained_value);
				throw;
			}
		}
	} catch (...) {
		ReleaseOwned(object);
		return nullptr;
	}
	return object;
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreate(
	const void* allocator, const void* const* keys, const void* const* values,
	darling_windows_CFIndex count, const void* key_callbacks, const void* value_callbacks)
{
	return DarlingCreateGuestDictionary(keys, values, count,
		static_cast<const DarlingCFDictionaryKeyCallbacks*>(key_callbacks),
		static_cast<const DarlingCFDictionaryValueCallbacks*>(value_callbacks), false, 0,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity,
	const void* key_callbacks, const void* value_callbacks)
{
	return DarlingCreateGuestDictionary(nullptr, nullptr, 0,
		static_cast<const DarlingCFDictionaryKeyCallbacks*>(key_callbacks),
		static_cast<const DarlingCFDictionaryValueCallbacks*>(value_callbacks), true, capacity,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateCopy(
	const void* allocator, darling_windows_CFDictionaryRef value)
{
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Dictionary) return nullptr;
	const auto callbacks = DarlingGetCallbacksForDictionary(source);
	std::vector<const void*> keys;
	std::vector<const void*> values;
	try {
		keys.reserve(source->dictionary.size());
		values.reserve(source->dictionary.size());
		for (const auto& entry : source->dictionary) {
			keys.push_back(entry.first);
			values.push_back(entry.second);
		}
	} catch (...) { return nullptr; }
	return DarlingCreateGuestDictionary(keys.data(), values.data(),
		static_cast<darling_windows_CFIndex>(keys.size()), &callbacks.first,
		&callbacks.second, false, 0, allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity, darling_windows_CFDictionaryRef value)
{
	if (capacity < 0) return nullptr;
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Dictionary) return nullptr;
	const auto callbacks = DarlingGetCallbacksForDictionary(source);
	std::vector<const void*> keys;
	std::vector<const void*> values;
	try {
		keys.reserve(source->dictionary.size());
		values.reserve(source->dictionary.size());
		for (const auto& entry : source->dictionary) {
			keys.push_back(entry.first);
			values.push_back(entry.second);
		}
	} catch (...) { return nullptr; }
	return DarlingCreateGuestDictionary(keys.data(), values.data(),
		static_cast<darling_windows_CFIndex>(keys.size()), &callbacks.first,
		&callbacks.second, true, capacity, allocator);
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionarySetValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_dictionary)
		(void)darling_windows_CFDictionarySetValue(value, key, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryAddValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_dictionary &&
		!darling_windows_CFDictionaryContainsKey(value, key))
		(void)darling_windows_CFDictionarySetValue(value, key, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryReplaceValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_dictionary &&
		darling_windows_CFDictionaryContainsKey(value, key))
		(void)darling_windows_CFDictionarySetValue(value, key, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryRemoveValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_dictionary)
		(void)darling_windows_CFDictionaryRemoveValue(value, key);
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryRemoveAllValues(
	darling_windows_CFDictionaryRef value)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_dictionary)
		darling_windows_CFDictionaryRemoveAllValues(value);
}

namespace {
using DarlingGuestDictionaryApplier = void (DARLING_CF_GUEST_ABI *)(
	const void*, const void*, void*);
struct DarlingGuestDictionaryApplyContext final {
	DarlingGuestDictionaryApplier callback{};
	void* context{};
};
void DarlingInvokeGuestDictionaryApplier(const void* key, const void* value, void* raw_context)
{
	auto* context = static_cast<DarlingGuestDictionaryApplyContext*>(raw_context);
	if (context == nullptr || context->callback == nullptr) return;
	try { context->callback(key, value, context->context); }
	catch (...) { /* Do not unwind through the CoreFoundation collection loop. */ }
}
}

extern "C" DARLING_CF_GUEST_ABI void CFDictionaryApplyFunction(
	darling_windows_CFDictionaryRef value, DarlingGuestDictionaryApplier callback,
	void* context)
{
	if (callback == nullptr) return;
	DarlingGuestDictionaryApplyContext apply_context{callback, context};
	darling_windows_CFDictionaryApplyFunction(value,
		&DarlingInvokeGuestDictionaryApplier, &apply_context);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFSetGetTypeID()
{
	return darling_windows_CFSetGetTypeID();
}

namespace {
std::pair<DarlingCFDictionaryKeyCallbacks, const void*> DarlingGetCallbacksForSet(
	const Object* object)
{
	if (object != nullptr && object->has_set_callback_table)
		return {object->set_callbacks, object->set_allocator};
	return {kCFTypeSetCallBacks, nullptr};
}
darling_windows_CFSetRef DarlingCreateGuestSet(const void* const* values,
	darling_windows_CFIndex count, const DarlingCFDictionaryKeyCallbacks* callbacks,
	bool mutable_set, darling_windows_CFIndex capacity, const void* allocator);
}

namespace {
darling_windows_CFSetRef DarlingCreateGuestSet(const void* const* values,
	darling_windows_CFIndex count, const DarlingCFDictionaryKeyCallbacks* callbacks,
	bool mutable_set, darling_windows_CFIndex capacity, const void* allocator)
{
	if (count < 0 || capacity < 0 || (count != 0 && values == nullptr)) return nullptr;
	DarlingCFDictionaryKeyCallbacks copied_callbacks{};
	if (callbacks != nullptr) copied_callbacks = *callbacks;
	if (copied_callbacks.version != 0) return nullptr;
	auto* object = CreateObjectWithAllocator(allocator, Kind::Set);
	if (object == nullptr) return nullptr;
	object->mutable_set = mutable_set;
	object->has_set_callback_table = true;
	object->set_callbacks = copied_callbacks;
	object->set_allocator = allocator;
	try {
		object->set_entries.reserve(static_cast<std::size_t>((std::max)(count, capacity)));
		for (darling_windows_CFIndex index = 0; index < count; ++index) {
			if (values[index] == nullptr) { ReleaseOwned(object); return nullptr; }
			const auto hash = HashSetValue(object, values[index]);
			if (FindSetEntry(object, values[index], hash) != object->set_entries.end()) continue;
			const auto* retained = RetainSetValue(object, values[index]);
			if (retained == nullptr) { ReleaseOwned(object); return nullptr; }
			try { object->set_entries.emplace(hash, retained); }
			catch (...) { ReleaseSetValue(object, retained); throw; }
		}
	} catch (...) {
		ReleaseOwned(object);
		return nullptr;
	}
	return object;
}
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreate(
	const void* allocator, const void* const* values, darling_windows_CFIndex count,
	const void* callbacks)
{
	return DarlingCreateGuestSet(values, count,
		static_cast<const DarlingCFDictionaryKeyCallbacks*>(callbacks), false, 0, allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity, const void* callbacks)
{
	return DarlingCreateGuestSet(nullptr, 0,
		static_cast<const DarlingCFDictionaryKeyCallbacks*>(callbacks), true, capacity,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateCopy(
	const void* allocator, darling_windows_CFSetRef value)
{
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Set) return nullptr;
	const auto callbacks = DarlingGetCallbacksForSet(source);
	std::vector<const void*> values;
	try {
		values.reserve(source->set_entries.size());
		for (const auto& entry : source->set_entries) values.push_back(entry.second);
	} catch (...) { return nullptr; }
	return DarlingCreateGuestSet(values.data(),
		static_cast<darling_windows_CFIndex>(values.size()), &callbacks.first, false, 0, allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity, darling_windows_CFSetRef value)
{
	if (capacity < 0) return nullptr;
	const auto* source = static_cast<const Object*>(value);
	if (source == nullptr || source->kind != Kind::Set) return nullptr;
	const auto callbacks = DarlingGetCallbacksForSet(source);
	std::vector<const void*> values;
	try {
		values.reserve(source->set_entries.size());
		for (const auto& entry : source->set_entries) values.push_back(entry.second);
	} catch (...) { return nullptr; }
	return DarlingCreateGuestSet(values.data(),
		static_cast<darling_windows_CFIndex>(values.size()), &callbacks.first, true, capacity,
		allocator);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFSetGetCount(
	darling_windows_CFSetRef value)
{
	return darling_windows_CFSetGetCount(value);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFSetContainsValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate)
{
	return darling_windows_CFSetContainsValue(value, candidate) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFSetGetCountOfValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate)
{
	return darling_windows_CFSetContainsValue(value, candidate) ? 1 : 0;
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFSetGetValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate)
{
	return darling_windows_CFSetGetValue(value, candidate);
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFSetGetValueIfPresent(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate,
	const void** output)
{
	const auto result = darling_windows_CFSetGetValue(value, candidate);
	if (result == nullptr) return 0;
	if (output != nullptr) *output = result;
	return 1;
}

extern "C" DARLING_CF_GUEST_ABI void CFSetGetValues(
	darling_windows_CFSetRef value, const void** output)
{
	darling_windows_CFSetGetValues(value, output);
}

namespace {
struct DarlingGuestSetApplyContext final {
	darling_windows_CFSetGuestApplierFunction callback{};
	void* context{};
};
void DarlingInvokeGuestSetApplier(const void* value, void* raw_context)
{
	auto* context = static_cast<DarlingGuestSetApplyContext*>(raw_context);
	if (context == nullptr || context->callback == nullptr) return;
	try { context->callback(value, context->context); }
	catch (...) { /* Keep guest exceptions inside the framework boundary. */ }
}
}

extern "C" DARLING_CF_GUEST_ABI void CFSetApplyFunction(
	darling_windows_CFSetRef value, darling_windows_CFSetGuestApplierFunction callback,
	void* context)
{
	if (callback == nullptr) return;
	DarlingGuestSetApplyContext apply_context{callback, context};
	darling_windows_CFSetApplyFunction(value, &DarlingInvokeGuestSetApplier,
		&apply_context);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetAddValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		(void)darling_windows_CFSetAddValue(value, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetSetValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object == nullptr || !object->mutable_set || element == nullptr) return;
	const auto hash = HashSetValue(object, element);
	const auto found = FindSetEntry(object, element, hash);
	if (found != object->set_entries.end()) {
		const auto* retained = RetainSetValue(object, element);
		if (retained == nullptr) return;
		const auto retained_hash = HashSetValue(object, retained);
		const auto* previous = found->second;
		auto node = object->set_entries.extract(found);
		ReleaseSetValue(object, previous);
		node.key() = retained_hash;
		node.mapped() = retained;
		object->set_entries.insert(std::move(node));
		return;
	}
	(void)darling_windows_CFSetAddValue(value, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetReplaceValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object == nullptr || !object->mutable_set || element == nullptr) return;
	const auto hash = HashSetValue(object, element);
	const auto found = FindSetEntry(object, element, hash);
	if (found == object->set_entries.end()) return;
	const auto* retained = RetainSetValue(object, element);
	if (retained == nullptr) return;
	const auto retained_hash = HashSetValue(object, retained);
	const auto* previous = found->second;
	auto node = object->set_entries.extract(found);
	ReleaseSetValue(object, previous);
	node.key() = retained_hash;
	node.mapped() = retained;
	object->set_entries.insert(std::move(node));
}

extern "C" DARLING_CF_GUEST_ABI void CFSetRemoveValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		(void)darling_windows_CFSetRemoveValue(value, element);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetRemoveAllValues(darling_windows_CFSetRef value)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		darling_windows_CFSetRemoveAllValues(value);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetUnion(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		(void)darling_windows_CFSetUnion(value, source);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetIntersect(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		(void)darling_windows_CFSetIntersect(value, source);
}

extern "C" DARLING_CF_GUEST_ABI void CFSetSubtract(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source)
{
	auto* object = As(value);
	if (object != nullptr && object->mutable_set)
		(void)darling_windows_CFSetSubtract(value, source);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFBooleanGetTypeID()
{
	return darling_windows_CFBooleanGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFBooleanGetValue(
	darling_windows_CFBooleanRef value)
{
	return darling_windows_CFBooleanIsTrue(value) ? 1 : 0;
}

extern "C" std::uintptr_t darling_windows_corefoundation_symbol(const char* name)
{
	if (name == nullptr) return 0;
	if (*name == '_') ++name;
	if (std::strcmp(name, "CFRunLoopGetCurrent") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopGetCurrent);
	if (std::strcmp(name, "CFRunLoopGetMain") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopGetMain);
	if (std::strcmp(name, "CFRunLoopGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopGetTypeID);
	if (std::strcmp(name, "CFRunLoopCopyCurrentMode") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopCopyCurrentMode);
	if (std::strcmp(name, "CFRunLoopCopyAllModes") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopCopyAllModes);
	if (std::strcmp(name, "CFRunLoopRunInMode") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopRunInMode);
	if (std::strcmp(name, "CFRunLoopRun") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopRun);
	if (std::strcmp(name, "CFRunLoopIsWaiting") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopIsWaiting);
	if (std::strcmp(name, "CFRunLoopStop") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopStop);
	if (std::strcmp(name, "CFRunLoopWakeUp") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopWakeUp);
	if (std::strcmp(name, "CFRunLoopPerformBlock") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopPerformBlock);
	if (std::strcmp(name, "CFRunLoopAddCommonMode") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopAddCommonMode);
	if (std::strcmp(name, "CFRunLoopSourceGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceGetTypeID);
	if (std::strcmp(name, "CFRunLoopSourceCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceCreate);
	if (std::strcmp(name, "CFRunLoopSourceGetOrder") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceGetOrder);
	if (std::strcmp(name, "CFRunLoopSourceGetContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceGetContext);
	if (std::strcmp(name, "CFRunLoopSourceSignal") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceSignal);
	if (std::strcmp(name, "CFRunLoopSourceIsValid") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceIsValid);
	if (std::strcmp(name, "CFRunLoopSourceInvalidate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopSourceInvalidate);
	if (std::strcmp(name, "CFMachPortGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortGetTypeID);
	if (std::strcmp(name, "CFMachPortCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortCreate);
	if (std::strcmp(name, "CFMachPortCreateWithPort") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortCreateWithPort);
	if (std::strcmp(name, "CFMachPortCreateRunLoopSource") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortCreateRunLoopSource);
	if (std::strcmp(name, "CFMachPortGetContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortGetContext);
	if (std::strcmp(name, "CFMachPortGetPort") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortGetPort);
	if (std::strcmp(name, "CFMachPortIsValid") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortIsValid);
	if (std::strcmp(name, "CFMachPortInvalidate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortInvalidate);
	if (std::strcmp(name, "CFMachPortSetInvalidationCallBack") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortSetInvalidationCallBack);
	if (std::strcmp(name, "CFMachPortGetInvalidationCallBack") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFMachPortGetInvalidationCallBack);
	if (std::strcmp(name, "CFAllocatorGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorGetTypeID);
	if (std::strcmp(name, "CFAllocatorCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorCreate);
	if (std::strcmp(name, "CFAllocatorGetContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorGetContext);
	if (std::strcmp(name, "CFAllocatorAllocate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorAllocate);
	if (std::strcmp(name, "CFAllocatorDeallocate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorDeallocate);
	if (std::strcmp(name, "CFAllocatorReallocate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorReallocate);
	if (std::strcmp(name, "CFAllocatorGetPreferredSizeForSize") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorGetPreferredSizeForSize);
	if (std::strcmp(name, "CFAllocatorGetDefault") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorGetDefault);
	if (std::strcmp(name, "CFAllocatorSetDefault") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFAllocatorSetDefault);
	if (std::strcmp(name, "kCFAllocatorDefault") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorDefault);
	if (std::strcmp(name, "kCFAllocatorSystemDefault") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorSystemDefault);
	if (std::strcmp(name, "kCFAllocatorMalloc") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorMalloc);
	if (std::strcmp(name, "kCFAllocatorMallocZone") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorMallocZone);
	if (std::strcmp(name, "kCFAllocatorNull") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorNull);
	if (std::strcmp(name, "kCFAllocatorUseContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFAllocatorUseContext);
	if (std::strcmp(name, "CFRunLoopContainsSource") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopContainsSource);
	if (std::strcmp(name, "CFRunLoopAddSource") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopAddSource);
	if (std::strcmp(name, "CFRunLoopRemoveSource") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopRemoveSource);
	if (std::strcmp(name, "CFRunLoopTimerGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerGetTypeID);
	if (std::strcmp(name, "CFRunLoopTimerCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerCreate);
	if (std::strcmp(name, "CFRunLoopTimerGetNextFireDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerGetNextFireDate);
	if (std::strcmp(name, "CFRunLoopTimerSetNextFireDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerSetNextFireDate);
	if (std::strcmp(name, "CFRunLoopTimerGetInterval") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerGetInterval);
	if (std::strcmp(name, "CFRunLoopTimerDoesRepeat") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerDoesRepeat);
	if (std::strcmp(name, "CFRunLoopTimerGetOrder") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerGetOrder);
	if (std::strcmp(name, "CFRunLoopTimerInvalidate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerInvalidate);
	if (std::strcmp(name, "CFRunLoopTimerIsValid") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerIsValid);
	if (std::strcmp(name, "CFRunLoopTimerGetContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopTimerGetContext);
	if (std::strcmp(name, "CFRunLoopContainsTimer") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopContainsTimer);
	if (std::strcmp(name, "CFRunLoopAddTimer") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopAddTimer);
	if (std::strcmp(name, "CFRunLoopRemoveTimer") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopRemoveTimer);
	if (std::strcmp(name, "CFRunLoopGetNextTimerFireDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopGetNextTimerFireDate);
	if (std::strcmp(name, "CFRunLoopObserverGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverGetTypeID);
	if (std::strcmp(name, "CFRunLoopObserverCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverCreate);
	if (std::strcmp(name, "CFRunLoopObserverGetActivities") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverGetActivities);
	if (std::strcmp(name, "CFRunLoopObserverDoesRepeat") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverDoesRepeat);
	if (std::strcmp(name, "CFRunLoopObserverGetOrder") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverGetOrder);
	if (std::strcmp(name, "CFRunLoopObserverGetContext") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverGetContext);
	if (std::strcmp(name, "CFRunLoopObserverIsValid") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverIsValid);
	if (std::strcmp(name, "CFRunLoopObserverInvalidate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopObserverInvalidate);
	if (std::strcmp(name, "CFRunLoopContainsObserver") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopContainsObserver);
	if (std::strcmp(name, "CFRunLoopAddObserver") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopAddObserver);
	if (std::strcmp(name, "CFRunLoopRemoveObserver") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFRunLoopRemoveObserver);
	if (std::strcmp(name, "CFStringGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetTypeID);
	if (std::strcmp(name, "CFStringCreateWithCString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringCreateWithCString);
	if (std::strcmp(name, "CFStringGetLength") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetLength);
	if (std::strcmp(name, "CFStringGetCString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetCString);
	if (std::strcmp(name, "CFStringCreateWithCharacters") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringCreateWithCharacters);
	if (std::strcmp(name, "CFStringCreateCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringCreateCopy);
	if (std::strcmp(name, "CFStringCreateMutable") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringCreateMutable);
	if (std::strcmp(name, "CFStringCreateMutableCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringCreateMutableCopy);
	if (std::strcmp(name, "CFURLCreateWithString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateWithString);
	if (std::strcmp(name, "CFURLCreateWithFileSystemPath") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateWithFileSystemPath);
	if (std::strcmp(name, "CFURLCreateWithFileSystemPathRelativeToBase") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateWithFileSystemPathRelativeToBase);
	if (std::strcmp(name, "CFURLCreateFromFileSystemRepresentation") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateFromFileSystemRepresentation);
	if (std::strcmp(name, "CFURLCreateStringByReplacingPercentEscapes") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateStringByReplacingPercentEscapes);
	if (std::strcmp(name, "CFURLCreateStringByReplacingPercentEscapesUsingEncoding") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateStringByReplacingPercentEscapesUsingEncoding);
	if (std::strcmp(name, "CFURLCreateStringByAddingPercentEscapes") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCreateStringByAddingPercentEscapes);
	if (std::strcmp(name, "CFURLGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLGetTypeID);
	if (std::strcmp(name, "CFURLCanBeDecomposed") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCanBeDecomposed);
	if (std::strcmp(name, "CFURLGetString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLGetString);
	if (std::strcmp(name, "CFURLCopyFileSystemPath") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyFileSystemPath);
	if (std::strcmp(name, "CFURLCopyPath") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyPath);
	if (std::strcmp(name, "CFURLCopyStrictPath") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyStrictPath);
	if (std::strcmp(name, "CFURLCopyParameterString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyParameterString);
	if (std::strcmp(name, "CFURLCopyLastPathComponent") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyLastPathComponent);
	if (std::strcmp(name, "CFURLCopyPathExtension") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyPathExtension);
	if (std::strcmp(name, "CFURLCopyScheme") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyScheme);
	if (std::strcmp(name, "CFURLCopyResourceSpecifier") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyResourceSpecifier);
	if (std::strcmp(name, "CFURLCopyHostName") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyHostName);
	if (std::strcmp(name, "CFURLCopyNetLocation") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyNetLocation);
	if (std::strcmp(name, "CFURLCopyQueryString") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyQueryString);
	if (std::strcmp(name, "CFURLCopyFragment") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyFragment);
	if (std::strcmp(name, "CFURLCopyUserName") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyUserName);
	if (std::strcmp(name, "CFURLCopyPassword") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyPassword);
	if (std::strcmp(name, "CFURLGetPortNumber") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLGetPortNumber);
	if (std::strcmp(name, "CFURLHasDirectoryPath") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLHasDirectoryPath);
	if (std::strcmp(name, "CFURLGetFileSystemRepresentation") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLGetFileSystemRepresentation);
	if (std::strcmp(name, "CFURLCopyAbsoluteURL") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFURLCopyAbsoluteURL);
	if (std::strcmp(name, "CFStringGetCStringPtr") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetCStringPtr);
	if (std::strcmp(name, "CFStringGetCharacterAtIndex") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetCharacterAtIndex);
	if (std::strcmp(name, "CFStringGetCharacters") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetCharacters);
	if (std::strcmp(name, "CFStringGetMaximumSizeForEncoding") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetMaximumSizeForEncoding);
	if (std::strcmp(name, "CFStringGetMaximumSizeOfFileSystemRepresentation") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetMaximumSizeOfFileSystemRepresentation);
	if (std::strcmp(name, "CFStringGetFileSystemRepresentation") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFStringGetFileSystemRepresentation);
	if (std::strcmp(name, "CFArrayGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetTypeID);
	if (std::strcmp(name, "CFArrayCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayCreate);
	if (std::strcmp(name, "CFArrayCreateMutable") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayCreateMutable);
	if (std::strcmp(name, "CFArrayCreateCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayCreateCopy);
	if (std::strcmp(name, "CFArrayCreateMutableCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayCreateMutableCopy);
	if (std::strcmp(name, "CFArrayGetCount") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetCount);
	if (std::strcmp(name, "CFArrayGetValueAtIndex") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetValueAtIndex);
	if (std::strcmp(name, "CFArrayGetValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetValues);
	if (std::strcmp(name, "CFArrayContainsValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayContainsValue);
	if (std::strcmp(name, "CFArrayGetCountOfValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetCountOfValue);
	if (std::strcmp(name, "CFArrayGetFirstIndexOfValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetFirstIndexOfValue);
	if (std::strcmp(name, "CFArrayGetLastIndexOfValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayGetLastIndexOfValue);
	if (std::strcmp(name, "CFArrayAppendValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayAppendValue);
	if (std::strcmp(name, "CFArrayInsertValueAtIndex") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayInsertValueAtIndex);
	if (std::strcmp(name, "CFArrayRemoveValueAtIndex") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayRemoveValueAtIndex);
	if (std::strcmp(name, "CFArraySetValueAtIndex") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArraySetValueAtIndex);
	if (std::strcmp(name, "CFArrayReplaceValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayReplaceValues);
	if (std::strcmp(name, "CFArrayRemoveAllValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayRemoveAllValues);
	if (std::strcmp(name, "CFArrayApplyFunction") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayApplyFunction);
	if (std::strcmp(name, "CFArrayAppendArray") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayAppendArray);
	if (std::strcmp(name, "CFArrayExchangeValuesAtIndices") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayExchangeValuesAtIndices);
	if (std::strcmp(name, "CFArraySortValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArraySortValues);
	if (std::strcmp(name, "CFArrayBSearchValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFArrayBSearchValues);
	if (std::strcmp(name, "CFNumberGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFNumberGetTypeID);
	if (std::strcmp(name, "CFNumberCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFNumberCreate);
	if (std::strcmp(name, "CFNumberGetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFNumberGetValue);
	if (std::strcmp(name, "CFNumberIsFloatType") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFNumberIsFloatType);
	if (std::strcmp(name, "CFNumberCompare") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFNumberCompare);
	if (std::strcmp(name, "CFDateGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDateGetTypeID);
	if (std::strcmp(name, "CFDateCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDateCreate);
	if (std::strcmp(name, "CFDateGetAbsoluteTime") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDateGetAbsoluteTime);
	if (std::strcmp(name, "CFDateCompare") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDateCompare);
	if (std::strcmp(name, "CFDateGetTimeIntervalSinceDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDateGetTimeIntervalSinceDate);
	if (std::strcmp(name, "CFDictionaryGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryGetTypeID);
	if (std::strcmp(name, "CFDictionaryGetCount") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryGetCount);
	if (std::strcmp(name, "CFDictionaryGetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryGetValue);
	if (std::strcmp(name, "CFDictionaryGetValueIfPresent") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryGetValueIfPresent);
	if (std::strcmp(name, "CFDictionaryContainsKey") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryContainsKey);
	if (std::strcmp(name, "CFDictionaryContainsValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryContainsValue);
	if (std::strcmp(name, "CFDictionaryGetKeysAndValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryGetKeysAndValues);
	if (std::strcmp(name, "CFDictionaryCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryCreate);
	if (std::strcmp(name, "CFDictionaryCreateMutable") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryCreateMutable);
	if (std::strcmp(name, "CFDictionaryCreateCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryCreateCopy);
	if (std::strcmp(name, "CFDictionaryCreateMutableCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryCreateMutableCopy);
	if (std::strcmp(name, "CFDictionarySetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionarySetValue);
	if (std::strcmp(name, "CFDictionaryAddValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryAddValue);
	if (std::strcmp(name, "CFDictionaryReplaceValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryReplaceValue);
	if (std::strcmp(name, "CFDictionaryRemoveValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryRemoveValue);
	if (std::strcmp(name, "CFDictionaryRemoveAllValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryRemoveAllValues);
	if (std::strcmp(name, "CFDictionaryApplyFunction") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFDictionaryApplyFunction);
	if (std::strcmp(name, "kCFTypeDictionaryKeyCallBacks") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFTypeDictionaryKeyCallBacks);
	if (std::strcmp(name, "kCFTypeDictionaryValueCallBacks") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFTypeDictionaryValueCallBacks);
	if (std::strcmp(name, "kCFTypeSetCallBacks") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFTypeSetCallBacks);
	if (std::strcmp(name, "kCFTypeArrayCallBacks") == 0)
		return reinterpret_cast<std::uintptr_t>(&kCFTypeArrayCallBacks);
	if (std::strcmp(name, "CFSetGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetTypeID);
	if (std::strcmp(name, "CFSetCreate") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetCreate);
	if (std::strcmp(name, "CFSetCreateMutable") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetCreateMutable);
	if (std::strcmp(name, "CFSetCreateCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetCreateCopy);
	if (std::strcmp(name, "CFSetCreateMutableCopy") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetCreateMutableCopy);
	if (std::strcmp(name, "CFSetGetCount") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetCount);
	if (std::strcmp(name, "CFSetContainsValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetContainsValue);
	if (std::strcmp(name, "CFSetGetCountOfValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetCountOfValue);
	if (std::strcmp(name, "CFSetGetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetValue);
	if (std::strcmp(name, "CFSetGetValueIfPresent") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetValueIfPresent);
	if (std::strcmp(name, "CFSetGetValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetGetValues);
	if (std::strcmp(name, "CFSetApplyFunction") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetApplyFunction);
	if (std::strcmp(name, "CFSetAddValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetAddValue);
	if (std::strcmp(name, "CFSetSetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetSetValue);
	if (std::strcmp(name, "CFSetReplaceValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetReplaceValue);
	if (std::strcmp(name, "CFSetRemoveValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetRemoveValue);
	if (std::strcmp(name, "CFSetRemoveAllValues") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetRemoveAllValues);
	if (std::strcmp(name, "CFSetUnion") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetUnion);
	if (std::strcmp(name, "CFSetIntersect") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetIntersect);
	if (std::strcmp(name, "CFSetSubtract") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFSetSubtract);
	if (std::strcmp(name, "CFBooleanGetTypeID") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFBooleanGetTypeID);
	if (std::strcmp(name, "CFBooleanGetValue") == 0)
		return reinterpret_cast<std::uintptr_t>(&CFBooleanGetValue);
	return 0;
}

extern "C" void* darling_windows_CFNotificationCenterGetLocal()
{
	static int local_center;
	return &local_center;
}

extern "C" bool darling_windows_CFNotificationCenterAddObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name)
{
	if (center == nullptr || callback == nullptr) return false;
	std::lock_guard lock(notification_mutex);
	auto& entries = notifications[name == nullptr ? notification_wildcard : name];
	for (const auto& entry : entries)
		if (entry.observer == observer && entry.callback == callback) return true;
	entries.push_back({observer, callback});
	return true;
}

extern "C" bool darling_windows_CFNotificationCenterRemoveObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name)
{
	if (center == nullptr) return false;
	std::lock_guard lock(notification_mutex);
	if (name == nullptr) {
		bool removed = false;
		for (auto it = notifications.begin(); it != notifications.end();) {
			const auto before = it->second.size();
			it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
				[observer, callback](const NotificationObserver& entry) {
					return entry.observer == observer && (callback == nullptr || entry.callback == callback);
				}), it->second.end());
			removed = removed || before != it->second.size();
			if (it->second.empty()) it = notifications.erase(it); else ++it;
		}
		return removed;
	}
	auto found = notifications.find(name);
	if (found == notifications.end()) return false;
	auto& entries = found->second;
	const auto before = entries.size();
	entries.erase(std::remove_if(entries.begin(), entries.end(),
		[observer, callback](const NotificationObserver& entry) {
			return entry.observer == observer && (callback == nullptr || entry.callback == callback);
		}), entries.end());
	const bool removed = before != entries.size();
	if (entries.empty()) notifications.erase(found);
	return removed;
}

extern "C" void darling_windows_CFNotificationCenterPostNotification(void* center,
	const char* name, const void* object)
{
	if (center == nullptr || name == nullptr) return;
	std::vector<NotificationObserver> snapshot;
	{
		std::lock_guard lock(notification_mutex);
		const auto found = notifications.find(name);
		if (found != notifications.end()) snapshot = found->second;
		const auto wildcard = notifications.find(notification_wildcard);
		if (wildcard != notifications.end())
			snapshot.insert(snapshot.end(), wildcard->second.begin(), wildcard->second.end());
	}
	for (const auto& entry : snapshot) entry.callback(entry.observer, name, object);
}

namespace {
Object* AsBundle(const void* value)
{
	auto* object = As(value);
	return object != nullptr && object->kind == Kind::Bundle ? object : nullptr;
}

std::string PathUtf8(const std::filesystem::path& path)
{
	const auto value = path.u8string();
	return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path NormalizeBundlePath(const std::filesystem::path& path)
{
	std::error_code error;
	const auto canonical = std::filesystem::weakly_canonical(path, error);
	if (!error) return canonical.lexically_normal();
	return std::filesystem::absolute(path).lexically_normal();
}

std::vector<std::filesystem::path> BundleVersionDirectories(
	const std::filesystem::path& root)
{
	std::vector<std::filesystem::path> versions{root / "Versions" / "Current"};
	std::error_code error;
	const auto version_root = root / "Versions";
	if (!std::filesystem::is_directory(version_root, error) || error) return versions;
	try {
		std::vector<std::filesystem::path> concrete;
		for (std::filesystem::directory_iterator iterator(version_root, error), end;
			!error && iterator != end; iterator.increment(error)) {
			if (iterator->path().filename() == "Current") continue;
			std::error_code entry_error;
			if (iterator->is_directory(entry_error) && !entry_error)
				concrete.push_back(iterator->path());
		}
		// Current is authoritative when present; if a transfer dropped that
		// symlink, prefer the greatest version name as a deterministic fallback.
		std::sort(concrete.rbegin(), concrete.rend());
		versions.insert(versions.end(), concrete.begin(), concrete.end());
	} catch (...) { /* Keep the conventional Current path even if enumeration fails. */ }
	return versions;
}

std::vector<std::filesystem::path> BundleResourceRoots(
	const std::filesystem::path& root)
{
	std::vector<std::filesystem::path> resources{
		root / "Contents" / "Resources", root / "Resources"};
	for (const auto& version : BundleVersionDirectories(root))
		resources.push_back(version / "Resources");
	return resources;
}

darling_windows_CFDictionaryRef LoadBundleInfoDictionary(
	const std::filesystem::path& root)
{
	std::vector<std::filesystem::path> plist_paths{
		root / "Contents" / "Info.plist", root / "Info.plist"};
	for (const auto& resources : BundleResourceRoots(root))
		plist_paths.push_back(resources / "Info.plist");
	for (const auto& plist_path : plist_paths) {
		try {
			std::ifstream input(plist_path, std::ios::binary);
			if (!input) continue;
			std::string contents((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			if (contents.size() > static_cast<std::size_t>(
				(std::numeric_limits<darling_windows_CFIndex>::max)())) continue;
			const auto* properties = contents.rfind("bplist00", 0) == 0
				? darling_windows_CFPropertyListCreateFromBinary(contents.data(),
					static_cast<darling_windows_CFIndex>(contents.size()))
				: darling_windows_CFPropertyListCreateFromXML(contents.c_str());
			if (properties != nullptr && darling_windows_CFGetTypeID(properties) ==
				darling_windows_CFDictionaryGetTypeID())
				return static_cast<darling_windows_CFDictionaryRef>(properties);
			darling_windows_CFRelease(properties);
		} catch (...) { /* Invalid or inaccessible Info.plist is not a valid executable lookup. */ }
	}
	return nullptr;
}

darling_windows_CFDictionaryRef BundleInfoDictionary(const Object* bundle)
{
	if (bundle == nullptr || bundle->kind != Kind::Bundle) return nullptr;
	std::lock_guard lock(bundle->bundle_info_mutex);
	if (bundle->bundle_info_dictionary == nullptr) {
		try {
			bundle->bundle_info_dictionary = LoadBundleInfoDictionary(
				std::filesystem::u8path(bundle->bundle_path));
		} catch (...) { return nullptr; }
	}
	return static_cast<darling_windows_CFDictionaryRef>(bundle->bundle_info_dictionary);
}

std::filesystem::path BundleExecutablePath(const Object* bundle)
{
	if (bundle == nullptr || bundle->kind != Kind::Bundle) return {};
	const auto root = std::filesystem::u8path(bundle->bundle_path);
	const auto properties = BundleInfoDictionary(bundle);
	const auto key = darling_windows_CFStringCreateWithCString("CFBundleExecutable");
	const auto value = properties == nullptr || key == nullptr ? nullptr :
		darling_windows_CFDictionaryGetValue(properties, key);
	std::array<char, 4096> executable{};
	const bool valid = value != nullptr && darling_windows_CFStringGetCString(value,
		executable.data(), static_cast<darling_windows_CFIndex>(executable.size()));
	darling_windows_CFRelease(key);
	std::vector<std::filesystem::path> candidates;
	if (valid && executable[0] != '\0') {
		const std::filesystem::path executable_name =
			std::filesystem::u8path(executable.data());
		// CFBundleExecutable is a file name, not a relative path. Reject path
		// components so malformed bundle metadata cannot escape its bundle root.
		if (executable_name.filename() != executable_name || executable_name == "." ||
			executable_name == ".." || executable_name.has_root_path() ||
		std::find(executable.begin(), executable.end(), ':') != executable.end()) return {};
		candidates.push_back(root / "Contents" / "MacOS" / executable_name);
		for (const auto& version : BundleVersionDirectories(root))
			candidates.push_back(version / executable_name);
		candidates.push_back(root / executable_name);
	}
	const auto name = root.filename().stem();
	if (!name.empty()) {
		candidates.push_back(root / "Contents" / "MacOS" / name);
		for (const auto& version : BundleVersionDirectories(root))
			candidates.push_back(version / name);
		candidates.push_back(root / name);
	}
	for (const auto& candidate : candidates) {
		std::error_code error;
		if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
	}
	return {};
}

bool ReadMachOArchitectures(const std::filesystem::path& path,
	std::vector<std::int32_t>& architectures)
{
	using darling::windows_host::FAT_MAGIC_64_BE;
	using darling::windows_host::FAT_MAGIC_BE;
	using darling::windows_host::MH_MAGIC;
	using darling::windows_host::MH_MAGIC_64;
	using darling::windows_host::MachHeader32;
	std::ifstream input(path, std::ios::binary);
	if (!input) return false;
	std::error_code error;
	const auto file_bytes = std::filesystem::file_size(path, error);
	if (error || file_bytes < sizeof(MachHeader32) ||
		file_bytes > static_cast<std::uintmax_t>(
			(std::numeric_limits<std::streamoff>::max)())) return false;
	auto read_at = [&input](std::uint64_t offset, void* output, std::size_t bytes) {
		input.clear();
		input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		return input.good() && input.read(static_cast<char*>(output),
			static_cast<std::streamsize>(bytes)).good();
	};
	auto big_endian_32 = [](const std::uint8_t* bytes) {
		return (static_cast<std::uint32_t>(bytes[0]) << 24) |
			(static_cast<std::uint32_t>(bytes[1]) << 16) |
			(static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
	};
	auto big_endian_64 = [&big_endian_32](const std::uint8_t* bytes) {
		return (static_cast<std::uint64_t>(big_endian_32(bytes)) << 32) |
			big_endian_32(bytes + 4);
	};
	std::uint32_t raw_magic = 0;
	if (!read_at(0, &raw_magic, sizeof(raw_magic))) return false;
	if (raw_magic != FAT_MAGIC_BE && raw_magic != FAT_MAGIC_64_BE) {
		MachHeader32 header{};
		if (!read_at(0, &header, sizeof(header)) ||
			(header.magic != MH_MAGIC && header.magic != MH_MAGIC_64)) return false;
		architectures.push_back(header.cpu_type);
		return true;
	}
	std::array<std::uint8_t, 8> fat_header{};
	if (!read_at(0, fat_header.data(), fat_header.size())) return false;
	const std::uint32_t count = big_endian_32(fat_header.data() + 4);
	if (count == 0 || count > 128) return false;
	const std::size_t record_bytes = raw_magic == FAT_MAGIC_64_BE ? 32 : 20;
	const std::uint64_t table_bytes = static_cast<std::uint64_t>(count) * record_bytes;
	if (table_bytes > file_bytes - fat_header.size()) return false;
	std::vector<std::uint8_t> table(static_cast<std::size_t>(table_bytes));
	if (!read_at(fat_header.size(), table.data(), table.size())) return false;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto* record = table.data() + static_cast<std::size_t>(index) * record_bytes;
		const auto cpu_type = static_cast<std::int32_t>(big_endian_32(record));
		const std::uint64_t offset = raw_magic == FAT_MAGIC_64_BE ?
			big_endian_64(record + 8) : big_endian_32(record + 8);
		const std::uint64_t slice_bytes = raw_magic == FAT_MAGIC_64_BE ?
			big_endian_64(record + 16) : big_endian_32(record + 12);
		if (offset > file_bytes || slice_bytes > file_bytes - offset ||
			slice_bytes < sizeof(MachHeader32)) return false;
		MachHeader32 slice_header{};
		if (!read_at(offset, &slice_header, sizeof(slice_header)) ||
			(slice_header.magic != MH_MAGIC && slice_header.magic != MH_MAGIC_64) ||
			slice_header.cpu_type != cpu_type) return false;
		if (std::find(architectures.begin(), architectures.end(), cpu_type) ==
			architectures.end()) architectures.push_back(cpu_type);
	}
	return !architectures.empty();
}

darling_windows_CFArrayRef CreateArchitectureArray(
	const std::filesystem::path& executable_path)
{
	std::vector<std::int32_t> architectures;
	if (!ReadMachOArchitectures(executable_path, architectures)) return nullptr;
	std::vector<const void*> numbers;
	numbers.reserve(architectures.size());
	for (const auto architecture : architectures) {
		const auto number = darling_windows_CFNumberCreateInteger(architecture);
		if (number == nullptr) {
			for (const auto* value : numbers) darling_windows_CFRelease(value);
			return nullptr;
		}
		numbers.push_back(number);
	}
	const auto result = darling_windows_CFArrayCreate(numbers.data(),
		static_cast<darling_windows_CFIndex>(numbers.size()));
	for (const auto* value : numbers) darling_windows_CFRelease(value);
	return result;
}

void AppendUTF8(std::string& output, std::uint32_t codepoint)
{
	if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
	else if (codepoint <= 0x7ff) {
		output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
		output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
	} else if (codepoint <= 0xffff) {
		output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
		output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
	} else {
		output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
		output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
	}
}

bool DecodeStringsText(const std::vector<unsigned char>& bytes, std::string& text)
{
	if (bytes.size() >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) ||
		(bytes[0] == 0xfe && bytes[1] == 0xff))) {
		const bool little_endian = bytes[0] == 0xff;
		const auto count = (bytes.size() - 2) / 2;
		std::vector<wchar_t> wide;
		wide.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto first = bytes[2 + index * 2];
			const auto second = bytes[3 + index * 2];
			wide.push_back(static_cast<wchar_t>(little_endian
				? (first | (static_cast<unsigned>(second) << 8))
				: (second | (static_cast<unsigned>(first) << 8))));
		}
		if ((bytes.size() - 2) % 2 != 0) return false;
		if (wide.empty()) { text.clear(); return true; }
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0) return false;
		text.resize(static_cast<std::size_t>(required));
		return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide.data(), static_cast<int>(wide.size()), text.data(), required,
			nullptr, nullptr) == required;
	}
	std::size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
		bytes[2] == 0xbf ? 3 : 0;
	if (bytes.size() == offset) { text.clear(); return true; }
	text.assign(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
	return true;
}

struct OpenStepStringsParser final {
	std::string_view text;
	std::size_t position = 0;
	bool SkipTrivia()
	{
		for (;;) {
			while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
				++position;
			if (position + 1 < text.size() && text[position] == '/' && text[position + 1] == '/') {
				position += 2;
				while (position < text.size() && text[position] != '\n') ++position;
				continue;
			}
			if (position + 1 < text.size() && text[position] == '/' && text[position + 1] == '*') {
				const auto end = text.find("*/", position + 2);
				if (end == std::string_view::npos) return false;
				position = end + 2;
				continue;
			}
			return true;
		}
	}
	bool ParseString(std::string& result)
	{
		if (!SkipTrivia() || position >= text.size()) return false;
		result.clear();
		if (text[position] != '"') {
			const auto begin = position;
			while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position])) &&
				text[position] != '=' && text[position] != ';') ++position;
			if (position == begin) return false;
			result.assign(text.substr(begin, position - begin));
			return true;
		}
		++position;
		while (position < text.size()) {
			const auto character = text[position++];
			if (character == '"') return true;
			if (character != '\\') { result.push_back(character); continue; }
			if (position >= text.size()) return false;
			const auto escape = text[position++];
			switch (escape) {
			case 'n': result.push_back('\n'); break;
			case 'r': result.push_back('\r'); break;
			case 't': result.push_back('\t'); break;
			case 'b': result.push_back('\b'); break;
			case 'f': result.push_back('\f'); break;
			case '\n': break;
			case '\r': if (position < text.size() && text[position] == '\n') ++position; break;
			case 'u':
			case 'U': {
				auto read_hex = [&]() -> std::optional<std::uint32_t> {
					if (text.size() - position < 4) return std::nullopt;
					std::uint32_t result = 0;
					for (int digit = 0; digit < 4; ++digit) {
						const auto hex = text[position++];
						result <<= 4;
						if (hex >= '0' && hex <= '9') result |= hex - '0';
						else if (hex >= 'a' && hex <= 'f') result |= hex - 'a' + 10;
						else if (hex >= 'A' && hex <= 'F') result |= hex - 'A' + 10;
						else return std::nullopt;
					}
					return result;
				};
				auto decoded = read_hex();
				if (!decoded) return false;
				std::uint32_t codepoint = *decoded;
				if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
					if (text.size() - position < 2 || text[position++] != '\\') return false;
					const auto low_marker = text[position++];
					if (low_marker != 'u' && low_marker != 'U') return false;
					const auto low = read_hex();
					if (!low || *low < 0xdc00 || *low > 0xdfff) return false;
					codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*low - 0xdc00);
				} else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return false;
				AppendUTF8(result, codepoint);
				break;
			}
			default: result.push_back(escape); break;
			}
		}
		return false;
	}
	darling_windows_CFDictionaryRef Parse()
	{
		auto dictionary = darling_windows_CFDictionaryCreateMutable(0);
		if (dictionary == nullptr) return nullptr;
		for (;;) {
			if (!SkipTrivia()) { darling_windows_CFRelease(dictionary); return nullptr; }
			if (position >= text.size()) break;
			std::string key_text, value_text;
			if (!ParseString(key_text) || !SkipTrivia() || position >= text.size() ||
				text[position++] != '=' || !ParseString(value_text) || !SkipTrivia() ||
				position >= text.size() || text[position++] != ';') {
				darling_windows_CFRelease(dictionary);
				return nullptr;
			}
			const auto key = darling_windows_CFStringCreateWithCString(key_text.c_str());
			const auto value = darling_windows_CFStringCreateWithCString(value_text.c_str());
			const bool inserted = key != nullptr && value != nullptr &&
				darling_windows_CFDictionarySetValue(dictionary, key, value);
			darling_windows_CFRelease(key);
			darling_windows_CFRelease(value);
			if (!inserted) { darling_windows_CFRelease(dictionary); return nullptr; }
		}
		return dictionary;
	}
};

darling_windows_CFDictionaryRef LoadStringsTable(const std::filesystem::path& path)
{
	try {
		std::ifstream input(path, std::ios::binary);
		if (!input) return nullptr;
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
		if (bytes.size() > 16 * 1024 * 1024) return nullptr;
		std::string text;
		if (!DecodeStringsText(bytes, text)) return nullptr;
		if (text.rfind("bplist00", 0) == 0) {
			const auto object = darling_windows_CFPropertyListCreateFromBinary(text.data(),
				static_cast<darling_windows_CFIndex>(text.size()));
			if (object != nullptr && darling_windows_CFGetTypeID(object) ==
				darling_windows_CFDictionaryGetTypeID())
				return static_cast<darling_windows_CFDictionaryRef>(object);
			darling_windows_CFRelease(object);
			return nullptr;
		}
		const auto first = text.find_first_not_of(" \t\r\n");
		if (first != std::string::npos && (text.compare(first, 5, "<?xml") == 0 ||
			text.find("<plist", first) != std::string::npos)) {
			const auto object = darling_windows_CFPropertyListCreateFromXML(text.c_str());
			if (object != nullptr && darling_windows_CFGetTypeID(object) ==
				darling_windows_CFDictionaryGetTypeID())
				return static_cast<darling_windows_CFDictionaryRef>(object);
			darling_windows_CFRelease(object);
			return nullptr;
		}
		return OpenStepStringsParser{text}.Parse();
	} catch (...) { return nullptr; }
}

std::vector<std::string> PreferredBundleLocalizations(const Object* bundle)
{
	std::vector<std::string> result;
	ULONG language_count = 0;
	ULONG buffer_length = 0;
	GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &language_count, nullptr, &buffer_length);
	if (buffer_length != 0 && buffer_length < 65536) {
		std::vector<wchar_t> languages(buffer_length);
		if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &language_count,
			languages.data(), &buffer_length)) {
			for (const wchar_t* language = languages.data(); *language != L'\0';
				language += std::wcslen(language) + 1) {
				const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
					language, -1, nullptr, 0, nullptr, nullptr);
				if (required <= 1) continue;
				std::string full(static_cast<std::size_t>(required), '\0');
				if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, language, -1,
					full.data(), required, nullptr, nullptr) <= 0) continue;
				full.resize(static_cast<std::size_t>(required - 1));
				result.push_back(full);
				std::replace(full.begin(), full.end(), '-', '_');
				result.push_back(full);
				const auto separator = full.find_first_of("_-");
				if (separator != std::string::npos) result.push_back(full.substr(0, separator));
			}
		}
	}
	const auto development_key = darling_windows_CFStringCreateWithCString(
		"CFBundleDevelopmentRegion");
	const auto* bundle_object = AsBundle(bundle);
	const auto dictionary = BundleInfoDictionary(bundle_object);
	const auto development = development_key == nullptr || dictionary == nullptr ? nullptr :
		darling_windows_CFDictionaryGetValue(dictionary, development_key);
	std::array<char, 128> region{};
	if (development != nullptr && darling_windows_CFStringGetCString(development,
		region.data(), static_cast<darling_windows_CFIndex>(region.size())))
		result.emplace_back(region.data());
	darling_windows_CFRelease(development_key);
	result.emplace_back("Base");
	return result;
}

darling_windows_CFTypeRef FindLocalizedString(Object* bundle, const std::string& key,
	const std::string& table)
{
	if (bundle == nullptr || bundle->kind != Kind::Bundle || table.empty() ||
	table.find_first_of("/\\") != std::string::npos || table == "." || table == "..")
		return nullptr;
	const auto root = std::filesystem::u8path(bundle->bundle_path);
	const auto resource_roots = BundleResourceRoots(root);
	for (const auto& localization : PreferredBundleLocalizations(bundle)) {
		if (localization.empty() || localization == "." || localization == ".." ||
			localization.find_first_of("/\\") != std::string::npos) continue;
		for (const auto& resources : resource_roots) {
			const auto dictionary = LoadStringsTable(resources /
				std::filesystem::u8path(localization + ".lproj") /
				std::filesystem::u8path(table + ".strings"));
			if (dictionary == nullptr) continue;
			const auto key_object = darling_windows_CFStringCreateWithCString(key.c_str());
			const auto value = key_object == nullptr ? nullptr :
				darling_windows_CFDictionaryGetValue(dictionary, key_object);
			if (value != nullptr && darling_windows_CFGetTypeID(value) ==
				darling_windows_CFStringGetTypeID()) {
				darling_windows_CFRetain(value);
				darling_windows_CFRelease(key_object);
				darling_windows_CFRelease(dictionary);
				return value;
			}
			darling_windows_CFRelease(key_object);
			darling_windows_CFRelease(dictionary);
		}
	}
	for (const auto& resources : resource_roots) {
		const auto dictionary = LoadStringsTable(resources / std::filesystem::u8path(table + ".strings"));
		if (dictionary == nullptr) continue;
		const auto key_object = darling_windows_CFStringCreateWithCString(key.c_str());
		const auto value = key_object == nullptr ? nullptr :
			darling_windows_CFDictionaryGetValue(dictionary, key_object);
		if (value != nullptr && darling_windows_CFGetTypeID(value) ==
			darling_windows_CFStringGetTypeID()) {
			darling_windows_CFRetain(value);
			darling_windows_CFRelease(key_object);
			darling_windows_CFRelease(dictionary);
			return value;
		}
		darling_windows_CFRelease(key_object);
		darling_windows_CFRelease(dictionary);
	}
	return nullptr;
}

darling_windows_CFBundleRef CreateBundleForPath(const std::filesystem::path& path)
{
	std::error_code error;
	if (!std::filesystem::is_directory(path, error) || error) return nullptr;
	const auto normalized_path = PathUtf8(NormalizeBundlePath(path));
	{
		std::lock_guard lock(bundle_registry_mutex);
		const auto found = bundle_registry.find(normalized_path);
		if (found != bundle_registry.end()) {
			RetainOwned(found->second);
			return found->second;
		}
	}
	auto* object = new (std::nothrow) Object{};
	if (object == nullptr) return nullptr;
	try {
		object->kind = Kind::Bundle;
		object->bundle_path = normalized_path;
		std::lock_guard lock(bundle_registry_mutex);
		const auto [entry, inserted] = bundle_registry.emplace(normalized_path, object);
		if (inserted) {
			// Keep a registry reference so Get-rule lookups remain valid and
			// repeated CFBundleCreate calls return the same object.
			object->references.fetch_add(1, std::memory_order_relaxed);
			return object;
		}
		RetainOwned(entry->second);
		delete object;
		return entry->second;
	} catch (...) { delete object; return nullptr; }
}

darling_windows_CFURLRef CopyBundleSubdirectoryURL(darling_windows_CFBundleRef bundle,
	const std::vector<std::filesystem::path>& relative_paths)
{
	const auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	try {
		const auto root = std::filesystem::u8path(object->bundle_path);
		for (const auto& relative : relative_paths) {
			const auto candidate = root / relative;
			std::error_code error;
			if (std::filesystem::is_directory(candidate, error) && !error)
				return darling_windows_CFURLCreateWithFileSystemPath(
					PathUtf8(candidate).c_str());
		}
	} catch (...) { }
	return nullptr;
}

darling_windows_CFArrayRef CreateBundlesFromDirectory(
	darling_windows_CFURLRef directory_url, darling_windows_CFStringRef bundle_type)
{
	const auto* url = As(directory_url);
	const auto* type_object = bundle_type == nullptr ? nullptr : As(bundle_type);
	if (url == nullptr || url->kind != Kind::URL || url->string.empty() ||
		(type_object != nullptr && type_object->kind != Kind::String)) return nullptr;
	const bool filter_by_type = type_object != nullptr;
	const auto requested_type = type_object == nullptr ? std::string{} : type_object->string;
	if (requested_type.find_first_of("/\\:") != std::string::npos) return nullptr;
	std::vector<darling_windows_CFBundleRef> bundles;
	try {
		bundles.reserve(4096);
		const auto directory = std::filesystem::u8path(url->string);
		std::error_code error;
		if (!std::filesystem::is_directory(directory, error) || error)
			return darling_windows_CFArrayCreate(nullptr, 0);
		std::vector<std::filesystem::path> children;
		std::size_t inspected_entries = 0;
		for (std::filesystem::directory_iterator iterator(directory, error), end;
			!error && iterator != end && children.size() < 4096 &&
			inspected_entries < 65536; iterator.increment(error), ++inspected_entries) {
			std::error_code entry_error;
			if (iterator->is_directory(entry_error) && !entry_error)
				children.push_back(iterator->path());
		}
		std::sort(children.begin(), children.end());
		for (const auto& child : children) {
			if (filter_by_type) {
				auto extension = PathUtf8(child.extension());
				if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
				if (extension != requested_type) continue;
			}
			auto bundle = CreateBundleForPath(child);
			if (bundle == nullptr) continue;
			if (BundleInfoDictionary(AsBundle(bundle)) == nullptr) {
				darling_windows_CFRelease(bundle);
				continue;
			}
			bundles.push_back(bundle);
		}
		const auto result = darling_windows_CFArrayCreate(
			bundles.data(),
			static_cast<darling_windows_CFIndex>(bundles.size()));
		for (const auto bundle : bundles) darling_windows_CFRelease(bundle);
		return result;
	} catch (...) {
		for (const auto bundle : bundles) darling_windows_CFRelease(bundle);
		return nullptr;
	}
}
}

extern "C" void darling_windows_CFBundleSetMainExecutablePath(const char* utf8_path)
{
	std::lock_guard lock(bundle_state_mutex);
	main_executable_path = utf8_path == nullptr ? "" : utf8_path;
	if (main_executable_path.empty()) return;
	try {
		const auto executable = std::filesystem::u8path(main_executable_path);
		auto root = executable.parent_path();
		if (root.filename() == "MacOS" && root.parent_path().filename() == "Contents")
			root = root.parent_path().parent_path();
		std::string normalized_root = PathUtf8(NormalizeBundlePath(root));
		std::lock_guard registry_lock(bundle_registry_mutex);
		Object* previous_main = main_bundle_object;
		Object* selected_main = previous_main;
		std::unique_ptr<Object> new_main;
		const auto existing = bundle_registry.find(normalized_root);
		if (existing != bundle_registry.end()) selected_main = existing->second;
		if (selected_main == nullptr) {
			new_main.reset(new (std::nothrow) Object{});
			selected_main = new_main.get();
			if (selected_main == nullptr) return;
			selected_main->kind = Kind::Bundle;
		}
		const bool path_changed = selected_main->bundle_path != normalized_root;
		bundle_registry.insert_or_assign(normalized_root, selected_main);
		(void)new_main.release();
		if (path_changed) {
			std::lock_guard info_lock(selected_main->bundle_info_mutex);
			ReleaseOwned(As(selected_main->bundle_info_dictionary));
			selected_main->bundle_info_dictionary = nullptr;
			for (const auto* value : selected_main->bundle_localized_info_values)
				ReleaseOwned(As(value));
			selected_main->bundle_localized_info_values.clear();
			selected_main->bundle_path.swap(normalized_root);
			const auto old_entry = bundle_registry.find(normalized_root);
			if (old_entry != bundle_registry.end() && old_entry->second == selected_main)
				bundle_registry.erase(old_entry);
		}
		main_bundle_object = selected_main;
	} catch (...) { /* Keep the last successfully registered main bundle. */ }
}

extern "C" darling_windows_CFBundleRef darling_windows_CFBundleCreate(
	const void*, darling_windows_CFURLRef bundle_url)
{
	const auto* url = As(bundle_url);
	if (url == nullptr || url->kind != Kind::URL || url->string.empty()) return nullptr;
	try { return CreateBundleForPath(std::filesystem::u8path(url->string)); }
	catch (...) { return nullptr; }
}

extern "C" darling_windows_CFBundleRef darling_windows_CFBundleGetMainBundle()
{
	std::lock_guard lock(bundle_state_mutex);
	return main_executable_path.empty() ? nullptr : main_bundle_object;
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyBundleURL(
	darling_windows_CFBundleRef bundle)
{
	const auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	return darling_windows_CFURLCreateWithFileSystemPath(object->bundle_path.c_str());
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyExecutableURL(
	darling_windows_CFBundleRef bundle)
{
	const auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	try {
		const auto path = BundleExecutablePath(object);
		return path.empty() ? nullptr :
			darling_windows_CFURLCreateWithFileSystemPath(PathUtf8(path).c_str());
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCopyExecutableArchitectures(
	darling_windows_CFBundleRef bundle)
{
	const auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	try {
		const auto executable = BundleExecutablePath(object);
		return executable.empty() ? nullptr : CreateArchitectureArray(executable);
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCopyExecutableArchitecturesForURL(
	darling_windows_CFURLRef url)
{
	const auto* object = As(url);
	if (object == nullptr || object->kind != Kind::URL || object->string.empty()) return nullptr;
	try {
		const auto path = std::filesystem::u8path(object->string);
		std::error_code error;
		if (std::filesystem::is_directory(path, error) && !error) {
			const auto bundle = CreateBundleForPath(path);
			if (bundle == nullptr) return nullptr;
			const auto result = darling_windows_CFBundleCopyExecutableArchitectures(bundle);
			darling_windows_CFRelease(bundle);
			return result;
		}
		return CreateArchitectureArray(path);
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyResourcesDirectoryURL(
	darling_windows_CFBundleRef bundle)
{
	const auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	try {
		const auto root = std::filesystem::u8path(object->bundle_path);
		for (const auto& path : BundleResourceRoots(root)) {
			std::error_code error;
			if (std::filesystem::is_directory(path, error) && !error)
				return darling_windows_CFURLCreateWithFileSystemPath(PathUtf8(path).c_str());
		}
	} catch (...) { }
	return nullptr;
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyBuiltInPlugInsURL(
	darling_windows_CFBundleRef bundle)
{
	try {
		return CopyBundleSubdirectoryURL(bundle, {
			std::filesystem::path("Contents") / "PlugIns", "PlugIns"});
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyPrivateFrameworksURL(
	darling_windows_CFBundleRef bundle)
{
	try {
		return CopyBundleSubdirectoryURL(bundle, {
			std::filesystem::path("Contents") / "Frameworks", "Frameworks"});
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopySharedFrameworksURL(
	darling_windows_CFBundleRef bundle)
{
	try {
		return CopyBundleSubdirectoryURL(bundle, {
			std::filesystem::path("Contents") / "SharedFrameworks", "SharedFrameworks"});
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopySharedSupportURL(
	darling_windows_CFBundleRef bundle)
{
	try {
		return CopyBundleSubdirectoryURL(bundle, {
			std::filesystem::path("Contents") / "SharedSupport", "SharedSupport"});
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCreateBundlesFromDirectory(
	const void*, darling_windows_CFURLRef directory_url,
	darling_windows_CFStringRef bundle_type)
{
	return CreateBundlesFromDirectory(directory_url, bundle_type);
}

extern "C" darling_windows_CFBundleRef darling_windows_CFBundleGetBundleWithIdentifier(
	darling_windows_CFStringRef bundle_identifier)
{
	const auto* identifier = As(bundle_identifier);
	if (identifier == nullptr || identifier->kind != Kind::String) return nullptr;
	std::lock_guard registry_lock(bundle_registry_mutex);
	for (const auto& entry : bundle_registry) {
		const auto dictionary = BundleInfoDictionary(entry.second);
		const auto* dictionary_object = As(dictionary);
		if (dictionary_object == nullptr || dictionary_object->kind != Kind::Dictionary) continue;
		for (const auto& property : dictionary_object->dictionary) {
			const auto* key_object = As(property.first);
			const auto* value_object = As(property.second);
			if (key_object != nullptr && key_object->kind == Kind::String &&
				key_object->string == "CFBundleIdentifier" && value_object != nullptr &&
				value_object->kind == Kind::String &&
				value_object->string == identifier->string)
				return entry.second;
		}
	}
	return nullptr;
}

extern "C" darling_windows_CFArrayRef darling_windows_CFBundleGetAllBundles()
{
	try {
		std::lock_guard registry_lock(bundle_registry_mutex);
		std::vector<const void*> bundles;
		bundles.reserve(bundle_registry.size());
		for (const auto& entry : bundle_registry) bundles.push_back(entry.second);
		return darling_windows_CFArrayCreate(bundles.data(),
			static_cast<darling_windows_CFIndex>(bundles.size()));
	} catch (...) { return nullptr; }
}

extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyResourceURL(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef resource_name,
	darling_windows_CFStringRef resource_type, darling_windows_CFStringRef subdirectory)
{
	const auto* object = AsBundle(bundle);
	const auto* name = As(resource_name);
	const auto* type = resource_type == nullptr ? nullptr : As(resource_type);
	const auto* subdir = subdirectory == nullptr ? nullptr : As(subdirectory);
	if (object == nullptr || name == nullptr || name->kind != Kind::String ||
		name->string.empty() || (type != nullptr && type->kind != Kind::String) ||
		(subdir != nullptr && subdir->kind != Kind::String)) return nullptr;
	auto safe_relative = [](const std::string& value) {
		const auto path = std::filesystem::u8path(value);
		if (path.is_absolute()) return false;
		for (const auto& component : path)
			if (component == "..") return false;
		return true;
	};
	try {
		std::filesystem::path relative;
		if (subdir != nullptr && !subdir->string.empty()) {
			if (!safe_relative(subdir->string)) return nullptr;
			relative /= std::filesystem::u8path(subdir->string);
		}
		if (!safe_relative(name->string)) return nullptr;
		auto filename = std::filesystem::u8path(name->string);
		if (type != nullptr && !type->string.empty() && filename.extension().empty()) {
			if (type->string.find_first_of("/\\") != std::string::npos || type->string == "." ||
				type->string == "..") return nullptr;
			filename += std::filesystem::u8path(
				type->string.front() == '.' ? type->string : "." + type->string);
		}
		relative /= filename;
		const auto root = std::filesystem::u8path(object->bundle_path);
		const auto resource_roots = BundleResourceRoots(root);
		for (const auto& localization : PreferredBundleLocalizations(object)) {
			if (localization.empty() || localization == "." || localization == ".." ||
				localization.find_first_of("/\\") != std::string::npos) continue;
			const auto localized_relative =
				std::filesystem::u8path(localization + ".lproj") / relative;
			for (const auto& base : resource_roots) {
				const auto candidate = base / localized_relative;
				std::error_code error;
				if ((std::filesystem::is_regular_file(candidate, error) ||
					std::filesystem::is_directory(candidate, error)) && !error)
					return darling_windows_CFURLCreateWithFileSystemPath(
						PathUtf8(candidate).c_str());
			}
		}
		for (const auto& base : resource_roots) {
			const auto candidate = base / relative;
			std::error_code error;
			if ((std::filesystem::is_regular_file(candidate, error) ||
				std::filesystem::is_directory(candidate, error)) && !error)
				return darling_windows_CFURLCreateWithFileSystemPath(
					PathUtf8(candidate).c_str());
		}
	} catch (...) { }
	return nullptr;
}

extern "C" darling_windows_CFTypeID darling_windows_CFBundleGetTypeID()
{
	return TypeId(Kind::Bundle);
}

extern "C" bool darling_windows_CFBundleIsMainExecutable(
	darling_windows_CFBundleRef bundle)
{
	auto* object = AsBundle(bundle);
	if (object == nullptr) return false;
	std::lock_guard lock(bundle_state_mutex);
	return main_bundle_object != nullptr && !main_executable_path.empty() &&
		object->bundle_path == main_bundle_object->bundle_path;
}

extern "C" void* darling_windows_CFBundleGetExecutableHandle(
	darling_windows_CFBundleRef bundle)
{
	auto* object = AsBundle(bundle);
	if (object == nullptr) return nullptr;
	std::lock_guard lock(object->bundle_info_mutex);
	return object->bundle_executable_handle;
}

extern "C" bool darling_windows_CFBundleSetExecutableHandle(
	darling_windows_CFBundleRef bundle, void* handle,
	darling_windows_CFBundleReleaseExecutableCallback release_handle)
{
	auto* object = AsBundle(bundle);
	if (object == nullptr || handle == nullptr || release_handle == nullptr) return false;
	std::lock_guard lock(object->bundle_info_mutex);
	if (object->bundle_executable_handle != nullptr) return false;
	object->bundle_executable_handle = handle;
	object->bundle_release_executable = release_handle;
	return true;
}

extern "C" void darling_windows_CFBundleReleaseExecutable(
	darling_windows_CFBundleRef bundle)
{
	auto* object = AsBundle(bundle);
	if (object == nullptr || darling_windows_CFBundleIsMainExecutable(bundle)) return;
	void* handle = nullptr;
	darling_windows_CFBundleReleaseExecutableCallback release_handle = nullptr;
	{
		std::lock_guard lock(object->bundle_info_mutex);
		handle = object->bundle_executable_handle;
		release_handle = object->bundle_release_executable;
		object->bundle_executable_handle = nullptr;
		object->bundle_release_executable = nullptr;
	}
	if (handle != nullptr && release_handle != nullptr) {
		try { release_handle(handle); }
		catch (...) { /* Loader cleanup callbacks must not escape CFBundle. */ }
	}
}

extern "C" darling_windows_CFDictionaryRef darling_windows_CFBundleGetInfoDictionary(
	darling_windows_CFBundleRef bundle)
{
	return BundleInfoDictionary(AsBundle(bundle));
}

extern "C" darling_windows_CFTypeRef darling_windows_CFBundleGetValueForInfoDictionaryKey(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key)
{
	auto* bundle_object = AsBundle(bundle);
	const auto* key_object = As(key);
	if (bundle_object == nullptr || key_object == nullptr || key_object->kind != Kind::String)
		return nullptr;
	try {
		const auto localized = FindLocalizedString(bundle_object, key_object->string, "InfoPlist");
		if (localized != nullptr) {
			std::lock_guard lock(bundle_object->bundle_info_mutex);
			bundle_object->bundle_localized_info_values.push_back(localized);
			return localized;
		}
	} catch (...) { }
	const auto dictionary = BundleInfoDictionary(bundle_object);
	return dictionary == nullptr ? nullptr : darling_windows_CFDictionaryGetValue(dictionary, key);
}

extern "C" darling_windows_CFStringRef darling_windows_CFBundleGetIdentifier(
	darling_windows_CFBundleRef bundle)
{
	const auto dictionary = BundleInfoDictionary(AsBundle(bundle));
	const auto key = darling_windows_CFStringCreateWithCString("CFBundleIdentifier");
	if (key == nullptr) return nullptr;
	const auto value = dictionary == nullptr ? nullptr :
		darling_windows_CFDictionaryGetValue(dictionary, key);
	darling_windows_CFRelease(key);
	return value != nullptr && darling_windows_CFGetTypeID(value) ==
		darling_windows_CFStringGetTypeID() ? static_cast<darling_windows_CFStringRef>(value) : nullptr;
}

extern "C" darling_windows_CFStringRef darling_windows_CFBundleCopyLocalizedString(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key,
	darling_windows_CFStringRef value, darling_windows_CFStringRef table_name)
{
	auto* bundle_object = AsBundle(bundle);
	const auto* key_object = As(key);
	const auto* value_object = value == nullptr ? nullptr : As(value);
	const auto* table_object = table_name == nullptr ? nullptr : As(table_name);
	if (bundle_object == nullptr || key_object == nullptr || key_object->kind != Kind::String ||
		(value_object != nullptr && value_object->kind != Kind::String) ||
		(table_object != nullptr && table_object->kind != Kind::String)) return nullptr;
	const auto table = table_object == nullptr || table_object->string.empty()
		? std::string("Localizable") : table_object->string;
	try {
		const auto localized = FindLocalizedString(bundle_object, key_object->string, table);
		if (localized != nullptr) {
			const auto copy = darling_windows_CFStringCreateCopy(localized);
			darling_windows_CFRelease(localized);
			return copy;
		}
		return darling_windows_CFStringCreateCopy(value_object == nullptr ? key : value);
	} catch (...) { return nullptr; }
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef CFBundleCreate(
	const void* allocator, darling_windows_CFURLRef bundle_url)
{
	return darling_windows_CFBundleCreate(allocator, bundle_url);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef CFBundleGetMainBundle()
{
	return darling_windows_CFBundleGetMainBundle();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyBundleURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyBundleURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyExecutableURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyExecutableURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCopyExecutableArchitectures(darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyExecutableArchitectures(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCopyExecutableArchitecturesForURL(darling_windows_CFURLRef url)
{
	return darling_windows_CFBundleCopyExecutableArchitecturesForURL(url);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFBundleCopyResourcesDirectoryURL(darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyResourcesDirectoryURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyBuiltInPlugInsURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyBuiltInPlugInsURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyPrivateFrameworksURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopyPrivateFrameworksURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopySharedFrameworksURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopySharedFrameworksURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopySharedSupportURL(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleCopySharedSupportURL(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCreateBundlesFromDirectory(const void* allocator,
	darling_windows_CFURLRef directory_url, darling_windows_CFStringRef bundle_type)
{
	return darling_windows_CFBundleCreateBundlesFromDirectory(
		allocator, directory_url, bundle_type);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef
CFBundleGetBundleWithIdentifier(darling_windows_CFStringRef bundle_identifier)
{
	return darling_windows_CFBundleGetBundleWithIdentifier(bundle_identifier);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFBundleGetAllBundles()
{
	return darling_windows_CFBundleGetAllBundles();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyResourceURL(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef resource_name,
	darling_windows_CFStringRef resource_type, darling_windows_CFStringRef subdirectory)
{
	return darling_windows_CFBundleCopyResourceURL(bundle, resource_name,
		resource_type, subdirectory);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFBundleGetTypeID()
{
	return darling_windows_CFBundleGetTypeID();
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFBundleGetInfoDictionary(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleGetInfoDictionary(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef
CFBundleGetValueForInfoDictionaryKey(darling_windows_CFBundleRef bundle,
	darling_windows_CFStringRef key)
{
	return darling_windows_CFBundleGetValueForInfoDictionaryKey(bundle, key);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFBundleGetIdentifier(
	darling_windows_CFBundleRef bundle)
{
	return darling_windows_CFBundleGetIdentifier(bundle);
}

extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFBundleCopyLocalizedString(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key,
	darling_windows_CFStringRef value, darling_windows_CFStringRef table_name)
{
	return darling_windows_CFBundleCopyLocalizedString(bundle, key, value, table_name);
}
