/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_objc.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct objc_selector final {
	std::string name;
};

struct objc_ivar final {
	std::string name;
	std::string types;
	std::ptrdiff_t offset{};
};

struct objc_property final {
	std::string name;
	std::string attributes;
};

struct objc_method final {
	SEL selector{};
	IMP implementation{};
	std::string types;
	Class owner_class{};
	bool class_method{};
};

struct objc_class final {
	std::string name;
	Class superclass{};
	Class metaclass{};
	Class owner_class{};
	bool is_metaclass{};
	struct Method final {
		IMP implementation{};
		std::string types;
	};
	std::unordered_map<SEL, Method> methods;
	std::unordered_map<SEL, Method> class_methods;
	std::unordered_map<std::string, std::unique_ptr<objc_ivar>> ivars;
	std::unordered_map<std::string, std::unique_ptr<objc_property>> properties;
	std::unordered_set<Protocol> protocols;
	bool registered{};
};

struct objc_protocol final {
	std::string name;
	std::unordered_set<Protocol> parents;
	std::unordered_set<SEL> required_instance_methods;
	std::unordered_set<SEL> optional_instance_methods;
	std::unordered_set<SEL> required_class_methods;
	std::unordered_set<SEL> optional_class_methods;
	std::unordered_map<SEL, std::string> required_instance_types;
	std::unordered_map<SEL, std::string> optional_instance_types;
	std::unordered_map<SEL, std::string> required_class_types;
	std::unordered_map<SEL, std::string> optional_class_types;
	bool registered{};
};

struct darling_objc_image_token final {
	std::vector<Class> classes;
	std::vector<Protocol> protocols;
};

struct objc_object final {
	Class isa{};
	std::atomic<std::uint32_t> retain_count{1};
};

struct DarlingObjcCallbackBlock final {
	std::atomic<std::uint32_t> retain_count{1};
	DarlingObjcCallback1 callback{};
	void* context{};
};

struct DarlingObjcAppleBlockDescriptor final {
	std::uintptr_t reserved{};
	std::uintptr_t size{};
	void (*copy_helper)(void*, const void*){};
	void (*dispose_helper)(const void*){};
	const char* signature{};
};

struct DarlingObjcAppleBlock final {
	void* isa{};
	std::int32_t flags{};
	std::int32_t reserved{};
	id (*invoke)(void*, id){};
	DarlingObjcAppleBlockDescriptor* descriptor{};
	std::atomic<std::uint32_t> retain_count{1};
	DarlingObjcCallback1 callback{};
	void* context{};
};

id AppleBlockInvoke(void* raw_block, id argument)
{
	auto* block = static_cast<DarlingObjcAppleBlock*>(raw_block);
	return block && block->callback ? block->callback(block->context, argument) :
		nullptr;
}

namespace {

thread_local darling_objc_image_token* active_image_token = nullptr;

// The low-level libclosure entry points receive the compiler's block literal,
// not the bridge-only DarlingObjcAppleBlock wrapper.  Keep ownership state for
// copies made from stack blocks in a side table so the ABI header remains
// layout-compatible on both pointer widths.
constexpr std::int32_t AppleBlockHasCopyDispose = 1 << 25;
constexpr std::int32_t AppleBlockNeedsFree = 1 << 24;
constexpr std::int32_t AppleBlockIsGlobal = 1 << 28;
constexpr std::int32_t AppleBlockHasSignature = 1 << 30;

struct AppleBlockHeader final {
	void* isa{};
	std::int32_t flags{};
	std::int32_t reserved{};
	void (*invoke)(void*, ...){};
	const void* descriptor{};
};

struct AppleBlockRuntimeState final {
	std::uint32_t retain_count = 1;
	std::size_t size = 0;
	void (*dispose_helper)(const void*){};
};

std::mutex& AppleBlockRuntimeMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::unordered_set<void*>& ManagedAppleBlocks()
{
	static std::unordered_set<void*> blocks;
	return blocks;
}

std::unordered_map<void*, AppleBlockRuntimeState>& ExternalAppleBlocks()
{
	static std::unordered_map<void*, AppleBlockRuntimeState> blocks;
	return blocks;
}

struct AppleByrefHeader final {
	void* isa{};
	AppleByrefHeader* forwarding{};
	std::int32_t flags{};
	std::int32_t size{};
	void (*keep_helper)(void*, void*){};
	void (*destroy_helper)(void*){};
};

struct AppleByrefRuntimeState final {
	std::uint32_t retain_count = 1;
	AppleByrefHeader* stack_source{};
	void (*destroy_helper)(void*){};
};

std::unordered_map<void*, AppleByrefRuntimeState>& ExternalAppleByrefs()
{
	static std::unordered_map<void*, AppleByrefRuntimeState> byrefs;
	return byrefs;
}

void* CopyExternalAppleByref(void* raw_byref)
{
	if (raw_byref == nullptr)
		return nullptr;
	constexpr std::int32_t byref_needs_free = 1 << 24;
	constexpr std::int32_t byref_has_copy_dispose = 1 << 25;
	auto* source = static_cast<AppleByrefHeader*>(raw_byref);
	AppleByrefHeader* forwarding = source->forwarding != nullptr ?
		source->forwarding : source;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		const auto found = ExternalAppleByrefs().find(forwarding);
		if (found != ExternalAppleByrefs().end()) {
			++found->second.retain_count;
			return forwarding;
		}
	}
	if ((forwarding->flags & byref_needs_free) != 0)
		return forwarding;
	if (forwarding->size < static_cast<std::int32_t>(sizeof(AppleByrefHeader)) ||
		forwarding->size > 64 * 1024 * 1024)
		return raw_byref;
	auto* copy = static_cast<AppleByrefHeader*>(HeapAlloc(
		GetProcessHeap(), 0, static_cast<std::size_t>(forwarding->size)));
	if (copy == nullptr)
		return nullptr;
	std::memcpy(copy, forwarding, static_cast<std::size_t>(forwarding->size));
	copy->flags |= byref_needs_free;
	copy->forwarding = copy;
	if ((forwarding->flags & byref_has_copy_dispose) != 0 &&
		forwarding->keep_helper != nullptr)
		forwarding->keep_helper(copy, forwarding);
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		ExternalAppleByrefs().emplace(copy, AppleByrefRuntimeState{
			1, forwarding, forwarding->destroy_helper});
	}
	source->forwarding = copy;
	return copy;
}

void ReleaseExternalAppleByref(void* raw_byref)
{
	if (raw_byref == nullptr)
		return;
	auto* source = static_cast<AppleByrefHeader*>(raw_byref);
	auto* forwarding = source->forwarding != nullptr ? source->forwarding : source;
	AppleByrefRuntimeState state{};
	bool release = false;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		const auto found = ExternalAppleByrefs().find(forwarding);
		if (found == ExternalAppleByrefs().end())
			return;
		if (--found->second.retain_count == 0) {
			state = found->second;
			ExternalAppleByrefs().erase(found);
			release = true;
		}
	}
	if (!release)
		return;
	if (state.destroy_helper != nullptr)
		state.destroy_helper(forwarding);
	if (state.stack_source != nullptr)
		state.stack_source->forwarding = state.stack_source;
	HeapFree(GetProcessHeap(), 0, forwarding);
}

bool IsManagedAppleBlock(void* raw_block)
{
	std::lock_guard lock(AppleBlockRuntimeMutex());
	return ManagedAppleBlocks().contains(raw_block);
}

bool ReadExternalDescriptor(const AppleBlockHeader& block,
	std::size_t& size, void (**copy_helper)(void*, const void*),
	void (**dispose_helper)(const void*))
{
	if (block.descriptor == nullptr)
		return false;
	const auto* cursor = static_cast<const std::uint8_t*>(block.descriptor);
	std::memcpy(&size, cursor + sizeof(std::uintptr_t), sizeof(size));
	cursor += sizeof(std::uintptr_t) * 2;
	*copy_helper = nullptr;
	*dispose_helper = nullptr;
	if ((block.flags & AppleBlockHasCopyDispose) != 0) {
		std::memcpy(copy_helper, cursor, sizeof(*copy_helper));
		cursor += sizeof(*copy_helper);
		std::memcpy(dispose_helper, cursor, sizeof(*dispose_helper));
	}
	return size >= sizeof(AppleBlockHeader) && size <= 64u * 1024u * 1024u;
}

void* CopyExternalAppleBlock(void* raw_block)
{
	if (raw_block == nullptr)
		return nullptr;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		const auto found = ExternalAppleBlocks().find(raw_block);
		if (found != ExternalAppleBlocks().end()) {
			++found->second.retain_count;
			return raw_block;
		}
	}
	const auto& block = *static_cast<const AppleBlockHeader*>(raw_block);
	if ((block.flags & AppleBlockIsGlobal) != 0)
		return raw_block;
	// An already heap-owned block from an unknown runtime is not safe to free
	// without owning its allocator metadata.  Retain-only is fail-safe here;
	// blocks copied by this bridge are tracked below.
	if ((block.flags & AppleBlockNeedsFree) != 0)
		return raw_block;
	std::size_t size = 0;
	void (*copy_helper)(void*, const void*) = nullptr;
	void (*dispose_helper)(const void*) = nullptr;
	if (!ReadExternalDescriptor(block, size, &copy_helper, &dispose_helper))
		return raw_block;
	void* copy = HeapAlloc(GetProcessHeap(), 0, size);
	if (copy == nullptr)
		return nullptr;
	std::memcpy(copy, raw_block, size);
	auto* copy_header = static_cast<AppleBlockHeader*>(copy);
	copy_header->flags |= AppleBlockNeedsFree;
	if (copy_helper != nullptr)
		copy_helper(copy, raw_block);
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		ExternalAppleBlocks().emplace(copy,
			AppleBlockRuntimeState{1, size, dispose_helper});
	}
	return copy;
}

