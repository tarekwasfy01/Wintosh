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

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_OBJC_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_OBJC_GUEST_ABI
#endif

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

SEL DARLING_OBJC_GUEST_ABI sel_registerName(const char* name);
const char* DARLING_OBJC_GUEST_ABI sel_getName(SEL selector);

Class DARLING_OBJC_GUEST_ABI objc_getClass(const char* name);
Class DARLING_OBJC_GUEST_ABI objc_getMetaClass(const char* name);
int DARLING_OBJC_GUEST_ABI objc_getClassList(Class* buffer, int buffer_count);
std::size_t DARLING_OBJC_GUEST_ABI darling_objc_register_macho_classlist(const void* classlist,
	std::size_t bytes, std::intptr_t image_slide = 0,
	std::uintptr_t image_vm_begin = 0, std::uintptr_t image_vm_end = 0);
std::size_t DARLING_OBJC_GUEST_ABI darling_objc_register_macho_categories(const void* categories,
	std::size_t bytes);
std::size_t DARLING_OBJC_GUEST_ABI darling_objc_register_macho_categories_with_slide(
	const void* categories, std::size_t bytes, std::intptr_t image_slide,
	std::uintptr_t image_vm_begin, std::uintptr_t image_vm_end);
std::size_t DARLING_OBJC_GUEST_ABI darling_objc_register_macho_selrefs(const void* selrefs,
	std::size_t bytes, std::intptr_t image_slide = 0,
	std::uintptr_t image_vm_begin = 0, std::uintptr_t image_vm_end = 0);
std::size_t DARLING_OBJC_GUEST_ABI darling_objc_register_macho_constant_strings(
	const void* strings, std::size_t bytes);
bool DARLING_OBJC_GUEST_ABI darling_objc_is_constant_string(id object);
using darling_objc_object_finalizer = void (*)(id object);
bool DARLING_OBJC_GUEST_ABI darling_objc_register_object_finalizer(id object,
	darling_objc_object_finalizer finalizer);
struct darling_objc_image_token;
darling_objc_image_token* DARLING_OBJC_GUEST_ABI darling_objc_begin_image_registration();
void DARLING_OBJC_GUEST_ABI darling_objc_end_image_registration(darling_objc_image_token* token);
bool DARLING_OBJC_GUEST_ABI darling_objc_load_image(darling_objc_image_token* token,
	bool arm64_guest);
using darling_objc_guest_method_invoker = void (*)(IMP implementation,
	id receiver, SEL selector, void* context);
using darling_objc_guest_function_invoker = bool (*)(std::uintptr_t function,
	const std::uintptr_t* arguments, std::size_t argument_count,
	std::uintptr_t* result, void* context);
using darling_objc_guest_memory_mapper = bool (*)(void* address,
	std::size_t bytes, bool guest_writes, bool map, void* context);
using darling_objc_guest_block_allocate = void* (*)(std::size_t bytes,
	void* context);
using darling_objc_guest_block_deallocate = void (*)(void* allocation,
	void* context);
darling_objc_guest_method_invoker DARLING_OBJC_GUEST_ABI
darling_objc_set_guest_method_invoker(darling_objc_guest_method_invoker invoker,
	void* context, void** previous_context);
darling_objc_guest_function_invoker darling_objc_set_guest_function_invoker(
	darling_objc_guest_function_invoker invoker, void* context,
	void** previous_context);
bool darling_objc_invoke_guest_function(std::uintptr_t function,
	const std::uintptr_t* arguments, std::size_t argument_count);
bool darling_objc_invoke_guest_function_result(std::uintptr_t function,
	const std::uintptr_t* arguments, std::size_t argument_count,
	std::uintptr_t* result);
darling_objc_guest_memory_mapper darling_objc_set_guest_memory_mapper(
	darling_objc_guest_memory_mapper mapper, void* context,
	void** previous_context);
bool darling_objc_change_guest_memory_region(void* address, std::size_t bytes,
	bool guest_writes, bool map);
