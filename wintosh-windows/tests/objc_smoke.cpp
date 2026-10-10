/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_objc.h"
#include "darling_windows_stdio.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::int64_t observed_void_int64 = 0;
int observed_void_int = 0;

id ReturnSelf(id receiver, SEL)
{
	return receiver;
}

id ReturnArgument(id, SEL, id argument)
{
	return argument;
}

id ReturnFixedArgument(id, SEL, id argument)
{
	return argument == nullptr ? nullptr : reinterpret_cast<id>(
		static_cast<std::uintptr_t>(0x5678));
}

id ReturnSecondArgument(id, SEL, id, id second)
{
	return second;
}

std::int64_t AddSeven(id, SEL, std::int64_t value)
{
	return value + 7;
}

std::int64_t ReturnInt64(id, SEL) { return 42; }
std::uint64_t ReturnUInt64(id, SEL) { return 42; }
std::uint64_t AddUInt64(id, SEL, std::uint64_t value) { return value + 7; }
bool ReturnTrue(id, SEL) { return true; }
double ReturnDouble(id, SEL) { return 3.5; }
void* ReturnPointer(id, SEL) {
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x4321));
}

int AddFiveInt(id, SEL, int value)
{
	return value + 5;
}

int ReturnInt(id, SEL) { return 42; }

void MarkCalled(id, SEL)
{
}

void ObserveInt64(id, SEL, std::int64_t value)
{
	observed_void_int64 = value;
}

void ObserveInt(id, SEL, int value)
{
	observed_void_int = value;
}

bool IsPositive(id, SEL, bool value)
{
	return value;
}

double Scale(id, SEL, double value)
{
	return value * 2.0;
}

double AddDoubles(id, SEL, double first, double second)
{
	return first + second;
}

void* EchoPointer(id, SEL, void* value)
{
	return value;
}

std::int64_t AddTwo(id, SEL, std::int64_t first, std::int64_t second)
{
	return first + second;
}

DarlingObjcRect MakeRect(id, SEL)
{
	return {1.0, 2.0, 320.0, 240.0};
}

id ClassIdentity(Class cls, SEL)
{
	return reinterpret_cast<id>(cls);
}

id ClassNull(Class, SEL)
{
	return nullptr;
}

id CallbackIdentity(void*, id argument)
{
	return argument;
}

struct AppleBlockTestDescriptor final {
	std::uintptr_t reserved{};
	std::uintptr_t size{};
	void (*copy_helper)(void*, const void*){};
	void (*dispose_helper)(const void*){};
	const char* signature{};
};

struct AppleBlockTestLiteral final {
	void* isa{};
	std::int32_t flags{};
	std::int32_t reserved{};
	id (*invoke)(void*, id){};
	AppleBlockTestDescriptor* descriptor{};
	int captured{};
};

int apple_block_copies = 0;
int apple_block_disposes = 0;

void CopyAppleBlockCapture(void* destination, const void* source)
{
	++apple_block_copies;
	std::memcpy(static_cast<std::uint8_t*>(destination) +
		offsetof(AppleBlockTestLiteral, captured),
		static_cast<const std::uint8_t*>(source) +
		offsetof(AppleBlockTestLiteral, captured), sizeof(int));
}

void DisposeAppleBlockCapture(const void*)
{
	++apple_block_disposes;
}

struct AppleByrefTestCell final {
	void* isa{};
	AppleByrefTestCell* forwarding{};
	std::int32_t flags{};
	std::int32_t size{};
	void (*keep_helper)(void*, void*){};
	void (*destroy_helper)(void*){};
	int captured{};
};

int apple_byref_copies = 0;
int apple_byref_destroys = 0;

void CopyAppleByrefCapture(void* destination, void* source)
{
	++apple_byref_copies;
	std::memcpy(static_cast<std::uint8_t*>(destination) +
		offsetof(AppleByrefTestCell, captured),
		static_cast<const std::uint8_t*>(source) +
		offsetof(AppleByrefTestCell, captured), sizeof(int));
}

void DestroyAppleByrefCapture(void*)
{
	++apple_byref_destroys;
}

id InvokeAppleBlockCapture(void* raw_block, id argument)
{
	return static_cast<AppleBlockTestLiteral*>(raw_block)->captured == 41 ?
		argument : nullptr;
}

id ImageMethod(id receiver, SEL)
{
	return receiver;
}

id ApplyBlock(id, SEL, DarlingObjcCallbackBlock* block)
{
	return darling_objc_block_invoke1(block, reinterpret_cast<id>(block));
}

}

