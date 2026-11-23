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
#include <optional>
#include <string>
#include <string_view>
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
	bool guest_abi{};
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
		bool guest_abi{};
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
	std::vector<id> constant_strings;
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

struct DarlingObjcExceptionCarrier final {
	id object{};
};

id AppleBlockInvoke(void* raw_block, id argument)
{
	auto* block = static_cast<DarlingObjcAppleBlock*>(raw_block);
	return block && block->callback ? block->callback(block->context, argument) :
		nullptr;
}

#if defined(_MSC_VER)
#pragma warning(suppress: 4297)
#endif
extern "C" [[noreturn]] void darling_objc_exception_throw(id object)
{
	throw DarlingObjcExceptionCarrier{object};
}

extern "C" bool darling_objc_exception_try(
	DarlingObjcExceptionCallback callback, void* context, id* caught_object)
{
	if (caught_object != nullptr)
		*caught_object = nullptr;
	if (callback == nullptr)
		return false;
	try {
		callback(context);
		return false;
	} catch (const DarlingObjcExceptionCarrier& exception) {
		if (caught_object != nullptr)
			*caught_object = exception.object;
		return true;
	} catch (...) {
		return false;
	}
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
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
using AppleBlockCopyHelper = void(__attribute__((sysv_abi)) *)(void*, const void*);
using AppleBlockDisposeHelper = void(__attribute__((sysv_abi)) *)(const void*);
#else
using AppleBlockCopyHelper = void (*)(void*, const void*);
using AppleBlockDisposeHelper = void (*)(const void*);
#endif

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
	AppleBlockDisposeHelper dispose_helper{};
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
	std::size_t& size, AppleBlockCopyHelper* copy_helper,
	AppleBlockDisposeHelper* dispose_helper)
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
	AppleBlockCopyHelper copy_helper = nullptr;
	AppleBlockDisposeHelper dispose_helper = nullptr;
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

std::unordered_set<id>& ConstantStrings()
{
	static std::unordered_set<id> strings;
	return strings;
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
	MethodViewStorage().back()->guest_abi = method.guest_abi;
	return MethodViewStorage().back().get();
}

struct AutoreleasePoolFrame final {
	std::vector<id> objects;
};
thread_local std::vector<std::unique_ptr<AutoreleasePoolFrame>> AutoreleasePools;
std::unordered_map<id, std::unordered_set<id*>> WeakReferences;
std::unordered_map<id, darling_objc_object_finalizer> ObjectFinalizers;
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

struct DarlingObjcSuper final {
	id receiver;
	Class super_class;
};

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
struct DarlingSysVObjcCallFrame final {
	std::uintptr_t gp[6];
	alignas(16) unsigned char xmm[8][16];
	const std::uintptr_t* stack_arguments;
	std::uintptr_t vector_argument_count;
	alignas(16) unsigned char xmm_return[16];
};
static_assert(offsetof(DarlingSysVObjcCallFrame, xmm) == 48);
static_assert(offsetof(DarlingSysVObjcCallFrame, stack_arguments) == 176);
static_assert(offsetof(DarlingSysVObjcCallFrame, vector_argument_count) == 184);
static_assert(offsetof(DarlingSysVObjcCallFrame, xmm_return) == 192);
static_assert(sizeof(DarlingSysVObjcCallFrame) == 208);
extern "C" std::uintptr_t darling_windows_objc_invoke_sysv_raw(
	DarlingSysVObjcCallFrame* frame, IMP implementation,
	std::size_t stack_argument_bytes);

namespace {
struct ObjcMethodSnapshot final {
	IMP implementation{};
	std::string types;
	bool guest_abi{};
};

enum class ObjcScalarKind { General, Floating, Void, Unsupported };

bool SkipObjcType(const std::string& encoding, std::size_t& cursor)
{
	while (cursor < encoding.size() &&
		std::string_view("rnNoORV").find(encoding[cursor]) !=
			std::string_view::npos) ++cursor;
	if (cursor >= encoding.size()) return false;
	const char code = encoding[cursor++];
	if (code == '^') return SkipObjcType(encoding, cursor);
	if (code == '@' && cursor < encoding.size()) {
		if (encoding[cursor] == '?') {
			++cursor;
			return true;
		}
	if (encoding[cursor] == '"') {
		++cursor;
		while (cursor < encoding.size() && encoding[cursor] != '"') ++cursor;
		if (cursor == encoding.size()) return false;
		++cursor;
	}
	return true;
	}
	if (code == '{' || code == '(' || code == '[') {
		const char close = code == '{' ? '}' : code == '(' ? ')' : ']';
		unsigned depth = 1;
		bool quoted = false;
		while (cursor < encoding.size() && depth != 0) {
			const char current = encoding[cursor++];
			if (current == '"') quoted = !quoted;
			if (quoted) continue;
			if (current == code) ++depth;
			else if (current == close) --depth;
		}
		return depth == 0;
	}
	return std::string_view("cCsSiIlLqQBfdv@#:*?b").find(code) !=
		std::string_view::npos;
}

std::optional<std::pair<ObjcScalarKind, std::size_t>>
ClassifyScalarObjcType(const std::string& encoding, std::size_t cursor)
{
	const std::size_t start = cursor;
	while (cursor < encoding.size() &&
		std::string_view("rnNoORV").find(encoding[cursor]) !=
			std::string_view::npos) ++cursor;
	if (cursor >= encoding.size()) return std::nullopt;
	const char code = encoding[cursor];
	if (code == '^') {
		++cursor;
		if (!SkipObjcType(encoding, cursor)) return std::nullopt;
		return std::pair{ObjcScalarKind::General, cursor - start};
	}
	if (code == '{' || code == '(' || code == '[' || code == 'b' ||
		code == '?' || code == 'D') return std::nullopt;
	if (code == '@' && cursor + 1 < encoding.size() && encoding[cursor + 1] == '?')
		return std::pair{ObjcScalarKind::General, cursor + 2 - start};
	if (code == '@' && cursor + 1 < encoding.size() &&
		encoding[cursor + 1] == '"') {
		const auto end = encoding.find('"', cursor + 2);
		if (end == std::string::npos) return std::nullopt;
		return std::pair{ObjcScalarKind::General, end + 1 - start};
	}
	if (code == 'f' || code == 'd')
		return std::pair{ObjcScalarKind::Floating, cursor + 1 - start};
	if (code == 'v')
		return std::pair{ObjcScalarKind::Void, cursor + 1 - start};
	if (std::string_view("cCsSiIlLqQB@#:*").find(code) !=
		std::string_view::npos)
		return std::pair{ObjcScalarKind::General, cursor + 1 - start};
	return std::nullopt;
}

std::optional<std::size_t> GenericGuestStackArgumentBytes(
	const std::string& encoding, ObjcScalarKind& return_kind)
{
	const auto result_type = ClassifyScalarObjcType(encoding, 0);
	if (!result_type) return std::nullopt;
	return_kind = result_type->first;
	std::size_t cursor = result_type->second;
	std::size_t general_registers = 0;
	std::size_t floating_registers = 0;
	std::size_t stack_slots = 0;
	while (cursor < encoding.size()) {
		const auto argument = ClassifyScalarObjcType(encoding, cursor);
		if (!argument || argument->first == ObjcScalarKind::Void)
			return std::nullopt;
		cursor += argument->second;
		if (argument->first == ObjcScalarKind::Floating) {
			if (floating_registers < 8) ++floating_registers;
			else ++stack_slots;
		} else {
			if (general_registers < 6) ++general_registers;
			else ++stack_slots;
		}
	}
	if (stack_slots > 64) return std::nullopt;
	return stack_slots * sizeof(std::uint64_t);
}

template <typename Return, typename... Arguments>
Return InvokeObjcImplementation(IMP implementation, bool guest_abi, id receiver,
	SEL selector, Arguments... arguments)
{
	if (guest_abi) {
		using Function = Return (__attribute__((sysv_abi)) *)(id, SEL, Arguments...);
		return reinterpret_cast<Function>(implementation)(receiver, selector,
			arguments...);
	}
	using Function = Return (*)(id, SEL, Arguments...);
	return reinterpret_cast<Function>(implementation)(receiver, selector,
		arguments...);
}

ObjcMethodSnapshot LookupMethodSnapshot(Class start, SEL selector,
	bool class_method)
{
	ObjcMethodSnapshot result;
	if (start == nullptr || selector == nullptr) return result;
	std::lock_guard lock(RuntimeMutex());
	for (Class current = start; current != nullptr; current = current->superclass) {
		const auto& methods = (class_method && !current->is_metaclass) ?
			current->class_methods : current->methods;
		const auto found = methods.find(selector);
		if (found == methods.end()) continue;
		result.implementation = found->second.implementation;
		result.types = found->second.types;
		result.guest_abi = found->second.guest_abi;
		break;
	}
	return result;
}

std::string CanonicalObjcEncoding(const std::string& encoding)
{
	std::string result;
	result.reserve(encoding.size());
	bool quoted = false;
	for (const unsigned char value : encoding) {
		if (value == '"') quoted = !quoted;
		if (!quoted && value >= '0' && value <= '9') continue;
		result.push_back(static_cast<char>(value));
	}
	return result;
}
}

