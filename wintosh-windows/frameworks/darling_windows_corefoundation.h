/* Minimal CoreFoundation-compatible primitives; GPL-3.0-only. */
#pragma once

#include <cstddef>
#include <cstdint>

using darling_windows_CFIndex = std::ptrdiff_t;
struct darling_windows_CFRange { darling_windows_CFIndex location; darling_windows_CFIndex length; };
enum darling_windows_CFStringCompareFlags {
	darling_windows_CFCompareCaseInsensitive = 1 << 0
};
using darling_windows_CFTypeRef = const void*;
using darling_windows_CFStringRef = const void*;
using darling_windows_CFDataRef = const void*;
using darling_windows_CFArrayRef = const void*;
using darling_windows_CFDictionaryRef = const void*;
using darling_windows_CFSetRef = const void*;
using darling_windows_CFDateRef = const void*;
using darling_windows_CFURLRef = const void*;
using darling_windows_CFBooleanRef = const void*;
using darling_windows_CFRunLoopRef = void*;
using darling_windows_CFRunLoopBlock = void (*)(void* context);
using darling_windows_CFNotificationCallback = void (*)(const void* observer,
	const char* name, const void* object);
using darling_windows_CFArrayApplierFunction = void (*)(const void* value, void* context);
using darling_windows_CFDictionaryApplierFunction = void (*)(const void* key,
	const void* value, void* context);

using darling_windows_CFTypeID = std::uint64_t;

extern "C" darling_windows_CFRange darling_windows_CFRangeMake(
	darling_windows_CFIndex location, darling_windows_CFIndex length);
extern "C" darling_windows_CFIndex darling_windows_CFRangeGetMax(
	darling_windows_CFRange range);

extern "C" void* darling_windows_CFRetain(const void* value);
extern "C" void darling_windows_CFRelease(const void* value);
extern "C" darling_windows_CFTypeID darling_windows_CFGetTypeID(const void* value);
extern "C" std::uint64_t darling_windows_CFHash(const void* value);
extern "C" bool darling_windows_CFEqual(const void* left, const void* right);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCString(
	const char* value);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithBytes(
	const unsigned char* bytes, darling_windows_CFIndex length, int encoding,
	bool is_external_representation);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithFormat(
	const char* format, ...);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateCopy(
	darling_windows_CFStringRef value);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetLength(
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
extern "C" int darling_windows_CFStringCompare(darling_windows_CFStringRef left,
	darling_windows_CFStringRef right);
extern "C" darling_windows_CFRange darling_windows_CFStringFind(
	darling_windows_CFStringRef value, darling_windows_CFStringRef needle, int flags);
extern "C" bool darling_windows_CFStringAppendCString(darling_windows_CFStringRef value,
	const char* suffix);
extern "C" bool darling_windows_CFStringReplaceAll(darling_windows_CFStringRef value,
	darling_windows_CFStringRef target, darling_windows_CFStringRef replacement);
extern "C" bool darling_windows_CFStringReplaceRange(darling_windows_CFStringRef value,
	darling_windows_CFIndex location, darling_windows_CFIndex length, const char* replacement);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetBytes(
	darling_windows_CFStringRef value, darling_windows_CFIndex location,
	darling_windows_CFIndex length, char* output, darling_windows_CFIndex capacity);
extern "C" bool darling_windows_CFStringTrimWhitespace(darling_windows_CFStringRef value);
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreate(
	const void* bytes, darling_windows_CFIndex length);
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreateCopy(
	darling_windows_CFDataRef value);
extern "C" const unsigned char* darling_windows_CFDataGetBytePtr(
	darling_windows_CFDataRef value);
extern "C" unsigned char* darling_windows_CFDataGetMutableBytePtr(
	darling_windows_CFDataRef value);
extern "C" darling_windows_CFIndex darling_windows_CFDataGetLength(
	darling_windows_CFDataRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDataGetTypeID();
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
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreateCopy(
	darling_windows_CFArrayRef value);
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
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreateCopy(
	darling_windows_CFDictionaryRef value);
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
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreateCopy(
	darling_windows_CFSetRef value);
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
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateCopy(
	darling_windows_CFDateRef value);
extern "C" double darling_windows_CFDateGetAbsoluteTime(darling_windows_CFDateRef value);
extern "C" darling_windows_CFTypeID darling_windows_CFDateGetTypeID();
extern "C" int darling_windows_CFDateCompare(darling_windows_CFDateRef left,
	darling_windows_CFDateRef right);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreateByAddingTimeInterval(
    darling_windows_CFDateRef value, double seconds);
extern "C" double darling_windows_CFDateGetTimeIntervalSinceDate(
    darling_windows_CFDateRef value, darling_windows_CFDateRef reference);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithFileSystemPath(
	const char* path);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateFromFileSystemRepresentation(
	const unsigned char* bytes, darling_windows_CFIndex length, bool is_directory);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFileSystemPath(
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
extern "C" darling_windows_CFBooleanRef darling_windows_CFBooleanGetValue(bool value);
extern "C" bool darling_windows_CFBooleanIsTrue(darling_windows_CFBooleanRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFNullGetValue();
extern "C" darling_windows_CFStringRef darling_windows_CFPropertyListCreateXML(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromXML(
	const char* xml);
extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromBinary(
	const void* bytes, darling_windows_CFIndex length);
extern "C" darling_windows_CFRunLoopRef darling_windows_CFRunLoopGetCurrent();
extern "C" int darling_windows_CFRunLoopRunInMode(double seconds, bool return_after_source);
extern "C" void darling_windows_CFRunLoopStop(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopIsRunning(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopPerformBlock(darling_windows_CFRunLoopRef loop,
	darling_windows_CFRunLoopBlock block, void* context);
extern "C" void darling_windows_CFRunLoopWakeUp(darling_windows_CFRunLoopRef loop);
extern "C" bool darling_windows_CFRunLoopPerformOneShotTimer(
	darling_windows_CFRunLoopRef loop, double seconds,
	darling_windows_CFRunLoopBlock block, void* context);
extern "C" void* darling_windows_CFNotificationCenterGetLocal();
extern "C" bool darling_windows_CFNotificationCenterAddObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name);
extern "C" bool darling_windows_CFNotificationCenterRemoveObserver(void* center,
	const void* observer, darling_windows_CFNotificationCallback callback, const char* name);
extern "C" void darling_windows_CFNotificationCenterPostNotification(void* center,
	const char* name, const void* object);