bool darling_objc_has_guest_memory_mapper();
bool darling_objc_has_guest_function_invoker();
void DARLING_OBJC_GUEST_ABI darling_objc_set_guest_block_allocator(
	darling_objc_guest_block_allocate allocate,
	darling_objc_guest_block_deallocate deallocate, void* context,
	darling_objc_guest_block_allocate* previous_allocate,
	darling_objc_guest_block_deallocate* previous_deallocate,
	void** previous_context);
void DARLING_OBJC_GUEST_ABI darling_objc_ensure_class_initialized(Class cls);
void DARLING_OBJC_GUEST_ABI darling_objc_queue_guest_callback(IMP implementation,
	id receiver, SEL selector);
bool DARLING_OBJC_GUEST_ABI darling_objc_run_pending_guest_callbacks();
bool DARLING_OBJC_GUEST_ABI darling_objc_has_guest_method_invoker();
void DARLING_OBJC_GUEST_ABI darling_objc_unregister_image(darling_objc_image_token* token);
Protocol DARLING_OBJC_GUEST_ABI objc_allocateProtocol(const char* name);
Protocol DARLING_OBJC_GUEST_ABI objc_getProtocol(const char* name);
const char* DARLING_OBJC_GUEST_ABI protocol_getName(Protocol protocol);
unsigned int DARLING_OBJC_GUEST_ABI objc_getProtocolList(Protocol* buffer, unsigned int buffer_count);
unsigned int DARLING_OBJC_GUEST_ABI class_getProtocolList(Class cls, Protocol* buffer,
	unsigned int buffer_count);
void DARLING_OBJC_GUEST_ABI protocol_addMethodDescription(Protocol protocol, SEL selector,
	const char* types, bool required, bool instance);
void DARLING_OBJC_GUEST_ABI objc_registerProtocol(Protocol protocol);
bool DARLING_OBJC_GUEST_ABI class_addProtocol(Class cls, Protocol protocol);
bool DARLING_OBJC_GUEST_ABI class_conformsToProtocol(Class cls, Protocol protocol);
bool DARLING_OBJC_GUEST_ABI protocol_addProtocol(Protocol protocol, Protocol parent);
bool DARLING_OBJC_GUEST_ABI protocol_conformsToProtocol(Protocol protocol, Protocol parent);
bool DARLING_OBJC_GUEST_ABI protocol_hasMethodDescription(Protocol protocol, SEL selector,
	bool required, bool instance);
objc_method_description DARLING_OBJC_GUEST_ABI protocol_getMethodDescription(Protocol protocol,
	SEL selector, bool required, bool instance);
Class DARLING_OBJC_GUEST_ABI objc_allocateClassPair(Class superclass, const char* name,
	std::size_t extra_bytes);
void DARLING_OBJC_GUEST_ABI objc_registerClassPair(Class cls);

const char* DARLING_OBJC_GUEST_ABI class_getName(Class cls);
Class DARLING_OBJC_GUEST_ABI class_getSuperclass(Class cls);
std::size_t DARLING_OBJC_GUEST_ABI class_getInstanceSize(Class cls);
bool DARLING_OBJC_GUEST_ABI class_addIvar(Class cls, const char* name, std::size_t size,
	std::uint8_t alignment, const char* types);