namespace {
std::uintptr_t DispatchSysVObjcCall(DarlingSysVObjcCallFrame* input,
	unsigned dispatch_kind)
{
	if (input == nullptr) return 0;
	DarlingSysVObjcCallFrame* frame = input;
	id receiver = nullptr;
	SEL selector = nullptr;
	std::size_t argument_index = 2;
	Class super_start = nullptr;
	if (dispatch_kind == 0) {
		receiver = reinterpret_cast<id>(frame->gp[0]);
		selector = reinterpret_cast<SEL>(frame->gp[1]);
	} else {
		const auto* super = reinterpret_cast<const DarlingObjcSuper*>(frame->gp[0]);
		if (super == nullptr) return 0;
		receiver = super->receiver;
		super_start = super->super_class;
		selector = reinterpret_cast<SEL>(frame->gp[1]);
		if (dispatch_kind == 2) {
			if (super_start == nullptr) return 0;
			super_start = super_start->superclass;
		}
	}
	if (receiver == nullptr || selector == nullptr) return 0;
	const bool class_method = object_isClass(receiver);
	const Class start = dispatch_kind != 0 ? super_start :
		(class_method ? reinterpret_cast<Class>(receiver) : receiver->isa);
	const auto method = LookupMethodSnapshot(start, selector, class_method);
	if (method.implementation == nullptr) return 0;
	const auto types = CanonicalObjcEncoding(method.types);
	const IMP imp = method.implementation;
	constexpr std::string_view range_argument_encoding = "{_NSRange=QQ}";
	if (types == std::string(range_argument_encoding) + "@:") {
		const DarlingObjcRange result = InvokeObjcImplementation<DarlingObjcRange>(
			imp, method.guest_abi, receiver, selector);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return static_cast<std::uintptr_t>(result.location);
	}
	if (types == std::string(range_argument_encoding) + "@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		const DarlingObjcRange result =
			InvokeObjcImplementation<DarlingObjcRange, id>(imp,
				method.guest_abi, receiver, selector, argument);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return static_cast<std::uintptr_t>(result.location);
	}
	if (types == std::string(range_argument_encoding) + "@:@Q") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		const std::uint64_t options = frame->gp[argument_index + 1];
		const DarlingObjcRange result =
			InvokeObjcImplementation<DarlingObjcRange, id, std::uint64_t>(imp,
				method.guest_abi, receiver, selector, argument, options);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return static_cast<std::uintptr_t>(result.location);
	}
	const std::string range_with_options_and_range =
		std::string(range_argument_encoding) + "@:@Q" +
		std::string(range_argument_encoding);
	if (types == range_with_options_and_range) {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		const std::uint64_t options = frame->gp[argument_index + 1];
		const DarlingObjcRange search_range{frame->gp[argument_index + 2],
			frame->gp[argument_index + 3]};
		const DarlingObjcRange result =
			InvokeObjcImplementation<DarlingObjcRange, id, std::uint64_t,
				DarlingObjcRange>(imp, method.guest_abi, receiver, selector,
				argument, options, search_range);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return static_cast<std::uintptr_t>(result.location);
	}
	if (types == std::string("@@:") + std::string(range_argument_encoding)) {
		const DarlingObjcRange range{frame->gp[argument_index],
			frame->gp[argument_index + 1]};
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, DarlingObjcRange>(imp, method.guest_abi,
				receiver, selector, range));
	}
	if (types == std::string("v@:") + std::string(range_argument_encoding)) {
		const DarlingObjcRange range{frame->gp[argument_index],
			frame->gp[argument_index + 1]};
		InvokeObjcImplementation<void, DarlingObjcRange>(imp, method.guest_abi,
			receiver, selector, range);
		return 0;
	}
	if (types == std::string("v@:") + std::string(range_argument_encoding) + "@") {
		const DarlingObjcRange range{frame->gp[argument_index],
			frame->gp[argument_index + 1]};
		const id value = reinterpret_cast<id>(frame->gp[argument_index + 2]);
		InvokeObjcImplementation<void, DarlingObjcRange, id>(imp,
			method.guest_abi, receiver, selector, range, value);
		return 0;
	}
	if (types == std::string("v@:@") +
		std::string(range_argument_encoding)) {
		const id value = reinterpret_cast<id>(frame->gp[argument_index]);
		const DarlingObjcRange range{frame->gp[argument_index + 1],
			frame->gp[argument_index + 2]};
		InvokeObjcImplementation<void, id, DarlingObjcRange>(imp,
			method.guest_abi, receiver, selector, value, range);
		return 0;
	}
	if (types == std::string("v@:") + std::string(range_argument_encoding) + "^vQ") {
		const DarlingObjcRange range{frame->gp[argument_index],
			frame->gp[argument_index + 1]};
		const void* bytes = reinterpret_cast<const void*>(
			frame->gp[argument_index + 2]);
		const std::uint64_t length = frame->gp[argument_index + 3];
		InvokeObjcImplementation<void, DarlingObjcRange, const void*,
			std::uint64_t>(imp, method.guest_abi, receiver, selector, range,
				bytes, length);
		return 0;
	}
	constexpr std::string_view point_encoding = "{CGPoint=dd}";
	if (types == "{CGPoint=dd}@:" || types == "{CGSize=dd}@:") {
		const DarlingObjcPoint result = InvokeObjcImplementation<DarlingObjcPoint>(
			imp, method.guest_abi, receiver, selector);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:{CGPoint=dd}" || types == "v@:{CGSize=dd}") {
		DarlingObjcPoint point{};
		std::memcpy(&point.x, frame->xmm[0], sizeof(point.x));
		std::memcpy(&point.y, frame->xmm[1], sizeof(point.y));
		InvokeObjcImplementation<void, DarlingObjcPoint>(imp, method.guest_abi,
			receiver, selector, point);
		return 0;
	}
	if (types == "B@:{CGPoint=dd}") {
		DarlingObjcPoint point{};
		std::memcpy(&point.x, frame->xmm[0], sizeof(point.x));
		std::memcpy(&point.y, frame->xmm[1], sizeof(point.y));
		return InvokeObjcImplementation<bool, DarlingObjcPoint>(imp,
			method.guest_abi, receiver, selector, point) ? 1u : 0u;
	}
	if (types == "{CGPoint=dd}@:{CGPoint=dd}") {
		DarlingObjcPoint point{};
		std::memcpy(&point.x, frame->xmm[0], sizeof(point.x));
		std::memcpy(&point.y, frame->xmm[1], sizeof(point.y));
		const DarlingObjcPoint result =
			InvokeObjcImplementation<DarlingObjcPoint, DarlingObjcPoint>(imp,
				method.guest_abi, receiver, selector, point);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "{CGSize=dd}@:{CGSize=dd}") {
		DarlingObjcPoint size{};
		std::memcpy(&size.x, frame->xmm[0], sizeof(size.x));
		std::memcpy(&size.y, frame->xmm[1], sizeof(size.y));
		const DarlingObjcPoint result =
			InvokeObjcImplementation<DarlingObjcPoint, DarlingObjcPoint>(imp,
				method.guest_abi, receiver, selector, size);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "{CGSize=dd}@:{CGSize=dd}@") {
		DarlingObjcPoint size{};
		std::memcpy(&size.x, frame->xmm[0], sizeof(size.x));
		std::memcpy(&size.y, frame->xmm[1], sizeof(size.y));
		const id other_view = reinterpret_cast<id>(frame->gp[argument_index]);
		const DarlingObjcPoint result =
			InvokeObjcImplementation<DarlingObjcPoint, DarlingObjcPoint, id>(imp,
				method.guest_abi, receiver, selector, size, other_view);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:{CGPoint=dd}@") {
		DarlingObjcPoint point{};
		std::memcpy(&point.x, frame->xmm[0], sizeof(point.x));
		std::memcpy(&point.y, frame->xmm[1], sizeof(point.y));
		const id attributes = reinterpret_cast<id>(frame->gp[argument_index]);
		InvokeObjcImplementation<void, DarlingObjcPoint, id>(imp,
			method.guest_abi, receiver, selector, point, attributes);
		return 0;
	}
	if (types == std::string(point_encoding) + "@:" +
		std::string(point_encoding) + "@") {
		DarlingObjcPoint point{};
		std::memcpy(&point.x, frame->xmm[0], sizeof(point.x));
		std::memcpy(&point.y, frame->xmm[1], sizeof(point.y));
		const id other_view = reinterpret_cast<id>(frame->gp[argument_index]);
		const DarlingObjcPoint result =
			InvokeObjcImplementation<DarlingObjcPoint, DarlingObjcPoint, id>(
				imp, method.guest_abi, receiver, selector, point, other_view);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:{CGPoint=dd}{CGPoint=dd}{CGPoint=dd}") {
		DarlingObjcPoint points[3]{};
		for (std::size_t index = 0; index < 3; ++index) {
			std::memcpy(&points[index].x, frame->xmm[index * 2],
				sizeof(double));
			std::memcpy(&points[index].y, frame->xmm[index * 2 + 1],
				sizeof(double));
		}
		InvokeObjcImplementation<void, DarlingObjcPoint, DarlingObjcPoint,
			DarlingObjcPoint>(imp, method.guest_abi, receiver, selector,
				points[0], points[1], points[2]);
		return 0;
	}
	if (types == "v@:{CGPoint=dd}{CGPoint=dd}") {
		DarlingObjcPoint start{}, end{};
		std::memcpy(&start.x, frame->xmm[0], sizeof(start.x));
		std::memcpy(&start.y, frame->xmm[1], sizeof(start.y));
		std::memcpy(&end.x, frame->xmm[2], sizeof(end.x));
		std::memcpy(&end.y, frame->xmm[3], sizeof(end.y));
		InvokeObjcImplementation<void, DarlingObjcPoint, DarlingObjcPoint>(imp,
			method.guest_abi, receiver, selector, start, end);
		return 0;
	}
	if (types == "v@:{CGPoint=dd}{CGPoint=dd}d") {
		DarlingObjcPoint corner{}, next{};
		std::memcpy(&corner.x, frame->xmm[0], sizeof(corner.x));
		std::memcpy(&corner.y, frame->xmm[1], sizeof(corner.y));
		std::memcpy(&next.x, frame->xmm[2], sizeof(next.x));
		std::memcpy(&next.y, frame->xmm[3], sizeof(next.y));
		double radius = 0;
		std::memcpy(&radius, frame->xmm[4], sizeof(radius));
		InvokeObjcImplementation<void, DarlingObjcPoint, DarlingObjcPoint, double>(
			imp, method.guest_abi, receiver, selector, corner, next, radius);
		return 0;
	}
	if (types == "v@:{CGPoint=dd}dddB") {
		DarlingObjcPoint center{};
		std::memcpy(&center.x, frame->xmm[0], sizeof(center.x));
		std::memcpy(&center.y, frame->xmm[1], sizeof(center.y));
		double radius = 0.0, start_angle = 0.0, end_angle = 0.0;
		std::memcpy(&radius, frame->xmm[2], sizeof(radius));
		std::memcpy(&start_angle, frame->xmm[3], sizeof(start_angle));
		std::memcpy(&end_angle, frame->xmm[4], sizeof(end_angle));
		const bool clockwise = (frame->gp[argument_index] & 0xffu) != 0;
		InvokeObjcImplementation<void, DarlingObjcPoint, double, double,
			double, bool>(imp, method.guest_abi, receiver, selector, center,
				radius, start_angle, end_angle, clockwise);
		return 0;
	}
	constexpr std::string_view rect_argument_encoding =
		"{CGRect={CGPoint=dd}{CGSize=dd}}";
	constexpr std::string_view affine_transform_argument_encoding =
		"{NSAffineTransformStruct=dddddd}";
	if (types == std::string("v@:") +
		std::string(affine_transform_argument_encoding)) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcAffineTransform transform{};
		std::memcpy(&transform, frame->stack_arguments, sizeof(transform));
		InvokeObjcImplementation<void, DarlingObjcAffineTransform>(imp,
			method.guest_abi, receiver, selector, transform);
		return 0;
	}
	constexpr std::string_view operating_system_version_argument_encoding =
		"{NSOperatingSystemVersion=qqq}";
	if (types == std::string("B@:") +
		std::string(operating_system_version_argument_encoding)) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcOperatingSystemVersion version{};
		std::memcpy(&version, frame->stack_arguments, sizeof(version));
		return InvokeObjcImplementation<bool, DarlingObjcOperatingSystemVersion>(
			imp, method.guest_abi, receiver, selector, version) ? 1u : 0u;
	}
	const std::string rect_with_radii_encoding =
		"@@:" + std::string(rect_argument_encoding) + "dd";
	const std::string void_rect_with_radii_encoding =
		"v@:" + std::string(rect_argument_encoding) + "dd";
	if (types == rect_with_radii_encoding ||
		types == void_rect_with_radii_encoding) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect rect{};
		double x_radius = 0.0;
		double y_radius = 0.0;
		std::memcpy(&rect, frame->stack_arguments, sizeof(rect));
		std::memcpy(&x_radius, frame->xmm[0], sizeof(x_radius));
		std::memcpy(&y_radius, frame->xmm[1], sizeof(y_radius));
		if (types == rect_with_radii_encoding) {
			return reinterpret_cast<std::uintptr_t>(
				InvokeObjcImplementation<id, DarlingObjcRect, double, double>(imp,
					method.guest_abi, receiver, selector, rect, x_radius, y_radius));
		}
		InvokeObjcImplementation<void, DarlingObjcRect, double, double>(imp,
			method.guest_abi, receiver, selector, rect, x_radius, y_radius);
		return 0;
	}
	if (types == std::string("@@:") + std::string(rect_argument_encoding)) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect rect{};
		std::memcpy(&rect, frame->stack_arguments, sizeof(rect));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, DarlingObjcRect>(imp, method.guest_abi,
				receiver, selector, rect));
	}
	if (types == std::string("v@:") + std::string(rect_argument_encoding)) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect rect{};
		std::memcpy(&rect, frame->stack_arguments, sizeof(rect));
		InvokeObjcImplementation<void, DarlingObjcRect>(imp, method.guest_abi,
			receiver, selector, rect);
		return 0;
	}
	if (types == std::string("v@:") + std::string(rect_argument_encoding) + "B") {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect rect{};
		std::memcpy(&rect, frame->stack_arguments, sizeof(rect));
		const bool flag = (frame->gp[argument_index] & 0xffu) != 0;
		InvokeObjcImplementation<void, DarlingObjcRect, bool>(imp,
			method.guest_abi, receiver, selector, rect, flag);
		return 0;
	}
	if (types == std::string("B@:") + std::string(rect_argument_encoding)) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect rect{};
		std::memcpy(&rect, frame->stack_arguments, sizeof(rect));
		return InvokeObjcImplementation<bool, DarlingObjcRect>(imp,
			method.guest_abi, receiver, selector, rect) ? 1u : 0u;
	}
	constexpr std::string_view rect_initializer_prefix =
		"@@:{CGRect={CGPoint=dd}{CGSize=dd}}";
	if (types.size() == rect_initializer_prefix.size() + 3 &&
		types.compare(0, rect_initializer_prefix.size(),
			rect_initializer_prefix) == 0 &&
		std::string_view("QqIiLl").find(types[rect_initializer_prefix.size()]) !=
			std::string_view::npos &&
		std::string_view("QqIiLl").find(types[rect_initializer_prefix.size() + 1]) !=
			std::string_view::npos &&
		std::string_view("cCB").find(types[rect_initializer_prefix.size() + 2]) !=
			std::string_view::npos &&
		std::strcmp(sel_getName(selector),
			"initWithContentRect:styleMask:backing:defer:") == 0) {
		if (frame->stack_arguments == nullptr) return 0;
		DarlingObjcRect content_rect{};
		std::memcpy(&content_rect, frame->stack_arguments,
			sizeof(content_rect));
		const std::uint64_t style_mask = frame->gp[argument_index];
		const std::uint64_t backing = frame->gp[argument_index + 1];
		const std::uint64_t defer = frame->gp[argument_index + 2];
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, DarlingObjcRect, std::uint64_t,
				std::uint64_t, std::uint64_t>(imp, method.guest_abi, receiver,
				selector, content_rect, style_mask, backing, defer));
	}

	if (types == "@@:") {
		if (method.guest_abi) {
			using Function = id (__attribute__((sysv_abi)) *)(id, SEL);
			return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector));
		}
		using Function = id (*)(id, SEL);
		return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector));
	}
	if (types == "@@:Q") {
		const std::uint64_t argument = frame->gp[argument_index];
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, std::uint64_t>(imp, method.guest_abi,
				receiver, selector, argument));
	}
	if (types == "@@:i") {
		const int argument = static_cast<int>(frame->gp[argument_index]);
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, int>(imp, method.guest_abi, receiver,
				selector, argument));
	}
	if (types == "@@:q") {
		const auto argument = static_cast<std::int64_t>(frame->gp[argument_index]);
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, std::int64_t>(imp, method.guest_abi,
				receiver, selector, argument));
	}
	if (types == "@@:B") {
		const bool argument = (frame->gp[argument_index] & 0xffu) != 0;
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, bool>(imp, method.guest_abi, receiver,
				selector, argument));
	}
	if (types == "@@:d") {
		double argument = 0;
		std::memcpy(&argument, frame->xmm[0], sizeof(argument));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double>(imp, method.guest_abi, receiver,
				selector, argument));
	}
	if (types == "@@:@d") {
		const id name = reinterpret_cast<id>(frame->gp[argument_index]);
		double point_size = 0.0;
		std::memcpy(&point_size, frame->xmm[0], sizeof(point_size));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, id, double>(imp, method.guest_abi,
				receiver, selector, name, point_size));
	}
	if (types == "@@:d@") {
		double fraction = 0.0;
		std::memcpy(&fraction, frame->xmm[0], sizeof(fraction));
		const id other_color = reinterpret_cast<id>(frame->gp[argument_index]);
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double, id>(imp, method.guest_abi,
				receiver, selector, fraction, other_color));
	}
	if (types == "@@:d@::@B") {
		double interval = 0.0;
		std::memcpy(&interval, frame->xmm[0], sizeof(interval));
		const id target = reinterpret_cast<id>(frame->gp[argument_index]);
		const SEL timer_selector = reinterpret_cast<SEL>(
			frame->gp[argument_index + 1]);
		const id user_info = reinterpret_cast<id>(
			frame->gp[argument_index + 2]);
		const bool repeats =
			(frame->gp[argument_index + 3] & 0xffu) != 0;
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double, id, SEL, id, bool>(imp,
				method.guest_abi, receiver, selector, interval, target,
				timer_selector, user_info, repeats));
	}
	if (types == "*@:") {
		const auto result = InvokeObjcImplementation<const char*>(imp,
			method.guest_abi, receiver, selector);
		return reinterpret_cast<std::uintptr_t>(result);
	}
	if (types == "^v@:") {
		const auto result = InvokeObjcImplementation<void*>(imp,
			method.guest_abi, receiver, selector);
		return reinterpret_cast<std::uintptr_t>(result);
	}
	if (types == "v@:^d") {
		auto* components = reinterpret_cast<double*>(frame->gp[argument_index]);
		InvokeObjcImplementation<void, double*>(imp, method.guest_abi, receiver,
			selector, components);
		return 0;
	}
	if (types == "v@:^dqd") {
		const auto* pattern = reinterpret_cast<const double*>(
			frame->gp[argument_index]);
		const auto count = static_cast<std::int64_t>(
			frame->gp[argument_index + 1]);
		double phase = 0;
		std::memcpy(&phase, frame->xmm[0], sizeof(phase));
		InvokeObjcImplementation<void, const double*, std::int64_t, double>(imp,
			method.guest_abi, receiver, selector, pattern, count, phase);
		return 0;
	}
	if (types == "v@:^d^q^d") {
		auto* pattern = reinterpret_cast<double*>(frame->gp[argument_index]);
		auto* count = reinterpret_cast<std::int64_t*>(
			frame->gp[argument_index + 1]);
		auto* phase = reinterpret_cast<double*>(
			frame->gp[argument_index + 2]);
		InvokeObjcImplementation<void, double*, std::int64_t*, double*>(imp,
			method.guest_abi, receiver, selector, pattern, count, phase);
		return 0;
	}
	if (types == "v@:") {
		if (method.guest_abi) {
			using Function = void (__attribute__((sysv_abi)) *)(id, SEL);
			reinterpret_cast<Function>(imp)(receiver, selector);
		} else {
			using Function = void (*)(id, SEL);
			reinterpret_cast<Function>(imp)(receiver, selector);
		}
		return 0;
	}
	if (types == "i@:") {
		if (method.guest_abi) {
			using Function = int (__attribute__((sysv_abi)) *)(id, SEL);
			return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(reinterpret_cast<Function>(imp)(receiver, selector)));
		}
		using Function = int (*)(id, SEL);
		return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(reinterpret_cast<Function>(imp)(receiver, selector)));
	}
	if (types == "q@:") {
		if (method.guest_abi) {
			using Function = std::int64_t (__attribute__((sysv_abi)) *)(id, SEL);
			return static_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector));
		}
		using Function = std::int64_t (*)(id, SEL);
		return static_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector));
	}
	if (types == "Q@:") {
		return static_cast<std::uintptr_t>(
			InvokeObjcImplementation<std::uint64_t>(imp, method.guest_abi,
				receiver, selector));
	}
	if (types == "B@:") {
		if (method.guest_abi) {
			using Function = bool (__attribute__((sysv_abi)) *)(id, SEL);
			return reinterpret_cast<Function>(imp)(receiver, selector) ? 1 : 0;
		}
		using Function = bool (*)(id, SEL);
		return reinterpret_cast<Function>(imp)(receiver, selector) ? 1 : 0;
	}
	if (types == "@@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		if (method.guest_abi) {
			using Function = id (__attribute__((sysv_abi)) *)(id, SEL, id);
			return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector, argument));
		}
		using Function = id (*)(id, SEL, id);
		return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector, argument));
	}
	if (types == "B@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		return InvokeObjcImplementation<bool, id>(imp, method.guest_abi,
			receiver, selector, argument) ? 1 : 0;
	}
	if (types == "B@:#") {
		const Class argument = reinterpret_cast<Class>(frame->gp[argument_index]);
		return InvokeObjcImplementation<bool, Class>(imp, method.guest_abi,
			receiver, selector, argument) ? 1 : 0;
	}
	if (types == "B@::") {
		const SEL argument = reinterpret_cast<SEL>(frame->gp[argument_index]);
		return InvokeObjcImplementation<bool, SEL>(imp, method.guest_abi,
			receiver, selector, argument) ? 1 : 0;
	}
	if (types == "B@:^v") {
		void* argument = reinterpret_cast<void*>(frame->gp[argument_index]);
		return InvokeObjcImplementation<bool, void*>(imp, method.guest_abi,
			receiver, selector, argument) ? 1 : 0;
	}
	if (types == "@@:^v") {
		void* argument = reinterpret_cast<void*>(frame->gp[argument_index]);
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, void*>(imp, method.guest_abi,
				receiver, selector, argument));
	}
	if (types == "@@:^vQ") {
		const auto* values = reinterpret_cast<const id*>(
			frame->gp[argument_index]);
		const std::uint64_t count = frame->gp[argument_index + 1];
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, const id*, std::uint64_t>(imp,
				method.guest_abi, receiver, selector, values, count));
	}
	if (types == "@@:^v^vQ") {
		const auto* values = reinterpret_cast<const id*>(
			frame->gp[argument_index]);
		const auto* keys = reinterpret_cast<const id*>(
			frame->gp[argument_index + 1]);
		const std::uint64_t count = frame->gp[argument_index + 2];
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, const id*, const id*, std::uint64_t>(imp,
				method.guest_abi, receiver, selector, values, keys, count));
	}
	if (types == "@@:*") {
		const char* argument = reinterpret_cast<const char*>(
			frame->gp[argument_index]);
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, const char*>(imp, method.guest_abi,
				receiver, selector, argument));
	}
	if (types == "v@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		if (method.guest_abi) {
			using Function = void (__attribute__((sysv_abi)) *)(id, SEL, id);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		} else {
			using Function = void (*)(id, SEL, id);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		}
		return 0;
	}
	if (types == "v@:@:@@") {
		const id observer = reinterpret_cast<id>(frame->gp[argument_index]);
		const SEL callback = reinterpret_cast<SEL>(frame->gp[argument_index + 1]);
		const id name = reinterpret_cast<id>(frame->gp[argument_index + 2]);
		const id object = reinterpret_cast<id>(frame->gp[argument_index + 3]);
		InvokeObjcImplementation<void, id, SEL, id, id>(imp, method.guest_abi,
			receiver, selector, observer, callback, name, object);
		return 0;
	}
	if (types == "v@::") {
		const SEL argument = reinterpret_cast<SEL>(frame->gp[argument_index]);
		InvokeObjcImplementation<void, SEL>(imp, method.guest_abi, receiver,
			selector, argument);
		return 0;
	}
	if (types == "v@:i") {
		const int argument = static_cast<int>(frame->gp[argument_index]);
		if (method.guest_abi) {
			using Function = void (__attribute__((sysv_abi)) *)(id, SEL, int);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		} else {
			using Function = void (*)(id, SEL, int);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		}
		return 0;
	}
	if (types == "v@:B" || types == "v@:c") {
		const bool argument = (frame->gp[argument_index] & 0xffu) != 0;
		InvokeObjcImplementation<void, bool>(imp, method.guest_abi, receiver,
			selector, argument);
		return 0;
	}
	if (types == "q@:q") {
		const auto argument = static_cast<std::int64_t>(frame->gp[argument_index]);
		if (method.guest_abi) {
			using Function = std::int64_t (__attribute__((sysv_abi)) *)(id, SEL, std::int64_t);
			return static_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector, argument));
		}
		using Function = std::int64_t (*)(id, SEL, std::int64_t);
		return static_cast<std::uintptr_t>(reinterpret_cast<Function>(imp)(receiver, selector, argument));
	}
	if (types == "q@:q^{CGPoint=dd}") {
		const auto index = static_cast<std::int64_t>(frame->gp[argument_index]);
		auto* points = reinterpret_cast<DarlingObjcPoint*>(
			frame->gp[argument_index + 1]);
		return static_cast<std::uintptr_t>(
			InvokeObjcImplementation<std::int64_t, std::int64_t,
				DarlingObjcPoint*>(imp, method.guest_abi, receiver, selector,
				index, points));
	}
	if (types == "q@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		return static_cast<std::uintptr_t>(InvokeObjcImplementation<std::int64_t, id>(
			imp, method.guest_abi, receiver, selector, argument));
	}
	if (types == "v@:q") {
		const auto argument = static_cast<std::int64_t>(frame->gp[argument_index]);
		if (method.guest_abi) {
			using Function = void (__attribute__((sysv_abi)) *)(id, SEL, std::int64_t);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		} else {
			using Function = void (*)(id, SEL, std::int64_t);
			reinterpret_cast<Function>(imp)(receiver, selector, argument);
		}
		return 0;
	}
	if (types == "v@:q@") {
		const auto integer_argument = static_cast<std::int64_t>(
			frame->gp[argument_index]);
		const id object_argument = reinterpret_cast<id>(
			frame->gp[argument_index + 1]);
		InvokeObjcImplementation<void, std::int64_t, id>(imp,
			method.guest_abi, receiver, selector, integer_argument,
			object_argument);
		return 0;
	}
	if (types == "v@:B@") {
		const bool boolean_argument =
			(frame->gp[argument_index] & 0xffu) != 0;
		const id object_argument = reinterpret_cast<id>(
			frame->gp[argument_index + 1]);
		InvokeObjcImplementation<void, bool, id>(imp, method.guest_abi,
			receiver, selector, boolean_argument, object_argument);
		return 0;
	}
	if (types == "v@:@Q") {
		const id object_argument = reinterpret_cast<id>(frame->gp[argument_index]);
		const std::uint64_t integer_argument = frame->gp[argument_index + 1];
		InvokeObjcImplementation<void, id, std::uint64_t>(imp,
			method.guest_abi, receiver, selector, object_argument,
			integer_argument);
		return 0;
	}
	if (types == "v@:Q") {
		InvokeObjcImplementation<void, std::uint64_t>(imp, method.guest_abi,
			receiver, selector, frame->gp[argument_index]);
		return 0;
	}
	if (types == "v@:Q@") {
		const std::uint64_t index = frame->gp[argument_index];
		const id value = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		InvokeObjcImplementation<void, std::uint64_t, id>(imp,
			method.guest_abi, receiver, selector, index, value);
		return 0;
	}
	if (types == "v@:^vQ") {
		const void* bytes = reinterpret_cast<const void*>(
			frame->gp[argument_index]);
		const std::uint64_t length = frame->gp[argument_index + 1];
		InvokeObjcImplementation<void, const void*, std::uint64_t>(imp,
			method.guest_abi, receiver, selector, bytes, length);
		return 0;
	}
	if (types == "v@:^{CGPoint=dd}q") {
		const auto* points = reinterpret_cast<const DarlingObjcPoint*>(
			frame->gp[argument_index]);
		const auto count = static_cast<std::int64_t>(
			frame->gp[argument_index + 1]);
		InvokeObjcImplementation<void, const DarlingObjcPoint*, std::int64_t>(
			imp, method.guest_abi, receiver, selector, points, count);
		return 0;
	}
	if (types == "B@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		return InvokeObjcImplementation<bool>(imp, method.guest_abi, receiver,
			selector, argument) ? 1 : 0;
	}
	if (types == "B@:@@") {
		const id first = reinterpret_cast<id>(frame->gp[argument_index]);
		const id second = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		return InvokeObjcImplementation<bool>(imp, method.guest_abi, receiver,
			selector, first, second) ? 1 : 0;
	}
	if (types == "@@:@@") {
		const id first = reinterpret_cast<id>(frame->gp[argument_index]);
		const id second = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		return reinterpret_cast<std::uintptr_t>(InvokeObjcImplementation<id>(imp,
			method.guest_abi, receiver, selector, first, second));
	}
	if (types == "@@:@@@") {
		const id first = reinterpret_cast<id>(frame->gp[argument_index]);
		const id second = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		const id third = reinterpret_cast<id>(frame->gp[argument_index + 2]);
		return reinterpret_cast<std::uintptr_t>(InvokeObjcImplementation<id>(imp,
			method.guest_abi, receiver, selector, first, second, third));
	}
	if (types == "@@:@@:") {
		const id title = reinterpret_cast<id>(frame->gp[argument_index]);
		const id target = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		const SEL action = reinterpret_cast<SEL>(frame->gp[argument_index + 2]);
		return reinterpret_cast<std::uintptr_t>(InvokeObjcImplementation<id,
			id, id, SEL>(imp, method.guest_abi, receiver, selector, title, target,
			action));
	}
	if (types == "@@:dddd") {
		double values[4]{};
		for (std::size_t index = 0; index < 4; ++index)
			std::memcpy(&values[index], frame->xmm[index], sizeof(double));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double, double, double, double>(
				imp, method.guest_abi, receiver, selector, values[0], values[1],
				values[2], values[3]));
	}
	if (types == "@@:ddddd") {
		double values[5]{};
		for (std::size_t index = 0; index < 5; ++index)
			std::memcpy(&values[index], frame->xmm[index], sizeof(double));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double, double, double, double, double>(
				imp, method.guest_abi, receiver, selector, values[0], values[1],
				values[2], values[3], values[4]));
	}
	if (types == "@@:dd") {
		double values[2]{};
		std::memcpy(&values[0], frame->xmm[0], sizeof(double));
		std::memcpy(&values[1], frame->xmm[1], sizeof(double));
		return reinterpret_cast<std::uintptr_t>(
			InvokeObjcImplementation<id, double, double>(imp, method.guest_abi,
				receiver, selector, values[0], values[1]));
	}
	if (types == "v@:@@") {
		const id first = reinterpret_cast<id>(frame->gp[argument_index]);
		const id second = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		InvokeObjcImplementation<void>(imp, method.guest_abi, receiver, selector,
			first, second);
		return 0;
	}
	if (types == "v@:@@@") {
		const id first = reinterpret_cast<id>(frame->gp[argument_index]);
		const id second = reinterpret_cast<id>(frame->gp[argument_index + 1]);
		const id third = reinterpret_cast<id>(frame->gp[argument_index + 2]);
		InvokeObjcImplementation<void, id, id, id>(imp, method.guest_abi,
			receiver, selector, first, second, third);
		return 0;
	}
	if (types == "q@:qq") {
		const auto first = static_cast<std::int64_t>(frame->gp[argument_index]);
		const auto second = static_cast<std::int64_t>(frame->gp[argument_index + 1]);
		return static_cast<std::uintptr_t>(InvokeObjcImplementation<std::int64_t>(
			imp, method.guest_abi, receiver, selector, first, second));
	}
	if (types == "i@:i") {
		const int argument = static_cast<int>(frame->gp[argument_index]);
		return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(
			InvokeObjcImplementation<int>(imp, method.guest_abi, receiver,
				selector, argument)));
	}
	if (types == "d@:") {
		const double result = InvokeObjcImplementation<double>(imp, method.guest_abi,
			receiver, selector);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "d@:@") {
		const id argument = reinterpret_cast<id>(frame->gp[argument_index]);
		const double result = InvokeObjcImplementation<double, id>(imp,
			method.guest_abi, receiver, selector, argument);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "B@:N^dN^dN^dN^d" ||
		types == "B@:^d^d^d^d" ||
		types == "B@:o^do^do^do^d" ||
		types == "c@:N^dN^dN^dN^d" ||
		types == "c@:^d^d^d^d" ||
		types == "c@:o^do^do^do^d") {
		double* components[4]{};
		for (std::size_t index = 0; index < 4; ++index)
			components[index] = reinterpret_cast<double*>(
				frame->gp[argument_index + index]);
		return static_cast<std::uintptr_t>(
			InvokeObjcImplementation<bool, double*, double*, double*, double*>(
				imp, method.guest_abi, receiver, selector, components[0],
				components[1], components[2], components[3]));
	}
	if (types == "B@:o^do^do^do^do^d") {
		if (frame->stack_arguments == nullptr) return 0;
		double* components[5]{};
		for (std::size_t index = 0; index < 4; ++index)
			components[index] = reinterpret_cast<double*>(
				frame->gp[argument_index + index]);
		components[4] = reinterpret_cast<double*>(frame->stack_arguments[0]);
		return static_cast<std::uintptr_t>(
			InvokeObjcImplementation<bool, double*, double*, double*, double*, double*>(
				imp, method.guest_abi, receiver, selector, components[0],
				components[1], components[2], components[3], components[4]));
	}
	if (types == "B@:o^do^d" || types == "B@:N^dN^d" ||
		types == "B@:^d^d") {
		double* white = reinterpret_cast<double*>(frame->gp[argument_index]);
		double* alpha = reinterpret_cast<double*>(frame->gp[argument_index + 1]);
		return static_cast<std::uintptr_t>(
			InvokeObjcImplementation<bool, double*, double*>(imp,
				method.guest_abi, receiver, selector, white, alpha));
	}
	if (types == "f@:") {
		const float result = InvokeObjcImplementation<float>(imp, method.guest_abi,
			receiver, selector);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "d@:d") {
		double argument = 0;
		std::memcpy(&argument, frame->xmm[0], sizeof(argument));
		const double result = InvokeObjcImplementation<double>(imp, method.guest_abi,
			receiver, selector, argument);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:d") {
		double argument = 0;
		std::memcpy(&argument, frame->xmm[0], sizeof(argument));
		InvokeObjcImplementation<void>(imp, method.guest_abi, receiver, selector,
			argument);
		return 0;
	}
	if (types == "d@:dd") {
		double first = 0, second = 0;
		std::memcpy(&first, frame->xmm[0], sizeof(first));
		std::memcpy(&second, frame->xmm[1], sizeof(second));
		const double result = InvokeObjcImplementation<double>(imp, method.guest_abi,
			receiver, selector, first, second);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:dd") {
		double first = 0, second = 0;
		std::memcpy(&first, frame->xmm[0], sizeof(first));
		std::memcpy(&second, frame->xmm[1], sizeof(second));
		InvokeObjcImplementation<void>(imp, method.guest_abi, receiver, selector,
			first, second);
		return 0;
	}
	if (types == "v@:d@") {
		double value = 0.0;
		std::memcpy(&value, frame->xmm[0], sizeof(value));
		const id key = reinterpret_cast<id>(frame->gp[argument_index]);
		InvokeObjcImplementation<void, double, id>(imp, method.guest_abi,
			receiver, selector, value, key);
		return 0;
	}
	if (types == "f@:f") {
		float argument = 0;
		std::memcpy(&argument, frame->xmm[0], sizeof(argument));
		const float result = InvokeObjcImplementation<float>(imp, method.guest_abi,
			receiver, selector, argument);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:f") {
		float argument = 0;
		std::memcpy(&argument, frame->xmm[0], sizeof(argument));
		InvokeObjcImplementation<void>(imp, method.guest_abi, receiver, selector,
			argument);
		return 0;
	}
	if (types == "f@:ff") {
		float first = 0, second = 0;
		std::memcpy(&first, frame->xmm[0], sizeof(first));
		std::memcpy(&second, frame->xmm[1], sizeof(second));
		const float result = InvokeObjcImplementation<float>(imp, method.guest_abi,
			receiver, selector, first, second);
		std::memcpy(frame->xmm_return, &result, sizeof(result));
		return 0;
	}
	if (types == "v@:ff") {
		float first = 0, second = 0;
		std::memcpy(&first, frame->xmm[0], sizeof(first));
		std::memcpy(&second, frame->xmm[1], sizeof(second));
		InvokeObjcImplementation<void>(imp, method.guest_abi, receiver, selector,
			first, second);
		return 0;
	}
	if (method.guest_abi) {
		ObjcScalarKind return_kind = ObjcScalarKind::Unsupported;
		const auto stack_bytes = GenericGuestStackArgumentBytes(types,
			return_kind);
		if (stack_bytes && *stack_bytes <= 512 &&
			(*stack_bytes == 0 || frame->stack_arguments != nullptr)) {
			const auto result = darling_windows_objc_invoke_sysv_raw(frame, imp,
				*stack_bytes);
			if (return_kind == ObjcScalarKind::Floating) return 0;
			return return_kind == ObjcScalarKind::Void ? 0 : result;
		}
	}
	return 0;
}
}

