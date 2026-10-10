/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <cstddef>
#include <cstdarg>
#include <cstdint>

extern "C" {

struct objc_selector;
struct objc_class;
struct objc_object;
struct objc_protocol;
struct objc_method;
struct DarlingObjcCallbackBlock;
struct DarlingObjcAppleBlock;
struct objc_ivar;
struct objc_property;

using SEL = objc_selector*;
using Class = objc_class*;
using id = objc_object*;
using IMP = void*;
using Protocol = objc_protocol*;
using Method = objc_method*;
using Ivar = objc_ivar*;
using objc_property_t = objc_property*;
struct objc_property_attribute_t final {
	const char* name;
	const char* value;
};

using objc_AssociationPolicy = std::uintptr_t;
constexpr objc_AssociationPolicy OBJC_ASSOCIATION_ASSIGN = 0;
constexpr objc_AssociationPolicy OBJC_ASSOCIATION_RETAIN_NONATOMIC = 1;
constexpr objc_AssociationPolicy OBJC_ASSOCIATION_COPY_NONATOMIC = 3;
constexpr objc_AssociationPolicy OBJC_ASSOCIATION_RETAIN = 0x301;
constexpr objc_AssociationPolicy OBJC_ASSOCIATION_COPY = 0x303;

struct objc_method_description final {
	SEL name;
	const char* types;
};

SEL sel_registerName(const char* name);
const char* sel_getName(SEL selector);

Class objc_getClass(const char* name);
Class objc_getMetaClass(const char* name);
int objc_getClassList(Class* buffer, int buffer_count);
std::size_t darling_objc_register_macho_classlist(const void* classlist,
	std::size_t bytes);
std::size_t darling_objc_register_macho_categories(const void* categories,
	std::size_t bytes);
std::size_t darling_objc_register_macho_selrefs(const void* selrefs,
	std::size_t bytes);
struct darling_objc_image_token;
darling_objc_image_token* darling_objc_begin_image_registration();
void darling_objc_end_image_registration(darling_objc_image_token* token);
void darling_objc_unregister_image(darling_objc_image_token* token);
Protocol objc_allocateProtocol(const char* name);
Protocol objc_getProtocol(const char* name);
const char* protocol_getName(Protocol protocol);
unsigned int objc_getProtocolList(Protocol* buffer, unsigned int buffer_count);
unsigned int class_getProtocolList(Class cls, Protocol* buffer,
	unsigned int buffer_count);
void protocol_addMethodDescription(Protocol protocol, SEL selector,
	const char* types, bool required, bool instance);
void objc_registerProtocol(Protocol protocol);
bool class_addProtocol(Class cls, Protocol protocol);
bool class_conformsToProtocol(Class cls, Protocol protocol);
bool protocol_addProtocol(Protocol protocol, Protocol parent);
bool protocol_conformsToProtocol(Protocol protocol, Protocol parent);
bool protocol_hasMethodDescription(Protocol protocol, SEL selector,
	bool required, bool instance);
objc_method_description protocol_getMethodDescription(Protocol protocol,
	SEL selector, bool required, bool instance);
Class objc_allocateClassPair(Class superclass, const char* name,
	std::size_t extra_bytes);
void objc_registerClassPair(Class cls);

const char* class_getName(Class cls);
Class class_getSuperclass(Class cls);
std::size_t class_getInstanceSize(Class cls);
bool class_addIvar(Class cls, const char* name, std::size_t size,
	std::uint8_t alignment, const char* types);
Method class_getInstanceMethod(Class cls, SEL selector);
Method class_getClassMethod(Class cls, SEL selector);
bool class_respondsToSelector(Class cls, SEL selector);
Method* class_copyMethodList(Class cls, unsigned int* out_count);
SEL method_getName(Method method);
IMP method_getImplementation(Method method);
IMP method_setImplementation(Method method, IMP implementation);
const char* method_getTypeEncoding(Method method);
Ivar class_getInstanceVariable(Class cls, const char* name);
const char* ivar_getName(Ivar ivar);
const char* ivar_getTypeEncoding(Ivar ivar);
std::ptrdiff_t ivar_getOffset(Ivar ivar);
Ivar* class_copyIvarList(Class cls, unsigned int* out_count);
objc_property_t class_getProperty(Class cls, const char* name);
bool class_addProperty(Class cls, const char* name,
	const objc_property_attribute_t* attributes, unsigned int attribute_count);
const char* property_getName(objc_property_t property);
const char* property_getAttributes(objc_property_t property);
objc_property_t* class_copyPropertyList(Class cls, unsigned int* out_count);
Protocol* class_copyProtocolList(Class cls, unsigned int* out_count);
bool class_addMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
IMP class_replaceMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
bool class_addClassMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
IMP class_getMethodImplementation(Class cls, SEL selector);
const char* class_getMethodTypeEncoding(Class cls, SEL selector);

id class_createInstance(Class cls, std::size_t extra_bytes);
Class object_getClass(id object);
id object_getIvar(id object, Ivar ivar);
void object_setIvar(id object, Ivar ivar, id value);
const char* object_getClassName(id object);
bool object_isClass(id object);
id objc_retain(id object);
void objc_release(id object);
void objc_storeStrong(id* location, id object);
id objc_autorelease(id object);
id objc_retainAutoreleasedReturnValue(id object);
id objc_getAssociatedObject(id object, const void* key);
void objc_setAssociatedObject(id object, const void* key, id value,
	objc_AssociationPolicy policy);
void objc_removeAssociatedObjects(id object);
void* objc_autoreleasePoolPush();
void objc_autoreleasePoolPop(void* token);
id objc_initWeak(id* location, id object);
id objc_storeWeak(id* location, id object);
id objc_loadWeak(id* location);
id objc_loadWeakRetained(id* location);
void objc_destroyWeak(id* location);
id objc_copyWeak(id* destination, id* source);
id objc_moveWeak(id* destination, id* source);
id objc_msgSend(id receiver, SEL selector, ...);
id objc_msgSendSuper(id receiver, Class superclass, SEL selector, ...);
id darling_objc_msgSend_class0(Class cls, SEL selector);

// Typed entry points used by the Windows bridge until a full variadic ABI
// trampoline is available. The names are deliberately project-specific.
id darling_objc_msgSend_object1(id receiver, SEL selector, id argument);
id darling_objc_msgSend_object0(id receiver, SEL selector);
id darling_objc_msgSend_object2(id receiver, SEL selector, id first, id second);
id darling_objc_msgSend_block1(id receiver, SEL selector,
	DarlingObjcCallbackBlock* argument);
std::int64_t darling_objc_msgSend_int64_1(id receiver, SEL selector,
	std::int64_t argument);
std::int64_t darling_objc_msgSend_int64_2(id receiver, SEL selector,
	std::int64_t first, std::int64_t second);
bool darling_objc_msgSend_bool1(id receiver, SEL selector, bool argument);
void* darling_objc_msgSend_pointer1(id receiver, SEL selector, void* argument);
void darling_objc_msgSend_void1_int64(id receiver, SEL selector,
	std::int64_t argument);
void darling_objc_msgSend_void1_int(id receiver, SEL selector, int argument);
double darling_objc_msgSend_double1(id receiver, SEL selector, double argument);
double darling_objc_msgSend_double2(id receiver, SEL selector, double first,
	double second);
void darling_objc_msgSend_void0(id receiver, SEL selector);
enum DarlingObjcValueKind : std::uint32_t {
	DARLING_OBJC_VOID = 0,
	DARLING_OBJC_OBJECT = 1,
	DARLING_OBJC_INT64 = 2,
	DARLING_OBJC_BOOL = 3,
	DARLING_OBJC_DOUBLE = 4,
	DARLING_OBJC_POINTER = 5,
	DARLING_OBJC_BLOCK = 6,
};

struct DarlingObjcValue final {
	DarlingObjcValueKind kind;
	union {
		id object;
		std::int64_t integer;
		bool boolean;
		double floating;
		void* pointer;
		DarlingObjcCallbackBlock* block;
	};
};

struct DarlingObjcRect final {
	double x;
	double y;
	double width;
	double height;
};

DarlingObjcRect darling_objc_msgSend_rect0(id receiver, SEL selector);

using DarlingObjcCallback1 = id (*)(void* context, id argument);

DarlingObjcValue darling_objc_invoke0(id receiver, SEL selector);
DarlingObjcValue darling_objc_invoke1(id receiver, SEL selector,
	DarlingObjcValue argument);
DarlingObjcValue darling_objc_invoke2(id receiver, SEL selector,
	DarlingObjcValue first, DarlingObjcValue second);
DarlingObjcRect darling_objc_invoke_rect0(id receiver, SEL selector);
DarlingObjcCallbackBlock* darling_objc_block_create1(
	DarlingObjcCallback1 callback, void* context);
DarlingObjcCallbackBlock* darling_objc_block_copy(
	DarlingObjcCallbackBlock* block);
void darling_objc_block_release(DarlingObjcCallbackBlock* block);
id darling_objc_block_invoke1(DarlingObjcCallbackBlock* block, id argument);
DarlingObjcAppleBlock* darling_objc_apple_block_create1(
	DarlingObjcCallback1 callback, void* context);
DarlingObjcAppleBlock* darling_objc_apple_block_copy(
	DarlingObjcAppleBlock* block);
void darling_objc_apple_block_release(DarlingObjcAppleBlock* block);
id darling_objc_apple_block_invoke1(DarlingObjcAppleBlock* block, id argument);
const char* darling_objc_apple_block_signature(DarlingObjcAppleBlock* block);
void* objc_retainBlock(void* block);
void objc_releaseBlock(void* block);
void _Block_object_assign(void* destination, const void* object, const int flags);
void _Block_object_dispose(const void* object, const int flags);

}