void ReleaseExternalAppleBlock(void* raw_block)
{
	if (raw_block == nullptr)
		return;
	AppleBlockRuntimeState state{};
	bool release = false;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		const auto found = ExternalAppleBlocks().find(raw_block);
		if (found == ExternalAppleBlocks().end())
			return;
		if (--found->second.retain_count == 0) {
			state = found->second;
			ExternalAppleBlocks().erase(found);
			release = true;
		} 
	}
	if (!release)
		return;
	if (state.dispose_helper != nullptr)
		state.dispose_helper(raw_block);
	HeapFree(GetProcessHeap(), 0, raw_block);
}

// Objective-C metadata keeps pointer-sized fields, but the class_ro_t and
// category_t offsets differ between the 32-bit and 64-bit ABIs.
constexpr std::size_t ClassDataNameOffset = sizeof(std::uintptr_t) == 8 ? 24 : 20;
constexpr std::size_t ClassDataMethodsOffset = sizeof(std::uintptr_t) == 8 ? 32 : 24;
constexpr std::size_t ClassDataProtocolsOffset = sizeof(std::uintptr_t) == 8 ? 40 : 28;
constexpr std::size_t ClassDataIvarsOffset = sizeof(std::uintptr_t) == 8 ? 48 : 32;
constexpr std::size_t ClassDataPropertiesOffset = sizeof(std::uintptr_t) == 8 ? 56 : 36;
constexpr std::size_t ProtocolNameOffset = sizeof(std::uintptr_t) == 8 ? 8 : 4;
constexpr std::size_t ProtocolInstanceMethodsOffset = sizeof(std::uintptr_t) == 8 ? 24 : 12;
constexpr std::size_t CategoryClassOffset = sizeof(std::uintptr_t) == 8 ? 8 : 4;
constexpr std::size_t CategoryInstanceMethodsOffset = sizeof(std::uintptr_t) == 8 ? 16 : 8;
constexpr std::size_t CategoryClassMethodsOffset = sizeof(std::uintptr_t) == 8 ? 24 : 12;
constexpr std::size_t CategoryProtocolsOffset = sizeof(std::uintptr_t) == 8 ? 32 : 16;
constexpr std::size_t CategoryInstancePropertiesOffset = sizeof(std::uintptr_t) == 8 ? 40 : 20;
constexpr std::size_t CategoryClassPropertiesOffset = sizeof(std::uintptr_t) == 8 ? 48 : 24;
constexpr std::uintptr_t ClassDataPointerMask = sizeof(std::uintptr_t) == 8 ?
	~static_cast<std::uintptr_t>(0x7) : ~static_cast<std::uintptr_t>(0x3);

std::mutex& RuntimeMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::unordered_map<std::string, SEL>& Selectors()
{
	static std::unordered_map<std::string, SEL> selectors;
	return selectors;
}

std::unordered_map<std::string, Class>& Classes()
{
	static std::unordered_map<std::string, Class> classes;
	return classes;
}

std::vector<std::unique_ptr<objc_selector>>& SelectorStorage()
{
	static std::vector<std::unique_ptr<objc_selector>> storage;
	return storage;
}

std::vector<std::unique_ptr<objc_class>>& ClassStorage()
{
	static std::vector<std::unique_ptr<objc_class>> storage;
	return storage;
}

Class EnsureMetaClass(Class cls)
{
	if (!cls || cls->is_metaclass) return cls;
	if (cls->metaclass) return cls->metaclass;
	ClassStorage().push_back(std::make_unique<objc_class>());
	Class meta = ClassStorage().back().get();
	meta->name = cls->name;
	meta->is_metaclass = true;
	meta->owner_class = cls;
	meta->superclass = cls->superclass ? EnsureMetaClass(cls->superclass) : nullptr;
	meta->methods = cls->class_methods;
	cls->metaclass = meta;
	return meta;
}

std::unordered_map<std::string, Protocol>& Protocols()
{
	static std::unordered_map<std::string, Protocol> protocols;
	return protocols;
}

std::vector<std::unique_ptr<objc_protocol>>& ProtocolStorage()
{
	static std::vector<std::unique_ptr<objc_protocol>> storage;
	return storage;
}

std::vector<std::unique_ptr<objc_method>>& MethodViewStorage()
{
	static std::vector<std::unique_ptr<objc_method>> storage;
	return storage;
}

Method MakeMethodView(Class owner, SEL selector, const objc_class::Method& method,
	bool class_method)
{
	MethodViewStorage().push_back(std::make_unique<objc_method>());
	MethodViewStorage().back()->selector = selector;
	MethodViewStorage().back()->implementation = method.implementation;
	MethodViewStorage().back()->types = method.types;
	MethodViewStorage().back()->owner_class = owner;
	MethodViewStorage().back()->class_method = class_method;
	return MethodViewStorage().back().get();
}

struct AutoreleasePoolFrame final {
	std::vector<id> objects;
};
thread_local std::vector<std::unique_ptr<AutoreleasePoolFrame>> AutoreleasePools;
std::unordered_map<id, std::unordered_set<id*>> WeakReferences;
struct AssociatedObjectEntry final {
	id value = nullptr;
	objc_AssociationPolicy policy = OBJC_ASSOCIATION_ASSIGN;
};
std::unordered_map<id, std::unordered_map<const void*, AssociatedObjectEntry>>
	AssociatedObjects;

bool AssociationRetains(objc_AssociationPolicy policy) noexcept
{
	return policy == OBJC_ASSOCIATION_RETAIN_NONATOMIC ||
		policy == OBJC_ASSOCIATION_RETAIN || policy == OBJC_ASSOCIATION_COPY_NONATOMIC ||
		policy == OBJC_ASSOCIATION_COPY;
}

id Dispatch(id receiver, Class start, SEL selector)
{
	if (!receiver || !selector)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = start; current; current = current->superclass) {
		const auto method = current->methods.find(selector);
		if (method != current->methods.end()) {
			using NoArgumentMethod = id (*)(id, SEL);
			return reinterpret_cast<NoArgumentMethod>(method->second.implementation)(receiver,
				selector);
		}
	}
	return nullptr;
}

IMP FindMethod(id receiver, SEL selector)
{
	if (!receiver || !selector)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = receiver->isa; current; current = current->superclass) {
		const auto method = current->methods.find(selector);
		if (method != current->methods.end())
			return method->second.implementation;
	}
	return nullptr;
}

void RegisterMethodList(Class cls, std::uintptr_t method_list_address,
	bool class_method = false)
{
	if (!cls || !method_list_address)
		return;
	std::uint32_t entry_size = 0;
	std::uint32_t method_count = 0;
	std::memcpy(&entry_size, reinterpret_cast<const void*>(method_list_address),
		sizeof(entry_size));
	std::memcpy(&method_count,
		reinterpret_cast<const std::uint8_t*>(method_list_address) + 4,
		sizeof(method_count));
	if (entry_size < sizeof(std::uintptr_t) * 3 || method_count > 65536)
		return;
	for (std::uint32_t method_index = 0; method_index < method_count;
		++method_index) {
		const auto method_address = method_list_address + 8ull +
			static_cast<std::uint64_t>(method_index) * entry_size;
		std::uintptr_t name_address = 0;
		std::uintptr_t type_address = 0;
		std::uintptr_t implementation = 0;
		std::memcpy(&name_address, reinterpret_cast<const void*>(method_address),
			sizeof(name_address));
		std::memcpy(&type_address,
			reinterpret_cast<const std::uint8_t*>(method_address) +
				sizeof(std::uintptr_t), sizeof(type_address));
		std::memcpy(&implementation,
			reinterpret_cast<const std::uint8_t*>(method_address) +
				sizeof(std::uintptr_t) * 2, sizeof(implementation));
		const auto* name = reinterpret_cast<const char*>(name_address);
		const auto* types = reinterpret_cast<const char*>(type_address);
		if (!name || !types || !implementation || strnlen_s(name, 1024) == 1024 ||
			strnlen_s(types, 1024) == 1024)
			continue;
		if (class_method)
			class_addClassMethod(cls, sel_registerName(name),
				reinterpret_cast<IMP>(implementation), types);
		else
			class_addMethod(cls, sel_registerName(name),
				reinterpret_cast<IMP>(implementation), types);
	}
}

void RegisterProtocolList(Class cls, std::uintptr_t protocol_list_address)
{
	if (!cls || !protocol_list_address)
		return;
	std::uintptr_t protocol_count = 0;
	std::memcpy(&protocol_count,
		reinterpret_cast<const std::uint8_t*>(protocol_list_address) +
			sizeof(std::uintptr_t), sizeof(protocol_count));
	if (protocol_count > 65536)
		return;
	for (std::uintptr_t index = 0; index < protocol_count; ++index) {
		std::uintptr_t protocol_address = 0;
		std::memcpy(&protocol_address,
			reinterpret_cast<const std::uint8_t*>(protocol_list_address) +
				sizeof(std::uintptr_t) * (index + 2), sizeof(protocol_address));
		if (!protocol_address)
			continue;
		const char* name = nullptr;
	std::memcpy(&name, reinterpret_cast<const std::uint8_t*>(protocol_address) +
		ProtocolNameOffset,
			sizeof(name));
		if (!name || name[0] == '\0' || strnlen_s(name, 1024) == 1024)
			continue;
		Protocol protocol = objc_getProtocol(name);
		if (!protocol) {
			protocol = objc_allocateProtocol(name);
			if (protocol)
				objc_registerProtocol(protocol);
		}
		if (protocol)
			class_addProtocol(cls, protocol);
		if (protocol) {
			std::uintptr_t protocol_methods[4]{};
			std::memcpy(protocol_methods,
				reinterpret_cast<const std::uint8_t*>(protocol_address) +
					ProtocolInstanceMethodsOffset,
				sizeof(protocol_methods));
			for (unsigned method_index = 0; method_index < 4; ++method_index) {
				const bool instance = method_index == 0 || method_index == 2;
				const bool required = method_index < 2;
				const auto list = protocol_methods[method_index];
				if (!list)
					continue;
				std::uint32_t entry_size = 0;
				std::uint32_t method_count = 0;
				std::memcpy(&entry_size, reinterpret_cast<const void*>(list), 4);
				std::memcpy(&method_count,
					reinterpret_cast<const std::uint8_t*>(list) + 4, 4);
				if (entry_size < sizeof(std::uintptr_t) * 2 || method_count > 65536)
					continue;
				for (std::uint32_t method = 0; method < method_count; ++method) {
					const auto address = list + 8ull +
						static_cast<std::uint64_t>(method) * entry_size;
					std::uintptr_t selector_address = 0;
					std::uintptr_t types_address = 0;
					std::memcpy(&selector_address, reinterpret_cast<const void*>(address),
						sizeof(selector_address));
					std::memcpy(&types_address,
						reinterpret_cast<const std::uint8_t*>(address) + sizeof(selector_address),
						sizeof(types_address));
					const auto* selector_name = reinterpret_cast<const char*>(selector_address);
					const auto* types = reinterpret_cast<const char*>(types_address);
					if (selector_name && types && strnlen_s(selector_name, 1024) < 1024 &&
						strnlen_s(types, 1024) < 1024)
						protocol_addMethodDescription(protocol,
							sel_registerName(selector_name), types, required, instance);
				}
			}
		}
	}
}