extern "C" std::uintptr_t darling_windows_objc_msgSend_sysv_dispatch(
	DarlingSysVObjcCallFrame* frame)
{
	return DispatchSysVObjcCall(frame, 0);
}

extern "C" std::uintptr_t darling_windows_objc_msgSendSuper_sysv_dispatch(
	DarlingSysVObjcCallFrame* frame)
{
	return DispatchSysVObjcCall(frame, 1);
}

extern "C" std::uintptr_t darling_windows_objc_msgSendSuper2_sysv_dispatch(
	DarlingSysVObjcCallFrame* frame)
{
	return DispatchSysVObjcCall(frame, 2);
}
#endif

std::uintptr_t NormalizeImagePointer(std::uintptr_t address,
	std::intptr_t image_slide, std::uintptr_t image_vm_begin,
	std::uintptr_t image_vm_end)
{
	if (image_vm_begin <= address && address < image_vm_end)
		return static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(address) + image_slide);
	return address;
}

void RegisterMethodList(Class cls, std::uintptr_t method_list_address,
	bool class_method = false, std::intptr_t image_slide = 0,
	std::uintptr_t image_vm_begin = 0, std::uintptr_t image_vm_end = 0)
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
	const bool relative_list = (entry_size & 0x80000000u) != 0;
	entry_size &= 0x0000ffffu;
	const std::uint32_t minimum_entry_size = relative_list ? 12u :
		static_cast<std::uint32_t>(sizeof(std::uintptr_t) * 3);
	if (entry_size < minimum_entry_size || method_count > 65536)
		return;
	for (std::uint32_t method_index = 0; method_index < method_count;
		++method_index) {
		const auto method_address = method_list_address + 8ull +
			static_cast<std::uint64_t>(method_index) * entry_size;
		std::uintptr_t name_address = 0;
		std::uintptr_t type_address = 0;
		std::uintptr_t implementation = 0;
		if (relative_list) {
			std::int32_t name_offset = 0;
			std::int32_t type_offset = 0;
			std::int32_t implementation_offset = 0;
			std::memcpy(&name_offset, reinterpret_cast<const void*>(method_address), 4);
			std::memcpy(&type_offset, reinterpret_cast<const void*>(method_address + 4), 4);
			std::memcpy(&implementation_offset,
				reinterpret_cast<const void*>(method_address + 8), 4);
			name_address = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(method_address) + name_offset);
			type_address = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(method_address + 4) + type_offset);
			implementation = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(method_address + 8) + implementation_offset);
			// Modern clang emits selector-reference-relative names unless the
			// direct-selector flag is set on the method-list header.
			std::uint32_t list_flags = 0;
			std::memcpy(&list_flags, reinterpret_cast<const void*>(method_list_address), 4);
			if ((list_flags & 0x40000000u) == 0) {
				std::uintptr_t selector_name = 0;
				std::memcpy(&selector_name, reinterpret_cast<const void*>(name_address),
					sizeof(selector_name));
				selector_name = NormalizeImagePointer(selector_name, image_slide,
					image_vm_begin, image_vm_end);
				name_address = selector_name;
			}
		} else {
			std::memcpy(&name_address, reinterpret_cast<const void*>(method_address),
				sizeof(name_address));
			std::memcpy(&type_address,
				reinterpret_cast<const std::uint8_t*>(method_address) +
					sizeof(std::uintptr_t), sizeof(type_address));
			std::memcpy(&implementation,
				reinterpret_cast<const std::uint8_t*>(method_address) +
					sizeof(std::uintptr_t) * 2, sizeof(implementation));
		}
		const auto* name = reinterpret_cast<const char*>(name_address);
		const auto* types = reinterpret_cast<const char*>(type_address);
		if (!name || !types || !implementation || strnlen_s(name, 1024) == 1024 ||
			strnlen_s(types, 1024) == 1024)
			continue;
		const SEL selector = sel_registerName(name);
		const bool added = class_method ?
			class_addClassMethod(cls, selector, reinterpret_cast<IMP>(implementation), types) :
			class_addMethod(cls, selector, reinterpret_cast<IMP>(implementation), types);
		if (added) {
			std::lock_guard lock(RuntimeMutex());
			auto& methods = class_method ? cls->class_methods : cls->methods;
			const auto method = methods.find(selector);
			if (method != methods.end()) {
				method->second.guest_abi = true;
				if (class_method && cls->metaclass)
					cls->metaclass->methods[selector] = method->second;
			}
		}
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
	const void* classlist, std::size_t bytes, std::intptr_t image_slide,
	std::uintptr_t image_vm_begin, std::uintptr_t image_vm_end)
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
			RegisterMethodList(cls, method_list_address, false, image_slide,
				image_vm_begin, image_vm_end);
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
					RegisterMethodList(cls, meta_methods, true, image_slide,
						image_vm_begin, image_vm_end);
				}
			}
		}
	}
	return registered;
}