Method DARLING_OBJC_GUEST_ABI class_getInstanceMethod(Class cls, SEL selector);
Method DARLING_OBJC_GUEST_ABI class_getClassMethod(Class cls, SEL selector);
bool DARLING_OBJC_GUEST_ABI class_respondsToSelector(Class cls, SEL selector);
Method* DARLING_OBJC_GUEST_ABI class_copyMethodList(Class cls, unsigned int* out_count);
SEL DARLING_OBJC_GUEST_ABI method_getName(Method method);
IMP DARLING_OBJC_GUEST_ABI method_getImplementation(Method method);
IMP DARLING_OBJC_GUEST_ABI method_setImplementation(Method method, IMP implementation);
void DARLING_OBJC_GUEST_ABI method_exchangeImplementations(Method first, Method second);
const char* DARLING_OBJC_GUEST_ABI method_getTypeEncoding(Method method);
Ivar DARLING_OBJC_GUEST_ABI class_getInstanceVariable(Class cls, const char* name);
const char* DARLING_OBJC_GUEST_ABI ivar_getName(Ivar ivar);
const char* DARLING_OBJC_GUEST_ABI ivar_getTypeEncoding(Ivar ivar);
std::ptrdiff_t DARLING_OBJC_GUEST_ABI ivar_getOffset(Ivar ivar);
Ivar* DARLING_OBJC_GUEST_ABI class_copyIvarList(Class cls, unsigned int* out_count);
objc_property_t DARLING_OBJC_GUEST_ABI class_getProperty(Class cls, const char* name);
bool DARLING_OBJC_GUEST_ABI class_addProperty(Class cls, const char* name,
	const objc_property_attribute_t* attributes, unsigned int attribute_count);
const char* DARLING_OBJC_GUEST_ABI property_getName(objc_property_t property);
const char* DARLING_OBJC_GUEST_ABI property_getAttributes(objc_property_t property);
objc_property_t* DARLING_OBJC_GUEST_ABI class_copyPropertyList(Class cls, unsigned int* out_count);
Protocol* DARLING_OBJC_GUEST_ABI class_copyProtocolList(Class cls, unsigned int* out_count);
bool DARLING_OBJC_GUEST_ABI class_addMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
IMP DARLING_OBJC_GUEST_ABI class_replaceMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
bool DARLING_OBJC_GUEST_ABI class_addClassMethod(Class cls, SEL selector, IMP implementation,
	const char* types);
IMP DARLING_OBJC_GUEST_ABI class_getMethodImplementation(Class cls, SEL selector);
const char* DARLING_OBJC_GUEST_ABI class_getMethodTypeEncoding(Class cls, SEL selector);

id DARLING_OBJC_GUEST_ABI class_createInstance(Class cls, std::size_t extra_bytes);
Class DARLING_OBJC_GUEST_ABI object_getClass(id object);
id DARLING_OBJC_GUEST_ABI object_getIvar(id object, Ivar ivar);
void DARLING_OBJC_GUEST_ABI object_setIvar(id object, Ivar ivar, id value);
const char* DARLING_OBJC_GUEST_ABI object_getClassName(id object);
bool DARLING_OBJC_GUEST_ABI object_isClass(id object);
id DARLING_OBJC_GUEST_ABI objc_retain(id object);
void DARLING_OBJC_GUEST_ABI objc_release(id object);
void DARLING_OBJC_GUEST_ABI objc_storeStrong(id* location, id object);
id DARLING_OBJC_GUEST_ABI objc_autorelease(id object);
id DARLING_OBJC_GUEST_ABI objc_autoreleaseReturnValue(id object);
id DARLING_OBJC_GUEST_ABI objc_retainAutorelease(id object);
id DARLING_OBJC_GUEST_ABI objc_retainAutoreleaseReturnValue(id object);
id DARLING_OBJC_GUEST_ABI objc_retainAutoreleasedReturnValue(id object);
id DARLING_OBJC_GUEST_ABI objc_unsafeClaimAutoreleasedReturnValue(id object);
id DARLING_OBJC_GUEST_ABI objc_opt_self(id object);
Class DARLING_OBJC_GUEST_ABI objc_opt_class(id object);
bool DARLING_OBJC_GUEST_ABI objc_opt_respondsToSelector(id object, SEL selector);
bool DARLING_OBJC_GUEST_ABI objc_opt_isKindOfClass(id object, Class cls);
id DARLING_OBJC_GUEST_ABI objc_getAssociatedObject(id object, const void* key);
void DARLING_OBJC_GUEST_ABI objc_setAssociatedObject(id object, const void* key, id value,
	objc_AssociationPolicy policy);