void RegisterIvarList(Class cls, std::uintptr_t ivar_list_address)
{
	if (!cls || !ivar_list_address)
		return;
	std::uint32_t entry_size = 0;
	std::uint32_t ivar_count = 0;
	std::memcpy(&entry_size, reinterpret_cast<const void*>(ivar_list_address), 4);
	std::memcpy(&ivar_count,
		reinterpret_cast<const std::uint8_t*>(ivar_list_address) + 4, 4);
	if (entry_size < sizeof(std::uintptr_t) * 3 + 8 || ivar_count > 65536)
		return;
	for (std::uint32_t index = 0; index < ivar_count; ++index) {
		const auto address = ivar_list_address + 8ull +
			static_cast<std::uint64_t>(index) * entry_size;
		std::uintptr_t offset_address = 0;
		std::uintptr_t name_address = 0;
		std::uintptr_t type_address = 0;
		std::memcpy(&offset_address, reinterpret_cast<const void*>(address),
			sizeof(offset_address));
		std::memcpy(&name_address, reinterpret_cast<const std::uint8_t*>(address) +
			sizeof(std::uintptr_t), sizeof(name_address));
		std::memcpy(&type_address, reinterpret_cast<const std::uint8_t*>(address) +
			sizeof(std::uintptr_t) * 2,
			sizeof(type_address));
		const auto* name = reinterpret_cast<const char*>(name_address);
		const auto* types = reinterpret_cast<const char*>(type_address);
		if (!name || !types || strnlen_s(name, 1024) == 1024 ||
			strnlen_s(types, 1024) == 1024)
			continue;
		std::ptrdiff_t offset = 0;
		if (offset_address)
			std::memcpy(&offset, reinterpret_cast<const void*>(offset_address),
				sizeof(offset));
		auto ivar = std::make_unique<objc_ivar>();
		ivar->name = name;
		ivar->types = types;
		ivar->offset = offset;
		cls->ivars[ivar->name] = std::move(ivar);
	}
}

void RegisterPropertyList(Class cls, std::uintptr_t property_list_address)
{
	if (!cls || !property_list_address)
		return;
	std::uint32_t entry_size = 0;
	std::uint32_t property_count = 0;
	std::memcpy(&entry_size, reinterpret_cast<const void*>(property_list_address), 4);
	std::memcpy(&property_count,
		reinterpret_cast<const std::uint8_t*>(property_list_address) + 4, 4);
	if (entry_size < sizeof(std::uintptr_t) * 2 || property_count > 65536)
		return;
	for (std::uint32_t index = 0; index < property_count; ++index) {
		const auto address = property_list_address + 8ull +
			static_cast<std::uint64_t>(index) * entry_size;
		std::uintptr_t name_address = 0;
		std::uintptr_t attributes_address = 0;
		std::memcpy(&name_address, reinterpret_cast<const void*>(address),
			sizeof(name_address));
		std::memcpy(&attributes_address,
			reinterpret_cast<const std::uint8_t*>(address) + sizeof(std::uintptr_t),
			sizeof(attributes_address));
		const auto* name = reinterpret_cast<const char*>(name_address);
		const auto* attributes = reinterpret_cast<const char*>(attributes_address);
		if (!name || !attributes || strnlen_s(name, 1024) == 1024 ||
			strnlen_s(attributes, 4096) == 4096)
			continue;
		auto property = std::make_unique<objc_property>();
		property->name = name;
		property->attributes = attributes;
		cls->properties[property->name] = std::move(property);
	}
}

}

extern "C" SEL sel_registerName(const char* name)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto existing = Selectors().find(name);
	if (existing != Selectors().end())
		return existing->second;
	SelectorStorage().push_back(std::make_unique<objc_selector>());
	SelectorStorage().back()->name = name;
	SEL selector = SelectorStorage().back().get();
	Selectors().emplace(SelectorStorage().back()->name, selector);
	return selector;
}

extern "C" const char* sel_getName(SEL selector)
{
	return selector ? selector->name.c_str() : nullptr;
}

extern "C" Class objc_getClass(const char* name)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto existing = Classes().find(name);
	return existing == Classes().end() ? nullptr : existing->second;
}

extern "C" Class objc_getMetaClass(const char* name)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto existing = Classes().find(name);
	return existing == Classes().end() ? nullptr : EnsureMetaClass(existing->second);
}

extern "C" int objc_getClassList(Class* buffer, int buffer_count)
{
	std::lock_guard lock(RuntimeMutex());
	const int total = static_cast<int>(Classes().size());
	if (!buffer || buffer_count <= 0)
		return total;
	int copied = 0;
	for (const auto& entry : Classes()) {
		if (copied == buffer_count)
			break;
		buffer[copied++] = entry.second;
	}
	return copied;
}

extern "C" std::size_t darling_objc_register_macho_classlist(
	const void* classlist, std::size_t bytes)
{
	if (!classlist || bytes < sizeof(std::uintptr_t))
		return 0;
	const auto count = bytes / sizeof(std::uintptr_t);
	std::size_t registered = 0;
	for (std::size_t index = 0; index < count; ++index) {
		std::uintptr_t class_address = 0;
		std::memcpy(&class_address,
			static_cast<const std::uint8_t*>(classlist) +
				index * sizeof(class_address), sizeof(class_address));
		if (!class_address)
			continue;
		std::uintptr_t data_address = 0;
		std::memcpy(&data_address,
			reinterpret_cast<const std::uint8_t*>(class_address) +
				sizeof(std::uintptr_t) * 4, sizeof(data_address));
		data_address &= ClassDataPointerMask;
		if (!data_address)
			continue;
		const char* name = nullptr;
		std::memcpy(&name, reinterpret_cast<const std::uint8_t*>(data_address) +
			ClassDataNameOffset,
			sizeof(name));
		if (!name || name[0] == '\0')
			continue;
		const std::size_t name_length = strnlen_s(name, 1024);
		if (name_length == 0 || name_length == 1024)
			continue;
		const std::string class_name(name, name_length);
		Class cls = objc_getClass(class_name.c_str());
		const bool is_new_class = cls == nullptr;
		if (is_new_class)
			cls = objc_allocateClassPair(nullptr, class_name.c_str(), 0);
		if (cls) {
			if (is_new_class) {
				objc_registerClassPair(cls);
				if (active_image_token)
					active_image_token->classes.push_back(cls);
				++registered;
			}
			std::uintptr_t method_list_address = 0;
			std::memcpy(&method_list_address,
					reinterpret_cast<const std::uint8_t*>(data_address) +
					ClassDataMethodsOffset,
				sizeof(method_list_address));
			RegisterMethodList(cls, method_list_address);
		std::uintptr_t protocol_list_address = 0;
		std::memcpy(&protocol_list_address,
					reinterpret_cast<const std::uint8_t*>(data_address) +
					ClassDataProtocolsOffset,
			sizeof(protocol_list_address));
		RegisterProtocolList(cls, protocol_list_address);
			std::uintptr_t ivar_list_address = 0;
			std::memcpy(&ivar_list_address,
					reinterpret_cast<const std::uint8_t*>(data_address) +
					ClassDataIvarsOffset,
				sizeof(ivar_list_address));
			RegisterIvarList(cls, ivar_list_address);
			std::uintptr_t property_list_address = 0;
			std::memcpy(&property_list_address,
					reinterpret_cast<const std::uint8_t*>(data_address) +
					ClassDataPropertiesOffset,
				sizeof(property_list_address));
			RegisterPropertyList(cls, property_list_address);
			std::uintptr_t meta_class_address = 0;
			std::memcpy(&meta_class_address,
				reinterpret_cast<const std::uint8_t*>(class_address),
				sizeof(meta_class_address));
			if (meta_class_address) {
				std::uintptr_t meta_data_address = 0;
				std::memcpy(&meta_data_address,
					reinterpret_cast<const std::uint8_t*>(meta_class_address) +
						sizeof(std::uintptr_t) * 4, sizeof(meta_data_address));
				meta_data_address &= ClassDataPointerMask;
				if (meta_data_address) {
					std::uintptr_t meta_methods = 0;
					std::memcpy(&meta_methods,
							reinterpret_cast<const std::uint8_t*>(meta_data_address) +
								ClassDataMethodsOffset,
						sizeof(meta_methods));
					RegisterMethodList(cls, meta_methods, true);
				}
			}
		}
	}
	return registered;
}

extern "C" std::size_t darling_objc_register_macho_selrefs(
	const void* selrefs, std::size_t bytes)
{
	if (!selrefs || bytes < sizeof(std::uintptr_t))
		return 0;
	const auto count = bytes / sizeof(std::uintptr_t);
	std::size_t registered = 0;
	for (std::size_t index = 0; index < count; ++index) {
		std::uintptr_t name_address = 0;
		std::memcpy(&name_address,
			static_cast<const std::uint8_t*>(selrefs) +
				index * sizeof(name_address), sizeof(name_address));
		const auto* name = reinterpret_cast<const char*>(name_address);
		if (!name || name[0] == '\0' || strnlen_s(name, 1024) == 1024)
			continue;
		if (sel_registerName(name))
			++registered;
	}
	return registered;
}