extern "C" std::size_t darling_objc_register_macho_selrefs(
	const void* selrefs, std::size_t bytes, std::intptr_t image_slide,
	std::uintptr_t image_vm_begin, std::uintptr_t image_vm_end)
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
		name_address = NormalizeImagePointer(name_address, image_slide,
			image_vm_begin, image_vm_end);
		const auto* name = reinterpret_cast<const char*>(name_address);
		if (!name || name[0] == '\0' || strnlen_s(name, 1024) == 1024)
			continue;
		if (sel_registerName(name))
			++registered;
	}
	return registered;
}

extern "C" std::size_t darling_objc_register_macho_constant_strings(
	const void* strings, std::size_t bytes)
{
	// Clang emits each x86_64 Darwin __cfstring record as
	// { isa, uint32 flags, padding, bytes pointer, uint64 length }.
	constexpr std::size_t record_size = 32;
	if (strings == nullptr || sizeof(std::uintptr_t) != 8 ||
		bytes < record_size || bytes % record_size != 0 || active_image_token == nullptr)
		return 0;
	const auto count = bytes / record_size;
	std::lock_guard lock(RuntimeMutex());
	for (std::size_t index = 0; index < count; ++index) {
		auto* object = reinterpret_cast<id>(
			const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(strings)) +
				index * record_size);
		ConstantStrings().insert(object);
		active_image_token->constant_strings.push_back(object);
	}
	return count;
}

extern "C" bool darling_objc_is_constant_string(id object)
{
	if (object == nullptr) return false;
	std::lock_guard lock(RuntimeMutex());
	return ConstantStrings().contains(object);
}

extern "C" bool darling_objc_register_object_finalizer(id object,
	darling_objc_object_finalizer finalizer)
{
	if (object == nullptr || finalizer == nullptr ||
		darling_objc_is_constant_string(object))
		return false;
	std::lock_guard lock(RuntimeMutex());
	return ObjectFinalizers.emplace(object, finalizer).second;
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
	for (id object : token->constant_strings)
		ConstantStrings().erase(object);
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
				selector, method->second, true);
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
	if (first->types != second->types)
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
	if (darling_objc_is_constant_string(object))
		return object;
	if (object)
		object->retain_count.fetch_add(1, std::memory_order_relaxed);
	return object;
}

extern "C" void objc_release(id object)
{
	if (!object || darling_objc_is_constant_string(object))
		return;
	std::vector<id> associated;
	darling_objc_object_finalizer finalizer = nullptr;
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
		const auto finalizer_entry = ObjectFinalizers.find(object);
		if (finalizer_entry != ObjectFinalizers.end()) {
			finalizer = finalizer_entry->second;
			ObjectFinalizers.erase(finalizer_entry);
		}
	}
	for (id value : associated)
		objc_release(value);
	if (finalizer != nullptr)
		finalizer(object);
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

extern "C" id objc_autoreleaseReturnValue(id object)
{
	return objc_autorelease(object);
}

extern "C" id objc_retainAutorelease(id object)
{
	if (object)
		objc_retain(object);
	return objc_autorelease(object);
}

extern "C" id objc_retainAutoreleaseReturnValue(id object)
{
	return objc_retainAutorelease(object);
}

extern "C" id objc_unsafeClaimAutoreleasedReturnValue(id object)
{
	return object;
}

extern "C" id objc_opt_self(id object)
{
	return object;
}

extern "C" Class objc_opt_class(id object)
{
	return object_getClass(object);
}

extern "C" bool objc_opt_respondsToSelector(id object, SEL selector)
{
	return object != nullptr && selector != nullptr &&
		class_respondsToSelector(object_getClass(object), selector);
}

