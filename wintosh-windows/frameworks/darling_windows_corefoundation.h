/* Minimal CoreFoundation-compatible primitives; GPL-3.0-only. */
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_CF_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_CF_GUEST_ABI
#endif

using darling_windows_CFIndex = std::ptrdiff_t;
struct darling_windows_CFRange { darling_windows_CFIndex location; darling_windows_CFIndex length; };
enum darling_windows_CFStringCompareFlags {
	darling_windows_CFCompareCaseInsensitive = 1 << 0,
	darling_windows_CFCompareBackwards = 1 << 1,
	darling_windows_CFCompareNumerically = 1 << 2
};
using darling_windows_CFTypeRef = const void*;
enum DarlingCFRunLoopRunResult : std::int32_t {
	DarlingCFRunLoopRunFinished = 1,
	DarlingCFRunLoopRunStopped = 2,
	DarlingCFRunLoopRunTimedOut = 3,
	DarlingCFRunLoopRunHandledSource = 4
};
using darling_windows_CFOpaqueDestroyCallback = void (*)(void* payload);
using darling_windows_CFBundleReleaseExecutableCallback = void (*)(void* handle);
using darling_windows_CFDataProviderReleaseCallback = void (DARLING_CF_GUEST_ABI *)(
	void* info, const void* data, std::size_t size);
using darling_windows_CFDataProviderGetBytesCallback = std::size_t (DARLING_CF_GUEST_ABI *)(
	void* info, void* buffer, std::size_t count);
using darling_windows_CFDataProviderSkipForwardCallback = std::int64_t (DARLING_CF_GUEST_ABI *)(
	void* info, std::int64_t count);
using darling_windows_CFDataProviderRewindCallback = void (DARLING_CF_GUEST_ABI *)(void* info);
using darling_windows_CFDataProviderReleaseInfoCallback = void (DARLING_CF_GUEST_ABI *)(void* info);
using darling_windows_CFDataProviderGetBytePointerCallback = const void* (DARLING_CF_GUEST_ABI *)(void* info);
using darling_windows_CFDataProviderReleaseBytePointerCallback = void (DARLING_CF_GUEST_ABI *)(
	void* info, const void* pointer);
using darling_windows_CFDataProviderGetBytesAtPositionCallback = std::size_t (DARLING_CF_GUEST_ABI *)(
	void* info, std::int64_t position, void* buffer, std::size_t count);