extern "C" std::size_t darling_objc_register_macho_categories(
	const void* categories, std::size_t bytes)
{
	if (!categories || bytes < sizeof(std::uintptr_t))
		return 0;
	const auto count = bytes / sizeof(std::uintptr_t);
	std::size_t registered = 0;
	for (std::size_t index = 0; index < count; ++index) {
		std::uintptr_t category_address = 0;
		std::memcpy(&category_address,
			static_cast<const std::uint8_t*>(categories) +
				index * sizeof(category_address), sizeof(category_address));
		if (!category_address)
			continue;
		std::uintptr_t class_address = 0;
		std::uintptr_t instance_methods = 0;
		std::uintptr_t class_methods = 0;
		std::uintptr_t protocols = 0;
		std::uintptr_t instance_properties = 0;
		std::uintptr_t class_properties = 0;
		std::memcpy(&class_address,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryClassOffset,
			sizeof(class_address));
		std::memcpy(&instance_methods,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryInstanceMethodsOffset,
			sizeof(instance_methods));
		std::memcpy(&class_methods,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryClassMethodsOffset,
			sizeof(class_methods));
		std::memcpy(&protocols,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryProtocolsOffset,
			sizeof(protocols));
		std::memcpy(&instance_properties,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryInstancePropertiesOffset,
			sizeof(instance_properties));
		std::memcpy(&class_properties,
				reinterpret_cast<const std::uint8_t*>(category_address) +
				CategoryClassPropertiesOffset,
			sizeof(class_properties));
		if (!class_address)
			continue;
		std::uintptr_t data_address = 0;
		std::memcpy(&data_address,
			reinterpret_cast<const std::uint8_t*>(class_address) +
				sizeof(std::uintptr_t) * 4, sizeof(data_address));
		data_address &= ClassDataPointerMask;
		if (!data_address)
			continue;
		const char* name = nullptr;
		std::memcpy(&name, reinterpret_cast<const std::uint8_t*>(data_address) +
			ClassDataNameOffset,
			sizeof(name));
		if (!name || strnlen_s(name, 1024) == 1024)
			continue;
		Class cls = objc_getClass(name);
		if (!cls)
			continue;
		RegisterMethodList(cls, instance_methods);
		RegisterMethodList(cls, class_methods, true);
		RegisterProtocolList(cls, protocols);
		RegisterPropertyList(cls, instance_properties);
		RegisterPropertyList(cls, class_properties);
		++registered;
	}
	return registered;
}

extern "C" Protocol objc_allocateProtocol(const char* name)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	if (Protocols().find(name) != Protocols().end())
		return nullptr;
	ProtocolStorage().push_back(std::make_unique<objc_protocol>());
	ProtocolStorage().back()->name = name;
	return ProtocolStorage().back().get();
}

extern "C" Protocol objc_getProtocol(const char* name)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto protocol = Protocols().find(name);
	return protocol == Protocols().end() ? nullptr : protocol->second;
}

extern "C" const char* protocol_getName(Protocol protocol)
{
	return protocol ? protocol->name.c_str() : nullptr;
}

extern "C" unsigned int objc_getProtocolList(Protocol* buffer,
	unsigned int buffer_count)
{
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(Protocols().size());
	if (!buffer || buffer_count == 0)
		return count;
	unsigned int index = 0;
	for (const auto& entry : Protocols()) {
		if (index >= buffer_count)
			break;
		buffer[index++] = entry.second;
	}
	return count;
}

extern "C" unsigned int class_getProtocolList(Class cls, Protocol* buffer,
	unsigned int buffer_count)
{
	if (!cls)
		return 0;
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(cls->protocols.size());
	if (!buffer || buffer_count == 0)
		return count;
	unsigned int index = 0;
	for (Protocol protocol : cls->protocols) {
		if (index >= buffer_count)
			break;
		buffer[index++] = protocol;
	}
	return count;
}

extern "C" void protocol_addMethodDescription(Protocol protocol, SEL selector,
	const char* types, bool required, bool instance)
{
	if (!protocol || !selector)
		return;
	std::lock_guard lock(RuntimeMutex());
	if (required && instance) {
		protocol->required_instance_methods.insert(selector);
		protocol->required_instance_types[selector] = types ? types : "";
	} else if (!required && instance) {
		protocol->optional_instance_methods.insert(selector);
		protocol->optional_instance_types[selector] = types ? types : "";
	} else if (required) {
		protocol->required_class_methods.insert(selector);
		protocol->required_class_types[selector] = types ? types : "";
	} else {
		protocol->optional_class_methods.insert(selector);
		protocol->optional_class_types[selector] = types ? types : "";
	}
}

extern "C" bool protocol_hasMethodDescription(Protocol protocol, SEL selector,
	bool required, bool instance)
{
	if (!protocol || !selector)
		return false;
	std::lock_guard lock(RuntimeMutex());
	std::vector<Protocol> pending{protocol};
	std::unordered_set<Protocol> visited;
	while (!pending.empty()) {
		const Protocol current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second)
			continue;
		const auto* methods = required ?
			(instance ? &current->required_instance_methods :
				&current->required_class_methods) :
			(instance ? &current->optional_instance_methods :
				&current->optional_class_methods);
		if (methods->find(selector) != methods->end())
			return true;
		for (Protocol parent : current->parents)
			pending.push_back(parent);
	}
	return false;
}

extern "C" objc_method_description protocol_getMethodDescription(
	Protocol protocol, SEL selector, bool required, bool instance)
{
	if (!protocol || !selector)
		return {nullptr, nullptr};
	std::lock_guard lock(RuntimeMutex());
	std::vector<Protocol> pending{protocol};
	std::unordered_set<Protocol> visited;
	while (!pending.empty()) {
		const Protocol current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second)
			continue;
		const auto* methods = required ?
			(instance ? &current->required_instance_types :
				&current->required_class_types) :
			(instance ? &current->optional_instance_types :
				&current->optional_class_types);
		const auto method = methods->find(selector);
		if (method != methods->end())
			return {selector, method->second.c_str()};
		for (Protocol parent : current->parents)
			pending.push_back(parent);
	}
	return {nullptr, nullptr};
}

extern "C" void objc_registerProtocol(Protocol protocol)
{
	if (!protocol)
		return;
	std::lock_guard lock(RuntimeMutex());
	Protocols()[protocol->name] = protocol;
	protocol->registered = true;
	if (active_image_token)
		active_image_token->protocols.push_back(protocol);
}

extern "C" darling_objc_image_token* darling_objc_begin_image_registration()
{
	if (active_image_token != nullptr)
		return nullptr;
	auto* token = new darling_objc_image_token;
	active_image_token = token;
	return token;
}

extern "C" void darling_objc_end_image_registration(darling_objc_image_token* token)
{
	if (active_image_token == token)
		active_image_token = nullptr;
}

extern "C" void darling_objc_unregister_image(darling_objc_image_token* token)
{
	if (token == nullptr)
		return;
	std::lock_guard lock(RuntimeMutex());
	for (Class cls : token->classes) {
		if (cls == nullptr) continue;
		bool referenced = false;
		for (const auto& entry : Classes()) {
			if (entry.second != cls && entry.second->superclass == cls) {
				referenced = true;
				break;
			}
		}
		if (!referenced) {
			auto found = Classes().find(cls->name);
			if (found != Classes().end() && found->second == cls)
				Classes().erase(found);
		}
	}
	for (Protocol protocol : token->protocols) {
		if (protocol == nullptr) continue;
		auto found = Protocols().find(protocol->name);
		if (found != Protocols().end() && found->second == protocol)
			Protocols().erase(found);
	}
	delete token;
}

extern "C" bool class_addProtocol(Class cls, Protocol protocol)
{
	if (!cls || !protocol)
		return false;
	std::lock_guard lock(RuntimeMutex());
	return cls->protocols.insert(protocol).second;
}

extern "C" bool class_conformsToProtocol(Class cls, Protocol protocol)
{
	if (!cls || !protocol)
		return false;
	std::lock_guard lock(RuntimeMutex());
	std::vector<Protocol> pending;
	std::unordered_set<Protocol> visited;
	for (Class current = cls; current; current = current->superclass)
		for (Protocol attached : current->protocols)
			pending.push_back(attached);
	while (!pending.empty()) {
		const Protocol current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second)
			continue;
		if (current == protocol)
			return true;
		for (Protocol parent : current->parents)
			pending.push_back(parent);
	}
	return false;
}

extern "C" bool protocol_addProtocol(Protocol protocol, Protocol parent)
{
	if (!protocol || !parent || protocol == parent)
		return false;
	std::lock_guard lock(RuntimeMutex());
	return protocol->parents.insert(parent).second;
}

extern "C" bool protocol_conformsToProtocol(Protocol protocol, Protocol parent)
{
	if (!protocol || !parent)
		return false;
	std::lock_guard lock(RuntimeMutex());
	std::vector<Protocol> pending{protocol};
	std::unordered_set<Protocol> visited;
	while (!pending.empty()) {
		const Protocol current = pending.back();
		pending.pop_back();
		if (!visited.insert(current).second)
			continue;
		if (current == parent)
			return true;
		for (Protocol candidate : current->parents)
			pending.push_back(candidate);
	}
	return false;
}