extern "C" bool objc_opt_isKindOfClass(id object, Class cls)
{
	if (object == nullptr || cls == nullptr)
		return false;
	for (Class current = object_getClass(object); current != nullptr;
		current = class_getSuperclass(current)) {
		if (current == cls)
			return true;
	}
	return false;
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

namespace {
struct DarlingWindowsVarargs {
	va_list* cursor = nullptr;
	template<typename T> T Next() { return va_arg(*cursor, T); }
};

struct DarlingArm64GuestVarargs {
	const std::uint8_t* cursor = nullptr;

	template<typename T>
	T Next()
	{
		const auto address = reinterpret_cast<std::uintptr_t>(cursor);
		const auto alignment = std::max<std::uintptr_t>(8, alignof(T));
		const auto aligned = (address + alignment - 1) & ~(alignment - 1);
		T value{};
		std::memcpy(&value, reinterpret_cast<const void*>(aligned), sizeof(T));
		cursor = reinterpret_cast<const std::uint8_t*>(aligned +
			((sizeof(T) + 7) & ~std::size_t{7}));
		return value;
	}
};

std::string CanonicalObjcDispatchEncoding(const char* encoding)
{
	std::string result;
	if (!encoding) return result;
	bool quoted = false;
	for (const unsigned char value : std::string_view(encoding)) {
		if (value == '"') quoted = !quoted;
		if (!quoted && value >= '0' && value <= '9') continue;
		result.push_back(static_cast<char>(value));
	}
	return result;
}
}
#define DARLING_OBJC_VA_LIST(name, stack) auto name = varargs
#define DARLING_OBJC_VA_ARG(list, type) (list).template Next<type>()
#define DARLING_OBJC_VA_END(list) ((void)0)
template<typename VarargsReader>
id DarlingObjcMsgSendDispatch(id receiver, SEL selector,
	VarargsReader& varargs)
{
	if (!receiver || !selector)
		return nullptr;
	// Class objects are represented by the runtime's Class storage, not by an
	// objc_object header. Resolve their class-method encoding before reading
	// receiver->isa, then dispatch the no-argument forms used by +alloc and
	// singleton accessors through the dedicated class-method table.
	if (object_isClass(receiver)) {
		const Class cls = reinterpret_cast<Class>(receiver);
		const Method method = class_getClassMethod(cls, selector);
		const std::string class_encoding = CanonicalObjcDispatchEncoding(
			method_getTypeEncoding(method));
		const char* class_types = class_encoding.c_str();
		const IMP implementation = method_getImplementation(method);
		if (!class_types || !implementation) return nullptr;
		if (std::strcmp(class_types, "@@:") == 0)
			return darling_objc_msgSend_class0(cls, selector);
		if (std::strcmp(class_types, "@@:@@:") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const id title = DARLING_OBJC_VA_ARG(class_arguments, id);
			const id target = DARLING_OBJC_VA_ARG(class_arguments, id);
			const SEL action = DARLING_OBJC_VA_ARG(class_arguments, SEL);
			DARLING_OBJC_VA_END(class_arguments);
			using ClassFactory = id (*)(Class, SEL, id, id, SEL);
			return reinterpret_cast<ClassFactory>(implementation)(cls, selector,
				title, target, action);
		}
		if (std::strcmp(class_types, "@@:d") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const double point_size = DARLING_OBJC_VA_ARG(class_arguments, double);
			DARLING_OBJC_VA_END(class_arguments);
			using ClassFontFactory = id (*)(Class, SEL, double);
			return reinterpret_cast<ClassFontFactory>(implementation)(cls,
				selector, point_size);
		}
		if (std::strcmp(class_types, "@@:dddd") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const double first = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double second = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double third = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double fourth = DARLING_OBJC_VA_ARG(class_arguments, double);
			DARLING_OBJC_VA_END(class_arguments);
			using ClassColorFactory = id (*)(Class, SEL, double, double,
				double, double);
			return reinterpret_cast<ClassColorFactory>(implementation)(cls,
				selector, first, second, third, fourth);
		}
		if (std::strcmp(class_types, "@@:@d") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const id name = DARLING_OBJC_VA_ARG(class_arguments, id);
			const double point_size = DARLING_OBJC_VA_ARG(class_arguments, double);
			DARLING_OBJC_VA_END(class_arguments);
			using ClassFontFactory = id (*)(Class, SEL, id, double);
			return reinterpret_cast<ClassFontFactory>(implementation)(cls,
				selector, name, point_size);
		}
		if (std::strcmp(class_types, "@@:d@::@B") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const double interval = DARLING_OBJC_VA_ARG(class_arguments, double);
			const id target = DARLING_OBJC_VA_ARG(class_arguments, id);
			const SEL timer_selector = DARLING_OBJC_VA_ARG(class_arguments, SEL);
			const id user_info = DARLING_OBJC_VA_ARG(class_arguments, id);
			const bool repeats = DARLING_OBJC_VA_ARG(class_arguments, int) != 0;
			DARLING_OBJC_VA_END(class_arguments);
			using ClassTimerFactory = id (*)(Class, SEL, double, id, SEL, id, bool);
			return reinterpret_cast<ClassTimerFactory>(implementation)(cls,
				selector, interval, target, timer_selector, user_info, repeats);
		}
		if (std::strcmp(class_types, "@@:ddddd") == 0) {
			DARLING_OBJC_VA_LIST(class_arguments, darwin_arguments);
			const double cyan = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double magenta = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double yellow = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double black = DARLING_OBJC_VA_ARG(class_arguments, double);
			const double alpha = DARLING_OBJC_VA_ARG(class_arguments, double);
			DARLING_OBJC_VA_END(class_arguments);
			using ClassColorFactory = id (*)(Class, SEL, double, double, double,
				double, double);
			return reinterpret_cast<ClassColorFactory>(implementation)(cls,
				selector, cyan, magenta, yellow, black, alpha);
		}
		if (std::strcmp(class_types, "v@:") == 0) {
			reinterpret_cast<void (*)(Class, SEL)>(implementation)(cls, selector);
			return nullptr;
		}
		if (std::strcmp(class_types, "i@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<int (*)(Class, SEL)>(implementation)(cls, selector)));
		if (std::strcmp(class_types, "q@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::int64_t (*)(Class, SEL)>(implementation)(cls, selector)));
		if (std::strcmp(class_types, "B@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<bool (*)(Class, SEL)>(implementation)(cls, selector)));
		if (std::strcmp(class_types, "S@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::uint16_t (*)(Class, SEL)>(implementation)(cls, selector)));
		if (std::strcmp(class_types, "s@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<std::int16_t (*)(Class, SEL)>(implementation)(cls, selector)));
		if (std::strcmp(class_types, "^v@:") == 0 ||
			std::strcmp(class_types, "*@:") == 0)
			return reinterpret_cast<id>(reinterpret_cast<void* (*)(Class, SEL)>(
				implementation)(cls, selector));
		return nullptr;
	}
	const char* raw_types = class_getMethodTypeEncoding(receiver->isa, selector);
	if (!raw_types)
		return nullptr;
	const std::string encoding = CanonicalObjcDispatchEncoding(raw_types);
	const char* types = encoding.c_str();
	if (std::strcmp(types, "@@:") == 0)
		return Dispatch(receiver, receiver->isa, selector);
	if (std::strcmp(types, "v@:") == 0) {
		darling_objc_msgSend_void0(receiver, selector);
		return nullptr;
	}
	if (std::strcmp(types, "i@:") == 0 || std::strcmp(types, "q@:") == 0 ||
		std::strcmp(types, "B@:") == 0 || std::strcmp(types, "S@:") == 0 ||
		std::strcmp(types, "s@:") == 0 || std::strcmp(types, "^v@:") == 0 ||
		std::strcmp(types, "*@:") == 0) {
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
		if (std::strcmp(types, "S@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::uint16_t (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "s@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<std::int16_t (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "*@:") == 0)
			return reinterpret_cast<id>(const_cast<char*>(
				reinterpret_cast<const char* (*)(id, SEL)>(implementation)(
					receiver, selector)));
		return reinterpret_cast<id>(reinterpret_cast<void* (*)(id, SEL)>(implementation)(
			receiver, selector));
	}
	DARLING_OBJC_VA_LIST(arguments, darwin_arguments);
	id result = nullptr;
	if (std::strcmp(types, "@@:@") == 0) {
		result = darling_objc_msgSend_object1(receiver, selector,
			DARLING_OBJC_VA_ARG(arguments, id));
	} else if (std::strcmp(types,
		"@@:{CGRect={CGPoint=dd}{CGSize=dd}}") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using RectMethod = id (*)(id, SEL, DarlingObjcRect);
		const auto rect = DARLING_OBJC_VA_ARG(arguments, DarlingObjcRect);
		result = implementation ? reinterpret_cast<RectMethod>(implementation)(
			receiver, selector, rect) : nullptr;
	} else if (std::strcmp(types, "@@:@@") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using ObjectMethod = id (*)(id, SEL, id, id);
		const auto first = DARLING_OBJC_VA_ARG(arguments, id);
		const auto second = DARLING_OBJC_VA_ARG(arguments, id);
		result = implementation ? reinterpret_cast<ObjectMethod>(implementation)(
			receiver, selector, first, second) : nullptr;
	} else if (std::strcmp(types, "q@:q") == 0) {
		const auto value = darling_objc_msgSend_int64_1(receiver, selector,
			DARLING_OBJC_VA_ARG(arguments, std::int64_t));
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "q@:qq") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t,
			std::int64_t);
		const auto first = DARLING_OBJC_VA_ARG(arguments, std::int64_t);
		const auto second = DARLING_OBJC_VA_ARG(arguments, std::int64_t);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				first, second) : 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "i@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = int (*)(id, SEL, int);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				DARLING_OBJC_VA_ARG(arguments, int)) : 0;
		result = reinterpret_cast<id>(static_cast<std::intptr_t>(value));
	} else if (std::strcmp(types, "q@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, int);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				DARLING_OBJC_VA_ARG(arguments, int)) : 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(value));
	} else if (std::strcmp(types, "i@:q") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using IntegerMethod = int (*)(id, SEL, std::int64_t);
		const auto value = implementation ?
			reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
				DARLING_OBJC_VA_ARG(arguments, std::int64_t)) : 0;
		result = reinterpret_cast<id>(static_cast<std::intptr_t>(value));
	} else if (std::strcmp(types, "v@:i") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using VoidMethod = void (*)(id, SEL, int);
		if (implementation)
			reinterpret_cast<VoidMethod>(implementation)(receiver, selector,
				DARLING_OBJC_VA_ARG(arguments, int));
	} else if (std::strcmp(types, "v@:q") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using VoidMethod = void (*)(id, SEL, std::int64_t);
		if (implementation)
			reinterpret_cast<VoidMethod>(implementation)(receiver, selector,
				DARLING_OBJC_VA_ARG(arguments, std::int64_t));
	} else if (std::strcmp(types, "v@:^{CGPoint=dd}q") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using PointsMethod = void (*)(id, SEL, const DarlingObjcPoint*,
			std::int64_t);
		const auto* points = DARLING_OBJC_VA_ARG(arguments,
			const DarlingObjcPoint*);
		const auto count = DARLING_OBJC_VA_ARG(arguments, std::int64_t);
		if (implementation)
			reinterpret_cast<PointsMethod>(implementation)(receiver, selector,
				points, count);
	} else if (std::strcmp(types, "v@:@") == 0) {
		darling_objc_msgSend_void1_object(receiver, selector,
			DARLING_OBJC_VA_ARG(arguments, id));
	} else if (std::strcmp(types,
		"v@:{CGRect={CGPoint=dd}{CGSize=dd}}") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using RectMethod = void (*)(id, SEL, DarlingObjcRect);
		const auto rect = DARLING_OBJC_VA_ARG(arguments, DarlingObjcRect);
		if (implementation)
			reinterpret_cast<RectMethod>(implementation)(receiver, selector, rect);
	} else if (std::strcmp(types, "v@::") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using SelectorMethod = void (*)(id, SEL, SEL);
		const auto argument = DARLING_OBJC_VA_ARG(arguments, SEL);
		if (implementation)
			reinterpret_cast<SelectorMethod>(implementation)(receiver, selector,
				argument);
	} else if (std::strcmp(types, "v@:B") == 0 ||
		std::strcmp(types, "v@:c") == 0) {
		const IMP implementation = FindMethod(receiver, selector);
		using BoolMethod = void (*)(id, SEL, bool);
		const bool argument = DARLING_OBJC_VA_ARG(arguments, int) != 0;
		if (implementation)
			reinterpret_cast<BoolMethod>(implementation)(receiver, selector,
				argument);
	} else if (std::strcmp(types, "B@:B") == 0) {
		const bool value = DARLING_OBJC_VA_ARG(arguments, int) != 0;
		result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
			darling_objc_invoke1(receiver, selector,
				DarlingObjcValue{DARLING_OBJC_BOOL, {.boolean = value}}).boolean));
	} else if (std::strcmp(types, "^v@:^v") == 0) {
		const auto value = darling_objc_invoke1(receiver, selector,
			DarlingObjcValue{DARLING_OBJC_POINTER,
				{.pointer = DARLING_OBJC_VA_ARG(arguments, void*)}});
		result = reinterpret_cast<id>(value.pointer);
	} else if (std::strcmp(types, "@@:@?") == 0) {
		result = darling_objc_invoke1(receiver, selector,
			DarlingObjcValue{DARLING_OBJC_BLOCK,
				{.block = DARLING_OBJC_VA_ARG(arguments, DarlingObjcCallbackBlock*)}}).object;
	}
	DARLING_OBJC_VA_END(arguments);
	return result;
}

extern "C" id objc_msgSend(id receiver, SEL selector, ...)
{
	va_list arguments;
	va_start(arguments, selector);
	DarlingWindowsVarargs reader{&arguments};
	const id result = DarlingObjcMsgSendDispatch(receiver, selector, reader);
	va_end(arguments);
	return result;
}

#undef DARLING_OBJC_VA_LIST
#undef DARLING_OBJC_VA_ARG
#undef DARLING_OBJC_VA_END

extern "C" void darling_objc_msgSend_stret(
	void* return_storage, id receiver, SEL selector, ...)
{
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	std::uintptr_t guest_view_argument = 0;
	__asm__ volatile("movq %%rcx, %0" : "=r"(guest_view_argument));
#endif
	if (return_storage == nullptr) return;
	if (receiver == nullptr || selector == nullptr) return;
	DarlingObjcOperatingSystemVersion version{};
	constexpr const char* version_encoding =
		"{NSOperatingSystemVersion=qqq}@:";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const bool class_method = object_isClass(receiver);
		const Class start = class_method ? reinterpret_cast<Class>(receiver) : receiver->isa;
		const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
			class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == version_encoding) {
			version = InvokeObjcImplementation<DarlingObjcOperatingSystemVersion>(
				method.implementation, method.guest_abi, receiver, selector);
			std::memcpy(return_storage, &version, sizeof(version));
			return;
		}
	}
#else
	{
		const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
		const IMP implementation = FindMethod(receiver, selector);
		if (implementation != nullptr && types != nullptr &&
			std::strcmp(types, version_encoding) == 0) {
			using VersionMethod = DarlingObjcOperatingSystemVersion (*)(id, SEL);
			version = reinterpret_cast<VersionMethod>(implementation)(receiver, selector);
			std::memcpy(return_storage, &version, sizeof(version));
			return;
		}
	}
#endif
	constexpr const char* affine_transform_encoding =
		"{NSAffineTransformStruct=dddddd}@:";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const bool class_method = object_isClass(receiver);
		const Class start = class_method ? reinterpret_cast<Class>(receiver) :
			receiver->isa;
		const auto method = LookupMethodSnapshot(start, selector, class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == affine_transform_encoding) {
			const auto value = InvokeObjcImplementation<DarlingObjcAffineTransform>(
				method.implementation, method.guest_abi, receiver, selector);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#else
	{
		const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
			selector);
		const IMP implementation = FindMethod(receiver, selector);
		if (implementation != nullptr && types != nullptr &&
			std::strcmp(types, affine_transform_encoding) == 0) {
			using TransformMethod = DarlingObjcAffineTransform (*)(id, SEL);
			const auto value = reinterpret_cast<TransformMethod>(implementation)(
				receiver, selector);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#endif
	constexpr const char* rect_transform_encoding =
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const bool class_method = object_isClass(receiver);
		const Class start = class_method ? reinterpret_cast<Class>(receiver) :
			receiver->isa;
		const auto method = LookupMethodSnapshot(start, selector, class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == rect_transform_encoding) {
			DarlingObjcRect rect{};
			// The SysV frame pointer is followed by the saved return address;
			// the first stack-passed argument starts 16 bytes above it.
			const auto* stack_arguments = reinterpret_cast<const std::uint8_t*>(
				__builtin_frame_address(0)) + 16;
			std::memcpy(&rect, stack_arguments, sizeof(rect));
			const auto value = InvokeObjcImplementation<DarlingObjcRect,
				DarlingObjcRect>(method.implementation, method.guest_abi, receiver,
				selector, rect);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#else
	{
		const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
			selector);
		const IMP implementation = FindMethod(receiver, selector);
		if (implementation != nullptr && types != nullptr &&
			std::strcmp(types, rect_transform_encoding) == 0) {
			va_list arguments;
			va_start(arguments, selector);
			const auto rect = va_arg(arguments, DarlingObjcRect);
			va_end(arguments);
			using RectMethod = DarlingObjcRect (*)(id, SEL, DarlingObjcRect);
			const auto value = reinterpret_cast<RectMethod>(implementation)(receiver,
				selector, rect);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#endif
	constexpr const char* rect_conversion_encoding =
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}@";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const bool class_method = object_isClass(receiver);
		const Class start = class_method ? reinterpret_cast<Class>(receiver) :
			receiver->isa;
		const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
			class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == rect_conversion_encoding) {
			DarlingObjcRect rect{};
			const auto* stack_arguments = reinterpret_cast<const std::uint8_t*>(
				__builtin_frame_address(0)) + 16;
			std::memcpy(&rect, stack_arguments, sizeof(rect));
			const auto other_view = reinterpret_cast<id>(guest_view_argument);
			const auto value = InvokeObjcImplementation<DarlingObjcRect,
				DarlingObjcRect, id>(method.implementation, method.guest_abi,
				receiver, selector, rect, other_view);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#else
	{
		const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
			selector);
		const IMP implementation = FindMethod(receiver, selector);
		if (implementation != nullptr && types != nullptr &&
			std::strcmp(types, rect_conversion_encoding) == 0) {
			va_list arguments;
			va_start(arguments, selector);
			const auto rect = va_arg(arguments, DarlingObjcRect);
			const auto other_view = va_arg(arguments, id);
			va_end(arguments);
			using RectMethod = DarlingObjcRect (*)(id, SEL, DarlingObjcRect, id);
			const auto value = reinterpret_cast<RectMethod>(implementation)(
				receiver, selector, rect, other_view);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#endif
	DarlingObjcRect result{};
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const bool class_method = object_isClass(receiver);
	const Class start = class_method ? reinterpret_cast<Class>(receiver) :
		receiver->isa;
	const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
		class_method);
	if (method.implementation == nullptr || CanonicalObjcEncoding(method.types) !=
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:") return;
	result = InvokeObjcImplementation<DarlingObjcRect>(
		method.implementation, method.guest_abi, receiver, selector);
#else
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	const IMP implementation = FindMethod(receiver, selector);
	if (implementation == nullptr || types == nullptr || std::strcmp(types,
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:") != 0) return;
	using RectMethod = DarlingObjcRect (*)(id, SEL);
	result = reinterpret_cast<RectMethod>(implementation)(receiver, selector);
#endif
	std::memcpy(return_storage, &result, sizeof(result));
}

namespace {
void DispatchSuperRectStret(void* return_storage, const void* super_info,
	SEL selector, bool super2, const std::uint8_t* stack_arguments,
	id guest_view_argument, va_list* guest_arguments)
{
	if (return_storage == nullptr || super_info == nullptr || selector == nullptr)
		return;
	const auto* super = static_cast<const DarlingObjcSuper*>(super_info);
	if (super->receiver == nullptr || super->super_class == nullptr) return;
	Class start = super2 ? super->super_class->superclass : super->super_class;
	if (start == nullptr) return;
	const bool class_method = object_isClass(super->receiver);
	constexpr const char* rect_conversion_encoding =
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}@";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
			class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == rect_conversion_encoding) {
			DarlingObjcRect rect{};
			id other_view = guest_view_argument;
			if (stack_arguments != nullptr) {
				std::memcpy(&rect, stack_arguments, sizeof(rect));
			} else if (guest_arguments != nullptr) {
				rect = va_arg(*guest_arguments, DarlingObjcRect);
				other_view = va_arg(*guest_arguments, id);
			} else return;
			const auto value = InvokeObjcImplementation<DarlingObjcRect,
				DarlingObjcRect, id>(method.implementation, method.guest_abi,
				super->receiver, selector, rect, other_view);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#else
	{
		const Method method = class_method ? class_getClassMethod(start, selector) :
			class_getInstanceMethod(start, selector);
		if (method != nullptr && method_getTypeEncoding(method) != nullptr &&
			std::strcmp(method_getTypeEncoding(method),
				rect_conversion_encoding) == 0) {
			DarlingObjcRect rect{};
			id other_view = guest_view_argument;
			if (stack_arguments != nullptr) {
				std::memcpy(&rect, stack_arguments, sizeof(rect));
			} else if (guest_arguments != nullptr) {
				rect = va_arg(*guest_arguments, DarlingObjcRect);
				other_view = va_arg(*guest_arguments, id);
			} else return;
			using RectMethod = DarlingObjcRect (*)(id, SEL, DarlingObjcRect, id);
			const auto value = reinterpret_cast<RectMethod>(
				method_getImplementation(method))(super->receiver, selector, rect,
				other_view);
			std::memcpy(return_storage, &value, sizeof(value));
			return;
		}
	}
#endif
	constexpr const char* version_encoding =
		"{NSOperatingSystemVersion=qqq}@:";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	{
		const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
			class_method);
		if (method.implementation != nullptr &&
			CanonicalObjcEncoding(method.types) == version_encoding) {
			const auto version = InvokeObjcImplementation<
				DarlingObjcOperatingSystemVersion>(method.implementation,
				method.guest_abi, super->receiver, selector);
			std::memcpy(return_storage, &version, sizeof(version));
			return;
		}
	}
#else
	{
		const Method method = class_method ? class_getClassMethod(start, selector) :
			class_getInstanceMethod(start, selector);
		if (method != nullptr && method_getTypeEncoding(method) != nullptr &&
			std::strcmp(method_getTypeEncoding(method), version_encoding) == 0) {
			using VersionMethod = DarlingObjcOperatingSystemVersion (*)(id, SEL);
			const auto version = reinterpret_cast<VersionMethod>(
				method_getImplementation(method))(super->receiver, selector);
			std::memcpy(return_storage, &version, sizeof(version));
			return;
		}
	}
#endif
	DarlingObjcRect result{};
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const ObjcMethodSnapshot method = LookupMethodSnapshot(start, selector,
		class_method);
	if (method.implementation == nullptr || CanonicalObjcEncoding(method.types) !=
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:") return;
	result = InvokeObjcImplementation<DarlingObjcRect>(method.implementation,
		method.guest_abi, super->receiver, selector);
#else
	const Method method = class_method ? class_getClassMethod(start, selector) :
		class_getInstanceMethod(start, selector);
	if (method == nullptr || method_getTypeEncoding(method) == nullptr ||
		std::strcmp(method_getTypeEncoding(method),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:") != 0) return;
	using RectMethod = DarlingObjcRect (*)(id, SEL);
	result = reinterpret_cast<RectMethod>(method_getImplementation(method))(
		super->receiver, selector);
#endif
	std::memcpy(return_storage, &result, sizeof(result));
}
}

extern "C" void darling_objc_msgSendSuper_stret(void* return_storage,
	void* super_info, SEL selector, ...)
{
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	std::uintptr_t guest_view_argument = 0;
	__asm__ volatile("movq %%rcx, %0" : "=r"(guest_view_argument));
	const auto* stack_arguments = reinterpret_cast<const std::uint8_t*>(
		__builtin_frame_address(0)) + 16;
	DispatchSuperRectStret(return_storage, super_info, selector, false,
		stack_arguments, reinterpret_cast<id>(guest_view_argument), nullptr);
#else
	va_list arguments;
	va_start(arguments, selector);
	DispatchSuperRectStret(return_storage, super_info, selector, false,
		nullptr, nullptr, &arguments);
	va_end(arguments);
#endif
}

extern "C" void darling_objc_msgSendSuper2_stret(void* return_storage,
	void* super_info, SEL selector, ...)
{
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	std::uintptr_t guest_view_argument = 0;
	__asm__ volatile("movq %%rcx, %0" : "=r"(guest_view_argument));
	const auto* stack_arguments = reinterpret_cast<const std::uint8_t*>(
		__builtin_frame_address(0)) + 16;
	DispatchSuperRectStret(return_storage, super_info, selector, true,
		stack_arguments, reinterpret_cast<id>(guest_view_argument), nullptr);
#else
	va_list arguments;
	va_start(arguments, selector);
	DispatchSuperRectStret(return_storage, super_info, selector, true,
		nullptr, nullptr, &arguments);
	va_end(arguments);
#endif
}

template<typename VarargsReader>
id DarlingObjcMsgSendSuperDispatch(id receiver, Class superclass, SEL selector,
	VarargsReader& varargs)
{
	if (!receiver || !superclass || !selector)
		return nullptr;
	const char* raw_types = class_getMethodTypeEncoding(superclass, selector);
	if (!raw_types)
		return nullptr;
	const std::string encoding = CanonicalObjcDispatchEncoding(raw_types);
	const char* types = encoding.c_str();
	if (std::strcmp(types, "@@:") == 0)
		return Dispatch(receiver, superclass, selector);
	if (std::strcmp(types, "v@:") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation)
			reinterpret_cast<void (*)(id, SEL)>(implementation)(receiver, selector);
		return nullptr;
	}
	if (std::strcmp(types, "i@:") == 0 || std::strcmp(types, "q@:") == 0 ||
		std::strcmp(types, "B@:") == 0 || std::strcmp(types, "S@:") == 0 ||
		std::strcmp(types, "s@:") == 0 || std::strcmp(types, "^v@:") == 0) {
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
		if (std::strcmp(types, "S@:") == 0)
			return reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<std::uint16_t (*)(id, SEL)>(implementation)(receiver, selector)));
		if (std::strcmp(types, "s@:") == 0)
			return reinterpret_cast<id>(static_cast<std::intptr_t>(
				reinterpret_cast<std::int16_t (*)(id, SEL)>(implementation)(receiver, selector)));
		return reinterpret_cast<id>(reinterpret_cast<void* (*)(id, SEL)>(implementation)(
			receiver, selector));
	}
	id result = nullptr;
	if (std::strcmp(types, "@@:@@") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using ObjectMethod = id (*)(id, SEL, id, id);
		const auto first = varargs.template Next<id>();
		const auto second = varargs.template Next<id>();
		if (implementation)
			result = reinterpret_cast<ObjectMethod>(implementation)(receiver, selector,
				first, second);
	} else if (std::strcmp(types, "q@:q") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t);
		if (implementation)
			result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
					varargs.template Next<std::int64_t>())));
	} else if (std::strcmp(types, "q@:qq") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using IntegerMethod = std::int64_t (*)(id, SEL, std::int64_t,
			std::int64_t);
		const auto first = varargs.template Next<std::int64_t>();
		const auto second = varargs.template Next<std::int64_t>();
		if (implementation)
			result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<IntegerMethod>(implementation)(receiver, selector,
					first, second)));
	} else if (std::strcmp(types, "B@:B") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using BoolMethod = bool (*)(id, SEL, bool);
		const bool value = varargs.template Next<int>() != 0;
		if (implementation)
			result = reinterpret_cast<id>(static_cast<std::uintptr_t>(
				reinterpret_cast<BoolMethod>(implementation)(receiver, selector, value)));
	} else if (std::strcmp(types, "v@:i") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation)
			reinterpret_cast<void (*)(id, SEL, int)>(implementation)(receiver, selector,
				varargs.template Next<int>());
	} else if (std::strcmp(types, "v@:q") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation)
			reinterpret_cast<void (*)(id, SEL, std::int64_t)>(implementation)(receiver, selector,
				varargs.template Next<std::int64_t>());
	} else if (std::strcmp(types, "v@:^{CGPoint=dd}q") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using PointsMethod = void (*)(id, SEL, const DarlingObjcPoint*,
			std::int64_t);
		const auto* points = varargs.template Next<const DarlingObjcPoint*>();
		const auto count = varargs.template Next<std::int64_t>();
		if (implementation)
			reinterpret_cast<PointsMethod>(implementation)(receiver, selector,
				points, count);
	} else if (std::strcmp(types, "^v@:^v") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using PointerMethod = void* (*)(id, SEL, void*);
		if (implementation)
			result = reinterpret_cast<id>(reinterpret_cast<PointerMethod>(implementation)(
				receiver, selector, varargs.template Next<void*>()));
	} else if (std::strcmp(types, "@@:@?") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		using BlockMethod = id (*)(id, SEL, DarlingObjcCallbackBlock*);
		if (implementation)
			result = reinterpret_cast<BlockMethod>(implementation)(receiver, selector,
				varargs.template Next<DarlingObjcCallbackBlock*>());
	} else if (std::strcmp(types, "@@:@") == 0) {
		const IMP implementation = class_getMethodImplementation(superclass, selector);
		if (implementation) {
			using Method = id (*)(id, SEL, id);
			result = reinterpret_cast<Method>(implementation)(receiver, selector,
				varargs.template Next<id>());
		}
	}
	return result;
}

namespace {
struct alignas(16) DarlingArm64ObjcReturnRegisters final {
	std::uint64_t gp[2]{};
	double fp[4]{};
};
static_assert(offsetof(DarlingArm64ObjcReturnRegisters, fp) == 16);
static_assert(sizeof(DarlingArm64ObjcReturnRegisters) == 48);

template<typename Reader>
bool DispatchArm64NonIntegerReturn(id receiver, SEL selector, IMP implementation,
	const std::string& encoding, Reader& arguments,
	DarlingArm64ObjcReturnRegisters& result)
{
	if (!implementation) return false;
	if (encoding == "d@:") {
		result.fp[0] = reinterpret_cast<double (*)(id, SEL)>(implementation)(
			receiver, selector);
		return true;
	}
	if (encoding == "f@:") {
		const float value = reinterpret_cast<float (*)(id, SEL)>(implementation)(
			receiver, selector);
		std::memcpy(&result.fp[0], &value, sizeof(value));
		return true;
	}
	if (encoding == "d@:d") {
		const double value = arguments.template Next<double>();
		result.fp[0] = reinterpret_cast<double (*)(id, SEL, double)>(implementation)(
			receiver, selector, value);
		return true;
	}
	if (encoding == "f@:f") {
		const float value = static_cast<float>(arguments.template Next<double>());
		const float returned = reinterpret_cast<float (*)(id, SEL, float)>(
			implementation)(receiver, selector, value);
		std::memcpy(&result.fp[0], &returned, sizeof(returned));
		return true;
	}
	if (encoding == "{CGRect={CGPoint=dd}{CGSize=dd}}@:") {
		const auto value = reinterpret_cast<DarlingObjcRect (*)(id, SEL)>(
			implementation)(receiver, selector);
		std::memcpy(result.fp, &value, sizeof(value));
		return true;
	}
	if (encoding ==
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}@") {
		const auto rect = arguments.template Next<DarlingObjcRect>();
		const auto other_view = arguments.template Next<id>();
		const auto value = reinterpret_cast<DarlingObjcRect (*)(id, SEL,
			DarlingObjcRect, id)>(implementation)(receiver, selector, rect,
			other_view);
		std::memcpy(result.fp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{CGSize=dd}@:{CGSize=dd}@") {
		const auto size = arguments.template Next<DarlingObjcPoint>();
		const auto other_view = arguments.template Next<id>();
		const auto value = reinterpret_cast<DarlingObjcPoint (*)(id, SEL,
			DarlingObjcPoint, id)>(implementation)(receiver, selector, size,
			other_view);
		std::memcpy(result.fp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{CGPoint=dd}@:") {
		const auto value = reinterpret_cast<DarlingObjcPoint (*)(id, SEL)>(
			implementation)(receiver, selector);
		std::memcpy(result.fp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{CGSize=dd}@:") {
		const auto value = reinterpret_cast<DarlingObjcPoint (*)(id, SEL)>(
			implementation)(receiver, selector);
		std::memcpy(result.fp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{_NSRange=QQ}@:") {
		const auto value = reinterpret_cast<DarlingObjcRange (*)(id, SEL)>(
			implementation)(receiver, selector);
		std::memcpy(result.gp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{_NSRange=QQ}@:@") {
		const auto argument = arguments.template Next<id>();
		const auto value = reinterpret_cast<DarlingObjcRange (*)(id, SEL, id)>(
			implementation)(receiver, selector, argument);
		std::memcpy(result.gp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{_NSRange=QQ}@:@@Q") {
		const auto string = arguments.template Next<id>();
		const auto options = arguments.template Next<std::uint64_t>();
		const auto value = reinterpret_cast<DarlingObjcRange (*)(id, SEL, id,
			std::uint64_t)>(implementation)(receiver, selector, string, options);
		std::memcpy(result.gp, &value, sizeof(value));
		return true;
	}
	if (encoding == "{_NSRange=QQ}@:@@Q{_NSRange=QQ}") {
		const auto string = arguments.template Next<id>();
		const auto options = arguments.template Next<std::uint64_t>();
		const auto range = arguments.template Next<DarlingObjcRange>();
		const auto value = reinterpret_cast<DarlingObjcRange (*)(id, SEL,
			id, std::uint64_t, DarlingObjcRange)>(implementation)(receiver,
			selector, string, options, range);
		std::memcpy(result.gp, &value, sizeof(value));
		return true;
	}
	return false;
}

template<typename Reader>
bool DispatchArm64StructArgument(id receiver, SEL selector, IMP implementation,
	const std::string& encoding, Reader& arguments,
	DarlingArm64ObjcReturnRegisters& result)
{
	if (!implementation) return false;
	constexpr std::string_view range = "{_NSRange=QQ}";
	constexpr std::string_view point = "{CGPoint=dd}";
	constexpr std::string_view size = "{CGSize=dd}";
	constexpr std::string_view rect = "{CGRect={CGPoint=dd}{CGSize=dd}}";
	if (encoding == "@@:" + std::string(range)) {
		const auto value = arguments.template Next<DarlingObjcRange>();
		result.gp[0] = reinterpret_cast<std::uintptr_t>(
			reinterpret_cast<id (*)(id, SEL, DarlingObjcRange)>(implementation)(
				receiver, selector, value));
		return true;
	}
	if (encoding == "v@:" + std::string(range)) {
		const auto value = arguments.template Next<DarlingObjcRange>();
		reinterpret_cast<void (*)(id, SEL, DarlingObjcRange)>(implementation)(
			receiver, selector, value);
		return true;
	}
	if (encoding == "@@:" + std::string(point) ||
		encoding == "@@:" + std::string(size)) {
		const auto value = arguments.template Next<DarlingObjcPoint>();
		result.gp[0] = reinterpret_cast<std::uintptr_t>(
			reinterpret_cast<id (*)(id, SEL, DarlingObjcPoint)>(implementation)(
				receiver, selector, value));
		return true;
	}
	if (encoding == "v@:" + std::string(point) ||
		encoding == "v@:" + std::string(size)) {
		const auto value = arguments.template Next<DarlingObjcPoint>();
		reinterpret_cast<void (*)(id, SEL, DarlingObjcPoint)>(implementation)(
			receiver, selector, value);
		return true;
	}
	if (encoding == "@@:" + std::string(rect)) {
		const auto value = arguments.template Next<DarlingObjcRect>();
		result.gp[0] = reinterpret_cast<std::uintptr_t>(
			reinterpret_cast<id (*)(id, SEL, DarlingObjcRect)>(implementation)(
				receiver, selector, value));
		return true;
	}
	if (encoding == "v@:" + std::string(rect)) {
		const auto value = arguments.template Next<DarlingObjcRect>();
		reinterpret_cast<void (*)(id, SEL, DarlingObjcRect)>(implementation)(
			receiver, selector, value);
		return true;
	}
	return false;
}
}

extern "C" void darling_windows_objc_arm64_message_dispatch(
	const void* receiver_or_super, SEL selector, const void* argument_stack,
	DarlingArm64ObjcReturnRegisters* output, unsigned dispatch_kind,
	void* indirect_result)
{
	if (!output) return;
	*output = {};
	(void)indirect_result;
	if (!receiver_or_super || !selector || dispatch_kind > 2) return;
	const auto* super = dispatch_kind == 0 ? nullptr :
		static_cast<const DarlingObjcSuper*>(receiver_or_super);
	id receiver = super ? super->receiver :
		const_cast<id>(static_cast<const objc_object*>(receiver_or_super));
	if (!receiver) return;
	Class lookup = nullptr;
	if (super) {
		lookup = super->super_class;
		if (dispatch_kind == 2 && lookup) lookup = lookup->superclass;
	} else {
		lookup = object_isClass(receiver) ? reinterpret_cast<Class>(receiver) :
			receiver->isa;
	}
	if (!lookup) return;
	const bool class_method = object_isClass(receiver);
	const Method method = class_method ? class_getClassMethod(lookup, selector) :
		class_getInstanceMethod(lookup, selector);
	const IMP implementation = method ? method_getImplementation(method) : nullptr;
	const std::string encoding = CanonicalObjcDispatchEncoding(
		method ? method_getTypeEncoding(method) : nullptr);
	DarlingArm64GuestVarargs reader{
		static_cast<const std::uint8_t*>(argument_stack)};
	if (DispatchArm64NonIntegerReturn(receiver, selector, implementation,
		encoding, reader, *output)) return;
	if (DispatchArm64StructArgument(receiver, selector, implementation,
		encoding, reader, *output)) return;
	if (super) {
		output->gp[0] = reinterpret_cast<std::uintptr_t>(
			DarlingObjcMsgSendSuperDispatch(receiver, lookup, selector, reader));
	} else {
		output->gp[0] = reinterpret_cast<std::uintptr_t>(
			DarlingObjcMsgSendDispatch(receiver, selector, reader));
	}
}
extern "C" id objc_msgSendSuper(id receiver, Class superclass, SEL selector, ...)
{
	va_list arguments;
	va_start(arguments, selector);
	DarlingWindowsVarargs reader{&arguments};
	const id result = DarlingObjcMsgSendSuperDispatch(receiver, superclass,
		selector, reader);
	va_end(arguments);
	return result;
}

extern "C" id darling_objc_msgSendSuper_object0(id receiver, Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "@@:") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<id (*)(id, SEL)>(implementation)(receiver, selector) : nullptr;
}

extern "C" id darling_objc_msgSendSuper_object1(id receiver, Class superclass,
	SEL selector, id argument)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "@@:@") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<id (*)(id, SEL, id)>(implementation)(receiver, selector, argument) : nullptr;
}

extern "C" id darling_objc_msgSendSuper_object2(id receiver, Class superclass,
	SEL selector, id first, id second)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "@@:@@") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<id (*)(id, SEL, id, id)>(implementation)(receiver, selector, first, second) : nullptr;
}

extern "C" void* darling_objc_msgSendSuper_pointer0(id receiver, Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "^v@:") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<void* (*)(id, SEL)>(implementation)(receiver, selector) : nullptr;
}

