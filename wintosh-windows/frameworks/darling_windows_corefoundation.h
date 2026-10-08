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

extern "C" void* darling_windows_CFRetain(const void* value);
extern "C" void darling_windows_CFRelease(const void* value);
extern "C" bool darling_windows_CFEqual(const void* left, const void* right);
extern "C" darling_windows_CFStringRef darling_windows_CFStringCreateWithCString(
	const char* value);
extern "C" darling_windows_CFIndex darling_windows_CFStringGetLength(
	darling_windows_CFStringRef value);
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
extern "C" darling_windows_CFDataRef darling_windows_CFDataCreate(
	const void* bytes, darling_windows_CFIndex length);
extern "C" const unsigned char* darling_windows_CFDataGetBytePtr(
	darling_windows_CFDataRef value);
extern "C" darling_windows_CFIndex darling_windows_CFDataGetLength(
	darling_windows_CFDataRef value);
extern "C" bool darling_windows_CFDataGetBytes(darling_windows_CFDataRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count,
	unsigned char* output);
extern "C" darling_windows_CFArrayRef darling_windows_CFArrayCreate(
	const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCount(
	darling_windows_CFArrayRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFArrayGetValueAtIndex(
	darling_windows_CFArrayRef value, darling_windows_CFIndex index);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetFirstIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetCountOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" darling_windows_CFIndex darling_windows_CFArrayGetLastIndexOfValue(
	darling_windows_CFArrayRef value, const void* candidate);
extern "C" void darling_windows_CFArrayGetValues(darling_windows_CFArrayRef value,
	darling_windows_CFIndex range_start, darling_windows_CFIndex range_count, const void** output);
extern "C" const void* darling_windows_CFNumberCreateInteger(std::int64_t value);
extern "C" bool darling_windows_CFNumberGetInteger(const void* value, std::int64_t* result);
extern "C" const void* darling_windows_CFNumberCreateDouble(double value);
extern "C" bool darling_windows_CFNumberGetDouble(const void* value, double* result);
extern "C" darling_windows_CFDictionaryRef darling_windows_CFDictionaryCreate(
	const void* const* keys, const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFIndex darling_windows_CFDictionaryGetCount(
	darling_windows_CFDictionaryRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFDictionaryGetValue(
	darling_windows_CFDictionaryRef value, const void* key);
extern "C" bool darling_windows_CFDictionaryContainsKey(
	darling_windows_CFDictionaryRef value, const void* key);
extern "C" void darling_windows_CFDictionaryGetKeysAndValues(
	darling_windows_CFDictionaryRef value, const void** keys, const void** values);
extern "C" darling_windows_CFSetRef darling_windows_CFSetCreate(
	const void* const* values, darling_windows_CFIndex count);
extern "C" darling_windows_CFIndex darling_windows_CFSetGetCount(darling_windows_CFSetRef value);
extern "C" bool darling_windows_CFSetContainsValue(darling_windows_CFSetRef value,
	const void* candidate);
extern "C" darling_windows_CFTypeRef darling_windows_CFSetGetValue(
	darling_windows_CFSetRef value, const void* candidate);
extern "C" void darling_windows_CFSetGetValues(darling_windows_CFSetRef value, const void** output);
extern "C" darling_windows_CFDateRef darling_windows_CFDateCreate(double absolute_time);
extern "C" double darling_windows_CFDateGetAbsoluteTime(darling_windows_CFDateRef value);
extern "C" darling_windows_CFURLRef darling_windows_CFURLCreateWithFileSystemPath(
	const char* path);
extern "C" darling_windows_CFStringRef darling_windows_CFURLCopyFileSystemPath(
	darling_windows_CFURLRef value);
extern "C" darling_windows_CFBooleanRef darling_windows_CFBooleanGetValue(bool value);
extern "C" bool darling_windows_CFBooleanIsTrue(darling_windows_CFBooleanRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFNullGetValue();
extern "C" darling_windows_CFStringRef darling_windows_CFPropertyListCreateXML(
	darling_windows_CFTypeRef value);
extern "C" darling_windows_CFTypeRef darling_windows_CFPropertyListCreateFromXML(
	const char* xml);
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