extern "C" Class objc_allocateClassPair(Class superclass, const char* name,
	std::size_t)
{
	if (!name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	if (Classes().find(name) != Classes().end())
		return nullptr;
	ClassStorage().push_back(std::make_unique<objc_class>());
	ClassStorage().back()->name = name;
	ClassStorage().back()->superclass = superclass;
	return ClassStorage().back().get();
}

extern "C" void objc_registerClassPair(Class cls)
{
	if (!cls)
		return;
	std::lock_guard lock(RuntimeMutex());
	Classes()[cls->name] = cls;
	cls->registered = true;
}

extern "C" const char* class_getName(Class cls)
{
	return cls ? cls->name.c_str() : nullptr;
}

extern "C" Class class_getSuperclass(Class cls)
{
	return cls ? cls->superclass : nullptr;
}

namespace {
std::size_t IvarStorageSize(const std::string& encoding) noexcept
{
	if (encoding.empty()) return sizeof(void*);
	switch (encoding.front()) {
	case 'c': case 'C': case 'B': return 1;
	case 's': case 'S': return 2;
	case 'i': case 'I': case 'f': return 4;
	case 'q': case 'Q': case 'l': case 'L': case 'd': return 8;
	case '@': case '#': case ':': case '^': case '*': return sizeof(void*);
	default: return sizeof(void*);
	}
}
}

extern "C" std::size_t class_getInstanceSize(Class cls)
{
	if (!cls) return 0;
	std::lock_guard lock(RuntimeMutex());
	std::size_t size = sizeof(objc_object);
	for (Class current = cls; current; current = current->superclass) {
		for (const auto& [name, ivar] : current->ivars) {
			(void)name;
			if (ivar->offset >= 0)
				size = (std::max)(size, static_cast<std::size_t>(ivar->offset) +
					IvarStorageSize(ivar->types));
		}
	}
	return size;
}

extern "C" bool class_addIvar(Class cls, const char* name, std::size_t size,
	std::uint8_t alignment, const char* types)
{
	if (!cls || !name || name[0] == '\0' || !types || size == 0 ||
		alignment > 63 ||
		cls->registered)
		return false;
	const std::size_t requested_alignment = alignment == 0 ? 1 : (std::size_t{1} << alignment);
	const std::size_t current = class_getInstanceSize(cls);
	const std::size_t offset = (current + requested_alignment - 1) &
		~(requested_alignment - 1);
	auto ivar = std::make_unique<objc_ivar>();
	ivar->name = name;
	ivar->types = types;
	ivar->offset = static_cast<std::ptrdiff_t>(offset);
	std::lock_guard lock(RuntimeMutex());
	if (cls->ivars.find(name) != cls->ivars.end()) return false;
	cls->ivars.emplace(name, std::move(ivar));
	return true;
}

extern "C" Method class_getInstanceMethod(Class cls, SEL selector)
{
	if (!cls || !selector)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		auto method = current->methods.find(selector);
		if (method != current->methods.end())
			return MakeMethodView(current, selector, method->second, false);
	}
	return nullptr;
}

extern "C" Method class_getClassMethod(Class cls, SEL selector)
{
	if (!cls || !selector)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		const auto& method_map = current->is_metaclass ? current->methods : current->class_methods;
		auto method = method_map.find(selector);
		if (method != method_map.end())
			return MakeMethodView(current->is_metaclass ? current->owner_class : current,
				selector, method->second, current->is_metaclass);
	}
	return nullptr;
}

extern "C" bool class_respondsToSelector(Class cls, SEL selector)
{
	if (!cls || !selector)
		return false;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		if (current->methods.find(selector) != current->methods.end())
			return true;
		// Class objects dispatch through their metaclass. The adapter mirrors
		// class methods into that table; explicit metaclass handles retain the
		// ordinary instance-method lookup above.
		if (!cls->is_metaclass && current->metaclass &&
			current->metaclass->methods.find(selector) != current->metaclass->methods.end())
			return true;
	}
	return false;
}

extern "C" Method* class_copyMethodList(Class cls, unsigned int* out_count)
{
	if (out_count)
		*out_count = 0;
	if (!cls)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(cls->methods.size());
	if (out_count)
		*out_count = count;
	if (count == 0)
		return nullptr;
	auto* result = static_cast<Method*>(std::malloc(sizeof(Method) * count));
	if (!result)
		return nullptr;
	unsigned int index = 0;
	for (const auto& entry : cls->methods)
		result[index++] = MakeMethodView(cls, entry.first, entry.second, false);
	return result;
}

extern "C" SEL method_getName(Method method)
{
	return method ? method->selector : nullptr;
}

extern "C" IMP method_getImplementation(Method method)
{
	return method ? method->implementation : nullptr;
}

extern "C" IMP method_setImplementation(Method method, IMP implementation)
{
	if (!method || !implementation || !method->owner_class)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	auto& methods = method->class_method ? method->owner_class->class_methods :
		method->owner_class->methods;
	auto found = methods.find(method->selector);
	if (found == methods.end()) return nullptr;
	const IMP previous = found->second.implementation;
	found->second.implementation = implementation;
	method->implementation = implementation;
	if (method->class_method && method->owner_class->metaclass) {
		auto meta = method->owner_class->metaclass->methods.find(method->selector);
		if (meta != method->owner_class->metaclass->methods.end())
			meta->second.implementation = implementation;
	}
	return previous;
}

extern "C" void method_exchangeImplementations(Method first, Method second)
{
	if (!first || !second || !first->owner_class || !second->owner_class)
		return;
	std::lock_guard lock(RuntimeMutex());
	auto implementation_for = [](Method method) -> IMP& {
		auto& methods = method->class_method ? method->owner_class->class_methods :
			method->owner_class->methods;
		return methods.at(method->selector).implementation;
	};
	std::swap(implementation_for(first), implementation_for(second));
	first->implementation = implementation_for(first);
	second->implementation = implementation_for(second);
	if (first->class_method && first->owner_class->metaclass) {
		auto found = first->owner_class->metaclass->methods.find(first->selector);
		if (found != first->owner_class->metaclass->methods.end())
			found->second.implementation = first->implementation;
	}
	if (second->class_method && second->owner_class->metaclass) {
		auto found = second->owner_class->metaclass->methods.find(second->selector);
		if (found != second->owner_class->metaclass->methods.end())
			found->second.implementation = second->implementation;
	}
}

extern "C" const char* method_getTypeEncoding(Method method)
{
	return method ? method->types.c_str() : nullptr;
}

extern "C" Ivar class_getInstanceVariable(Class cls, const char* name)
{
	if (!cls || !name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		const auto ivar = current->ivars.find(name);
		if (ivar != current->ivars.end())
			return ivar->second.get();
	}
	return nullptr;
}

extern "C" const char* ivar_getName(Ivar ivar)
{
	return ivar ? ivar->name.c_str() : nullptr;
}

extern "C" const char* ivar_getTypeEncoding(Ivar ivar)
{
	return ivar ? ivar->types.c_str() : nullptr;
}

extern "C" std::ptrdiff_t ivar_getOffset(Ivar ivar)
{
	return ivar ? ivar->offset : 0;
}

extern "C" Ivar* class_copyIvarList(Class cls, unsigned int* out_count)
{
	if (out_count)
		*out_count = 0;
	if (!cls)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(cls->ivars.size());
	if (out_count)
		*out_count = count;
	if (count == 0)
		return nullptr;
	auto* result = static_cast<Ivar*>(std::malloc(sizeof(Ivar) * count));
	if (!result)
		return nullptr;
	unsigned int index = 0;
	for (const auto& entry : cls->ivars)
		result[index++] = entry.second.get();
	return result;
}

extern "C" objc_property_t class_getProperty(Class cls, const char* name)
{
	if (!cls || !name)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		auto property = current->properties.find(name);
		if (property != current->properties.end())
			return property->second.get();
	}
	return nullptr;
}

extern "C" const char* property_getName(objc_property_t property)
{
	return property ? property->name.c_str() : nullptr;
}

extern "C" const char* property_getAttributes(objc_property_t property)
{
	return property ? property->attributes.c_str() : nullptr;
}

extern "C" bool class_addProperty(Class cls, const char* name,
	const objc_property_attribute_t* attributes, unsigned int attribute_count)
{
	if (!cls || !name || name[0] == '\0' ||
		(attribute_count != 0 && attributes == nullptr))
		return false;
	auto property = std::make_unique<objc_property>();
	property->name = name;
	for (unsigned int index = 0; index < attribute_count; ++index) {
		if (!attributes[index].name || !attributes[index].value)
			return false;
		if (!property->attributes.empty()) property->attributes += ',';
		property->attributes += attributes[index].name;
		property->attributes += attributes[index].value;
	}
	std::lock_guard lock(RuntimeMutex());
	if (cls->properties.find(name) != cls->properties.end()) return false;
	cls->properties.emplace(property->name, std::move(property));
	return true;
}

extern "C" objc_property_t* class_copyPropertyList(Class cls,
	unsigned int* out_count)
{
	if (out_count)
		*out_count = 0;
	if (!cls)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(cls->properties.size());
	if (out_count)
		*out_count = count;
	if (count == 0)
		return nullptr;
	auto* result = static_cast<objc_property_t*>(std::malloc(
		sizeof(objc_property_t) * count));
	if (!result)
		return nullptr;
	unsigned int index = 0;
	for (const auto& entry : cls->properties)
		result[index++] = entry.second.get();
	return result;
}

extern "C" Protocol* class_copyProtocolList(Class cls,
	unsigned int* out_count)
{
	if (out_count)
		*out_count = 0;
	if (!cls)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto count = static_cast<unsigned int>(cls->protocols.size());
	if (out_count)
		*out_count = count;
	if (count == 0)
		return nullptr;
	auto* result = static_cast<Protocol*>(std::malloc(sizeof(Protocol) * count));
	if (!result)
		return nullptr;
	unsigned int index = 0;
	for (Protocol protocol : cls->protocols)
		result[index++] = protocol;
	return result;
}

extern "C" bool class_addMethod(Class cls, SEL selector, IMP implementation,
	const char* types)
{
	if (!cls || !selector || !implementation)
		return false;
	std::lock_guard lock(RuntimeMutex());
	return cls->methods.emplace(selector,
		objc_class::Method{implementation, types ? types : ""}).second;
}

extern "C" IMP class_replaceMethod(Class cls, SEL selector, IMP implementation,
	const char* types)
{
	if (!cls || !selector || !implementation)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	auto [entry, inserted] = cls->methods.emplace(selector,
		objc_class::Method{implementation, types ? types : ""});
	if (inserted)
		return nullptr;
	const IMP previous = entry->second.implementation;
	entry->second = objc_class::Method{implementation, types ? types : ""};
	return previous;
}