extern "C" void* darling_objc_msgSendSuper_pointer1(id receiver, Class superclass,
	SEL selector, void* argument)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "^v@:^v") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<void* (*)(id, SEL, void*)>(implementation)(receiver, selector, argument) : nullptr;
}

extern "C" void darling_objc_msgSendSuper_void_object1(id receiver,
	Class superclass, SEL selector, id argument)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:@") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, id)>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSendSuper_void_object2(id receiver,
	Class superclass, SEL selector, id first, id second)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:@@") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, id, id)>(implementation)(receiver, selector, first, second);
}

extern "C" void darling_objc_msgSendSuper_void_double1(id receiver,
	Class superclass, SEL selector, double argument)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:d") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, double)>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSendSuper_void_float1(id receiver,
	Class superclass, SEL selector, float argument)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:f") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, float)>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSendSuper_void_double2(id receiver,
	Class superclass, SEL selector, double first, double second)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:dd") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, double, double)>(implementation)(receiver, selector, first, second);
}

extern "C" void darling_objc_msgSendSuper_void_float2(id receiver,
	Class superclass, SEL selector, float first, float second)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:ff") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, float, float)>(implementation)(receiver, selector, first, second);
}

extern "C" double darling_objc_msgSendSuper_double0(id receiver, Class superclass,
	SEL selector)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "d@:") != 0)
		return 0.0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<double (*)(id, SEL)>(implementation)(receiver, selector) : 0.0;
}