using darling_windows_CFStringRef = const void*;
using darling_windows_CFMutableStringRef = void*;
using darling_windows_CFDataRef = const void*;
using darling_windows_CFMutableDataRef = void*;
using darling_windows_CFArrayRef = const void*;
using darling_windows_CFDictionaryRef = const void*;
using darling_windows_CFSetRef = const void*;
using darling_windows_CFDateRef = const void*;
using darling_windows_CFURLRef = const void*;
using darling_windows_CFBundleRef = const void*;
using darling_windows_CFBooleanRef = const void*;
using darling_windows_CFMachPortRef = const void*;
using darling_windows_CFAllocatorRef = const void*;
using darling_windows_CFRunLoopRef = void*;
using darling_windows_CFRunLoopBlock = void (*)(void* context);
using DarlingCFRunLoopSourceRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopSourceReleaseCallback = void (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopSourceCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopSourceEqualCallback = std::uint8_t (DARLING_CF_GUEST_ABI *)(const void* left, const void* right);
using DarlingCFRunLoopSourceHashCallback = std::uintptr_t (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopSourceScheduleCallback = void (DARLING_CF_GUEST_ABI *)(void* info,
	darling_windows_CFRunLoopRef loop, const void* mode);
using DarlingCFRunLoopSourceCancelCallback = void (DARLING_CF_GUEST_ABI *)(void* info,
	darling_windows_CFRunLoopRef loop, const void* mode);
using DarlingCFRunLoopSourcePerformCallback = void (DARLING_CF_GUEST_ABI *)(void* info);
using DarlingCFRunLoopTimerRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopTimerReleaseCallback = void (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopTimerCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopTimerCallout = void (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFTypeRef timer, void* info);
struct DarlingCFRunLoopTimerContext {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFRunLoopTimerRetainCallback retain;
	DarlingCFRunLoopTimerReleaseCallback release;
	DarlingCFRunLoopTimerCopyDescriptionCallback copy_description;
};
using DarlingCFRunLoopObserverRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopObserverReleaseCallback = void (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopObserverCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFRunLoopObserverCallout = void (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFTypeRef observer, std::uint64_t activity, void* info);
struct DarlingCFRunLoopObserverContext {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFRunLoopObserverRetainCallback retain;
	DarlingCFRunLoopObserverReleaseCallback release;
	DarlingCFRunLoopObserverCopyDescriptionCallback copy_description;
};
using DarlingCFMachPortRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFMachPortReleaseCallback = void (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFMachPortCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
struct DarlingCFMachPortContext {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFMachPortRetainCallback retain;
	DarlingCFMachPortReleaseCallback release;
	DarlingCFMachPortCopyDescriptionCallback copy_description;
};
using DarlingCFAllocatorRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFAllocatorReleaseCallback = void (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFAllocatorCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(const void* info);
using DarlingCFAllocatorAllocateCallback = void* (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFIndex size, std::uint64_t hint, void* info);
using DarlingCFAllocatorReallocateCallback = void* (DARLING_CF_GUEST_ABI *)(
	void* address, darling_windows_CFIndex size, std::uint64_t hint, void* info);
using DarlingCFAllocatorDeallocateCallback = void (DARLING_CF_GUEST_ABI *)(
	void* address, void* info);
using DarlingCFAllocatorPreferredSizeCallback = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFIndex size, std::uint64_t hint, void* info);
struct DarlingCFAllocatorContext {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFAllocatorRetainCallback retain;
	DarlingCFAllocatorReleaseCallback release;
	DarlingCFAllocatorCopyDescriptionCallback copy_description;
	DarlingCFAllocatorAllocateCallback allocate;
	DarlingCFAllocatorReallocateCallback reallocate;
	DarlingCFAllocatorDeallocateCallback deallocate;
	DarlingCFAllocatorPreferredSizeCallback preferred_size;
};
using DarlingCFMachPortCallout = void (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFMachPortRef port, void* message,
	darling_windows_CFIndex size, void* info);
using DarlingCFMachPortInvalidationCallout = void (DARLING_CF_GUEST_ABI *)(
	darling_windows_CFMachPortRef port, void* info);
struct DarlingCFRunLoopSourceContext {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFRunLoopSourceRetainCallback retain;
	DarlingCFRunLoopSourceReleaseCallback release;
	DarlingCFRunLoopSourceCopyDescriptionCallback copy_description;
	DarlingCFRunLoopSourceEqualCallback equal;
	DarlingCFRunLoopSourceHashCallback hash;
	DarlingCFRunLoopSourceScheduleCallback schedule;
	DarlingCFRunLoopSourceCancelCallback cancel;
	DarlingCFRunLoopSourcePerformCallback perform;
};
using DarlingCFRunLoopSource1GetPort = std::uint32_t (DARLING_CF_GUEST_ABI *)(void* info);
using DarlingCFRunLoopSource1Perform = void* (DARLING_CF_GUEST_ABI *)(
	void* message, darling_windows_CFIndex size, const void* allocator, void* info);
struct DarlingCFRunLoopSourceContext1 {
	darling_windows_CFIndex version;
	void* info;
	DarlingCFRunLoopSourceRetainCallback retain;
	DarlingCFRunLoopSourceReleaseCallback release;
	DarlingCFRunLoopSourceCopyDescriptionCallback copy_description;
	DarlingCFRunLoopSourceEqualCallback equal;
	DarlingCFRunLoopSourceHashCallback hash;
	DarlingCFRunLoopSource1GetPort get_port;
	DarlingCFRunLoopSource1Perform perform;
};
using darling_windows_CFNotificationCallback = void (*)(const void* observer,
	const char* name, const void* object);
using darling_windows_CFArrayApplierFunction = void (*)(const void* value, void* context);
using darling_windows_CFDictionaryApplierFunction = void (*)(const void* key,
	const void* value, void* context);

using darling_windows_CFTypeID = std::uint64_t;
using darling_windows_CFNumberType = std::int32_t;
using darling_windows_CFDictionaryGuestApplierFunction = void (DARLING_CF_GUEST_ABI *)(
	const void* key, const void* value, void* context);
using darling_windows_CFArrayGuestApplierFunction = void (DARLING_CF_GUEST_ABI *)(
	const void* value, void* context);
using darling_windows_CFComparatorFunction = std::int32_t (DARLING_CF_GUEST_ABI *)(
	const void* left, const void* right, void* context);
using DarlingCFArrayRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(
	const void* allocator, const void* value);
using DarlingCFArrayReleaseCallback = void (DARLING_CF_GUEST_ABI *)(
	const void* allocator, const void* value);
using DarlingCFArrayCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(
	const void* value);
using DarlingCFArrayEqualCallback = std::uint8_t (DARLING_CF_GUEST_ABI *)(
	const void* left, const void* right);
struct DarlingCFArrayCallbacks {
	darling_windows_CFIndex version;
	DarlingCFArrayRetainCallback retain;
	DarlingCFArrayReleaseCallback release;
	DarlingCFArrayCopyDescriptionCallback copy_description;
	DarlingCFArrayEqualCallback equal;
};
extern "C" const DarlingCFArrayCallbacks kCFTypeArrayCallBacks;
using DarlingCFDictionaryRetainCallback = const void* (DARLING_CF_GUEST_ABI *)(
	const void* allocator, const void* value);
using DarlingCFDictionaryReleaseCallback = void (DARLING_CF_GUEST_ABI *)(
	const void* allocator, const void* value);
using DarlingCFDictionaryCopyDescriptionCallback = const void* (DARLING_CF_GUEST_ABI *)(
	const void* value);
using DarlingCFDictionaryEqualCallback = std::uint8_t (DARLING_CF_GUEST_ABI *)(
	const void* left, const void* right);
using DarlingCFDictionaryHashCallback = std::uintptr_t (DARLING_CF_GUEST_ABI *)(
	const void* value);
struct DarlingCFDictionaryKeyCallbacks {
	darling_windows_CFIndex version;
	DarlingCFDictionaryRetainCallback retain;
	DarlingCFDictionaryReleaseCallback release;
	DarlingCFDictionaryCopyDescriptionCallback copy_description;
	DarlingCFDictionaryEqualCallback equal;
	DarlingCFDictionaryHashCallback hash;
};
struct DarlingCFDictionaryValueCallbacks {
	darling_windows_CFIndex version;
	DarlingCFDictionaryRetainCallback retain;
	DarlingCFDictionaryReleaseCallback release;
	DarlingCFDictionaryCopyDescriptionCallback copy_description;
};
extern "C" const DarlingCFDictionaryKeyCallbacks kCFTypeDictionaryKeyCallBacks;
extern "C" const DarlingCFDictionaryValueCallbacks kCFTypeDictionaryValueCallBacks;
extern "C" const DarlingCFDictionaryKeyCallbacks kCFTypeSetCallBacks;
extern "C" int darling_windows_CFStringGetSystemEncoding();

extern "C" darling_windows_CFRange darling_windows_CFRangeMake(
	darling_windows_CFIndex location, darling_windows_CFIndex length);
extern "C" darling_windows_CFIndex darling_windows_CFRangeGetMax(
	darling_windows_CFRange range);

extern "C" void* darling_windows_CFRetain(const void* value);
extern "C" void darling_windows_CFRelease(const void* value);
extern "C" darling_windows_CFTypeID darling_windows_CFGetTypeID(const void* value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRetain(const void* value);
extern "C" DARLING_CF_GUEST_ABI void CFRelease(const void* value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFGetTypeID(const void* value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDataRef CFDataCreate(
	const void* allocator, const unsigned char* bytes, darling_windows_CFIndex length);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableDataRef CFDataCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDataRef CFDataCreateCopy(
	const void* allocator, darling_windows_CFDataRef data);
extern "C" DARLING_CF_GUEST_ABI const unsigned char* CFDataGetBytePtr(
	darling_windows_CFDataRef data);
extern "C" DARLING_CF_GUEST_ABI unsigned char* CFDataGetMutableBytePtr(
	darling_windows_CFDataRef data);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFDataGetLength(
	darling_windows_CFDataRef data);
extern "C" DARLING_CF_GUEST_ABI void CFDataGetBytes(darling_windows_CFDataRef data,
	darling_windows_CFRange range, unsigned char* buffer);
extern "C" DARLING_CF_GUEST_ABI void CFDataAppendBytes(darling_windows_CFDataRef data,
	const unsigned char* bytes, darling_windows_CFIndex length);
extern "C" DARLING_CF_GUEST_ABI void CFDataReplaceBytes(darling_windows_CFDataRef data,
	darling_windows_CFRange range, const unsigned char* bytes,
	darling_windows_CFIndex length);
extern "C" DARLING_CF_GUEST_ABI void CFDataSetLength(darling_windows_CFDataRef data,
	darling_windows_CFIndex length);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDataGetTypeID();
extern "C" darling_windows_CFTypeRef darling_windows_CFImageDestinationCreateOpaque(
	void* payload, darling_windows_CFOpaqueDestroyCallback destroy_payload);
extern "C" void* darling_windows_CFImageDestinationGetOpaque(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFImageDestinationGetTypeID();
extern "C" darling_windows_CFTypeRef darling_windows_CFDataConsumerCreateOpaque(
	void* payload, darling_windows_CFOpaqueDestroyCallback destroy_payload);
extern "C" void* darling_windows_CFDataConsumerGetOpaque(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDataConsumerGetTypeID();
extern "C" std::uint64_t darling_windows_CFHash(const void* value);
extern "C" bool darling_windows_CFEqual(const void* left, const void* right);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCString(
	const char* value);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCStringNoCopy(
	const char* value, int encoding);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithFileSystemRepresentation(
	const unsigned char* bytes, darling_windows_CFIndex length);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCharacters(
	const std::uint16_t* characters, darling_windows_CFIndex length);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithPascalString(
	const std::uint8_t* value, int encoding);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithBytes(
	const unsigned char* bytes, darling_windows_CFIndex length, int encoding,
	bool is_external_representation);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithFormat(
	const char* format, ...);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateCopy(
	darling_windows_CFStringRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateMutableCopy(
	darling_windows_CFStringRef value, darling_windows_CFIndex capacity);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateMutable(
	darling_windows_CFIndex capacity);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithSubstring(
	darling_windows_CFStringRef value, darling_windows_CFRange range);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetLength(
	darling_windows_CFStringRef value);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetMaximumSizeForEncoding(
	darling_windows_CFIndex length, int encoding);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetMaximumSizeOfFileSystemRepresentation(
	darling_windows_CFStringRef value);
extern "C" bool darling_windows_CFStringGetFileSystemRepresentation(
	darling_windows_CFStringRef value, unsigned char* buffer,
	darling_windows_CFIndex buffer_capacity);
extern "C" bool darling_windows_CFStringGetCharacters(
	darling_windows_CFStringRef value, darling_windows_CFRange range, std::uint16_t* output);
extern "C" std::uint16_t darling_windows_CFStringGetCharacterAtIndex(
	darling_windows_CFStringRef value, darling_windows_CFIndex index);
extern "C" std::int32_t darling_windows_CFStringGetIntValue(
	darling_windows_CFStringRef value);
extern "C" double darling_windows_CFStringGetDoubleValue(
	darling_windows_CFStringRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFStringGetTypeID();
extern "C" bool darling_windows_CFStringGetCString(
	darling_windows_CFStringRef value, char* buffer, darling_windows_CFIndex capacity);
extern "C" const char* darling_windows_CFStringGetCStringPtr(
	darling_windows_CFStringRef value);
extern "C" bool darling_windows_CFStringHasPrefix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef prefix);
extern "C" bool darling_windows_CFStringHasSuffix(darling_windows_CFStringRef value,
	darling_windows_CFStringRef suffix);
extern "C" bool darling_windows_CFStringHasPrefixWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFStringRef prefix, int flags);
extern "C" bool darling_windows_CFStringHasSuffixWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFStringRef suffix, int flags);
extern "C" int darling_windows_CFStringCompare(darling_windows_CFStringRef left,
	darling_windows_CFStringRef right);
extern "C" int darling_windows_CFStringCompareWithOptions(
	darling_windows_CFStringRef left, darling_windows_CFStringRef right, int flags);
extern "C" darling_windows_CFRange darling_windows_CFStringFind(
	darling_windows_CFStringRef value, darling_windows_CFStringRef needle, int flags);
extern "C" darling_windows_CFRange darling_windows_CFStringFindWithOptions(
	darling_windows_CFStringRef value, darling_windows_CFRange range,
	darling_windows_CFStringRef needle, int flags);
extern "C" bool darling_windows_CFStringAppendCString(darling_windows_CFStringRef value,
	const char* suffix);
extern "C" bool darling_windows_CFStringAppend(darling_windows_CFStringRef value,
	darling_windows_CFStringRef suffix);
extern "C" bool darling_windows_CFStringReplaceAll(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement);
extern "C" bool darling_windows_CFStringFindAndReplace(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement,
	darling_windows_CFRange range, int flags);
extern "C" darling_windows_CFArrayRef darling_windows_CFStringCreateArrayBySeparatingStrings(
	darling_windows_CFStringRef value, darling_windows_CFStringRef separator);
extern "C" bool darling_windows_CFStringReplaceRange(darling_windows_CFStringRef value,
	darling_windows_CFIndex location, darling_windows_CFIndex length, const char* replacement);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetBytes(
	darling_windows_CFStringRef value, darling_windows_CFIndex location,
	darling_windows_CFIndex length, char* output, darling_windows_CFIndex capacity);
extern "C" bool darling_windows_CFStringTrimWhitespace(darling_windows_CFStringRef value);
extern "C" bool darling_windows_CFStringLowercase(darling_windows_CFStringRef value,
	const void* locale);
extern "C" bool darling_windows_CFStringUppercase(darling_windows_CFStringRef value,
	const void* locale);
extern "C" bool darling_windows_CFStringCapitalize(darling_windows_CFStringRef value,
	const void* locale);
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreate(
	const void* bytes, darling_windows_CFIndex length);
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreateMutable(
	darling_windows_CFIndex capacity);
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreateCopy(
	darling_windows_CFDataRef value);
extern "C" const unsigned char* darling_windows_CFDataGetBytePtr(
	darling_windows_CFDataRef value);
extern "C" unsigned char* darling_windows_CFDataGetMutableBytePtr(
	darling_windows_CFDataRef value);
extern "C" darling_windows_CFIndex darling_windows_CFDataGetLength(
	darling_windows_CFDataRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDataGetTypeID();
// WIC/ImageIO bridge object: a CoreFoundation-owned, typed encoded-image source.
extern "C" darling_windows_CFTypeRef darling_windows_CFImageSourceCreate(
	const void* bytes, darling_windows_CFIndex length, bool is_final);
extern "C" const unsigned char* darling_windows_CFImageSourceGetBytePtr(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFIndex darling_windows_CFImageSourceGetLength(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFImageSourceGetTypeID();
extern "C" bool darling_windows_CFImageSourceSetData(darling_windows_CFTypeRef value,
	const void* bytes, darling_windows_CFIndex length, bool is_final);
extern "C" bool darling_windows_CFImageSourceIsFinal(darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreate(
	const void* bytes, darling_windows_CFIndex length);
extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateExternal(
	void* info, const void* bytes, darling_windows_CFIndex length,
	darling_windows_CFDataProviderReleaseCallback release_callback);
extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateSequential(
	void* info, darling_windows_CFDataProviderGetBytesCallback get_bytes,
	darling_windows_CFDataProviderSkipForwardCallback skip_forward,
	darling_windows_CFDataProviderRewindCallback rewind,
	darling_windows_CFDataProviderReleaseInfoCallback release_info);
extern "C" darling_windows_CFTypeRef darling_windows_CFDataProviderCreateDirect(
	void* info, darling_windows_CFIndex length,
	darling_windows_CFDataProviderGetBytePointerCallback get_byte_pointer,
	darling_windows_CFDataProviderReleaseBytePointerCallback release_byte_pointer,
	darling_windows_CFDataProviderGetBytesAtPositionCallback get_bytes_at_position,
	darling_windows_CFDataProviderReleaseInfoCallback release_info);
extern "C" void darling_windows_CFDataProviderReleaseBytePointer(
	darling_windows_CFTypeRef value, const void* pointer);
extern "C" std::size_t darling_windows_CFDataProviderReadAtPosition(
	darling_windows_CFTypeRef value, void* buffer, std::int64_t position,
	std::size_t count);
extern "C" const unsigned char* darling_windows_CFDataProviderGetBytePtr(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFIndex darling_windows_CFDataProviderGetLength(
	darling_windows_CFTypeRef value);
extern "C" bool darling_windows_CFDataProviderIsSequential(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDataProviderGetTypeID();
extern "C" bool darling_windows_CFDataGetBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	unsigned char* output);
extern "C" bool darling_windows_CFDataAppendBytes(darling_windows_CFDataRef value,
	const void* bytes, darling_windows_CFIndex length);
extern "C" bool darling_windows_CFDataReplaceBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* bytes, darling_windows_CFIndex length);
extern "C" bool darling_windows_CFDataSetLength(darling_windows_CFDataRef value,
	darling_windows_CFIndex length);
extern "C" bool darling_windows_CFDataClear(darling_windows_CFDataRef value);
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreate(
	const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateMutable(
	darling_windows_CFIndex capacity);
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateCopy(
	darling_windows_CFArrayRef value);
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFArrayRef value);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCount(
	darling_windows_CFArrayRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFArrayGetTypeID();
extern "C" darling_windows_CFTypeRef darling_windows_CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" void darling_windows_CFArrayApplyFunction(darling_windows_CFArrayRef value,
	darling_windows_CFRange range, darling_windows_CFArrayApplierFunction function,
	void* context);
extern "C" void darling_windows_CFArrayGetValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count, const void** output);
extern "C" bool darling_windows_CFArrayAppendValue(darling_windows_CFArrayRef value,
	const void* element);
extern "C" bool darling_windows_CFArrayInsertValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index, const void* element);
extern "C" bool darling_windows_CFArrayRemoveValueAtIndex(darling_windows_CFArrayRef value,
	darling_windows_CFIndex index);
extern "C" bool darling_windows_CFArrayReplaceValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	const void* const* replacement, darling_windows_CFIndex replacement_count);
extern "C" bool darling_windows_CFArrayRemoveValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count);
extern "C" bool darling_windows_CFArrayAppendArray(darling_windows_CFArrayRef value,
	darling_windows_CFArrayRef source);
extern "C" void darling_windows_CFArrayRemoveAllValues(darling_windows_CFArrayRef value);
extern "C" const void* darling_windows_CFNumberCreateInteger(std::int64_t value);
extern "C" const void* darling_windows_CFNumberCreateCopy(const void* value);
extern "C" bool darling_windows_CFNumberGetInteger(const void* value, std::int64_t* result);
extern "C" darling_windows_CFTypeID darling_windows_CFNumberGetTypeID();
extern "C" const void* darling_windows_CFNumberCreateDouble(double value);
extern "C" bool darling_windows_CFNumberGetDouble(const void* value, double* result);
extern "C" int darling_windows_CFNumberCompare(const void* left, const void* right);
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreate(
	const void* const* keys, const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateMutable(
	darling_windows_CFIndex capacity);
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateCopy(
	darling_windows_CFDictionaryRef value);
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFDictionaryRef value);
extern "C" darling_windows_CFIndex darling_windows_CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDictionaryGetTypeID();
extern "C" darling_windows_CFTypeRef darling_windows_CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, const void* key);
extern "C" bool darling_windows_CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, const void* key);
extern "C" void darling_windows_CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values);
extern "C" void darling_windows_CFDictionaryApplyFunction(
	darling_windows_CFDictionaryRef value, darling_windows_CFDictionaryApplierFunction function,
	void* context);
extern "C" bool darling_windows_CFDictionarySetValue(darling_windows_CFDictionaryRef value,
	const void* key, const void* element);
extern "C" bool darling_windows_CFDictionaryRemoveValue(darling_windows_CFDictionaryRef value,
	const void* key);
extern "C" void darling_windows_CFDictionaryRemoveAllValues(darling_windows_CFDictionaryRef value);
extern "C" bool darling_windows_CFDictionaryMerge(darling_windows_CFDictionaryRef value,
	darling_windows_CFDictionaryRef source);
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreate(
	const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateMutable(
	darling_windows_CFIndex capacity);
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateCopy(
	darling_windows_CFSetRef value);
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateMutableCopy(
	darling_windows_CFIndex capacity, darling_windows_CFSetRef value);
extern "C" darling_windows_CFIndex darling_windows_CFSetGetCount(darling_windows_CFSetRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFSetGetTypeID();
extern "C" bool darling_windows_CFSetContainsValue(darling_windows_CFSetRef value,
	const void* candidate);
extern "C" darling_windows_CFTypeRef darling_windows_CFSetGetValue(
	darling_windows_CFSetRef value, const void* candidate);
extern "C" void darling_windows_CFSetGetValues(darling_windows_CFSetRef value, const void** output);
extern "C" void darling_windows_CFSetApplyFunction(darling_windows_CFSetRef value,
	darling_windows_CFArrayApplierFunction function, void* context);
extern "C" bool darling_windows_CFSetAddValue(darling_windows_CFSetRef value, const void* element);
extern "C" bool darling_windows_CFSetRemoveValue(darling_windows_CFSetRef value, const void* element);
extern "C" void darling_windows_CFSetRemoveAllValues(darling_windows_CFSetRef value);
extern "C" bool darling_windows_CFSetUnion(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source);
extern "C" bool darling_windows_CFSetIntersect(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source);
extern "C" bool darling_windows_CFSetSubtract(darling_windows_CFSetRef value,
	darling_windows_CFSetRef source);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreate(double absolute_time);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateWithTimeIntervalSinceNow(
	double seconds);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateCopy(
	darling_windows_CFDateRef value);
extern "C" double darling_windows_CFDateGetAbsoluteTime(darling_windows_CFDateRef value);
extern "C" double darling_windows_CFDateGetTimeIntervalSinceReferenceDate(
	darling_windows_CFDateRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDateGetTypeID();
extern "C" int darling_windows_CFDateCompare(darling_windows_CFDateRef left,
	darling_windows_CFDateRef right);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateByAddingTimeInterval(
    darling_windows_CFDateRef value, double seconds);
extern "C" double darling_windows_CFDateGetTimeIntervalSinceDate(
    darling_windows_CFDateRef value, darling_windows_CFDateRef reference);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithFileSystemPath(
	const char* path);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithString(
	darling_windows_CFStringRef string, darling_windows_CFURLRef base_url);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateFromFileSystemRepresentation(
	const unsigned char* bytes, darling_windows_CFIndex length, bool is_directory);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPath(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyParameterString(
	darling_windows_CFURLRef value);
// Wintosh additive bridge: returns the URL path after percent decoding.
// Malformed escape sequences remain unchanged.
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPathDecoded(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyQueryString(
	darling_windows_CFURLRef value);
// Wintosh additive bridge: returns the URL query after percent decoding.
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyQueryStringDecoded(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFragment(
	darling_windows_CFURLRef value);
// Wintosh additive bridge: returns the URL fragment after percent decoding.
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFragmentDecoded(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyScheme(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyResourceSpecifier(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyHostName(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyNetLocation(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyUserName(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPassword(
	darling_windows_CFURLRef value);
extern "C" int darling_windows_CFURLGetPortNumber(darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyLastPathComponent(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateCopyAppendingPathComponent(
	const void* allocator, darling_windows_CFURLRef value,
	darling_windows_CFStringRef component, bool is_directory);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateCopyDeletingLastPathComponent(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyPathExtension(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateCopyDeletingPathExtension(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCopyAbsoluteURL(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFStringRef darling_windows_CFURLGetString(
	darling_windows_CFURLRef value);
extern "C" bool darling_windows_CFURLHasDirectoryPath(darling_windows_CFURLRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFURLGetTypeID();
extern "C" bool darling_windows_CFURLGetFileSystemRepresentation(
	darling_windows_CFURLRef value, bool resolve_against_base, unsigned char* buffer,
	darling_windows_CFIndex buffer_capacity);
extern "C" darling_windows_CFBundleRef darling_windows_CFBundleCreate(
	const void* allocator, darling_windows_CFURLRef bundle_url);
extern "C" darling_windows_CFBundleRef darling_windows_CFBundleGetMainBundle();
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyBundleURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyExecutableURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCopyExecutableArchitectures(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCopyExecutableArchitecturesForURL(
	darling_windows_CFURLRef url);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyResourcesDirectoryURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyBuiltInPlugInsURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyPrivateFrameworksURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopySharedFrameworksURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopySharedSupportURL(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFURLRef darling_windows_CFBundleCopyResourceURL(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef resource_name,
	darling_windows_CFStringRef resource_type, darling_windows_CFStringRef subdirectory);
extern "C" darling_windows_CFArrayRef darling_windows_CFBundleCreateBundlesFromDirectory(
	const void* allocator, darling_windows_CFURLRef directory_url,
	darling_windows_CFStringRef bundle_type);
extern "C" darling_windows_CFBundleRef darling_windows_CFBundleGetBundleWithIdentifier(
	darling_windows_CFStringRef bundle_identifier);
extern "C" darling_windows_CFArrayRef darling_windows_CFBundleGetAllBundles();
extern "C" darling_windows_CFDictionaryRef darling_windows_CFBundleGetInfoDictionary(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFTypeRef darling_windows_CFBundleGetValueForInfoDictionaryKey(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key);
extern "C" darling_windows_CFStringRef darling_windows_CFBundleGetIdentifier(
	darling_windows_CFBundleRef bundle);
extern "C" darling_windows_CFStringRef darling_windows_CFBundleCopyLocalizedString(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key,
	darling_windows_CFStringRef value, darling_windows_CFStringRef table_name);
extern "C" darling_windows_CFTypeID darling_windows_CFBundleGetTypeID();
extern "C" void darling_windows_CFBundleSetMainExecutablePath(const char* utf8_path);
extern "C" bool darling_windows_CFBundleIsMainExecutable(
	darling_windows_CFBundleRef bundle);
extern "C" void* darling_windows_CFBundleGetExecutableHandle(
	darling_windows_CFBundleRef bundle);
extern "C" bool darling_windows_CFBundleSetExecutableHandle(
	darling_windows_CFBundleRef bundle, void* handle,
	darling_windows_CFBundleReleaseExecutableCallback release_handle);
extern "C" void darling_windows_CFBundleReleaseExecutable(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef CFBundleCreate(
	const void* allocator, darling_windows_CFURLRef bundle_url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef CFBundleGetMainBundle();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyBundleURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyExecutableURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCopyExecutableArchitectures(darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCopyExecutableArchitecturesForURL(darling_windows_CFURLRef url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyResourcesDirectoryURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyBuiltInPlugInsURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyPrivateFrameworksURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopySharedFrameworksURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopySharedSupportURL(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFBundleCopyResourceURL(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef resource_name,
	darling_windows_CFStringRef resource_type, darling_windows_CFStringRef subdirectory);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef
CFBundleCreateBundlesFromDirectory(const void* allocator,
	darling_windows_CFURLRef directory_url, darling_windows_CFStringRef bundle_type);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFBundleRef
CFBundleGetBundleWithIdentifier(darling_windows_CFStringRef bundle_identifier);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFBundleGetAllBundles();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFBundleGetInfoDictionary(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFBundleGetValueForInfoDictionaryKey(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFBundleGetIdentifier(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFBundleCopyLocalizedString(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef key,
	darling_windows_CFStringRef value, darling_windows_CFStringRef table_name);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFBundleGetTypeID();
extern "C" DARLING_CF_GUEST_ABI bool CFBundleIsExecutableLoaded(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI bool CFBundlePreflightExecutable(
	darling_windows_CFBundleRef bundle, void** error);
extern "C" DARLING_CF_GUEST_ABI bool CFBundleLoadExecutable(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI void CFBundleUnloadExecutable(
	darling_windows_CFBundleRef bundle);
extern "C" DARLING_CF_GUEST_ABI void* CFBundleGetFunctionPointerForName(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef function_name);
extern "C" DARLING_CF_GUEST_ABI void* CFBundleGetDataPointerForName(
	darling_windows_CFBundleRef bundle, darling_windows_CFStringRef symbol_name);
extern "C" darling_windows_CFBooleanRef darling_windows_CFBooleanGetValue(bool value);
extern "C" darling_windows_CFTypeID darling_windows_CFBooleanGetTypeID();
extern "C" bool darling_windows_CFBooleanIsTrue(darling_windows_CFBooleanRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFNullGetValue();
extern "C" darling_windows_CFTypeID darling_windows_CFNullGetTypeID();
extern "C" darling_windows_CFStringRef darling_windows_CFPropertyListCreateXML(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromXML(
	const char* xml);
extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromBinary(
	const void* bytes, darling_windows_CFIndex length);
// Internal discriminator/accessor for UID markers retained by the binary
// property-list reader. UIDs are archive references, not CFNumbers.
extern "C" darling_windows_CFTypeID darling_windows_CFPropertyListUIDGetTypeID();
extern "C" bool darling_windows_CFPropertyListUIDGetValue(
	darling_windows_CFTypeRef value, std::uint64_t* uid);
extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetCurrent();
extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetMain();
extern "C" void darling_windows_CFRunLoopUseCurrentAsMain();
extern "C" int darling_windows_CFRunLoopRunInMode(double seconds, bool return_after_source);
extern "C" int darling_windows_CFRunLoopRunInModeForLoop(
	darling_windows_CFRunLoopRef loop, const char* mode, double seconds,
	bool return_after_source);
extern "C" void darling_windows_CFRunLoopStop(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopIsRunning(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopIsWaiting(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopIsCommonMode(
	darling_windows_CFRunLoopRef loop, const char* mode);
extern "C" void darling_windows_CFRunLoopSetWaiting(
	darling_windows_CFRunLoopRef loop, bool waiting);
extern "C" bool darling_windows_CFRunLoopContainsPendingBlockForMode(
	darling_windows_CFRunLoopRef loop, const char* mode);
extern "C" bool darling_windows_CFRunLoopPerformBlock(darling_windows_CFRunLoopRef loop,
	darling_windows_CFRunLoopBlock block, void* context);
extern "C" bool darling_windows_CFRunLoopPerformBlockInMode(
	darling_windows_CFRunLoopRef loop, const char* mode,
	darling_windows_CFRunLoopBlock block, void* context);
extern "C" bool darling_windows_CFRunLoopPerformBlockInModesWithOrder(
	darling_windows_CFRunLoopRef loop, const char* const* modes,
	std::size_t mode_count, std::uint64_t order,
	darling_windows_CFRunLoopBlock block, void* context);
extern "C" void darling_windows_CFRunLoopAddCommonMode(
	darling_windows_CFRunLoopRef loop, const char* mode);
extern "C" void darling_windows_CFRunLoopWakeUp(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopPerformOneShotTimer(
	darling_windows_CFRunLoopRef loop, double seconds,
	darling_windows_CFRunLoopBlock block, void* context);
// Darwin guest-ABI entry points exported to Mach-O import resolution.
extern "C" std::uintptr_t darling_windows_corefoundation_symbol(const char* name);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFRunLoopRef CFRunLoopGetCurrent();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFRunLoopRef CFRunLoopGetMain();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFRunLoopCopyCurrentMode(
	darling_windows_CFRunLoopRef loop);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFRunLoopCopyAllModes(
	darling_windows_CFRunLoopRef loop);
extern "C" DARLING_CF_GUEST_ABI std::int32_t CFRunLoopRunInMode(
	darling_windows_CFStringRef mode, double seconds, std::uint8_t return_after_source);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRun();
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopStop(darling_windows_CFRunLoopRef loop);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopIsWaiting(
	darling_windows_CFRunLoopRef loop);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopWakeUp(darling_windows_CFRunLoopRef loop);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopPerformBlock(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef modes, void* block);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddCommonMode(
	darling_windows_CFRunLoopRef loop, darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopSourceGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopSourceCreate(
	const void* allocator, darling_windows_CFIndex order,
	const DarlingCFRunLoopSourceContext* context);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopSourceGetOrder(
	darling_windows_CFTypeRef source);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceGetContext(
	darling_windows_CFTypeRef source, DarlingCFRunLoopSourceContext* context);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceSignal(darling_windows_CFTypeRef source);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopSourceIsValid(darling_windows_CFTypeRef source);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopSourceInvalidate(darling_windows_CFTypeRef source);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsSource(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef source,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddSource(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef source,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveSource(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef source,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopTimerGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopTimerCreate(
	const void* allocator, double fire_date, double interval, std::uint64_t flags,
	darling_windows_CFIndex order, DarlingCFRunLoopTimerCallout callout,
	const DarlingCFRunLoopTimerContext* context);
extern "C" DARLING_CF_GUEST_ABI double CFRunLoopTimerGetNextFireDate(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerSetNextFireDate(
	darling_windows_CFTypeRef timer, double fire_date);
extern "C" DARLING_CF_GUEST_ABI double CFRunLoopTimerGetInterval(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopTimerDoesRepeat(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopTimerGetOrder(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerInvalidate(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopTimerIsValid(
	darling_windows_CFTypeRef timer);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopTimerGetContext(
	darling_windows_CFTypeRef timer, DarlingCFRunLoopTimerContext* context);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsTimer(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef timer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddTimer(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef timer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveTimer(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef timer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI double CFRunLoopGetNextTimerFireDate(
	darling_windows_CFRunLoopRef loop, darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFRunLoopObserverGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFRunLoopObserverCreate(
	const void* allocator, std::uint64_t activities, std::uint8_t repeats,
	darling_windows_CFIndex order, DarlingCFRunLoopObserverCallout callout,
	const DarlingCFRunLoopObserverContext* context);
extern "C" DARLING_CF_GUEST_ABI std::uint64_t CFRunLoopObserverGetActivities(
	darling_windows_CFTypeRef observer);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopObserverDoesRepeat(
	darling_windows_CFTypeRef observer);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFRunLoopObserverGetOrder(
	darling_windows_CFTypeRef observer);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopObserverGetContext(
	darling_windows_CFTypeRef observer, DarlingCFRunLoopObserverContext* context);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopObserverIsValid(
	darling_windows_CFTypeRef observer);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopObserverInvalidate(
	darling_windows_CFTypeRef observer);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFRunLoopContainsObserver(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef observer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopAddObserver(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef observer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI void CFRunLoopRemoveObserver(
	darling_windows_CFRunLoopRef loop, darling_windows_CFTypeRef observer,
	darling_windows_CFStringRef mode);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFMachPortGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMachPortRef CFMachPortCreate(
	const void* allocator, DarlingCFMachPortCallout callout,
	const DarlingCFMachPortContext* context, std::uint8_t* should_free_info);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMachPortRef CFMachPortCreateWithPort(
	const void* allocator, std::uint32_t port, DarlingCFMachPortCallout callout,
	const DarlingCFMachPortContext* context, std::uint8_t* should_free_info);
extern "C" DARLING_CF_GUEST_ABI void CFMachPortGetContext(
	darling_windows_CFMachPortRef port, DarlingCFMachPortContext* context);
extern "C" DARLING_CF_GUEST_ABI std::uint32_t CFMachPortGetPort(
	darling_windows_CFMachPortRef port);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFMachPortIsValid(
	darling_windows_CFMachPortRef port);
extern "C" DARLING_CF_GUEST_ABI void CFMachPortInvalidate(
	darling_windows_CFMachPortRef port);
extern "C" DARLING_CF_GUEST_ABI void CFMachPortSetInvalidationCallBack(
	darling_windows_CFMachPortRef port, DarlingCFMachPortInvalidationCallout callout);
extern "C" DARLING_CF_GUEST_ABI DarlingCFMachPortInvalidationCallout
CFMachPortGetInvalidationCallBack(darling_windows_CFMachPortRef port);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFAllocatorGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFAllocatorRef CFAllocatorCreate(
	const void* allocator, const DarlingCFAllocatorContext* context);
extern "C" DARLING_CF_GUEST_ABI void CFAllocatorGetContext(
	darling_windows_CFAllocatorRef allocator, DarlingCFAllocatorContext* context);
extern "C" DARLING_CF_GUEST_ABI void* CFAllocatorAllocate(
	darling_windows_CFAllocatorRef allocator, darling_windows_CFIndex size, std::uint64_t hint);
extern "C" DARLING_CF_GUEST_ABI void CFAllocatorDeallocate(
	darling_windows_CFAllocatorRef allocator, void* address);
extern "C" DARLING_CF_GUEST_ABI void* CFAllocatorReallocate(
	darling_windows_CFAllocatorRef allocator, void* address,
	darling_windows_CFIndex size, std::uint64_t hint);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFAllocatorGetPreferredSizeForSize(
	darling_windows_CFAllocatorRef allocator, darling_windows_CFIndex size, std::uint64_t hint);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFAllocatorRef CFAllocatorGetDefault();
extern "C" DARLING_CF_GUEST_ABI void CFAllocatorSetDefault(
	darling_windows_CFAllocatorRef allocator);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFMachPortCreateRunLoopSource(
	const void* allocator, darling_windows_CFMachPortRef port,
	darling_windows_CFIndex order);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFStringGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateWithCString(
	const void* allocator, const char* bytes, std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFStringGetLength(
	darling_windows_CFStringRef value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetCString(
	darling_windows_CFStringRef value, char* buffer, darling_windows_CFIndex capacity,
	std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateWithCharacters(
	const void* allocator, const std::uint16_t* characters, darling_windows_CFIndex length);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFStringCreateCopy(
	const void* allocator, darling_windows_CFStringRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableStringRef CFStringCreateMutable(
	const void* allocator, darling_windows_CFIndex max_length);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFMutableStringRef CFStringCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex max_length,
	darling_windows_CFStringRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateWithString(
	const void* allocator, darling_windows_CFStringRef string,
	darling_windows_CFURLRef base_url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateWithFileSystemPath(
	const void* allocator, darling_windows_CFStringRef file_path, std::int32_t path_style,
	std::uint8_t is_directory);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFURLCreateWithFileSystemPathRelativeToBase(const void* allocator,
	darling_windows_CFStringRef file_path, std::int32_t path_style,
	std::uint8_t is_directory, darling_windows_CFURLRef base_url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFURLCreateFromFileSystemRepresentation(const void* allocator, const std::uint8_t* bytes,
	darling_windows_CFIndex length, std::uint8_t is_directory);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef
CFURLCreateFromFileSystemRepresentationRelativeToBase(const void* allocator,
	const std::uint8_t* bytes, darling_windows_CFIndex length,
	std::uint8_t is_directory, darling_windows_CFURLRef base_url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateWithBytes(
	const void* allocator, const std::uint8_t* bytes, darling_windows_CFIndex length,
	std::uint32_t encoding, darling_windows_CFURLRef base_url);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateAbsoluteURLWithBytes(
	const void* allocator, const std::uint8_t* bytes, darling_windows_CFIndex length,
	std::uint32_t encoding, darling_windows_CFURLRef base_url,
	std::uint8_t use_compatibility_mode);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByReplacingPercentEscapes(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_escaped);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByReplacingPercentEscapesUsingEncoding(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_escaped,
	std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef
CFURLCreateStringByAddingPercentEscapes(const void* allocator,
	darling_windows_CFStringRef original_string,
	darling_windows_CFStringRef characters_to_leave_unescaped,
	darling_windows_CFStringRef legal_url_characters_to_escape,
	std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFURLGetTypeID();
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLCanBeDecomposed(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLGetString(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value, std::int32_t path_style);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPath(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyStrictPath(
	darling_windows_CFURLRef value, std::uint8_t* is_absolute);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyParameterString(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyLastPathComponent(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateCopyAppendingPathComponent(
	const void* allocator, darling_windows_CFURLRef value,
	darling_windows_CFStringRef component, std::uint8_t is_directory);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCreateCopyAppendingPathExtension(
	const void* allocator, darling_windows_CFURLRef value,
	darling_windows_CFStringRef extension);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPathExtension(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyScheme(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyResourceSpecifier(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyHostName(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyNetLocation(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyQueryString(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyFragment(
	darling_windows_CFURLRef value, darling_windows_CFStringRef characters_to_leave_escaped);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyUserName(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFStringRef CFURLCopyPassword(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI std::int32_t CFURLGetPortNumber(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLHasDirectoryPath(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFURLGetFileSystemRepresentation(
	darling_windows_CFURLRef value, std::uint8_t resolve_against_base,
	std::uint8_t* buffer, darling_windows_CFIndex buffer_capacity);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFURLRef CFURLCopyAbsoluteURL(
	darling_windows_CFURLRef value);
extern "C" DARLING_CF_GUEST_ABI const char* CFStringGetCStringPtr(
	darling_windows_CFStringRef value, std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI std::uint16_t CFStringGetCharacterAtIndex(
	darling_windows_CFStringRef value, darling_windows_CFIndex index);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetCharacters(
	darling_windows_CFStringRef value, darling_windows_CFRange range, std::uint16_t* output);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFStringGetMaximumSizeForEncoding(
	darling_windows_CFIndex length, std::uint32_t encoding);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex
CFStringGetMaximumSizeOfFileSystemRepresentation(darling_windows_CFStringRef value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFStringGetFileSystemRepresentation(
	darling_windows_CFStringRef value, char* buffer, darling_windows_CFIndex capacity);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFArrayGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetCount(
	darling_windows_CFArrayRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index);
extern "C" DARLING_CF_GUEST_ABI void CFArrayGetValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range, const void** output);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFArrayContainsValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreate(
	const void* allocator, const void* const* values, darling_windows_CFIndex count,
	const DarlingCFArrayCallbacks* callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity,
	const DarlingCFArrayCallbacks* callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateCopy(
	const void* allocator, darling_windows_CFArrayRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFArrayRef CFArrayCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity,
	darling_windows_CFArrayRef value);
extern "C" DARLING_CF_GUEST_ABI void CFArrayAppendValue(
	darling_windows_CFArrayRef value, darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFArrayInsertValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index,
	darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFArrayRemoveValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index);
extern "C" DARLING_CF_GUEST_ABI void CFArraySetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index,
	darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFArrayReplaceValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	const void* const* replacement, darling_windows_CFIndex replacement_count);
extern "C" DARLING_CF_GUEST_ABI void CFArrayRemoveAllValues(
	darling_windows_CFArrayRef value);
extern "C" DARLING_CF_GUEST_ABI void CFArrayApplyFunction(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFArrayGuestApplierFunction callback, void* context);
extern "C" DARLING_CF_GUEST_ABI void CFArrayAppendArray(
	darling_windows_CFArrayRef value, darling_windows_CFArrayRef source,
	darling_windows_CFRange range);
extern "C" DARLING_CF_GUEST_ABI void CFArrayExchangeValuesAtIndices(
	darling_windows_CFArrayRef value, darling_windows_CFIndex first,
	darling_windows_CFIndex second);
extern "C" DARLING_CF_GUEST_ABI void CFArraySortValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFComparatorFunction comparator, void* context);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFArrayBSearchValues(
	darling_windows_CFArrayRef value, darling_windows_CFRange range,
	darling_windows_CFTypeRef candidate,
	darling_windows_CFComparatorFunction comparator, void* context);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFNumberGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFNumberCreate(
	const void* allocator, darling_windows_CFNumberType type, const void* value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFNumberGetValue(
	darling_windows_CFTypeRef number, darling_windows_CFNumberType type, void* output);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFNumberIsFloatType(
	darling_windows_CFTypeRef number);
extern "C" DARLING_CF_GUEST_ABI std::intptr_t CFNumberCompare(
	darling_windows_CFTypeRef left, darling_windows_CFTypeRef right, const void* context);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDateGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDateRef CFDateCreate(
	const void* allocator, double absolute_time);
extern "C" DARLING_CF_GUEST_ABI double CFDateGetAbsoluteTime(
	darling_windows_CFDateRef value);
extern "C" DARLING_CF_GUEST_ABI std::intptr_t CFDateCompare(
	darling_windows_CFDateRef left, darling_windows_CFDateRef right, const void* context);
extern "C" DARLING_CF_GUEST_ABI double CFDateGetTimeIntervalSinceDate(
	darling_windows_CFDateRef value, darling_windows_CFDateRef reference);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFDictionaryGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryGetValueIfPresent(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	const void** output);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFDictionaryContainsValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreate(
	const void* allocator, const void* const* keys, const void* const* values,
	darling_windows_CFIndex count, const void* key_callbacks, const void* value_callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity,
	const void* key_callbacks, const void* value_callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateCopy(
	const void* allocator, darling_windows_CFDictionaryRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFDictionaryRef CFDictionaryCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity,
	darling_windows_CFDictionaryRef value);
extern "C" DARLING_CF_GUEST_ABI void CFDictionarySetValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryAddValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryReplaceValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key,
	darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryRemoveValue(
	darling_windows_CFDictionaryRef value, darling_windows_CFTypeRef key);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryRemoveAllValues(
	darling_windows_CFDictionaryRef value);
extern "C" DARLING_CF_GUEST_ABI void CFDictionaryApplyFunction(
	darling_windows_CFDictionaryRef value,
	darling_windows_CFDictionaryGuestApplierFunction callback, void* context);
using darling_windows_CFSetGuestApplierFunction = void (DARLING_CF_GUEST_ABI *)(
	const void* value, void* context);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFSetGetTypeID();
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreate(
	const void* allocator, const void* const* values, darling_windows_CFIndex count,
	const void* callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateMutable(
	const void* allocator, darling_windows_CFIndex capacity, const void* callbacks);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateCopy(
	const void* allocator, darling_windows_CFSetRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFSetRef CFSetCreateMutableCopy(
	const void* allocator, darling_windows_CFIndex capacity,
	darling_windows_CFSetRef value);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFSetGetCount(
	darling_windows_CFSetRef value);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFSetContainsValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFIndex CFSetGetCountOfValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeRef CFSetGetValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate);
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFSetGetValueIfPresent(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef candidate,
	const void** output);
extern "C" DARLING_CF_GUEST_ABI void CFSetGetValues(
	darling_windows_CFSetRef value, const void** output);
extern "C" DARLING_CF_GUEST_ABI void CFSetApplyFunction(
	darling_windows_CFSetRef value,
	darling_windows_CFSetGuestApplierFunction callback, void* context);
extern "C" DARLING_CF_GUEST_ABI void CFSetAddValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFSetSetValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFSetReplaceValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFSetRemoveValue(
	darling_windows_CFSetRef value, darling_windows_CFTypeRef element);
extern "C" DARLING_CF_GUEST_ABI void CFSetRemoveAllValues(darling_windows_CFSetRef value);
extern "C" DARLING_CF_GUEST_ABI void CFSetUnion(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source);
extern "C" DARLING_CF_GUEST_ABI void CFSetIntersect(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source);
extern "C" DARLING_CF_GUEST_ABI void CFSetSubtract(
	darling_windows_CFSetRef value, darling_windows_CFSetRef source);
extern "C" DARLING_CF_GUEST_ABI darling_windows_CFTypeID CFBooleanGetTypeID();
extern "C" DARLING_CF_GUEST_ABI std::uint8_t CFBooleanGetValue(
	darling_windows_CFBooleanRef value);
extern "C" void* darling_windows_CFNotificationCenterGetLocal();
extern "C" bool darling_windows_CFNotificationCenterAddObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name);
extern "C" bool darling_windows_CFNotificationCenterRemoveObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name);
extern "C" void darling_windows_CFNotificationCenterPostNotification(void* center,
	const char* name, const void* object);