extern "C" bool class_addClassMethod(Class cls, SEL selector, IMP implementation,
	const char* types)
{
	if (!cls || !selector || !implementation)
		return false;
	std::lock_guard lock(RuntimeMutex());
	const auto result = cls->class_methods.emplace(selector,
		objc_class::Method{implementation, types ? types : ""});
	if (result.second && cls->metaclass)
		cls->metaclass->methods.emplace(selector, result.first->second);
	return result.second;
}

extern "C" IMP class_getMethodImplementation(Class cls, SEL selector)
{
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		const auto method = current->methods.find(selector);
		if (method != current->methods.end())
			return method->second.implementation;
	}
	return nullptr;
}

extern "C" const char* class_getMethodTypeEncoding(Class cls, SEL selector)
{
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		const auto method = current->methods.find(selector);
		if (method != current->methods.end())
			return method->second.types.c_str();
	}
	return nullptr;
}

extern "C" id class_createInstance(Class cls, std::size_t extra_bytes)
{
	if (!cls)
		return nullptr;
	const std::size_t size = class_getInstanceSize(cls) + extra_bytes;
	auto* object = reinterpret_cast<id>(new unsigned char[size]);
	object->isa = cls;
	new (&object->retain_count) std::atomic<std::uint32_t>(1);
	return object;
}

extern "C" Class object_getClass(id object)
{
	return object ? object->isa : nullptr;
}

extern "C" id object_getIvar(id object, Ivar ivar)
{
	if (!object || !ivar || ivar->offset < 0) return nullptr;
	id value = nullptr;
	std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(object) + ivar->offset,
		sizeof(value));
	return value;
}

extern "C" void object_setIvar(id object, Ivar ivar, id value)
{
	if (!object || !ivar || ivar->offset < 0) return;
	std::memcpy(reinterpret_cast<std::uint8_t*>(object) + ivar->offset, &value,
		sizeof(value));
}

extern "C" const char* object_getClassName(id object)
{
	return object_getClass(object) ? class_getName(object_getClass(object)) : nullptr;
}

extern "C" bool object_isClass(id object)
{
	if (!object)
		return false;
	std::lock_guard lock(RuntimeMutex());
	for (const auto& entry : ClassStorage())
		if (reinterpret_cast<id>(entry.get()) == object)
			return true;
	return false;
}

extern "C" id objc_retain(id object)
{
	if (object)
		object->retain_count.fetch_add(1, std::memory_order_relaxed);
	return object;
}

extern "C" void objc_release(id object)
{
	if (!object)
		return;
	std::vector<id> associated;
	{
		std::lock_guard lock(RuntimeMutex());
		if (object->retain_count.fetch_sub(1, std::memory_order_acq_rel) != 1)
			return;
		const auto weak = WeakReferences.find(object);
		if (weak != WeakReferences.end()) {
			for (id* location : weak->second)
				if (location)
					*location = nullptr;
			WeakReferences.erase(weak);
		}
		const auto associations = AssociatedObjects.find(object);
		if (associations != AssociatedObjects.end()) {
			for (const auto& entry : associations->second)
				if (entry.second.value && AssociationRetains(entry.second.policy))
					associated.push_back(entry.second.value);
			AssociatedObjects.erase(associations);
		}
	}
	for (id value : associated)
		objc_release(value);
	delete[] reinterpret_cast<unsigned char*>(object);
}

extern "C" void objc_storeStrong(id* location, id object)
{
	if (!location) return;
	if (object) objc_retain(object);
	id previous = nullptr;
	{
		std::lock_guard lock(RuntimeMutex());
		previous = *location;
		*location = object;
	}
	if (previous) objc_release(previous);
}

extern "C" id objc_autorelease(id object)
{
	if (object && !AutoreleasePools.empty())
		AutoreleasePools.back()->objects.push_back(object);
	return object;
}

extern "C" id objc_retainAutoreleasedReturnValue(id object)
{
	return objc_retain(object);
}

extern "C" id objc_getAssociatedObject(id object, const void* key)
{
	if (!object || !key)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const auto object_entry = AssociatedObjects.find(object);
	if (object_entry == AssociatedObjects.end())
		return nullptr;
	const auto value = object_entry->second.find(key);
	return value == object_entry->second.end() ? nullptr : value->second.value;
}

extern "C" void objc_setAssociatedObject(id object, const void* key, id value,
	objc_AssociationPolicy policy)
{
	if (!object || !key)
		return;
	const bool retain = AssociationRetains(policy);
	if (retain && value)
		objc_retain(value);
	AssociatedObjectEntry previous{};
	bool had_previous = false;
	{
		std::lock_guard lock(RuntimeMutex());
		if (!value && !retain) {
			const auto object_entry = AssociatedObjects.find(object);
			if (object_entry != AssociatedObjects.end()) {
				const auto value_entry = object_entry->second.find(key);
				if (value_entry != object_entry->second.end()) {
					previous = value_entry->second;
					had_previous = true;
					object_entry->second.erase(value_entry);
				}
				if (object_entry->second.empty())
					AssociatedObjects.erase(object_entry);
			}
		} else {
			const auto object_entry = AssociatedObjects.find(object);
			if (object_entry != AssociatedObjects.end()) {
				const auto value_entry = object_entry->second.find(key);
				if (value_entry != object_entry->second.end()) {
					previous = value_entry->second;
					had_previous = true;
				}
			}
			AssociatedObjects[object][key] = {value, policy};
		}
	}
	if (had_previous && previous.value && AssociationRetains(previous.policy))
		objc_release(previous.value);
}

extern "C" void objc_removeAssociatedObjects(id object)
{
	if (!object)
		return;
	std::vector<id> values;
	{
		std::lock_guard lock(RuntimeMutex());
		const auto found = AssociatedObjects.find(object);
		if (found == AssociatedObjects.end())
			return;
		for (const auto& entry : found->second)
			if (entry.second.value && AssociationRetains(entry.second.policy))
				values.push_back(entry.second.value);
		AssociatedObjects.erase(found);
	}
	for (id value : values)
		objc_release(value);
}

extern "C" void* objc_autoreleasePoolPush()
{
	AutoreleasePools.push_back(std::make_unique<AutoreleasePoolFrame>());
	return AutoreleasePools.back().get();
}

extern "C" void objc_autoreleasePoolPop(void* token)
{
	if (AutoreleasePools.empty() || token != AutoreleasePools.back().get())
		return;
	const auto objects = std::move(AutoreleasePools.back()->objects);
	AutoreleasePools.pop_back();
	for (id object : objects)
		objc_release(object);
}

extern "C" id objc_initWeak(id* location, id object)
{
	if (location)
		*location = nullptr;
	return objc_storeWeak(location, object);
}

extern "C" id objc_storeWeak(id* location, id object)
{
	if (!location)
		return object;
	std::lock_guard lock(RuntimeMutex());
	if (*location) {
		const auto previous = WeakReferences.find(*location);
		if (previous != WeakReferences.end()) {
			previous->second.erase(location);
			if (previous->second.empty())
				WeakReferences.erase(previous);
		}
	}
	*location = object;
	if (object)
		WeakReferences[object].insert(location);
	return object;
}

extern "C" id objc_loadWeak(id* location)
{
	if (!location)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	return *location;
}

extern "C" id objc_loadWeakRetained(id* location)
{
	if (!location) return nullptr;
	std::lock_guard lock(RuntimeMutex());
	const id value = *location;
	if (value)
		value->retain_count.fetch_add(1, std::memory_order_relaxed);
	return value;
}

extern "C" void objc_destroyWeak(id* location)
{
	if (!location)
		return;
	std::lock_guard lock(RuntimeMutex());
	if (*location) {
		const auto previous = WeakReferences.find(*location);
		if (previous != WeakReferences.end()) {
			previous->second.erase(location);
			if (previous->second.empty())
				WeakReferences.erase(previous);
		}
	}
	*location = nullptr;
}

extern "C" id objc_copyWeak(id* destination, id* source)
{
	if (!destination || !source)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	if (destination == source)
		return *source;
	if (*destination) {
		const auto previous = WeakReferences.find(*destination);
		if (previous != WeakReferences.end()) {
			previous->second.erase(destination);
			if (previous->second.empty())
				WeakReferences.erase(previous);
		}
	}
	*destination = *source;
	if (*destination)
		WeakReferences[*destination].insert(destination);
	return *destination;
}

extern "C" id objc_moveWeak(id* destination, id* source)
{
	if (!source)
		return objc_storeWeak(destination, nullptr);
	if (destination == source)
		return *source;
	std::lock_guard lock(RuntimeMutex());
	if (!destination) return nullptr;
	if (*destination) {
		const auto previous = WeakReferences.find(*destination);
		if (previous != WeakReferences.end()) {
			previous->second.erase(destination);
			if (previous->second.empty())
				WeakReferences.erase(previous);
		}
	}
	*destination = *source;
	if (*source) {
		auto& references = WeakReferences[*source];
		references.erase(source);
		references.insert(destination);
	}
	*source = nullptr;
	return *destination;
}