void DARLING_OBJC_GUEST_ABI objc_removeAssociatedObjects(id object);
void* DARLING_OBJC_GUEST_ABI objc_autoreleasePoolPush();
void DARLING_OBJC_GUEST_ABI objc_autoreleasePoolPop(void* token);
id DARLING_OBJC_GUEST_ABI objc_initWeak(id* location, id object);
id DARLING_OBJC_GUEST_ABI objc_storeWeak(id* location, id object);
id DARLING_OBJC_GUEST_ABI objc_loadWeak(id* location);
id DARLING_OBJC_GUEST_ABI objc_loadWeakRetained(id* location);
void DARLING_OBJC_GUEST_ABI objc_destroyWeak(id* location);
id DARLING_OBJC_GUEST_ABI objc_copyWeak(id* destination, id* source);
id DARLING_OBJC_GUEST_ABI objc_moveWeak(id* destination, id* source);
// These host-ABI entry points serve native bridge calls. Mach-O guest messages
// resolve to the dedicated SysV assembly shims.
id objc_msgSend(id receiver, SEL selector, ...);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_stret(void* return_storage,
	id receiver, SEL selector, ...);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_stret(
	void* return_storage, void* super_info, SEL selector, ...);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper2_stret(
	void* return_storage, void* super_info, SEL selector, ...);
id objc_msgSendSuper(id receiver, Class superclass, SEL selector, ...);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_class0(Class cls, SEL selector);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_object0(id receiver, Class superclass, SEL selector);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_object1(id receiver, Class superclass, SEL selector,
	id argument);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_object2(id receiver, Class superclass, SEL selector,
	id first, id second);
void* DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_pointer0(id receiver, Class superclass, SEL selector);
void* DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_pointer1(id receiver, Class superclass, SEL selector,
	void* argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_object1(id receiver, Class superclass, SEL selector,
	id argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_object2(id receiver, Class superclass, SEL selector,
	id first, id second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_double1(id receiver, Class superclass, SEL selector,
	double argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_float1(id receiver, Class superclass, SEL selector,
	float argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_double2(id receiver, Class superclass, SEL selector,
	double first, double second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_float2(id receiver, Class superclass, SEL selector,
	float first, float second);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_double0(id receiver, Class superclass, SEL selector);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_double1(id receiver, Class superclass, SEL selector,
	double argument);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_double2(id receiver, Class superclass, SEL selector,
	double first, double second);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_float0(id receiver, Class superclass, SEL selector);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_float1(id receiver, Class superclass, SEL selector,
	float argument);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_float2(id receiver, Class superclass, SEL selector,
	float first, float second);
std::uint64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint64_0(id receiver, Class superclass,
	SEL selector);
std::uint64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint64_1(id receiver, Class superclass,
	SEL selector, std::uint64_t argument);
std::uint64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint64_2(id receiver, Class superclass,
	SEL selector, std::uint64_t first, std::uint64_t second);
unsigned int DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint0(id receiver, Class superclass,
	SEL selector);
unsigned int DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint1(id receiver, Class superclass,
	SEL selector, unsigned int argument);
unsigned int DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_uint2(id receiver, Class superclass,
	SEL selector, unsigned int first, unsigned int second);
bool DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_bool0(id receiver, Class superclass, SEL selector);
bool DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_bool1(id receiver, Class superclass, SEL selector,
	bool argument);
int DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_int0(id receiver, Class superclass, SEL selector);
int DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_int1(id receiver, Class superclass, SEL selector,
	int argument);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_int64_0(id receiver, Class superclass,
	SEL selector);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_int64_1(id receiver, Class superclass,
	SEL selector, std::int64_t argument);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_int64_2(id receiver, Class superclass,
	SEL selector, std::int64_t first, std::int64_t second);

// Typed entry points used by the Windows bridge until a full variadic ABI
// trampoline is available. The names are deliberately project-specific.
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_object1(id receiver, SEL selector, id argument);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_object0(id receiver, SEL selector);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_object2(id receiver, SEL selector, id first, id second);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_object_pointer1(id receiver, SEL selector, void* argument);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int64_0(id receiver, SEL selector);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int64_object1(
	id receiver, SEL selector, id argument);
std::uint64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_uint64_0(id receiver, SEL selector);
std::uint64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_uint64_1(id receiver, SEL selector,
	std::uint64_t argument);
int DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int0(id receiver, SEL selector);
int DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int1(id receiver, SEL selector, int argument);
unsigned int DARLING_OBJC_GUEST_ABI darling_objc_msgSend_uint0(id receiver, SEL selector);
unsigned int DARLING_OBJC_GUEST_ABI darling_objc_msgSend_uint1(id receiver, SEL selector,
	unsigned int argument);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_block1(id receiver, SEL selector,
	DarlingObjcCallbackBlock* argument);
id DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_block1(id receiver, Class superclass, SEL selector,
	DarlingObjcCallbackBlock* argument);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int64_1(id receiver, SEL selector,
	std::int64_t argument);
std::int64_t DARLING_OBJC_GUEST_ABI darling_objc_msgSend_int64_2(id receiver, SEL selector,
	std::int64_t first, std::int64_t second);
bool DARLING_OBJC_GUEST_ABI darling_objc_msgSend_bool1(id receiver, SEL selector, bool argument);
bool DARLING_OBJC_GUEST_ABI darling_objc_msgSend_bool_object1(id receiver, SEL selector, id argument);
bool DARLING_OBJC_GUEST_ABI darling_objc_msgSend_bool0(id receiver, SEL selector);
void* DARLING_OBJC_GUEST_ABI darling_objc_msgSend_pointer1(id receiver, SEL selector, void* argument);
void* DARLING_OBJC_GUEST_ABI darling_objc_msgSend_pointer0(id receiver, SEL selector);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_pointer(id receiver, SEL selector, void* argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_int64(id receiver, SEL selector,
	std::int64_t argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_int(id receiver, SEL selector, int argument);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSend_double1(id receiver, SEL selector, double argument);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSend_double0(id receiver, SEL selector);
double DARLING_OBJC_GUEST_ABI darling_objc_msgSend_double2(id receiver, SEL selector, double first,
	double second);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSend_float0(id receiver, SEL selector);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSend_float1(id receiver, SEL selector, float argument);
float DARLING_OBJC_GUEST_ABI darling_objc_msgSend_float2(id receiver, SEL selector, float first,
	float second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void0(id receiver, SEL selector);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_double(id receiver, SEL selector, double argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_float(id receiver, SEL selector, float argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void2_double(id receiver, SEL selector, double first, double second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void2_float(id receiver, SEL selector, float first, float second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_object(id receiver, SEL selector, id argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void2_object(id receiver, SEL selector, id first, id second);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_block(id receiver, SEL selector,
	DarlingObjcCallbackBlock* argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void1_block(id receiver, Class superclass, SEL selector,
	DarlingObjcCallbackBlock* argument);
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
id DARLING_OBJC_GUEST_ABI darling_objc_msgSend_object_rect1(id receiver,
	SEL selector, DarlingObjcRect argument);

struct DarlingObjcPoint final {
	double x;
	double y;
};
static_assert(sizeof(DarlingObjcPoint) == 16);
struct DarlingObjcAffineTransform final {
	double m11;
	double m12;
	double m21;
	double m22;
	double tX;
	double tY;
};
static_assert(sizeof(DarlingObjcAffineTransform) == 48);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSend_void1_size(id receiver,
	SEL selector, DarlingObjcPoint argument);

struct DarlingObjcRange final {
	std::uint64_t location;
	std::uint64_t length;
};
static_assert(sizeof(DarlingObjcRange) == 16);

struct DarlingObjcOperatingSystemVersion final {
	std::int64_t major_version;
	std::int64_t minor_version;
	std::int64_t patch_version;
};
static_assert(sizeof(DarlingObjcOperatingSystemVersion) == 24);

DarlingObjcRect DARLING_OBJC_GUEST_ABI darling_objc_msgSend_rect0(id receiver, SEL selector);
DarlingObjcRect DARLING_OBJC_GUEST_ABI darling_objc_msgSend_rect1(id receiver, SEL selector,
	DarlingObjcRect argument);
void darling_objc_msgSend_void_rect1(id receiver, SEL selector,
	DarlingObjcRect argument);
DarlingObjcRect DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_rect0(id receiver, Class superclass,
	SEL selector);
DarlingObjcRect DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_rect1(id receiver, Class superclass,
	SEL selector, DarlingObjcRect argument);
void DARLING_OBJC_GUEST_ABI darling_objc_msgSendSuper_void_rect1(id receiver, Class superclass,
	SEL selector, DarlingObjcRect argument);

using DarlingObjcCallback1 = id (*)(void* context, id argument);

// Wintosh-owned exception boundary for bridge callbacks.  This is deliberately
// not Apple's Objective-C personality/unwind ABI; it keeps a pending Objective-C
// object visible across a native callback while the full compiler ABI is absent.
using DarlingObjcExceptionCallback = void (*)(void* context);
[[noreturn]] void DARLING_OBJC_GUEST_ABI darling_objc_exception_throw(id object);
bool DARLING_OBJC_GUEST_ABI darling_objc_exception_try(DarlingObjcExceptionCallback callback,
	void* context, id* caught_object);

DarlingObjcValue DARLING_OBJC_GUEST_ABI darling_objc_invoke0(id receiver, SEL selector);
DarlingObjcValue DARLING_OBJC_GUEST_ABI darling_objc_invoke1(id receiver, SEL selector,
	DarlingObjcValue argument);
DarlingObjcValue DARLING_OBJC_GUEST_ABI darling_objc_invoke2(id receiver, SEL selector,
	DarlingObjcValue first, DarlingObjcValue second);
DarlingObjcRect DARLING_OBJC_GUEST_ABI darling_objc_invoke_rect0(id receiver, SEL selector);
DarlingObjcCallbackBlock* DARLING_OBJC_GUEST_ABI darling_objc_block_create1(
	DarlingObjcCallback1 callback, void* context);
DarlingObjcCallbackBlock* DARLING_OBJC_GUEST_ABI darling_objc_block_copy(
	DarlingObjcCallbackBlock* block);
void DARLING_OBJC_GUEST_ABI darling_objc_block_release(DarlingObjcCallbackBlock* block);
id DARLING_OBJC_GUEST_ABI darling_objc_block_invoke1(DarlingObjcCallbackBlock* block, id argument);
DarlingObjcAppleBlock* DARLING_OBJC_GUEST_ABI darling_objc_apple_block_create1(
	DarlingObjcCallback1 callback, void* context);
DarlingObjcAppleBlock* DARLING_OBJC_GUEST_ABI darling_objc_apple_block_copy(
	DarlingObjcAppleBlock* block);
void DARLING_OBJC_GUEST_ABI darling_objc_apple_block_release(DarlingObjcAppleBlock* block);
id DARLING_OBJC_GUEST_ABI darling_objc_apple_block_invoke1(DarlingObjcAppleBlock* block, id argument);
void DARLING_OBJC_GUEST_ABI darling_objc_apple_block_invoke0(void* block);
const char* DARLING_OBJC_GUEST_ABI darling_objc_apple_block_signature(DarlingObjcAppleBlock* block);
void* DARLING_OBJC_GUEST_ABI objc_retainBlock(void* block);
void DARLING_OBJC_GUEST_ABI objc_releaseBlock(void* block);
void DARLING_OBJC_GUEST_ABI _Block_object_assign(void* destination, const void* object, const int flags);
void DARLING_OBJC_GUEST_ABI _Block_object_dispose(const void* object, const int flags);

}

#undef DARLING_OBJC_GUEST_ABI
