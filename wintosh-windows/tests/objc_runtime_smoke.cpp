/*
 * This file is part of the Darling Windows port.
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3.
 */

#include "darling_windows_objc.h"

#include <cstring>
#include <iostream>
#include <cstdlib>

namespace {

void RuntimeMethod(id, SEL)
{
}

void SubRuntimeMethod(id, SEL)
{
}

void NoArgVoidMethod(id, SEL)
{
}

int NoArgIntMethod(id, SEL)
{
	return 42;
}

id EchoCallback(void*, id argument)
{
	return argument;
}

id EchoMethod(id, SEL, id argument)
{
	return argument;
}

std::int64_t AddOneMethod(id, SEL, std::int64_t argument)
{
	return argument + 1;
}

std::int64_t AddTwoMethod(id, SEL, std::int64_t first, std::int64_t second)
{
	return first + second;
}

bool ToggleMethod(id, SEL, bool value)
{
	return !value;
}

void* EchoPointerMethod(id, SEL, void* value)
{
	return value;
}

id CallBlockMethod(id receiver, SEL, DarlingObjcCallbackBlock* block)
{
	return darling_objc_block_invoke1(block, receiver);
}

id ClassProbeMethod(Class cls, SEL)
{
	return reinterpret_cast<id>(cls);
}

double PiMethod(id, SEL)
{
	return 3.5;
}

double ScaleMethod(id, SEL, double value)
{
	return value * 2.0;
}

double AddDoublesMethod(id, SEL, double first, double second)
{
	return first + second;
}

DarlingObjcRect RectMethod(id, SEL)
{
	return DarlingObjcRect{1.0, 2.0, 3.0, 4.0};
}

}