extern "C" id objc_msgSend(id receiver, SEL selector, ...)
{
	if (!receiver || !selector)
		return nullptr;
	const char* types = class_getMethodTypeEncoding(receiver->isa, selector);
	if (!types)
		return nullptr;
	if (std::strcmp(types, "@@:") == 0)
		return Dispatch(receiver, receiver->isa, selector);
	if (std::strcmp(types, "v@:") == 0) {
		darling_objc_msgSend_void0(receiver, selector);
		return nullptr;
	}
	if (std::strcmp(types, "i@:") == 0 || std::strcmp(types, "q@:") == 0 ||
		std::strcmp(types, "B@:") == 0 || std::strcmp(types, "^v@:") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		if (!implementation)
			return nullptr;
		if (std::strcmp(types, "i@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<int (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "q@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::int64_t (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "B@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<bool (*)(id, SEL)>(implementation)(receiver, selector)));
		return reinterpret_cast<id>(reinterpret_cast<void* (*)(id, SEL)>(implementation)(
			receiver, selector));
	}
	va_list arguments;
	va_start(arguments, selector);
	id result = nullptr;
	if (std::strcmp(types, "@@:@") == 0) {
		result = darling_objc_msgSend_object1(receiver, selector,
			va_arg(arguments, id));
	} else if (std::strcmp(types, "@@:@@") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using ObjectMethod = id (*)(id, SEL, id, id);
		const auto first = va_arg(arguments, id);
		const auto second = va_arg(arguments, id);
		result = implementation ? reinterpret_cast<ObjectMethod>(implementation)(
			receiver, selector, first, second) : nullptr;
	} else if (std::strcmp(types, "q@:q") == 0) {
		const auto value = darling_objc_msgSend_int64_1(receiver, selector,
			va_arg(arguments, std::int64_t));
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "q@:qq") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t,
			std::int64_t);
		const auto first = va_arg(arguments, std::int64_t);
		const auto second = va_arg(arguments, std::int64_t);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				first, second) : 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "i@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = int (*)(id, SEL, int);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				va_arg(arguments, int)) : 0;
		result = reinterpret_cast<id>(static_cast<std::intptr_t>(value));
	} else if (std::strcmp(types, "q@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, int);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				va_arg(arguments, int)) : 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "i@:q") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = int (*)(id, SEL, std::int64_t);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				va_arg(arguments, std::int64_t)) : 0;
		result = reinterpret_cast<id>(static_cast<std::intptr_t>(value));
	} else if (std::strcmp(types, "v@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using VoidMethod = void (*)(id, SEL, int);
		if (implementation)
			reinterpret_cast<VoidMethod>(implementation)(receiver, selector,
				va_arg(arguments, int));
	} else if (std::strcmp(types, "v@:q") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using VoidMethod = void (*)(id, SEL, std::int64_t);
		if (implementation)
			reinterpret_cast<VoidMethod>(implementation)(receiver, selector,
				va_arg(arguments, std::int64_t));
	} else if (std::strcmp(types, "B@:B") == 0) {
		const bool value = va_arg(arguments, int) != 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
			darling_objc_invoke1(receiver, selector,
				DarlingObjcValue{DARLING_OBJC_BOOL, {.boolean = value}}).boolean));
	} else if (std::strcmp(types, "^v@:^v") == 0) {
		const auto value = darling_objc_invoke1(receiver, selector,
			DarlingObjcValue{DARLING_OBJC_POINTER,
				{.pointer = va_arg(arguments, void*)}});
		result = reinterpret_cast<id>(value.pointer);
	} else if (std::strcmp(types, "@@:@?") == 0) {
		result = darling_objc_invoke1(receiver, selector,
			DarlingObjcValue{DARLING_OBJC_BLOCK,
				{.block = va_arg(arguments, DarlingObjcCallbackBlock*)}}).object;
	}
	va_end(arguments);
	return result;
}

extern "C" id objc_msgSendSuper(id receiver, Class superclass, SEL selector, ...)
{
	if (!receiver || !superclass || !selector)
		return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types)
		return nullptr;
	if (std::strcmp(types, "@@:") == 0)
		return Dispatch(receiver, superclass, selector);
	if (std::strcmp(types, "v@:") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation)
			reinterpret_cast<void (*)(id, SEL)>(implementation)(receiver, selector);
		return nullptr;
	}
	if (std::strcmp(types, "i@:") == 0 || std::strcmp(types, "q@:") == 0 ||
		std::strcmp(types, "B@:") == 0 || std::strcmp(types, "^v@:") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (!implementation)
			return nullptr;
		if (std::strcmp(types, "i@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<int (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "q@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::int64_t (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "B@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<bool (*)(id, SEL)>(implementation)(receiver, selector)));
		return reinterpret_cast<id>(reinterpret_cast<void* (*)(id, SEL)>(implementation)(
			receiver, selector));
	}
	va_list arguments;
	va_start(arguments, selector);
	id result = nullptr;
	if (std::strcmp(types, "@@:@@") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using ObjectMethod = id (*)(id, SEL, id, id);
		const auto first = va_arg(arguments, id);
		const auto second = va_arg(arguments, id);
		if (implementation)
			result = reinterpret_cast<ObjectMethod>(implementation)(receiver, selector,
				first, second);
	} else if (std::strcmp(types, "q@:q") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t);
		if (implementation)
			result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
					va_arg(arguments, std::int64_t))));
	} else if (std::strcmp(types, "q@:qq") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t,
			std::int64_t);
		const auto first = va_arg(arguments, std::int64_t);
		const auto second = va_arg(arguments, std::int64_t);
		if (implementation)
			result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
					first, second)));
	} else if (std::strcmp(types, "^v@:^v") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using PointerMethod = void* (*)(id, SEL, void*);
		if (implementation)
			result = reinterpret_cast<id>(reinterpret_cast<PointerMethod>(implementation)(
				receiver, selector, va_arg(arguments, void*)));
	} else if (std::strcmp(types, "@@:@?") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using BlockMethod = id (*)(id, SEL, DarlingObjcCallbackBlock*);
		if (implementation)
			result = reinterpret_cast<BlockMethod>(implementation)(receiver, selector,
				va_arg(arguments, DarlingObjcCallbackBlock*));
	} else if (std::strcmp(types, "@@:@") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation) {
			using Method = id (*)(id, SEL, id);
			result = reinterpret_cast<Method>(implementation)(receiver, selector,
				va_arg(arguments, id));
		}
	}
	va_end(arguments);
	return result;
}

extern "C" id darling_objc_msgSend_class0(Class cls, SEL selector)
{
	if (!cls || !selector)
		return nullptr;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = cls; current; current = current->superclass) {
		const auto& method_map = current->is_metaclass ? current->methods : current->class_methods;
		const auto method = method_map.find(selector);
		if (method == method_map.end())
			continue;
		if (method->second.types != "@@:")
			return nullptr;
		using ClassMethod = id (*)(Class, SEL);
		return reinterpret_cast<ClassMethod>(method->second.implementation)(
			cls->is_metaclass && cls->owner_class ? cls->owner_class : cls,
			selector);
	}
	return nullptr;
}

extern "C" id darling_objc_msgSend_object1(id receiver, SEL selector,
	id argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "@@:@") != 0)
		return nullptr;
	using ObjectMethod = id (*)(id, SEL, id);
	return reinterpret_cast<ObjectMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" id darling_objc_msgSend_object0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "@@:") != 0)
		return nullptr;
	using ObjectMethod = id (*)(id, SEL);
	return reinterpret_cast<ObjectMethod>(implementation)(receiver, selector);
}

extern "C" id darling_objc_msgSend_object2(id receiver, SEL selector,
	id first, id second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "@@:@@") != 0)
		return nullptr;
	using ObjectMethod = id (*)(id, SEL, id, id);
	return reinterpret_cast<ObjectMethod>(implementation)(receiver, selector,
		first, second);
}

extern "C" id darling_objc_msgSend_block1(id receiver, SEL selector,
	DarlingObjcCallbackBlock* argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "@@:@?") != 0)
		return nullptr;
	using BlockMethod = id (*)(id, SEL, DarlingObjcCallbackBlock*);
	return reinterpret_cast<BlockMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" std::int64_t darling_objc_msgSend_int64_1(id receiver, SEL selector,
	std::int64_t argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "q@:q") != 0)
		return 0;
	using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" std::int64_t darling_objc_msgSend_int64_2(id receiver, SEL selector,
	std::int64_t first, std::int64_t second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "q@:qq") != 0)
		return 0;
	using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t, std::int64_t);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
		first, second);
}

extern "C" bool darling_objc_msgSend_bool1(id receiver, SEL selector,
	bool argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "B@:B") != 0)
		return false;
	using BoolMethod = bool (*)(id, SEL, bool);
	return reinterpret_cast<BoolMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" void* darling_objc_msgSend_pointer1(id receiver, SEL selector,
	void* argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "^v@:^v") != 0)
		return nullptr;
	using PointerMethod = void* (*)(id, SEL, void*);
	return reinterpret_cast<PointerMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" void darling_objc_msgSend_void1_int64(id receiver, SEL selector,
	std::int64_t argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "v@:q") != 0)
		return;
	using VoidMethod = void (*)(id, SEL, std::int64_t);
	reinterpret_cast<VoidMethod>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSend_void1_int(id receiver, SEL selector,
	int argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "v@:i") != 0)
		return;
	using VoidMethod = void (*)(id, SEL, int);
	reinterpret_cast<VoidMethod>(implementation)(receiver, selector, argument);
}

extern "C" double darling_objc_msgSend_double1(id receiver, SEL selector,
	double argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "d@:d") != 0)
		return 0.0;
	using DoubleMethod = double (*)(id, SEL, double);
	return reinterpret_cast<DoubleMethod>(implementation)(receiver, selector,
		argument);
}

extern "C" double darling_objc_msgSend_double2(id receiver, SEL selector,
	double first, double second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "d@:dd") != 0)
		return 0.0;
	using DoubleMethod = double (*)(id, SEL, double, double);
	return reinterpret_cast<DoubleMethod>(implementation)(receiver, selector,
		first, second);
}

extern "C" void darling_objc_msgSend_void0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types, "v@:") != 0)
		return;
	using VoidMethod = void (*)(id, SEL);
	reinterpret_cast<VoidMethod>(implementation)(receiver, selector);
}