extern "C" double darling_objc_msgSendSuper_double1(id receiver, Class superclass,
	SEL selector, double argument)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "d@:d") != 0)
		return 0.0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<double (*)(id, SEL, double)>(implementation)(
		receiver, selector, argument) : 0.0;
}

extern "C" double darling_objc_msgSendSuper_double2(id receiver, Class superclass,
	SEL selector, double first, double second)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "d@:dd") != 0)
		return 0.0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<double (*)(id, SEL, double, double)>(implementation)(
		receiver, selector, first, second) : 0.0;
}

extern "C" float darling_objc_msgSendSuper_float0(id receiver, Class superclass,
	SEL selector)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "f@:") != 0)
		return 0.0f;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<float (*)(id, SEL)>(implementation)(receiver, selector) : 0.0f;
}

extern "C" float darling_objc_msgSendSuper_float1(id receiver, Class superclass,
	SEL selector, float argument)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "f@:f") != 0)
		return 0.0f;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<float (*)(id, SEL, float)>(implementation)(
		receiver, selector, argument) : 0.0f;
}

extern "C" float darling_objc_msgSendSuper_float2(id receiver, Class superclass,
	SEL selector, float first, float second)
{
	if (!receiver || !superclass || !selector ||
		std::strcmp(class_getMethodTypeEncoding(superclass, selector), "f@:ff") != 0)
		return 0.0f;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<float (*)(id, SEL, float, float)>(implementation)(
		receiver, selector, first, second) : 0.0f;
}

extern "C" std::uint64_t darling_objc_msgSendSuper_uint64_0(id receiver,
	Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "Q@:") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::uint64_t (*)(id, SEL)>(implementation)(
		receiver, selector) : 0;
}

extern "C" std::uint64_t darling_objc_msgSendSuper_uint64_1(id receiver,
	Class superclass, SEL selector, std::uint64_t argument)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "Q@:Q") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::uint64_t (*)(id, SEL, std::uint64_t)>(implementation)(
		receiver, selector, argument) : 0;
}

extern "C" std::uint64_t darling_objc_msgSendSuper_uint64_2(id receiver,
	Class superclass, SEL selector, std::uint64_t first, std::uint64_t second)
{
	if (!receiver || !superclass || !selector) return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "Q@:QQ") != 0) return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::uint64_t (*)(id, SEL, std::uint64_t, std::uint64_t)>(implementation)(receiver, selector, first, second) : 0;
}

extern "C" unsigned int darling_objc_msgSendSuper_uint0(id receiver,
	Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "I@:") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<unsigned int (*)(id, SEL)>(implementation)(
		receiver, selector) : 0;
}

extern "C" unsigned int darling_objc_msgSendSuper_uint1(id receiver,
	Class superclass, SEL selector, unsigned int argument)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "I@:I") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<unsigned int (*)(id, SEL, unsigned int)>(implementation)(
		receiver, selector, argument) : 0;
}

extern "C" unsigned int darling_objc_msgSendSuper_uint2(id receiver,
	Class superclass, SEL selector, unsigned int first, unsigned int second)
{
	if (!receiver || !superclass || !selector) return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "I@:II") != 0) return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<unsigned int (*)(id, SEL, unsigned int, unsigned int)>(implementation)(receiver, selector, first, second) : 0;
}

extern "C" bool darling_objc_msgSendSuper_bool0(id receiver, Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector)
		return false;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "B@:") != 0)
		return false;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation && reinterpret_cast<bool (*)(id, SEL)>(implementation)(receiver, selector);
}

extern "C" bool darling_objc_msgSendSuper_bool1(id receiver, Class superclass,
	SEL selector, bool argument)
{
	if (!receiver || !superclass || !selector)
		return false;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "B@:B") != 0)
		return false;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation && reinterpret_cast<bool (*)(id, SEL, bool)>(implementation)(
		receiver, selector, argument);
}

extern "C" int darling_objc_msgSendSuper_int0(id receiver, Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "i@:") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<int (*)(id, SEL)>(implementation)(receiver, selector) : 0;
}

extern "C" int darling_objc_msgSendSuper_int1(id receiver, Class superclass,
	SEL selector, int argument)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "i@:i") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<int (*)(id, SEL, int)>(implementation)(
		receiver, selector, argument) : 0;
}

extern "C" std::int64_t darling_objc_msgSendSuper_int64_0(id receiver,
	Class superclass, SEL selector)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "q@:") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::int64_t (*)(id, SEL)>(implementation)(
		receiver, selector) : 0;
}

extern "C" std::int64_t darling_objc_msgSendSuper_int64_1(id receiver,
	Class superclass, SEL selector, std::int64_t argument)
{
	if (!receiver || !superclass || !selector)
		return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "q@:q") != 0)
		return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::int64_t (*)(id, SEL, std::int64_t)>(implementation)(
		receiver, selector, argument) : 0;
}

extern "C" std::int64_t darling_objc_msgSendSuper_int64_2(id receiver,
	Class superclass, SEL selector, std::int64_t first, std::int64_t second)
{
	if (!receiver || !superclass || !selector) return 0;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "q@:qq") != 0) return 0;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	return implementation ? reinterpret_cast<std::int64_t (*)(id, SEL, std::int64_t, std::int64_t)>(implementation)(receiver, selector, first, second) : 0;
}