int main()
{
	SEL selector = sel_registerName("description");
	if (!selector || std::string(sel_getName(selector)) != "description")
		return 1;
	Class root = objc_allocateClassPair(nullptr, "DarlingRoot", 0);
	Class child = objc_allocateClassPair(root, "DarlingChild", 0);
	if (!root || !child || objc_getClass("DarlingChild") != nullptr)
		return 2;
	objc_registerClassPair(root);
	objc_registerClassPair(child);
	if (objc_getClass("DarlingChild") != child ||
		class_getSuperclass(child) != root ||
		std::string(class_getName(child)) != "DarlingChild")
		return 3;
	if (class_getInstanceSize(root) == 0 ||
		class_getInstanceSize(child) < class_getInstanceSize(root) ||
		darling_windows_host_symbol("class_getInstanceSize") == 0)
		return 31;
	Class ivar_class = objc_allocateClassPair(nullptr, "DarlingIvarClass", 0);
	if (!ivar_class || !class_addIvar(ivar_class, "value", sizeof(id), 3, "@") ||
		class_addIvar(ivar_class, "value", sizeof(id), 3, "@") ||
		darling_windows_host_symbol("class_addIvar") == 0)
		return 32;
	objc_registerClassPair(ivar_class);
	id ivar_object = class_createInstance(ivar_class, 0);
	Ivar value_ivar = class_getInstanceVariable(ivar_class, "value");
	id ivar_value = class_createInstance(child, 0);
	object_setIvar(ivar_object, value_ivar, ivar_value);
	if (!ivar_object || !value_ivar || object_getIvar(ivar_object, value_ivar) != ivar_value ||
		darling_windows_host_symbol("object_getIvar") == 0 ||
		darling_windows_host_symbol("object_setIvar") == 0)
		return 33;
	if (!class_addMethod(root, selector, reinterpret_cast<IMP>(&main), "@@:") ||
		class_addMethod(root, selector, reinterpret_cast<IMP>(&main), "@@:") ||
		class_getMethodImplementation(child, selector) !=
			reinterpret_cast<IMP>(&main))
		return 4;
	const auto replaced = class_replaceMethod(root, selector,
		reinterpret_cast<IMP>(&ReturnSelf), "@@:");
	const bool replacement_ok = replaced == reinterpret_cast<IMP>(&main) &&
		class_getMethodImplementation(child, selector) == reinterpret_cast<IMP>(&ReturnSelf) &&
		darling_windows_host_symbol("class_replaceMethod") != 0;
	const auto restored = class_replaceMethod(root, selector,
		reinterpret_cast<IMP>(&main), "@@:");
	if (!replacement_ok || restored != reinterpret_cast<IMP>(&ReturnSelf))
		return 41;
	Method method_view = class_getInstanceMethod(root, selector);
	const auto method_previous = method_setImplementation(method_view,
		reinterpret_cast<IMP>(&ReturnSelf));
	const bool method_mutation_ok = method_previous == reinterpret_cast<IMP>(&main) &&
		class_getMethodImplementation(child, selector) == reinterpret_cast<IMP>(&ReturnSelf) &&
		darling_windows_host_symbol("method_setImplementation") != 0;
	const auto method_restored = method_setImplementation(method_view,
		reinterpret_cast<IMP>(&main));
	if (!method_mutation_ok || method_restored != reinterpret_cast<IMP>(&ReturnSelf))
		return 42;
	SEL self_selector = sel_registerName("self");
	if (!class_addMethod(root, self_selector,
		reinterpret_cast<IMP>(&ReturnSelf), "@@:") ||
		class_addMethod(root, self_selector,
			reinterpret_cast<IMP>(&ReturnSelf), "@@:") ||
		class_getMethodImplementation(child, self_selector) !=
			reinterpret_cast<IMP>(&ReturnSelf))
		return 5;
	id object = class_createInstance(child, 0);
	if (!object || object_getClass(object) != child ||
		objc_msgSend(object, self_selector) != object ||
		objc_msgSendSuper(object, root, self_selector) != object)
		return 6;
	id strong_slot = nullptr;
	objc_storeStrong(&strong_slot, object);
	if (strong_slot != object || darling_windows_host_symbol("objc_storeStrong") == 0)
		return 34;
	objc_storeStrong(&strong_slot, nullptr);
	if (strong_slot != nullptr)
		return 35;
	SEL object_selector = sel_registerName("identity:");
	SEL exchange_selector = sel_registerName("exchange:");
	SEL integer_selector = sel_registerName("addSeven:");
	SEL int_selector = sel_registerName("addFiveInt:");
	SEL object_pair_selector = sel_registerName("second:");
	SEL super_pair_selector = sel_registerName("superSecond:");
	SEL super_pointer_selector = sel_registerName("superPointer:");
	SEL void_selector = sel_registerName("mark");
	if (!class_addMethod(child, object_selector,
		reinterpret_cast<IMP>(&ReturnArgument), "@@:@") ||
		!class_addMethod(child, exchange_selector,
			reinterpret_cast<IMP>(&ReturnFixedArgument), "@@:@") ||
		!class_addMethod(child, integer_selector,
			reinterpret_cast<IMP>(&AddSeven), "q@:q") ||
		!class_addMethod(child, int_selector,
			reinterpret_cast<IMP>(&AddFiveInt), "i@:i") ||
		!class_addMethod(child, object_pair_selector,
			reinterpret_cast<IMP>(&ReturnSecondArgument), "@@:@@") ||
		!class_addMethod(root, super_pair_selector,
			reinterpret_cast<IMP>(&ReturnSecondArgument), "@@:@@") ||
		!class_addMethod(root, super_pointer_selector,
			reinterpret_cast<IMP>(&EchoPointer), "^v@:^v") ||
		!class_addMethod(child, void_selector,
			reinterpret_cast<IMP>(&MarkCalled), "v@:"))
		return 7;
	Method exchange_first = class_getInstanceMethod(child, object_selector);
	Method exchange_second = class_getInstanceMethod(child, exchange_selector);
	if (!exchange_first || !exchange_second ||
		darling_objc_msgSend_object1(object, exchange_selector, object) !=
		reinterpret_cast<id>(static_cast<std::uintptr_t>(0x5678)))
		return 22;
	method_exchangeImplementations(exchange_first, exchange_second);
	if (darling_objc_msgSend_object1(object, object_selector, object) !=
		reinterpret_cast<id>(static_cast<std::uintptr_t>(0x5678)) ||
		darling_objc_msgSend_object1(object, exchange_selector, object) != object)
		return 22;
	method_exchangeImplementations(exchange_first, exchange_second);
	Method incompatible_method = class_getInstanceMethod(child, integer_selector);
	const IMP object_before_incompatible = method_getImplementation(exchange_first);
	method_exchangeImplementations(exchange_first, incompatible_method);
	if (method_getImplementation(exchange_first) != object_before_incompatible)
		return 23;
	if (std::string(class_getMethodTypeEncoding(child, object_selector)) !=
		"@@:@" || std::string(class_getMethodTypeEncoding(child, integer_selector)) !=
		"q@:q" || std::string(class_getMethodTypeEncoding(child, void_selector)) !=
		"v@:")
		return 8;
	if (darling_objc_msgSend_object1(object, object_selector, object) != object ||
		darling_objc_msgSend_int64_1(object, integer_selector, 35) != 42)
		return 90;
	if (static_cast<int>(reinterpret_cast<std::intptr_t>(
		objc_msgSend(object, int_selector, 37))) != 42)
		return 91;
	if (objc_msgSend(object, object_pair_selector, object,
		reinterpret_cast<id>(child)) !=
		reinterpret_cast<id>(child))
		return 92;
	if (darling_objc_msgSend_object2(object, object_pair_selector, object,
		reinterpret_cast<id>(child)) != reinterpret_cast<id>(child))
		return 19;
	if (objc_msgSendSuper(object, root, super_pair_selector, object,
		reinterpret_cast<id>(child)) != reinterpret_cast<id>(child))
		return 93;
	darling_objc_msgSend_void0(object, void_selector);
	SEL bool_selector = sel_registerName("positive:");
	SEL double_selector = sel_registerName("scale:");
	SEL double_pair_selector = sel_registerName("addDoubles:");
	SEL void_int64_selector = sel_registerName("observeInt64:");
	SEL void_int_selector = sel_registerName("observeInt:");
	SEL int64_zero_selector = sel_registerName("answer");
	SEL bool_zero_selector = sel_registerName("isReady");
	SEL double_zero_selector = sel_registerName("piValue");
	SEL pointer_zero_selector = sel_registerName("context");
	SEL int_zero_selector = sel_registerName("answerInt");
	SEL uint64_zero_selector = sel_registerName("answerUInt");
	SEL uint64_selector = sel_registerName("addUInt:");
	if (!class_addMethod(child, bool_selector,
		reinterpret_cast<IMP>(&IsPositive), "B@:B") ||
		!class_addMethod(child, double_selector,
			reinterpret_cast<IMP>(&Scale), "d@:d") ||
		!class_addMethod(child, double_pair_selector,
			reinterpret_cast<IMP>(&AddDoubles), "d@:dd"))
		return 10;
	if (!class_addMethod(child, void_int64_selector,
		reinterpret_cast<IMP>(&ObserveInt64), "v@:q"))
		return 10;
	if (!class_addMethod(child, void_int_selector,
		reinterpret_cast<IMP>(&ObserveInt), "v@:i"))
		return 10;
	if (!class_addMethod(child, int64_zero_selector, reinterpret_cast<IMP>(&ReturnInt64), "q@:") ||
		!class_addMethod(child, uint64_zero_selector, reinterpret_cast<IMP>(&ReturnUInt64), "Q@:") ||
		!class_addMethod(child, uint64_selector, reinterpret_cast<IMP>(&AddUInt64), "Q@:Q") ||
		!class_addMethod(child, int_zero_selector, reinterpret_cast<IMP>(&ReturnInt), "i@:") ||
		!class_addMethod(child, bool_zero_selector, reinterpret_cast<IMP>(&ReturnTrue), "B@:") ||
		!class_addMethod(child, double_zero_selector, reinterpret_cast<IMP>(&ReturnDouble), "d@:") ||
		!class_addMethod(child, pointer_zero_selector, reinterpret_cast<IMP>(&ReturnPointer), "^v@:"))
		return 10;
	const DarlingObjcValue bool_result = darling_objc_invoke1(object,
		bool_selector, DarlingObjcValue{DARLING_OBJC_BOOL, {.boolean = true}});
	const DarlingObjcValue double_result = darling_objc_invoke1(object,
		double_selector, DarlingObjcValue{DARLING_OBJC_DOUBLE, {.floating = 2.5}});
	const DarlingObjcValue void_result = darling_objc_invoke0(object,
		void_selector);
	if (bool_result.kind != DARLING_OBJC_BOOL || !bool_result.boolean ||
		darling_objc_msgSend_bool1(object, bool_selector, true) != true ||
		double_result.kind != DARLING_OBJC_DOUBLE ||
		double_result.floating != 5.0 ||
		darling_objc_msgSend_double1(object, double_selector, 3.5) != 7.0 ||
		darling_objc_msgSend_double2(object, double_pair_selector, 1.25, 2.75) != 4.0 ||
		darling_objc_msgSend_int64_0(object, int64_zero_selector) != 42 ||
		darling_objc_msgSend_uint64_0(object, uint64_zero_selector) != 42 ||
		darling_objc_msgSend_uint64_1(object, uint64_selector, 35) != 42 ||
		darling_objc_msgSend_int0(object, int_zero_selector) != 42 ||
		darling_objc_msgSend_int1(object, int_selector, 37) != 42 ||
		!darling_objc_msgSend_bool0(object, bool_zero_selector) ||
		darling_objc_msgSend_double0(object, double_zero_selector) != 3.5 ||
		void_result.kind != DARLING_OBJC_VOID)
		return 11;
	darling_objc_msgSend_void1_int64(object, void_int64_selector,
		0x123456789LL);
	if (observed_void_int64 != 0x123456789LL)
		return 20;
	darling_objc_msgSend_void1_int(object, void_int_selector, 123456);
	if (observed_void_int != 123456)
		return 21;
	SEL pointer_selector = sel_registerName("pointer:");
	SEL two_selector = sel_registerName("addTwo:");
	if (!class_addMethod(child, pointer_selector,
		reinterpret_cast<IMP>(&EchoPointer), "^v@:^v") ||
		!class_addMethod(child, two_selector,
			reinterpret_cast<IMP>(&AddTwo), "q@:qq"))
		return 12;
	void* pointer_marker = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234));
	if (darling_objc_msgSend_pointer1(object, pointer_selector, pointer_marker) !=
		pointer_marker)
		return 13;
	if (darling_objc_msgSend_pointer0(object, pointer_zero_selector) !=
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x4321)))
		return 13;
	if (static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
		objc_msgSend(object, two_selector, static_cast<std::int64_t>(19),
			static_cast<std::int64_t>(23)))) != 42)
		return 13;
	if (darling_objc_msgSend_int64_2(object, two_selector, 19, 23) != 42)
		return 18;
	int association_key = 0;
	id associated = class_createInstance(child, 0);
	objc_setAssociatedObject(object, &association_key, associated,
		OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	objc_release(associated);
	if (objc_getAssociatedObject(object, &association_key) != associated)
		return 14;
	objc_removeAssociatedObjects(object);
	if (objc_getAssociatedObject(object, &association_key) != nullptr)
		return 15;
	void* marker = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234));
	if (objc_msgSendSuper(object, root, super_pointer_selector, marker) !=
		reinterpret_cast<id>(marker))
		return 94;
	const DarlingObjcValue pointer_result = darling_objc_invoke1(object,
		pointer_selector, DarlingObjcValue{DARLING_OBJC_POINTER, {.pointer = marker}});
	const DarlingObjcValue two_result = darling_objc_invoke2(object, two_selector,
		DarlingObjcValue{DARLING_OBJC_INT64, {.integer = 19}},
		DarlingObjcValue{DARLING_OBJC_INT64, {.integer = 23}});
	if (pointer_result.kind != DARLING_OBJC_POINTER ||
		pointer_result.pointer != marker || two_result.kind != DARLING_OBJC_INT64 ||
		two_result.integer != 42)
		return 16;
	SEL rect_selector = sel_registerName("frame");
	if (!class_addMethod(child, rect_selector,
		reinterpret_cast<IMP>(&MakeRect),
		"{CGRect={CGPoint=dd}{CGSize=dd}}@:"))
		return 17;
	const DarlingObjcRect rect = darling_objc_invoke_rect0(object,
		rect_selector);
	const DarlingObjcRect direct_rect = darling_objc_msgSend_rect0(object,
		rect_selector);
	if (rect.x != 1.0 || rect.y != 2.0 || rect.width != 320.0 ||
		rect.height != 240.0 || direct_rect.x != 1.0 || direct_rect.y != 2.0 ||
		direct_rect.width != 320.0 || direct_rect.height != 240.0)
			return 18;
	SEL factory_selector = sel_registerName("factory");
	if (!class_addClassMethod(child, factory_selector,
		reinterpret_cast<IMP>(&ClassIdentity), "@@:") ||
		objc_getMetaClass("DarlingChild") == nullptr ||
		darling_objc_msgSend_class0(child, factory_selector) !=
		reinterpret_cast<id>(child))
		return 16;
	SEL swapped_factory_selector = sel_registerName("swappedFactory");
	if (!class_addClassMethod(child, swapped_factory_selector,
		reinterpret_cast<IMP>(&ClassNull), "@@:"))
		return 16;
	Method factory_method = class_getClassMethod(child, factory_selector);
	Method swapped_factory_method = class_getClassMethod(child, swapped_factory_selector);
	if (!factory_method || !swapped_factory_method ||
		darling_objc_msgSend_class0(child, swapped_factory_selector) != nullptr)
		return 22;
	method_exchangeImplementations(factory_method, swapped_factory_method);
	if (darling_objc_msgSend_class0(child, factory_selector) != nullptr ||
		darling_objc_msgSend_class0(child, swapped_factory_selector) !=
		reinterpret_cast<id>(child))
		return 22;
	method_exchangeImplementations(factory_method, swapped_factory_method);
	SEL inherited_factory_selector = sel_registerName("inheritedFactory");
	if (!class_addClassMethod(root, inherited_factory_selector,
		reinterpret_cast<IMP>(&ClassIdentity), "@@:") ||
		darling_objc_msgSend_class0(child, inherited_factory_selector) !=
		reinterpret_cast<id>(child))
		return 17;
	const int class_count = objc_getClassList(nullptr, 0);
	if (class_count < 2)
			return 18;
	std::vector<Class> classes(static_cast<std::size_t>(class_count));
	const int copied_class_count = objc_getClassList(classes.data(), class_count);
	bool found_root = false;
	bool found_child = false;
	for (int index = 0; index < copied_class_count; ++index) {
		found_root = found_root || classes[static_cast<std::size_t>(index)] == root;
		found_child = found_child || classes[static_cast<std::size_t>(index)] == child;
	}
	if (copied_class_count < class_count || !found_root || !found_child)
		return 19;
	Protocol printable = objc_allocateProtocol("DarlingPrintable");
	if (!printable)
		return 20;
	protocol_addMethodDescription(printable, selector, "@@:", true, true);
	objc_registerProtocol(printable);
	if (objc_getProtocol("DarlingPrintable") != printable ||
		!class_addProtocol(child, printable) ||
		!class_conformsToProtocol(child, printable) ||
		class_conformsToProtocol(root, printable))
		return 21;
	Protocol parent_protocol = objc_allocateProtocol("DarlingBaseProtocol");
	Protocol child_protocol = objc_allocateProtocol("DarlingChildProtocol");
	if (!parent_protocol || !child_protocol)
		return 21;
	objc_registerProtocol(parent_protocol);
	objc_registerProtocol(child_protocol);
	if (!protocol_addProtocol(child_protocol, parent_protocol) ||
		!protocol_conformsToProtocol(child_protocol, parent_protocol) ||
		!class_addProtocol(child, child_protocol) ||
		!class_conformsToProtocol(child, parent_protocol))
		return 22;
	if (darling_windows_host_symbol("_objc_msgSend") == 0 ||
		darling_windows_host_symbol("_objc_retain") == 0 ||
		darling_windows_host_symbol("_objc_getClassList") == 0 ||
		darling_windows_host_symbol("_libobjc_missing") != 0)
		return 23;
	struct RawClassData final {
		std::uint32_t flags{};
		std::uint32_t instance_start{};
		std::uint32_t instance_size{};
		std::uint32_t reserved{};
		const char* ivar_layout{};
		const char* name{};
		void* methods{};
		void* protocols{};
		void* ivars{};
		void* properties{};
	};
	struct RawClass final {
		std::uintptr_t isa{};
		std::uintptr_t superclass{};
		std::uintptr_t cache{};
		std::uintptr_t vtable{};
		std::uintptr_t data{};
	};
	RawClassData raw_data{};
	raw_data.name = "ImageRegisteredClass";
	struct RawMethod final {
		std::uintptr_t name{};
		std::uintptr_t types{};
		std::uintptr_t implementation{};
	};
	struct RawMethodList final {
		std::uint32_t entry_size{};
		std::uint32_t count{};
		RawMethod method{};
	};
	const char image_method_name[] = "imageMethod";
	const char image_method_types[] = "@@:";
	RawMethodList raw_methods{sizeof(RawMethod), 1,
		{reinterpret_cast<std::uintptr_t>(image_method_name),
			reinterpret_cast<std::uintptr_t>(image_method_types),
			reinterpret_cast<std::uintptr_t>(&ImageMethod)}};
	raw_data.methods = &raw_methods;
	RawClassData raw_meta_data{};
	RawMethodList raw_meta_methods{sizeof(RawMethod), 1,
		{reinterpret_cast<std::uintptr_t>("imageClassMethod"),
			reinterpret_cast<std::uintptr_t>(image_method_types),
			reinterpret_cast<std::uintptr_t>(&ClassIdentity)}};
	raw_meta_data.methods = &raw_meta_methods;
	struct RawProtocolMethod final {
		std::uintptr_t name{};
		std::uintptr_t types{};
	};
	struct RawProtocolMethodList final {
		std::uint32_t entry_size{};
		std::uint32_t count{};
		RawProtocolMethod method{};
	};
	struct RawProtocol final {
		std::uintptr_t isa{};
		std::uintptr_t name{};
		std::uintptr_t protocols{};
		std::uintptr_t instance_methods{};
		std::uintptr_t class_methods{};
		std::uintptr_t optional_instance_methods{};
		std::uintptr_t optional_class_methods{};
	};
	struct RawProtocolList final {
		std::uintptr_t next{};
		std::uintptr_t count{};
		std::uintptr_t protocol{};
	};
	const char image_protocol_name[] = "ImageProtocol";
	const char image_protocol_method_name[] = "imageProtocolMethod";
	RawProtocolMethodList raw_protocol_methods{sizeof(RawProtocolMethod), 1,
		{reinterpret_cast<std::uintptr_t>(image_protocol_method_name),
			reinterpret_cast<std::uintptr_t>(image_method_types)}};
	RawProtocol raw_protocol{};
	raw_protocol.name = reinterpret_cast<std::uintptr_t>(image_protocol_name);
	raw_protocol.instance_methods = reinterpret_cast<std::uintptr_t>(
		&raw_protocol_methods);
	RawProtocolList raw_protocol_list{0, 1,
		reinterpret_cast<std::uintptr_t>(&raw_protocol)};
	raw_data.protocols = &raw_protocol_list;
	const char category_protocol_name[] = "CategoryProtocol";
	RawProtocol category_protocol{};
	category_protocol.name = reinterpret_cast<std::uintptr_t>(category_protocol_name);
	RawProtocolList category_protocol_list{0, 1,
		reinterpret_cast<std::uintptr_t>(&category_protocol)};
	struct RawIvar final {
		std::uintptr_t offset_address{};
		std::uintptr_t name{};
		std::uintptr_t types{};
		std::uint32_t alignment{};
		std::uint32_t size{};
	};
	struct RawIvarList final {
		std::uint32_t entry_size{};
		std::uint32_t count{};
		RawIvar ivar{};
	};
	std::ptrdiff_t image_ivar_offset = 16;
	const char image_ivar_name[] = "imageValue";
	const char image_ivar_types[] = "q";
	RawIvarList raw_ivars{sizeof(RawIvar), 1,
		{reinterpret_cast<std::uintptr_t>(&image_ivar_offset),
			reinterpret_cast<std::uintptr_t>(image_ivar_name),
			reinterpret_cast<std::uintptr_t>(image_ivar_types), 3, 8}};
	raw_data.ivars = &raw_ivars;
	struct RawProperty final {
		std::uintptr_t name{};
		std::uintptr_t attributes{};
	};
	struct RawPropertyList final {
		std::uint32_t entry_size{};
		std::uint32_t count{};
		RawProperty property{};
	};
	const char image_property_name[] = "imageProperty";
	const char image_property_attributes[] = "Tq,N,V_imageValue";
	RawPropertyList raw_properties{sizeof(RawProperty), 1,
		{reinterpret_cast<std::uintptr_t>(image_property_name),
			reinterpret_cast<std::uintptr_t>(image_property_attributes)}};
	raw_data.properties = &raw_properties;
	RawClass raw_class{};
	RawClass raw_meta_class{};
	raw_meta_class.data = reinterpret_cast<std::uintptr_t>(&raw_meta_data);
	raw_class.isa = reinterpret_cast<std::uintptr_t>(&raw_meta_class);
	raw_class.data = reinterpret_cast<std::uintptr_t>(&raw_data);
	const std::uintptr_t raw_class_address = reinterpret_cast<std::uintptr_t>(&raw_class);
	if (darling_objc_register_macho_classlist(&raw_class_address,
		sizeof(raw_class_address)) != 1 ||
		objc_getClass("ImageRegisteredClass") == nullptr ||
		class_getMethodImplementation(objc_getClass("ImageRegisteredClass"),
			sel_registerName(image_method_name)) !=
		reinterpret_cast<IMP>(&ImageMethod) ||
		std::string(class_getMethodTypeEncoding(objc_getClass("ImageRegisteredClass"),
			sel_registerName(image_method_name))) != "@@:")
		return 24;
	Method image_method_metadata = class_getInstanceMethod(
		objc_getClass("ImageRegisteredClass"), sel_registerName(image_method_name));
	if (!image_method_metadata ||
		method_getName(image_method_metadata) != sel_registerName(image_method_name) ||
		method_getImplementation(image_method_metadata) != reinterpret_cast<IMP>(&ImageMethod) ||
		std::string(method_getTypeEncoding(image_method_metadata)) != "@@:")
		return 44;
	id introspection_object = class_createInstance(
		objc_getClass("ImageRegisteredClass"), 0);
	Method class_method_metadata = class_getClassMethod(
		objc_getClass("ImageRegisteredClass"), sel_registerName("imageClassMethod"));
	if (!class_respondsToSelector(objc_getClass("ImageRegisteredClass"),
		sel_registerName(image_method_name)) ||
		class_respondsToSelector(objc_getClass("ImageRegisteredClass"),
			sel_registerName("missingMethod")) ||
		!introspection_object ||
		std::string(object_getClassName(introspection_object)) !=
			"ImageRegisteredClass" || object_isClass(introspection_object) ||
		!object_isClass(reinterpret_cast<id>(objc_getClass("ImageRegisteredClass"))) ||
		!class_method_metadata ||
		method_getImplementation(class_method_metadata) !=
			reinterpret_cast<IMP>(&ClassIdentity) ||
		darling_windows_host_symbol("_class_getInstanceMethod") == 0 ||
		darling_windows_host_symbol("_class_getClassMethod") == 0 ||
		darling_windows_host_symbol("_object_getClassName") == 0)
	{
		objc_release(introspection_object);
		return 46;
	}
	objc_release(introspection_object);
	const char* required_objc_symbols[] = {
		"_class_copyMethodList", "_method_getName",
		"_method_getImplementation", "_method_getTypeEncoding",
		"_class_getInstanceVariable", "_ivar_getName",
		"_ivar_getTypeEncoding", "_ivar_getOffset", "_class_copyIvarList",
		"_class_getProperty", "_property_getName", "_property_getAttributes",
		"_class_copyPropertyList", "_protocol_getName",
		"_objc_getProtocolList", "_class_getProtocolList",
		"_class_copyProtocolList"};
	for (const char* symbol : required_objc_symbols) {
		if (darling_windows_host_symbol(symbol) == 0)
			return 47;
	}
	Protocol image_protocol = objc_getProtocol(image_protocol_name);
	if (!image_protocol ||
		!class_conformsToProtocol(objc_getClass("ImageRegisteredClass"), image_protocol) ||
		!protocol_hasMethodDescription(image_protocol,
			sel_registerName(image_protocol_method_name), true, true))
		return 25;
	const auto image_protocol_description = protocol_getMethodDescription(
		image_protocol, sel_registerName(image_protocol_method_name), true, true);
	if (image_protocol_description.name != sel_registerName(image_protocol_method_name) ||
		!image_protocol_description.types ||
		std::string(image_protocol_description.types) != "@@:" ||
		darling_windows_host_symbol("_protocol_getMethodDescription") == 0)
		return 48;
	Ivar image_ivar = class_getInstanceVariable(objc_getClass("ImageRegisteredClass"),
		image_ivar_name);
	if (!image_ivar || std::string(ivar_getName(image_ivar)) != image_ivar_name ||
		std::string(ivar_getTypeEncoding(image_ivar)) != image_ivar_types ||
		ivar_getOffset(image_ivar) != image_ivar_offset)
		return 26;
	objc_property_t image_property = class_getProperty(
		objc_getClass("ImageRegisteredClass"), image_property_name);
	if (!image_property ||
		std::string(property_getName(image_property)) != image_property_name ||
		std::string(property_getAttributes(image_property)) != image_property_attributes)
		return 37;
	SEL image_class_method_selector = sel_registerName("imageClassMethod");
	if (darling_objc_msgSend_class0(objc_getClass("ImageRegisteredClass"),
		image_class_method_selector) !=
		reinterpret_cast<id>(objc_getClass("ImageRegisteredClass")))
		return 27;
	struct RawCategory final {
		std::uintptr_t name{};
		std::uintptr_t cls{};
		std::uintptr_t instance_methods{};
		std::uintptr_t class_methods{};
		std::uintptr_t protocols{};
		std::uintptr_t instance_properties{};
		std::uintptr_t class_properties{};
	};
	const char category_method_name[] = "categoryMethod";
	const char category_class_method_name[] = "categoryClassMethod";
	const char category_property_name[] = "categoryProperty";
	const char category_property_attributes[] = "T@,N,V_categoryValue";
	RawMethodList category_methods{sizeof(RawMethod), 1,
		{reinterpret_cast<std::uintptr_t>(category_method_name),
			reinterpret_cast<std::uintptr_t>(image_method_types),
			reinterpret_cast<std::uintptr_t>(&ImageMethod)}};
	RawMethodList category_class_methods{sizeof(RawMethod), 1,
		{reinterpret_cast<std::uintptr_t>(category_class_method_name),
			reinterpret_cast<std::uintptr_t>(image_method_types),
			reinterpret_cast<std::uintptr_t>(&ClassIdentity)}};
	RawCategory category{0, raw_class_address,
		reinterpret_cast<std::uintptr_t>(&category_methods),
		reinterpret_cast<std::uintptr_t>(&category_class_methods),
		reinterpret_cast<std::uintptr_t>(&category_protocol_list),
		reinterpret_cast<std::uintptr_t>(&raw_properties), 0};
	const std::uintptr_t category_address = reinterpret_cast<std::uintptr_t>(&category);
	if (darling_objc_register_macho_categories(&category_address,
		sizeof(category_address)) != 1 ||
		class_getMethodImplementation(objc_getClass("ImageRegisteredClass"),
			sel_registerName(category_method_name)) !=
		reinterpret_cast<IMP>(&ImageMethod) ||
		method_getImplementation(class_getClassMethod(
			objc_getClass("ImageRegisteredClass"),
			sel_registerName(category_class_method_name))) !=
		reinterpret_cast<IMP>(&ClassIdentity) ||
		darling_objc_msgSend_class0(objc_getClass("ImageRegisteredClass"),
			sel_registerName(category_class_method_name)) !=
		reinterpret_cast<id>(objc_getClass("ImageRegisteredClass")))
		return 23;
	unsigned int copied_method_count = 0;
	Method* copied_methods = class_copyMethodList(
		objc_getClass("ImageRegisteredClass"), &copied_method_count);
	bool saw_image_method = false;
	bool saw_category_method = false;
	for (unsigned int index = 0; copied_methods && index < copied_method_count; ++index) {
		const char* method_name = sel_getName(method_getName(copied_methods[index]));
		if (method_name && std::string(method_name) == image_method_name)
			saw_image_method = true;
		if (method_name && std::string(method_name) == category_method_name)
			saw_category_method = true;
	}
	if (!copied_methods || copied_method_count != 2 || !saw_image_method ||
		!saw_category_method) {
		std::free(copied_methods);
		return 45;
	}
	std::free(copied_methods);
	RawPropertyList category_properties{sizeof(RawProperty), 1,
		{reinterpret_cast<std::uintptr_t>(category_property_name),
			reinterpret_cast<std::uintptr_t>(category_property_attributes)}};
	category.instance_properties = reinterpret_cast<std::uintptr_t>(&category_properties);
	if (darling_objc_register_macho_categories(&category_address,
		 sizeof(category_address)) != 1)
		return 38;
	objc_property_t category_property = class_getProperty(
		objc_getClass("ImageRegisteredClass"), category_property_name);
	if (!category_property ||
		std::string(property_getName(category_property)) != category_property_name ||
		std::string(property_getAttributes(category_property)) !=
			category_property_attributes)
		return 39;
	Protocol category_protocol_metadata = objc_getProtocol(category_protocol_name);
	if (!category_protocol_metadata ||
		!class_conformsToProtocol(objc_getClass("ImageRegisteredClass"),
			category_protocol_metadata))
		return 40;
	if (std::string(protocol_getName(category_protocol_metadata)) !=
		category_protocol_name ||
		class_getProtocolList(objc_getClass("ImageRegisteredClass"), nullptr, 0) < 2 ||
		objc_getProtocolList(nullptr, 0) < 5)
		return 41;
	Protocol class_protocols[4]{};
	const unsigned int class_protocol_count = class_getProtocolList(
		objc_getClass("ImageRegisteredClass"), class_protocols, 4);
	bool saw_category_protocol = false;
	for (unsigned int index = 0; index < class_protocol_count && index < 4; ++index) {
		if (class_protocols[index] == category_protocol_metadata)
			saw_category_protocol = true;
	}
	if (!saw_category_protocol)
		return 42;
	unsigned int copied_ivar_count = 0;
	Ivar* copied_ivars = class_copyIvarList(objc_getClass("ImageRegisteredClass"),
		&copied_ivar_count);
	unsigned int copied_property_count = 0;
	objc_property_t* copied_properties = class_copyPropertyList(
		objc_getClass("ImageRegisteredClass"), &copied_property_count);
	unsigned int copied_protocol_count = 0;
	Protocol* copied_protocols = class_copyProtocolList(
		objc_getClass("ImageRegisteredClass"), &copied_protocol_count);
	if (!copied_ivars || copied_ivar_count != 1 || !copied_properties ||
		copied_property_count != 2 || !copied_protocols ||
		copied_protocol_count != 2) {
		std::free(copied_ivars);
		std::free(copied_properties);
		std::free(copied_protocols);
		return 43;
	}
	std::free(copied_ivars);
	std::free(copied_properties);
	std::free(copied_protocols);
	const char image_selector_name[] = "imageSelector";
	const std::uintptr_t image_selector_ref =
		reinterpret_cast<std::uintptr_t>(image_selector_name);
	if (darling_objc_register_macho_selrefs(&image_selector_ref,
		sizeof(image_selector_ref)) != 1 ||
		std::string(sel_getName(sel_registerName(image_selector_name))) !=
		"imageSelector")
		return 24;
	id retained = objc_retain(object);
	if (retained != object || objc_retainAutoreleasedReturnValue(object) != object)
		return 23;
	objc_release(object);
	void* pool = objc_autoreleasePoolPush();
	if (!pool || objc_autorelease(object) != object || object_getClass(object) != child)
		return 24;
	objc_autoreleasePoolPop(pool);
	id nested_object = class_createInstance(child, 0);
	if (!nested_object || objc_retain(nested_object) != nested_object ||
		objc_retain(nested_object) != nested_object)
		return 25;
	id nested_weak = nullptr;
	if (objc_initWeak(&nested_weak, nested_object) != nested_object)
		return 26;
	void* outer_pool = objc_autoreleasePoolPush();
	objc_autorelease(nested_object);
	void* inner_pool = objc_autoreleasePoolPush();
	objc_autorelease(nested_object);
	objc_autoreleasePoolPop(inner_pool);
	objc_autoreleasePoolPop(outer_pool);
	objc_release(nested_object);
	if (objc_loadWeak(&nested_weak) != nullptr)
		return 27;
	objc_destroyWeak(&nested_weak);
	DarlingObjcCallbackBlock* callback = darling_objc_block_create1(
		&CallbackIdentity, nullptr);
	if (!callback || darling_objc_block_invoke1(callback, object) != object ||
		darling_objc_block_copy(callback) != callback)
		return 29;
	darling_objc_block_release(callback);
	darling_objc_block_release(callback);
	SEL apply_selector = sel_registerName("apply:");
	if (!class_addMethod(child, apply_selector,
		reinterpret_cast<IMP>(&ApplyBlock), "@@:@?"))
		return 30;
	DarlingObjcCallbackBlock* bridge_callback = darling_objc_block_create1(
		&CallbackIdentity, nullptr);
	const DarlingObjcValue block_result = darling_objc_invoke1(object,
		apply_selector, DarlingObjcValue{DARLING_OBJC_BLOCK, {.block = bridge_callback}});
	if (!bridge_callback || block_result.kind != DARLING_OBJC_OBJECT ||
		block_result.object != reinterpret_cast<id>(bridge_callback))
		return 31;
	if (darling_objc_msgSend_block1(object, apply_selector, bridge_callback) !=
		reinterpret_cast<id>(bridge_callback))
		return 33;
	darling_objc_block_release(bridge_callback);
	if (objc_msgSend(object, self_selector) != object ||
		darling_objc_msgSend_object0(object, self_selector) != object ||
		objc_msgSend(object, object_selector, object) != object ||
		static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(
			objc_msgSend(object, integer_selector, static_cast<std::int64_t>(35)))) != 42)
		return 32;
	DarlingObjcAppleBlock* apple_callback = darling_objc_apple_block_create1(
		&CallbackIdentity, nullptr);
	if (!apple_callback || darling_objc_apple_block_copy(apple_callback) !=
		apple_callback || std::string(darling_objc_apple_block_signature(
		apple_callback)) != "@@:" ||
		darling_objc_apple_block_invoke1(apple_callback, object) != object)
		return 36;
	if (objc_retainBlock(apple_callback) != apple_callback ||
		darling_windows_host_symbol("_Block_copy") == 0 ||
		darling_windows_host_symbol("_Block_release") == 0)
		return 37;
	objc_releaseBlock(apple_callback);
	darling_objc_apple_block_release(apple_callback);
	darling_objc_apple_block_release(apple_callback);
	AppleBlockTestDescriptor external_descriptor{
		0, sizeof(AppleBlockTestLiteral), &CopyAppleBlockCapture,
		&DisposeAppleBlockCapture, "@@:"};
	AppleBlockTestLiteral stack_block{
		nullptr, (1 << 25) | (1 << 30), 0, &InvokeAppleBlockCapture,
		&external_descriptor, 41};
	void* copied_block = objc_retainBlock(&stack_block);
	if (copied_block == nullptr || copied_block == &stack_block ||
		apple_block_copies != 1 ||
		static_cast<AppleBlockTestLiteral*>(copied_block)->invoke(
			copied_block, object) != object)
		return 38;
	if (objc_retainBlock(copied_block) != copied_block) {
		objc_releaseBlock(copied_block);
		objc_releaseBlock(copied_block);
		return 39;
	}
	objc_releaseBlock(copied_block);
	if (apple_block_disposes != 0) {
		objc_releaseBlock(copied_block);
		return 40;
	}
	objc_releaseBlock(copied_block);
	if (apple_block_disposes != 1)
		return 41;
	AppleBlockTestLiteral global_block{
		nullptr, (1 << 28) | (1 << 30), 0, &InvokeAppleBlockCapture,
		&external_descriptor, 41};
	if (objc_retainBlock(&global_block) != &global_block)
		return 42;
	objc_releaseBlock(&global_block);
	id captured = objc_retain(object);
	id captured_destination = nullptr;
	_Block_object_assign(&captured_destination, captured, 3);
	if (captured_destination != object ||
		darling_windows_host_symbol("_Block_object_assign") == 0 ||
		darling_windows_host_symbol("_Block_object_dispose") == 0)
		return 43;
	_Block_object_dispose(captured_destination, 3);
	objc_release(captured);
	AppleByrefTestCell byref_cell{};
	byref_cell.forwarding = &byref_cell;
	byref_cell.flags = 1 << 25;
	byref_cell.size = sizeof(AppleByrefTestCell);
	byref_cell.keep_helper = &CopyAppleByrefCapture;
	byref_cell.destroy_helper = &DestroyAppleByrefCapture;
	byref_cell.captured = 73;
	void* byref_destination = nullptr;
	void* byref_second_destination = nullptr;
	_Block_object_assign(&byref_destination, &byref_cell, 8);
	_Block_object_assign(&byref_second_destination, &byref_cell, 8);
	if (byref_destination == nullptr || byref_destination == &byref_cell ||
		byref_destination != byref_second_destination ||
		apple_byref_copies != 1 ||
		static_cast<AppleByrefTestCell*>(byref_destination)->captured != 73)
		return 44;
	_Block_object_dispose(byref_destination, 8);
	if (apple_byref_destroys != 0)
		return 45;
	_Block_object_dispose(byref_second_destination, 8);
	if (apple_byref_destroys != 1 || byref_cell.forwarding != &byref_cell)
		return 46;
	id weak_destination = nullptr;
	_Block_object_assign(&weak_destination, object, 3 | 16);
	if (weak_destination != object)
		return 47;
	_Block_object_dispose(weak_destination, 3 | 16);
	id weak_slot = nullptr;
	id retained_weak = nullptr;
	if (objc_initWeak(&weak_slot, object) != object ||
		objc_loadWeak(&weak_slot) != object ||
		(retained_weak = objc_loadWeakRetained(&weak_slot)) != object ||
		darling_windows_host_symbol("objc_loadWeakRetained") == 0)
		return 32;
	objc_release(retained_weak);
	id copied_slot = nullptr;
	if (objc_copyWeak(&copied_slot, &weak_slot) != object ||
		objc_loadWeak(&copied_slot) != object)
		return 33;
	id moved_slot = nullptr;
	if (objc_moveWeak(&moved_slot, &copied_slot) != object ||
		copied_slot != nullptr || objc_loadWeak(&moved_slot) != object)
		return 36;
	if (objc_moveWeak(&moved_slot, &moved_slot) != object)
		return 37;
	if (objc_copyWeak(&moved_slot, &moved_slot) != object)
		return 39;
	id externally_initialized = object;
	id externally_moved = nullptr;
	if (objc_moveWeak(&externally_moved, &externally_initialized) != object ||
		externally_initialized != nullptr ||
		objc_loadWeak(&externally_moved) != object)
		return 38;
	objc_destroyWeak(&weak_slot);
	if (objc_loadWeak(&weak_slot) != nullptr)
		return 34;
	objc_release(object);
	if (copied_slot != nullptr || objc_loadWeak(&copied_slot) != nullptr ||
		moved_slot != nullptr || objc_loadWeak(&moved_slot) != nullptr ||
		externally_moved != nullptr || objc_loadWeak(&externally_moved) != nullptr)
		return 35;
	objc_destroyWeak(&copied_slot);
	objc_destroyWeak(&moved_slot);
	objc_destroyWeak(&externally_moved);
	std::puts("DARWIN_OBJC_REGISTRY=PASS");
	return 0;
}