int main()
{
	SEL selector = sel_registerName("darlingRuntimeProbe:");
	if (!selector || std::strcmp(sel_getName(selector), "darlingRuntimeProbe:") != 0)
		return 2;

	Class cls = objc_allocateClassPair(nullptr, "DarlingRuntimeSmokeClass", 0);
	if (!cls)
		return 3;
	if (!class_addMethod(cls, selector, reinterpret_cast<IMP>(&RuntimeMethod), "v@:@"))
		return 4;
	const objc_property_attribute_t property_attributes[] = {
		{"T", "@\"NSString\""}, {"N", ""}, {"V", "_title"}};
	if (!class_addProperty(cls, "title", property_attributes, 3))
		return 4;
	const auto title_property = class_getProperty(cls, "title");
	if (!title_property || std::strcmp(property_getName(title_property), "title") != 0 ||
		std::strcmp(property_getAttributes(title_property),
			"T@\"NSString\",N,V_title") != 0)
		return 4;
	if (class_addProperty(cls, "title", property_attributes, 3))
		return 4;
	unsigned int property_count = 0;
	auto* properties = class_copyPropertyList(cls, &property_count);
	if (!properties || property_count != 1 || properties[0] != title_property)
		return 4;
	std::free(properties);
	SEL class_probe_selector = sel_registerName("classProbe");
	if (!class_addClassMethod(cls, class_probe_selector,
		reinterpret_cast<IMP>(&ClassProbeMethod), "@@:"))
		return 5;
	SEL echo_selector = sel_registerName("echoObject:");
	SEL add_selector = sel_registerName("addOne:");
	SEL add_two_selector = sel_registerName("add:and:");
	SEL toggle_selector = sel_registerName("toggle:");
	SEL pointer_selector = sel_registerName("pointer:");
	SEL block_selector = sel_registerName("callBlock:");
	SEL pi_selector = sel_registerName("floatingValue");
	SEL scale_selector = sel_registerName("scale:");
	SEL add_doubles_selector = sel_registerName("addDouble:and:");
	SEL rect_selector = sel_registerName("frame");
	SEL noarg_void_selector = sel_registerName("noArgVoid");
	SEL noarg_int_selector = sel_registerName("noArgInt");
	if (!class_addMethod(cls, echo_selector, reinterpret_cast<IMP>(&EchoMethod), "@@:@") ||
		!class_addMethod(cls, add_selector, reinterpret_cast<IMP>(&AddOneMethod), "q@:q") ||
		!class_addMethod(cls, add_two_selector, reinterpret_cast<IMP>(&AddTwoMethod), "q@:qq") ||
		!class_addMethod(cls, toggle_selector, reinterpret_cast<IMP>(&ToggleMethod), "B@:B") ||
		!class_addMethod(cls, pointer_selector, reinterpret_cast<IMP>(&EchoPointerMethod), "^v@:^v") ||
		!class_addMethod(cls, block_selector, reinterpret_cast<IMP>(&CallBlockMethod), "@@:@?") ||
		!class_addMethod(cls, pi_selector, reinterpret_cast<IMP>(&PiMethod), "d@:") ||
		!class_addMethod(cls, scale_selector, reinterpret_cast<IMP>(&ScaleMethod), "d@:d") ||
		!class_addMethod(cls, add_doubles_selector, reinterpret_cast<IMP>(&AddDoublesMethod), "d@:dd") ||
		!class_addMethod(cls, rect_selector, reinterpret_cast<IMP>(&RectMethod), "{DarlingObjcRect=dddd}@:") ||
		!class_addMethod(cls, noarg_void_selector, reinterpret_cast<IMP>(&NoArgVoidMethod), "v@:") ||
		!class_addMethod(cls, noarg_int_selector, reinterpret_cast<IMP>(&NoArgIntMethod), "i@:"))
		return 5;

	Protocol protocol = objc_allocateProtocol("DarlingRuntimeSmokeProtocol");
	if (!protocol)
		return 6;
	Protocol parent_protocol = objc_allocateProtocol("DarlingRuntimeSmokeParentProtocol");
	if (!parent_protocol)
		return 7;
	SEL inherited_selector = sel_registerName("inheritedProbe:");
	protocol_addMethodDescription(parent_protocol, inherited_selector, "v@:@", true, true);
	objc_registerProtocol(parent_protocol);
	if (!protocol_addProtocol(protocol, parent_protocol) ||
		!protocol_conformsToProtocol(protocol, parent_protocol))
		return 8;
	if (!protocol_hasMethodDescription(protocol, inherited_selector, true, true) ||
		protocol_getMethodDescription(protocol, inherited_selector, true, true).name !=
			inherited_selector)
		return 9;
	protocol_addMethodDescription(protocol, selector, "v@:@", true, true);
	objc_registerProtocol(protocol);
	if (!class_addProtocol(cls, protocol) || !class_conformsToProtocol(cls, protocol))
		return 10;
	if (!protocol_addProtocol(parent_protocol, protocol) ||
		!protocol_conformsToProtocol(parent_protocol, protocol) ||
		!class_conformsToProtocol(cls, parent_protocol))
		return 11;
	objc_registerClassPair(cls);
	Class meta = objc_getMetaClass("DarlingRuntimeSmokeClass");
	if (!meta || meta == cls || class_getSuperclass(meta) != nullptr ||
		!object_isClass(reinterpret_cast<id>(meta)) ||
		!class_getInstanceMethod(meta, class_probe_selector) ||
		method_getImplementation(class_getInstanceMethod(meta, class_probe_selector)) !=
			reinterpret_cast<IMP>(&ClassProbeMethod) ||
		darling_objc_msgSend_class0(meta, class_probe_selector) !=
			reinterpret_cast<id>(cls))
		return 16;
	SEL late_class_selector = sel_registerName("lateClassProbe");
	if (!class_addClassMethod(cls, late_class_selector,
		reinterpret_cast<IMP>(&ClassProbeMethod), "@@:") ||
		!class_getInstanceMethod(meta, late_class_selector) ||
		darling_objc_msgSend_class0(meta, late_class_selector) !=
		reinterpret_cast<id>(cls))
		return 16;
	if (darling_objc_msgSend_class0(cls, class_probe_selector) !=
		reinterpret_cast<id>(cls))
		return 16;
	Method class_method = class_getClassMethod(cls, class_probe_selector);
	if (!class_method || method_getName(class_method) != class_probe_selector ||
		method_getImplementation(class_method) != reinterpret_cast<IMP>(&ClassProbeMethod) ||
		std::strcmp(method_getTypeEncoding(class_method), "@@:") != 0)
		return 17;
	Class subclass = objc_allocateClassPair(cls, "DarlingRuntimeSmokeSubclass", 0);
	if (!subclass)
		return 18;
	if (!class_addMethod(subclass, selector, reinterpret_cast<IMP>(&SubRuntimeMethod), "v@:@"))
		return 19;
	objc_registerClassPair(subclass);
	if (class_getSuperclass(subclass) != cls ||
		!class_getInstanceMethod(subclass, selector) ||
		method_getImplementation(class_getInstanceMethod(subclass, selector)) !=
			reinterpret_cast<IMP>(&SubRuntimeMethod) ||
		method_getImplementation(class_getInstanceMethod(cls, selector)) !=
			reinterpret_cast<IMP>(&RuntimeMethod) ||
		!class_getClassMethod(subclass, class_probe_selector) ||
		darling_objc_msgSend_class0(subclass, class_probe_selector) !=
			reinterpret_cast<id>(subclass))
		return 20;

	if (objc_getClass("DarlingRuntimeSmokeClass") != cls ||
		!class_respondsToSelector(cls, selector))
		return 11;
	Method method = class_getInstanceMethod(cls, selector);
	if (!method || method_getImplementation(method) != reinterpret_cast<IMP>(&RuntimeMethod) ||
		std::strcmp(method_getTypeEncoding(method), "v@:@") != 0)
		return 12;
	if (!protocol_hasMethodDescription(protocol, selector, true, true))
		return 13;
	if (protocol_getMethodDescription(protocol, selector, true, true).name != selector)
		return 14;
	unsigned int method_count = 0;
	Method* methods = class_copyMethodList(cls, &method_count);
	if (!methods || method_count != 13 || method_getName(methods[0]) == nullptr)
		return 15;
	std::free(methods);
	unsigned int protocol_count = 0;
	Protocol* protocols = class_copyProtocolList(cls, &protocol_count);
	if (!protocols || protocol_count != 1 || protocols[0] != protocol)
		return 16;
	std::free(protocols);
	Class class_buffer[4]{};
	if (objc_getClassList(class_buffer, 4) < 1)
		return 17;
	Protocol protocol_buffer[4]{};
	if (objc_getProtocolList(protocol_buffer, 4) < 1)
		return 18;

	id object = class_createInstance(cls, 0);
	if (!object || object_getClass(object) != cls || object_isClass(object))
		return 19;
	if (objc_msgSend(object, noarg_void_selector) != nullptr ||
		reinterpret_cast<std::intptr_t>(objc_msgSend(object, noarg_int_selector)) != 42 ||
		reinterpret_cast<std::intptr_t>(objc_msgSendSuper(object, cls, noarg_int_selector)) != 42)
		return 36;
	DarlingObjcCallbackBlock* block = darling_objc_block_create1(&EchoCallback, nullptr);
	if (!block)
		return 17;
	DarlingObjcValue echo = darling_objc_invoke1(object, echo_selector,
		DarlingObjcValue{DARLING_OBJC_OBJECT, {.object = object}});
	if (echo.kind != DARLING_OBJC_OBJECT || echo.object != object)
		return 18;
	DarlingObjcValue incremented = darling_objc_invoke1(object, add_selector,
		DarlingObjcValue{DARLING_OBJC_INT64, {.integer = 41}});
	if (incremented.kind != DARLING_OBJC_INT64 || incremented.integer != 42)
		return 19;
	DarlingObjcValue summed = darling_objc_invoke2(object, add_two_selector,
		DarlingObjcValue{DARLING_OBJC_INT64, {.integer = 19}},
		DarlingObjcValue{DARLING_OBJC_INT64, {.integer = 23}});
	if (summed.kind != DARLING_OBJC_INT64 || summed.integer != 42)
		return 20;
	DarlingObjcValue double_sum = darling_objc_invoke2(object, add_doubles_selector,
		DarlingObjcValue{DARLING_OBJC_DOUBLE, {.floating = 1.25}},
		DarlingObjcValue{DARLING_OBJC_DOUBLE, {.floating = 2.75}});
	if (double_sum.kind != DARLING_OBJC_DOUBLE || double_sum.floating != 4.0)
		return 21;
	DarlingObjcValue block_result = darling_objc_invoke1(object, block_selector,
		DarlingObjcValue{DARLING_OBJC_BLOCK, {.block = block}});
	if (block_result.kind != DARLING_OBJC_OBJECT || block_result.object != object)
		return 22;
	DarlingObjcValue toggled = darling_objc_invoke1(object, toggle_selector,
		DarlingObjcValue{DARLING_OBJC_BOOL, {.boolean = true}});
	if (toggled.kind != DARLING_OBJC_BOOL || toggled.boolean)
		return 23;
	void* marker = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234));
	DarlingObjcValue pointed = darling_objc_invoke1(object, pointer_selector,
		DarlingObjcValue{DARLING_OBJC_POINTER, {.pointer = marker}});
	if (pointed.kind != DARLING_OBJC_POINTER || pointed.pointer != marker)
		return 24;
	DarlingObjcValue floating = darling_objc_invoke0(object, pi_selector);
	if (floating.kind != DARLING_OBJC_DOUBLE || floating.floating != 3.5)
		return 25;
	DarlingObjcValue scaled = darling_objc_invoke1(object, scale_selector,
		DarlingObjcValue{DARLING_OBJC_DOUBLE, {.floating = 2.25}});
	if (scaled.kind != DARLING_OBJC_DOUBLE || scaled.floating != 4.5)
		return 26;
	DarlingObjcRect rect = darling_objc_invoke_rect0(object, rect_selector);
	if (rect.x != 1.0 || rect.y != 2.0 || rect.width != 3.0 || rect.height != 4.0)
		return 27;
	const int associationKey = 0;
	objc_setAssociatedObject(object, &associationKey, object,
		OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	if (objc_getAssociatedObject(object, &associationKey) != object)
		return 28;
	objc_removeAssociatedObjects(object);
	void* pool = objc_autoreleasePoolPush();
	if (!pool)
		return 29;
	// Keep the caller's ownership while the autorelease pool consumes its
	// scheduled release; later weak/block probes still use this object.
	objc_retain(object);
	if (objc_autorelease(object) != object)
		return 30;
	objc_autoreleasePoolPop(pool);
	id weak = nullptr;
	if (objc_initWeak(&weak, object) != object || objc_loadWeak(&weak) != object)
		return 31;
	id copied = nullptr;
	if (objc_copyWeak(&copied, &weak) != object || objc_loadWeak(&copied) != object)
		return 32;
	objc_destroyWeak(&copied);
	objc_destroyWeak(&weak);
	if (darling_objc_block_invoke1(block, object) != object)
		return 33;
	DarlingObjcCallbackBlock* block_copy = darling_objc_block_copy(block);
	if (!block_copy || darling_objc_block_invoke1(block_copy, object) != object)
		return 34;
	darling_objc_block_release(block_copy);
	darling_objc_block_release(block);
	DarlingObjcAppleBlock* apple_block = darling_objc_apple_block_create1(&EchoCallback, nullptr);
	if (!apple_block || darling_objc_apple_block_invoke1(apple_block, object) != object ||
		!darling_objc_apple_block_signature(apple_block))
		return 35;
	darling_objc_apple_block_release(apple_block);
	objc_release(object);

	std::cout << "OBJC_RUNTIME=PASS CLASS_PROTOCOL_METHOD_ASSOCIATION\n";
	return 0;
}