extern "C" id darling_objc_msgSend_class0(Class cls, SEL selector)
{
	if (!cls || !selector)
		return nullptr;
	IMP implementation = nullptr;
	Class method_receiver = cls->is_metaclass && cls->owner_class ?
		cls->owner_class : cls;
	{
		std::lock_guard lock(RuntimeMutex());
		for (Class current = cls; current; current = current->superclass) {
			const auto& method_map = current->is_metaclass ? current->methods :
				current->class_methods;
			const auto method = method_map.find(selector);
			if (method == method_map.end()) continue;
			if (method->second.types != "@@:") return nullptr;
			implementation = method->second.implementation;
			break;
		}
	}
	if (!implementation) return nullptr;
	using ClassMethod = id (*)(Class, SEL);
	return reinterpret_cast<ClassMethod>(implementation)(method_receiver, selector);
}

extern "C" id darling_objc_msgSend_object1(id receiver, SEL selector,
	id argument)
{
	if (receiver == nullptr) return nullptr;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	if (method.implementation == nullptr ||
		CanonicalObjcEncoding(method.types) != "@@:@")
		return nullptr;
	return InvokeObjcImplementation<id>(method.implementation, method.guest_abi,
		receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		std::strcmp(types, "@@:@") != 0) return nullptr;
	return reinterpret_cast<id (*)(id, SEL, id)>(implementation)(receiver,
		selector, argument);
#endif
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

extern "C" id darling_objc_msgSend_object_rect1(id receiver, SEL selector,
	DarlingObjcRect argument)
{
	if (receiver == nullptr || selector == nullptr) return nullptr;
	constexpr const char* encoding =
		"@@:{CGRect={CGPoint=dd}{CGSize=dd}}";
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	if (method.implementation == nullptr ||
		CanonicalObjcEncoding(method.types) != encoding) return nullptr;
	return InvokeObjcImplementation<id, DarlingObjcRect>(method.implementation,
		method.guest_abi, receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		std::strcmp(types, encoding) != 0) return nullptr;
	using ObjectRectMethod = id (*)(id, SEL, DarlingObjcRect);
	return reinterpret_cast<ObjectRectMethod>(implementation)(receiver,
		selector, argument);
#endif
}

extern "C" id darling_objc_msgSend_object_pointer1(id receiver, SEL selector,
	void* argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types ||
		(std::strcmp(types, "@@:^v") != 0 && std::strcmp(types, "@@:*") != 0))
		return nullptr;
	using ObjectMethod = id (*)(id, SEL, void*);
	return reinterpret_cast<ObjectMethod>(implementation)(receiver, selector, argument);
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

extern "C" id darling_objc_msgSendSuper_block1(id receiver, Class superclass,
	SEL selector, DarlingObjcCallbackBlock* argument)
{
	if (!receiver || !superclass || !selector) return nullptr;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "@@:@?") != 0) return nullptr;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	using BlockMethod = id (*)(id, SEL, DarlingObjcCallbackBlock*);
	return implementation ? reinterpret_cast<BlockMethod>(implementation)(receiver, selector, argument) : nullptr;
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

extern "C" std::int64_t darling_objc_msgSend_int64_0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "q@:") != 0) return 0;
	using IntegerMethod = std::int64_t (*)(id, SEL);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector);
}

extern "C" std::int64_t darling_objc_msgSend_int64_object1(
	id receiver, SEL selector, id argument)
{
	if (receiver == nullptr) return 0;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	const auto encoding = CanonicalObjcEncoding(method.types);
	if (method.implementation == nullptr ||
		(encoding != "q@:@" && encoding != "i@:@")) return 0;
	return InvokeObjcImplementation<std::int64_t, id>(method.implementation,
		method.guest_abi, receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		(std::strcmp(types, "q@:@") != 0 && std::strcmp(types, "i@:@") != 0))
		return 0;
	return reinterpret_cast<std::int64_t (*)(id, SEL, id)>(implementation)(
		receiver, selector, argument);
#endif
}

extern "C" std::uint64_t darling_objc_msgSend_uint64_0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "Q@:") != 0) return 0;
	using IntegerMethod = std::uint64_t (*)(id, SEL);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector);
}

extern "C" std::uint64_t darling_objc_msgSend_uint64_1(id receiver, SEL selector,
	std::uint64_t argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "Q@:Q") != 0) return 0;
	using IntegerMethod = std::uint64_t (*)(id, SEL, std::uint64_t);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector, argument);
}

extern "C" int darling_objc_msgSend_int0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "i@:") != 0) return 0;
	using IntegerMethod = int (*)(id, SEL);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector);
}

extern "C" int darling_objc_msgSend_int1(id receiver, SEL selector, int argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "i@:i") != 0) return 0;
	using IntegerMethod = int (*)(id, SEL, int);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector, argument);
}

extern "C" unsigned int darling_objc_msgSend_uint0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "I@:") != 0) return 0;
	using IntegerMethod = unsigned int (*)(id, SEL);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector);
}

extern "C" unsigned int darling_objc_msgSend_uint1(id receiver, SEL selector,
	unsigned int argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "I@:I") != 0) return 0;
	using IntegerMethod = unsigned int (*)(id, SEL, unsigned int);
	return reinterpret_cast<IntegerMethod>(implementation)(receiver, selector, argument);
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

extern "C" bool darling_objc_msgSend_bool_object1(id receiver, SEL selector,
	id argument)
{
	if (receiver == nullptr) return false;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	const auto encoding = CanonicalObjcEncoding(method.types);
	if (method.implementation == nullptr ||
		(encoding != "B@:@" && encoding != "c@:@")) return false;
	return InvokeObjcImplementation<bool, id>(method.implementation,
		method.guest_abi, receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		(std::strcmp(types, "B@:@") != 0 &&
			std::strcmp(types, "c@:@") != 0)) return false;
	return reinterpret_cast<bool (*)(id, SEL, id)>(implementation)(receiver,
		selector, argument);
#endif
}

extern "C" bool darling_objc_msgSend_bool0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "B@:") != 0) return false;
	using BoolMethod = bool (*)(id, SEL);
	return reinterpret_cast<BoolMethod>(implementation)(receiver, selector);
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

extern "C" void* darling_objc_msgSend_pointer0(id receiver, SEL selector)
{
	if (receiver == nullptr) return nullptr;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	if (method.implementation == nullptr ||
		(CanonicalObjcEncoding(method.types) != "^v@:" &&
		CanonicalObjcEncoding(method.types) != "*@:")) return nullptr;
	return InvokeObjcImplementation<void*>(method.implementation,
		method.guest_abi, receiver, selector);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		(std::strcmp(types, "^v@:") != 0 && std::strcmp(types, "*@:") != 0))
		return nullptr;
	return reinterpret_cast<void* (*)(id, SEL)>(implementation)(receiver,
		selector);
#endif
}

extern "C" void darling_objc_msgSend_void1_pointer(id receiver, SEL selector,
	void* argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:^v") != 0) return;
	using VoidMethod = void (*)(id, SEL, void*);
	reinterpret_cast<VoidMethod>(implementation)(receiver, selector, argument);
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

extern "C" double darling_objc_msgSend_double0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "d@:") != 0) return 0.0;
	using DoubleMethod = double (*)(id, SEL);
	return reinterpret_cast<DoubleMethod>(implementation)(receiver, selector);
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

extern "C" float darling_objc_msgSend_float0(id receiver, SEL selector)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "f@:") != 0) return 0.0f;
	using FloatMethod = float (*)(id, SEL);
	return reinterpret_cast<FloatMethod>(implementation)(receiver, selector);
}

extern "C" float darling_objc_msgSend_float1(id receiver, SEL selector, float argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "f@:f") != 0) return 0.0f;
	using FloatMethod = float (*)(id, SEL, float);
	return reinterpret_cast<FloatMethod>(implementation)(receiver, selector, argument);
}

extern "C" float darling_objc_msgSend_float2(id receiver, SEL selector,
	float first, float second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "f@:ff") != 0) return 0.0f;
	using FloatMethod = float (*)(id, SEL, float, float);
	return reinterpret_cast<FloatMethod>(implementation)(receiver, selector, first, second);
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

extern "C" void darling_objc_msgSend_void1_double(id receiver, SEL selector, double argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:d") != 0) return;
	reinterpret_cast<void (*)(id, SEL, double)>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSend_void1_size(id receiver, SEL selector,
	DarlingObjcPoint argument)
{
	if (receiver == nullptr || selector == nullptr) return;
	#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const ObjcMethodSnapshot method = LookupMethodSnapshot(
		object_getClass(receiver), selector, false);
	if (method.implementation == nullptr ||
		CanonicalObjcEncoding(method.types) != "v@:{CGSize=dd}") return;
	InvokeObjcImplementation<void, DarlingObjcPoint>(method.implementation,
		method.guest_abi, receiver, selector, argument);
	#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		std::strcmp(types, "v@:{CGSize=dd}") != 0) return;
	reinterpret_cast<void (*)(id, SEL, DarlingObjcPoint)>(implementation)(
		receiver, selector, argument);
	#endif
}

extern "C" void darling_objc_msgSend_void1_float(id receiver, SEL selector, float argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:f") != 0) return;
	reinterpret_cast<void (*)(id, SEL, float)>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSend_void2_double(id receiver, SEL selector,
	double first, double second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:dd") != 0) return;
	reinterpret_cast<void (*)(id, SEL, double, double)>(implementation)(receiver, selector, first, second);
}

extern "C" void darling_objc_msgSend_void2_float(id receiver, SEL selector,
	float first, float second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:ff") != 0) return;
	reinterpret_cast<void (*)(id, SEL, float, float)>(implementation)(receiver, selector, first, second);
}

extern "C" void darling_objc_msgSend_void1_object(id receiver, SEL selector, id argument)
{
	if (receiver == nullptr) return;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	if (method.implementation == nullptr ||
		CanonicalObjcEncoding(method.types) != "v@:@") return;
	InvokeObjcImplementation<void>(method.implementation, method.guest_abi,
		receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		std::strcmp(types, "v@:@") != 0) return;
	reinterpret_cast<void (*)(id, SEL, id)>(implementation)(receiver, selector,
		argument);
#endif
}

extern "C" void darling_objc_msgSend_void2_object(id receiver, SEL selector,
	id first, id second)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:@@") != 0) return;
	reinterpret_cast<void (*)(id, SEL, id, id)>(implementation)(receiver, selector, first, second);
}

extern "C" void darling_objc_msgSend_void1_block(id receiver, SEL selector,
	DarlingObjcCallbackBlock* argument)
{
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types, "v@:@?") != 0) return;
	reinterpret_cast<void (*)(id, SEL, DarlingObjcCallbackBlock*)>(implementation)(
		receiver, selector, argument);
}

extern "C" void darling_objc_msgSendSuper_void1_block(id receiver, Class superclass,
	SEL selector, DarlingObjcCallbackBlock* argument)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "v@:@?") != 0) return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, DarlingObjcCallbackBlock*)>(implementation)(
			receiver, selector, argument);
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

extern "C" DarlingObjcRect darling_objc_msgSend_rect1(id receiver, SEL selector,
	DarlingObjcRect argument)
{
	const DarlingObjcRect empty{};
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver), selector);
	if (!implementation || !types || std::strcmp(types,
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}") != 0)
		return empty;
	using RectMethod = DarlingObjcRect (*)(id, SEL, DarlingObjcRect);
	return reinterpret_cast<RectMethod>(implementation)(receiver, selector, argument);
}

extern "C" void darling_objc_msgSend_void_rect1(id receiver, SEL selector,
	DarlingObjcRect argument)
{
	constexpr const char* encoding = "v@:{CGRect={CGPoint=dd}{CGSize=dd}}";
	if (receiver == nullptr || selector == nullptr) return;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	const auto method = LookupMethodSnapshot(object_getClass(receiver), selector,
		false);
	if (method.implementation == nullptr ||
		CanonicalObjcEncoding(method.types) != encoding) return;
	InvokeObjcImplementation<void, DarlingObjcRect>(method.implementation,
		method.guest_abi, receiver, selector, argument);
#else
	const IMP implementation = FindMethod(receiver, selector);
	const char* types = class_getMethodTypeEncoding(object_getClass(receiver),
		selector);
	if (implementation == nullptr || types == nullptr ||
		std::strcmp(types, encoding) != 0) return;
	using RectMethod = void (*)(id, SEL, DarlingObjcRect);
	reinterpret_cast<RectMethod>(implementation)(receiver, selector, argument);
#endif
}

extern "C" DarlingObjcRect darling_objc_msgSendSuper_rect0(id receiver,
	Class superclass, SEL selector)
{
	const DarlingObjcRect empty{};
	if (!receiver || !superclass || !selector) return empty;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types, "{CGRect={CGPoint=dd}{CGSize=dd}}@:") != 0)
		return empty;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	using RectMethod = DarlingObjcRect (*)(id, SEL);
	return implementation ? reinterpret_cast<RectMethod>(implementation)(receiver, selector) : empty;
}

extern "C" DarlingObjcRect darling_objc_msgSendSuper_rect1(id receiver,
	Class superclass, SEL selector, DarlingObjcRect argument)
{
	const DarlingObjcRect empty{};
	if (!receiver || !superclass || !selector) return empty;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types,
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}") != 0)
		return empty;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	using RectMethod = DarlingObjcRect (*)(id, SEL, DarlingObjcRect);
	return implementation ? reinterpret_cast<RectMethod>(implementation)(receiver, selector, argument) : empty;
}

extern "C" void darling_objc_msgSendSuper_void_rect1(id receiver,
	Class superclass, SEL selector, DarlingObjcRect argument)
{
	if (!receiver || !superclass || !selector) return;
	const char* types = class_getMethodTypeEncoding(superclass, selector);
	if (!types || std::strcmp(types,
		"v@:{CGRect={CGPoint=dd}{CGSize=dd}}") != 0)
		return;
	const IMP implementation = class_getMethodImplementation(superclass, selector);
	if (implementation)
		reinterpret_cast<void (*)(id, SEL, DarlingObjcRect)>(implementation)(
			receiver, selector, argument);
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

extern "C" void darling_objc_apple_block_invoke0(void* raw_block)
{
	if (raw_block == nullptr) return;
	if (IsManagedAppleBlock(raw_block)) {
		auto* block = static_cast<DarlingObjcAppleBlock*>(raw_block);
		if (block->invoke != nullptr) block->invoke(block, nullptr);
		return;
	}
	auto* block = static_cast<AppleBlockHeader*>(raw_block);
	if (block->invoke == nullptr) return;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
	using Invoke = void (__attribute__((sysv_abi)) *)(void*);
#else
	using Invoke = void (*)(void*);
#endif
	reinterpret_cast<Invoke>(block->invoke)(raw_block);
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