extern "C" DarlingObjcRect darling_objc_msgSend_rect0(id receiver, SEL selector)
{
	const DarlingObjcRect empty{};
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!implementation || !types || std::strcmp(types,
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:") != 0)
		return empty;
	using RectMethod = DarlingObjcRect (*)(id, SEL);
	return reinterpret_cast<RectMethod>(implementation)(receiver, selector);
}

extern "C" DarlingObjcValue darling_objc_invoke0(id receiver, SEL selector)
{
	DarlingObjcValue result{DARLING_OBJC_VOID, {}};
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (types && std::strcmp(types, "v@:") == 0)
		darling_objc_msgSend_void0(receiver, selector);
	else if (types && std::strcmp(types, "@@:") == 0)
		result = {DARLING_OBJC_OBJECT, {objc_msgSend(receiver, selector)}};
	else if (types && std::strcmp(types, "d@:") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using DoubleMethod = double (*)(id, SEL);
		result = {DARLING_OBJC_DOUBLE, {.floating = implementation ?
			reinterpret_cast<DoubleMethod>(implementation)(receiver, selector) : 0.0}};
	}
	return result;
}

extern "C" DarlingObjcValue darling_objc_invoke1(id receiver, SEL selector,
	DarlingObjcValue argument)
{
	DarlingObjcValue result{DARLING_OBJC_VOID, {}};
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!types)
		return result;
	if (std::strcmp(types, "@@:@") == 0 &&
		argument.kind == DARLING_OBJC_OBJECT) {
		result = {DARLING_OBJC_OBJECT,
			{darling_objc_msgSend_object1(receiver, selector, argument.object)}};
	} else if (std::strcmp(types, "q@:q") == 0 &&
		argument.kind == DARLING_OBJC_INT64) {
		result = {DARLING_OBJC_INT64,
			{.integer = darling_objc_msgSend_int64_1(receiver, selector,
				argument.integer)}};
	} else if (std::strcmp(types, "B@:B") == 0 &&
		argument.kind == DARLING_OBJC_BOOL) {
		const IMP implementation = FindMethod(receiver, selector);
		using BoolMethod = bool (*)(id, SEL, bool);
		result = {DARLING_OBJC_BOOL,
			{.boolean = implementation &&
				reinterpret_cast<BoolMethod>(implementation)(receiver, selector,
					argument.boolean)}};
	} else if (std::strcmp(types, "d@:d") == 0 &&
		argument.kind == DARLING_OBJC_DOUBLE) {
		const IMP implementation = FindMethod(receiver, selector);
		using DoubleMethod = double (*)(id, SEL, double);
		result = {DARLING_OBJC_DOUBLE,
			{.floating = implementation ?
				reinterpret_cast<DoubleMethod>(implementation)(receiver, selector,
					argument.floating) : 0.0}};
	} else if (std::strcmp(types, "^v@:^v") == 0 &&
		argument.kind == DARLING_OBJC_POINTER) {
		const IMP implementation = FindMethod(receiver, selector);
		using PointerMethod = void* (*)(id, SEL, void*);
		result = {DARLING_OBJC_POINTER,
			{.pointer = implementation ?
				reinterpret_cast<PointerMethod>(implementation)(receiver, selector,
					argument.pointer) : nullptr}};
	} else if (std::strcmp(types, "@@:@?") == 0 &&
		argument.kind == DARLING_OBJC_BLOCK) {
		const IMP implementation = FindMethod(receiver, selector);
		using BlockMethod = id (*)(id, SEL, DarlingObjcCallbackBlock*);
		result = {DARLING_OBJC_OBJECT,
			{.object = implementation ?
				reinterpret_cast<BlockMethod>(implementation)(receiver, selector,
					argument.block) : nullptr}};
	}
	return result;
}

extern "C" DarlingObjcValue darling_objc_invoke2(id receiver, SEL selector,
	DarlingObjcValue first, DarlingObjcValue second)
{
	DarlingObjcValue result{DARLING_OBJC_VOID, {}};
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (!types)
		return result;
	const IMP implementation = FindMethod(receiver, selector);
	if (!implementation)
		return result;
	if (std::strcmp(types, "q@:qq") == 0 &&
		first.kind == DARLING_OBJC_INT64 && second.kind == DARLING_OBJC_INT64) {
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t,
			std::int64_t);
		result = {DARLING_OBJC_INT64,
			{.integer = reinterpret_cast<IntegerMethod>(implementation)(receiver,
				selector, first.integer, second.integer)}};
	} else if (std::strcmp(types, "d@:dd") == 0 &&
		first.kind == DARLING_OBJC_DOUBLE && second.kind == DARLING_OBJC_DOUBLE) {
		using DoubleMethod = double (*)(id, SEL, double, double);
		result = {DARLING_OBJC_DOUBLE,
			{.floating = reinterpret_cast<DoubleMethod>(implementation)(receiver,
				selector, first.floating, second.floating)}};
	}
	return result;
}

extern "C" DarlingObjcRect darling_objc_invoke_rect0(id receiver,
	SEL selector)
{
	DarlingObjcRect result{};
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	constexpr const char* RectEncoding =
		"{DarlingObjcRect=dddd}@:";
	constexpr const char* CgRectEncoding =
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:";
	if (!types || (std::strcmp(types, RectEncoding) != 0 &&
		std::strcmp(types, CgRectEncoding) != 0))
		return result;
	const IMP implementation = FindMethod(receiver, selector);
	if (!implementation)
		return result;
	using RectMethod = DarlingObjcRect (*)(id, SEL);
	return reinterpret_cast<RectMethod>(implementation)(receiver, selector);
}

extern "C" DarlingObjcCallbackBlock* darling_objc_block_create1(
	DarlingObjcCallback1 callback, void* context)
{
	if (!callback)
		return nullptr;
	return new DarlingObjcCallbackBlock{1, callback, context};
}

extern "C" DarlingObjcCallbackBlock* darling_objc_block_copy(
	DarlingObjcCallbackBlock* block)
{
	if (block)
		block->retain_count.fetch_add(1, std::memory_order_relaxed);
	return block;
}

extern "C" void darling_objc_block_release(DarlingObjcCallbackBlock* block)
{
	if (block && block->retain_count.fetch_sub(1,
		std::memory_order_acq_rel) == 1)
		delete block;
}

extern "C" id darling_objc_block_invoke1(DarlingObjcCallbackBlock* block,
	id argument)
{
	return block && block->callback ? block->callback(block->context, argument) :
		nullptr;
}

extern "C" DarlingObjcAppleBlock* darling_objc_apple_block_create1(
	DarlingObjcCallback1 callback, void* context)
{
	if (!callback)
		return nullptr;
	static DarlingObjcAppleBlockDescriptor descriptor{
		0, sizeof(DarlingObjcAppleBlock), nullptr, nullptr, "@@:"};
	auto* block = new DarlingObjcAppleBlock{};
	block->invoke = &AppleBlockInvoke;
	block->descriptor = &descriptor;
	block->callback = callback;
	block->context = context;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		ManagedAppleBlocks().insert(block);
	}
	return block;
}

extern "C" DarlingObjcAppleBlock* darling_objc_apple_block_copy(
	DarlingObjcAppleBlock* block)
{
	if (!block)
		return nullptr;
	std::lock_guard lock(AppleBlockRuntimeMutex());
	if (!ManagedAppleBlocks().contains(block))
		return nullptr;
	block->retain_count.fetch_add(1, std::memory_order_relaxed);
	return block;
}

extern "C" void darling_objc_apple_block_release(DarlingObjcAppleBlock* block)
{
	if (!block)
		return;
	bool destroy = false;
	{
		std::lock_guard lock(AppleBlockRuntimeMutex());
		if (!ManagedAppleBlocks().contains(block))
			return;
		if (block->retain_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			ManagedAppleBlocks().erase(block);
			destroy = true;
		}
	}
	if (destroy)
		delete block;
}

extern "C" id darling_objc_apple_block_invoke1(DarlingObjcAppleBlock* block,
	id argument)
{
	return block && block->invoke ? block->invoke(block, argument) : nullptr;
}

extern "C" const char* darling_objc_apple_block_signature(
	DarlingObjcAppleBlock* block)
{
	return block && block->descriptor ? block->descriptor->signature : nullptr;
}

extern "C" void _Block_object_assign(void* destination, const void* object,
	const int flags)
{
	if (destination == nullptr)
		return;
	constexpr int block_field_is_object = 3;
	constexpr int block_field_is_block = 7;
	constexpr int block_field_is_byref = 8;
	constexpr int block_field_is_weak = 16;
	if (object == nullptr) {
		std::memset(destination, 0, sizeof(void*));
		return;
	}
	if ((flags & block_field_is_byref) == block_field_is_byref) {
		const auto copied = CopyExternalAppleByref(const_cast<void*>(object));
		std::memcpy(destination, &copied, sizeof(copied));
		return;
	}
	if ((flags & block_field_is_weak) != 0) {
		// Weak captures are copied as non-owning pointers.  Their weak-slot
		// registration is supplied by the compiler/runtime weak ABI, not by
		// the BlocksRuntime ownership helper.
		std::memcpy(destination, &object, sizeof(object));
		return;
	}
	if ((flags & block_field_is_block) == block_field_is_block) {
		const auto copied = objc_retainBlock(const_cast<void*>(object));
		std::memcpy(destination, &copied, sizeof(copied));
		return;
	}
	if ((flags & block_field_is_object) == block_field_is_object) {
		id retained = objc_retain(reinterpret_cast<id>(const_cast<void*>(object)));
		std::memcpy(destination, &retained, sizeof(retained));
		return;
	}
	std::memcpy(destination, &object, sizeof(object));
}

extern "C" void _Block_object_dispose(const void* object, const int flags)
{
	if (object == nullptr)
		return;
	constexpr int block_field_is_object = 3;
	constexpr int block_field_is_block = 7;
	constexpr int block_field_is_byref = 8;
	constexpr int block_field_is_weak = 16;
	if ((flags & block_field_is_byref) == block_field_is_byref)
	{
		ReleaseExternalAppleByref(const_cast<void*>(object));
		return;
	}
	if ((flags & block_field_is_weak) != 0)
		return;
	if ((flags & block_field_is_block) == block_field_is_block) {
		objc_releaseBlock(const_cast<void*>(object));
		return;
	}
	if ((flags & block_field_is_object) == block_field_is_object)
		objc_release(reinterpret_cast<id>(const_cast<void*>(object)));
}

extern "C" void* objc_retainBlock(void* raw_block)
{
	if (IsManagedAppleBlock(raw_block))
		return darling_objc_apple_block_copy(
			static_cast<DarlingObjcAppleBlock*>(raw_block));
	return CopyExternalAppleBlock(raw_block);
}

extern "C" void objc_releaseBlock(void* raw_block)
{
	if (IsManagedAppleBlock(raw_block)) {
		darling_objc_apple_block_release(
			static_cast<DarlingObjcAppleBlock*>(raw_block));
		return;
	}
	ReleaseExternalAppleBlock(raw_block);
}
