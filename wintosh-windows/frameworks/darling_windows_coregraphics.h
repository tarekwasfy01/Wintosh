/* Minimal CoreGraphics geometry primitives; GPL-3.0-only. */
#pragma once

#include "darling_windows_corefoundation.h"

#include <cstddef>
#include <cstdint>

struct darling_windows_CGAffineTransform {
	double a;
	double b;
	double c;
	double d;
	double tx;
	double ty;
};
struct darling_windows_CGPoint { double x; double y; };
struct darling_windows_CGSize { double width; double height; };
struct darling_windows_CGRect { darling_windows_CGPoint origin; darling_windows_CGSize size; };
struct darling_windows_CGVector { double dx; double dy; };
enum darling_windows_CGRectEdge { darling_windows_CGMinXEdge, darling_windows_CGMinYEdge,
	darling_windows_CGMaxXEdge, darling_windows_CGMaxYEdge };
struct darling_windows_CGColor { double red; double green; double blue; double alpha; };

struct darling_windows_CGColorSpace;
struct darling_windows_CGColorObject;
struct darling_windows_CGContextObject;
struct darling_windows_CGImageObject;
using darling_windows_CGColorSpaceRef = darling_windows_CGColorSpace*;
using darling_windows_CGColorRef = darling_windows_CGColorObject*;
using darling_windows_CGContextRef = darling_windows_CGContextObject*;
using darling_windows_CGImageRef = darling_windows_CGImageObject*;
using darling_windows_CGImageSourceRef = const void*;
using darling_windows_CGDataProviderRef = const void*;
enum darling_windows_CGImageSourceStatus : std::int32_t {
	darling_windows_kCGImageStatusUnexpectedEOF = -5,
	darling_windows_kCGImageStatusInvalidData = -4,
	darling_windows_kCGImageStatusUnknownType = -3,
	darling_windows_kCGImageStatusReadingHeader = -2,
	darling_windows_kCGImageStatusIncomplete = -1,
	darling_windows_kCGImageStatusComplete = 0
};
using darling_windows_CGBitmapInfo = std::uint32_t;
using darling_windows_CGBlendMode = std::int32_t;
using darling_windows_CGLineCap = std::int32_t;
enum : darling_windows_CGLineCap {
	darling_windows_kCGLineCapButt = 0,
	darling_windows_kCGLineCapRound = 1,
	darling_windows_kCGLineCapSquare = 2
};
using darling_windows_CGLineJoin = std::int32_t;
enum : darling_windows_CGLineJoin {
	darling_windows_kCGLineJoinMiter = 0,
	darling_windows_kCGLineJoinRound = 1,
	darling_windows_kCGLineJoinBevel = 2
};
enum : darling_windows_CGBlendMode {
	darling_windows_kCGBlendModeNormal = 0,
	darling_windows_kCGBlendModeMultiply = 1,
	darling_windows_kCGBlendModeScreen = 2,
	darling_windows_kCGBlendModeOverlay = 3,
	darling_windows_kCGBlendModeDarken = 4,
	darling_windows_kCGBlendModeLighten = 5,
	darling_windows_kCGBlendModeDifference = 10,
	darling_windows_kCGBlendModeExclusion = 11,
	darling_windows_kCGBlendModeClear = 16,
	darling_windows_kCGBlendModeCopy = 17,
	darling_windows_kCGBlendModeSourceIn = 18,
	darling_windows_kCGBlendModeSourceOut = 19,
	darling_windows_kCGBlendModeSourceAtop = 20,
	darling_windows_kCGBlendModeDestinationOver = 21,
	darling_windows_kCGBlendModeDestinationIn = 22,
	darling_windows_kCGBlendModeDestinationOut = 23,
	darling_windows_kCGBlendModeDestinationAtop = 24,
	darling_windows_kCGBlendModeXOR = 25
};

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_CG_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_CG_GUEST_ABI
#endif

using darling_windows_CGDataProviderReleaseDataCallback = void (DARLING_CG_GUEST_ABI *)(
	void* info, const void* data, std::size_t size);
using darling_windows_CGDataProviderGetBytesCallback = darling_windows_CFDataProviderGetBytesCallback;
using darling_windows_CGDataProviderSkipForwardCallback = darling_windows_CFDataProviderSkipForwardCallback;
using darling_windows_CGDataProviderRewindCallback = darling_windows_CFDataProviderRewindCallback;
using darling_windows_CGDataProviderReleaseInfoCallback = darling_windows_CFDataProviderReleaseInfoCallback;
using darling_windows_CGDataProviderGetBytePointerCallback = darling_windows_CFDataProviderGetBytePointerCallback;
using darling_windows_CGDataProviderReleaseBytePointerCallback = darling_windows_CFDataProviderReleaseBytePointerCallback;
using darling_windows_CGDataProviderGetBytesAtPositionCallback = darling_windows_CFDataProviderGetBytesAtPositionCallback;
struct darling_windows_CGDataProviderSequentialCallbacks {
	std::uint32_t version;
	darling_windows_CGDataProviderGetBytesCallback get_bytes;
	darling_windows_CGDataProviderSkipForwardCallback skip_forward;
	darling_windows_CGDataProviderRewindCallback rewind;
	darling_windows_CGDataProviderReleaseInfoCallback release_info;
};
struct darling_windows_CGDataProviderDirectCallbacks {
	std::uint32_t version;
	darling_windows_CGDataProviderGetBytePointerCallback get_byte_pointer;
	darling_windows_CGDataProviderReleaseBytePointerCallback release_byte_pointer;
	darling_windows_CGDataProviderGetBytesAtPositionCallback get_bytes_at_position;
	darling_windows_CGDataProviderReleaseInfoCallback release_info;
};
using darling_windows_CGDataConsumerPutBytesCallback = std::size_t (DARLING_CG_GUEST_ABI *)(
	void* info, const void* buffer, std::size_t count);
using darling_windows_CGDataConsumerReleaseInfoCallback = void (DARLING_CG_GUEST_ABI *)(
	void* info);
struct darling_windows_CGDataConsumerCallbacks {
	darling_windows_CGDataConsumerPutBytesCallback put_bytes;
	darling_windows_CGDataConsumerReleaseInfoCallback release_consumer;
};

extern "C" darling_windows_CGPoint darling_windows_CGPointMake(double x, double y);
extern "C" darling_windows_CGSize darling_windows_CGSizeMake(double width, double height);
extern "C" darling_windows_CGRect darling_windows_CGRectMake(double x, double y,
	double width, double height);
extern "C" darling_windows_CGRect darling_windows_CGRectStandardize(
	darling_windows_CGRect rect);
extern "C" darling_windows_CGRect darling_windows_CGRectGetBoundingBox(
	darling_windows_CGRect rect);
extern "C" bool darling_windows_CGRectIsStandardized(
	darling_windows_CGRect rect);
extern "C" darling_windows_CGVector darling_windows_CGVectorMake(double dx, double dy);
extern "C" darling_windows_CGPoint darling_windows_CGPointAdd(
	darling_windows_CGPoint point, darling_windows_CGVector vector);
extern "C" darling_windows_CGVector darling_windows_CGPointSubtract(
	darling_windows_CGPoint left, darling_windows_CGPoint right);
extern "C" double darling_windows_CGVectorDot(darling_windows_CGVector left,
	darling_windows_CGVector right);
extern "C" double darling_windows_CGVectorLength(darling_windows_CGVector vector);
extern "C" darling_windows_CGVector darling_windows_CGVectorNormalize(
	darling_windows_CGVector vector, bool* valid);

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMake(
	double a, double b, double c, double d, double tx, double ty);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformIdentity();
extern "C" bool darling_windows_CGAffineTransformIsIdentity(
	darling_windows_CGAffineTransform transform);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeTranslation(
	double tx, double ty);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeScale(
	double sx, double sy);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeRotation(
	double radians);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformTranslate(
	darling_windows_CGAffineTransform transform, double tx, double ty);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformScale(
	darling_windows_CGAffineTransform transform, double sx, double sy);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformRotate(
	darling_windows_CGAffineTransform transform, double radians);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformConcat(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right);
extern "C" darling_windows_CGPoint darling_windows_CGPointApplyAffineTransform(
	darling_windows_CGPoint point, darling_windows_CGAffineTransform transform);
extern "C" darling_windows_CGRect darling_windows_CGRectApplyAffineTransform(
	darling_windows_CGRect rect, darling_windows_CGAffineTransform transform);
extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformInvert(
	darling_windows_CGAffineTransform transform, bool* invertible);
extern "C" bool darling_windows_CGAffineTransformEqualToTransform(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right);
extern "C" bool darling_windows_CGRectIsEmpty(darling_windows_CGRect rect);
extern "C" darling_windows_CGRect darling_windows_CGRectNull();
extern "C" bool darling_windows_CGRectIsNull(darling_windows_CGRect rect);
extern "C" bool darling_windows_CGRectIsInfinite(darling_windows_CGRect rect);
extern "C" darling_windows_CGRect darling_windows_CGRectIntegral(
	darling_windows_CGRect rect);
extern "C" bool darling_windows_CGRectContainsPoint(darling_windows_CGRect rect,
	darling_windows_CGPoint point);
extern "C" bool darling_windows_CGRectContainsRect(darling_windows_CGRect rect,
	darling_windows_CGRect candidate);
extern "C" darling_windows_CGRect darling_windows_CGRectIntersection(
	darling_windows_CGRect left, darling_windows_CGRect right);
extern "C" darling_windows_CGRect darling_windows_CGRectUnion(
	darling_windows_CGRect left, darling_windows_CGRect right);
extern "C" darling_windows_CGRect darling_windows_CGRectInset(
	darling_windows_CGRect rect, double dx, double dy);
extern "C" darling_windows_CGRect darling_windows_CGRectOffset(
	darling_windows_CGRect rect, double dx, double dy);
extern "C" double darling_windows_CGRectGetMinX(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetMidX(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetMaxX(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetMinY(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetMidY(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetMaxY(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetWidth(darling_windows_CGRect rect);
extern "C" double darling_windows_CGRectGetHeight(darling_windows_CGRect rect);
extern "C" bool darling_windows_CGPointEqualToPoint(darling_windows_CGPoint left,
	darling_windows_CGPoint right);
extern "C" bool darling_windows_CGSizeEqualToSize(darling_windows_CGSize left,
	darling_windows_CGSize right);
extern "C" bool darling_windows_CGRectEqualToRect(darling_windows_CGRect left,
	darling_windows_CGRect right);
extern "C" void darling_windows_CGRectDivide(darling_windows_CGRect rect,
	double amount, darling_windows_CGRectEdge edge, darling_windows_CGRect* slice,
	darling_windows_CGRect* remainder);
extern "C" darling_windows_CGColor darling_windows_CGColorMakeRGBA(
	double red, double green, double blue, double alpha);
extern "C" double darling_windows_CGColorGetAlpha(darling_windows_CGColor color);
extern "C" bool darling_windows_CGColorGetRed(darling_windows_CGColor color,
	double* value);
extern "C" bool darling_windows_CGColorGetGreen(darling_windows_CGColor color,
	double* value);
extern "C" bool darling_windows_CGColorGetBlue(darling_windows_CGColor color,
	double* value);
extern "C" bool darling_windows_CGColorGetWhite(darling_windows_CGColor color,
	double* value);
extern "C" int darling_windows_CGColorGetNumberOfComponents(
	darling_windows_CGColor color);
extern "C" bool darling_windows_CGColorGetComponents(darling_windows_CGColor color,
	double* components, int capacity);
extern "C" bool darling_windows_CGColorEqual(darling_windows_CGColor left,
	darling_windows_CGColor right);
extern "C" darling_windows_CGColor darling_windows_CGColorMakeGray(
	double white, double alpha);
extern "C" darling_windows_CGColor darling_windows_CGColorMakeWhite(double alpha);
extern "C" darling_windows_CGColor darling_windows_CGColorMakeClear();

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef
CGColorSpaceCreateDeviceRGB();
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef
CGColorSpaceCreateDeviceGray();
extern "C" DARLING_CG_GUEST_ABI void CGColorSpaceRetain(darling_windows_CGColorSpaceRef space);
extern "C" DARLING_CG_GUEST_ABI void CGColorSpaceRelease(darling_windows_CGColorSpaceRef space);
extern "C" DARLING_CG_GUEST_ABI int CGColorSpaceGetNumberOfComponents(
	darling_windows_CGColorSpaceRef space);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorRef CGColorCreate(
	darling_windows_CGColorSpaceRef space, const double* components);
extern "C" DARLING_CG_GUEST_ABI void CGColorRetain(darling_windows_CGColorRef color);
extern "C" DARLING_CG_GUEST_ABI void CGColorRelease(darling_windows_CGColorRef color);
extern "C" DARLING_CG_GUEST_ABI double CGColorGetAlpha(darling_windows_CGColorRef color);

constexpr darling_windows_CGBitmapInfo darling_windows_kCGImageAlphaPremultipliedLast = 1;
constexpr darling_windows_CGBitmapInfo darling_windows_kCGImageAlphaPremultipliedFirst = 2;
constexpr darling_windows_CGBitmapInfo darling_windows_kCGImageAlphaLast = 3;
constexpr darling_windows_CGBitmapInfo darling_windows_kCGBitmapByteOrder32Little = 0x2000;
constexpr darling_windows_CGBitmapInfo darling_windows_kCGBitmapByteOrder32Big = 0x4000;

extern "C" DARLING_CG_GUEST_ABI darling_windows_CGContextRef CGBitmapContextCreate(
	void* data, std::size_t width, std::size_t height, std::size_t bits_per_component,
	std::size_t bytes_per_row, darling_windows_CGColorSpaceRef space,
	darling_windows_CGBitmapInfo bitmap_info);
extern "C" DARLING_CG_GUEST_ABI void CGContextRetain(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextRelease(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void* CGBitmapContextGetData(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetWidth(
	darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetHeight(
	darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGBitmapContextGetBytesPerRow(
	darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef
CGBitmapContextCreateImage(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGImageRetain(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI void CGImageRelease(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithData(const void* data, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithURL(darling_windows_CFURLRef url, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateIncremental(const void* options);
extern "C" DARLING_CG_GUEST_ABI void CGImageSourceUpdateData(
	darling_windows_CGImageSourceRef source, darling_windows_CFDataRef data, bool is_final);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageSourceGetCount(
	darling_windows_CGImageSourceRef source);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGImageSourceGetTypeID();
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceStatus
CGImageSourceGetStatus(darling_windows_CGImageSourceRef source);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceStatus
CGImageSourceGetStatusAtIndex(darling_windows_CGImageSourceRef source, std::size_t index);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFStringRef CGImageSourceGetType(
	darling_windows_CGImageSourceRef source);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFArrayRef
CGImageSourceCopyTypeIdentifiers();
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef CGImageSourceCreateImageAtIndex(
	darling_windows_CGImageSourceRef source, std::size_t index, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageRef
CGImageSourceCreateThumbnailAtIndex(darling_windows_CGImageSourceRef source,
	std::size_t index, const void* options);
extern "C" const void* kCGImageSourceThumbnailMaxPixelSize;
extern "C" const void* kCGImageSourceCreateThumbnailFromImageAlways;
extern "C" const void* kCGImageSourceCreateThumbnailFromImageIfAbsent;
extern "C" const void* kCGImageSourceCreateThumbnailWithTransform;
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDictionaryRef
CGImageSourceCopyPropertiesAtIndex(darling_windows_CGImageSourceRef source,
	std::size_t index, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDictionaryRef
CGImageSourceCopyProperties(darling_windows_CGImageSourceRef source, const void* options);
extern "C" const void* kCGImagePropertyImageCount;
extern "C" const void* kCGImagePropertyPixelWidth;
extern "C" const void* kCGImagePropertyPixelHeight;
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef CGDataConsumerCreate(
	void* info, const darling_windows_CGDataConsumerCallbacks* callbacks);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerCreateWithCFData(darling_windows_CFMutableDataRef data);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerCreateWithURL(darling_windows_CFURLRef url);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGDataConsumerRetain(darling_windows_CFTypeRef consumer);
extern "C" DARLING_CG_GUEST_ABI void CGDataConsumerRelease(
	darling_windows_CFTypeRef consumer);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGDataConsumerGetTypeID();
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithData(darling_windows_CFMutableDataRef data,
	darling_windows_CFStringRef type, std::size_t count, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithDataConsumer(darling_windows_CFTypeRef consumer,
	darling_windows_CFStringRef type, std::size_t count, const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeRef
CGImageDestinationCreateWithURL(darling_windows_CFURLRef url,
	darling_windows_CFStringRef type, std::size_t count, const void* options);
extern "C" DARLING_CG_GUEST_ABI void CGImageDestinationAddImage(
	darling_windows_CFTypeRef destination, darling_windows_CGImageRef image,
	const void* properties);
extern "C" DARLING_CG_GUEST_ABI void CGImageDestinationAddImageFromSource(
	darling_windows_CFTypeRef destination, darling_windows_CGImageSourceRef source,
	std::size_t index, const void* properties);
extern "C" DARLING_CG_GUEST_ABI bool CGImageDestinationFinalize(
	darling_windows_CFTypeRef destination);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFArrayRef
CGImageDestinationCopyTypeIdentifiers();
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGImageDestinationGetTypeID();
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetWidth(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetHeight(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBytesPerRow(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBitsPerComponent(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGImageGetBitsPerPixel(darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithCFData(darling_windows_CFDataRef data);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithData(void* info, const void* data, std::size_t size,
	darling_windows_CGDataProviderReleaseDataCallback release_data);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateSequential(void* info,
	const darling_windows_CGDataProviderSequentialCallbacks* callbacks);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateDirect(void* info, std::int64_t size,
	const darling_windows_CGDataProviderDirectCallbacks* callbacks);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithURL(darling_windows_CFURLRef url);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderCreateWithFilename(const char* path);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGDataProviderRef
CGDataProviderRetain(darling_windows_CGDataProviderRef provider);
extern "C" DARLING_CG_GUEST_ABI void CGDataProviderRelease(
	darling_windows_CGDataProviderRef provider);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFTypeID CGDataProviderGetTypeID();
extern "C" DARLING_CG_GUEST_ABI std::size_t CGDataProviderGetSize(
	darling_windows_CGDataProviderRef provider);
extern "C" DARLING_CG_GUEST_ABI const void* CGDataProviderGetBytePointer(
	darling_windows_CGDataProviderRef provider);
extern "C" DARLING_CG_GUEST_ABI void CGDataProviderReleaseBytePointer(
	darling_windows_CGDataProviderRef provider, const void* pointer);
extern "C" DARLING_CG_GUEST_ABI std::size_t CGDataProviderGetBytesAtPosition(
	darling_windows_CGDataProviderRef provider, void* buffer, std::int64_t position,
	std::size_t count);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CFDataRef CGDataProviderCopyData(
	darling_windows_CGDataProviderRef provider);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGImageSourceRef
CGImageSourceCreateWithDataProvider(darling_windows_CGDataProviderRef provider,
	const void* options);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGColorSpaceRef CGImageGetColorSpace(
	darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGBitmapInfo CGImageGetBitmapInfo(
	darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetRGBFillColor(
	darling_windows_CGContextRef context, double red, double green, double blue, double alpha);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetRGBStrokeColor(
	darling_windows_CGContextRef context, double red, double green, double blue, double alpha);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineWidth(
	darling_windows_CGContextRef context, double width);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineCap(
	darling_windows_CGContextRef context, darling_windows_CGLineCap cap);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineJoin(
	darling_windows_CGContextRef context, darling_windows_CGLineJoin join);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetMiterLimit(
	darling_windows_CGContextRef context, double limit);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetFlatness(
	darling_windows_CGContextRef context, double flatness);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetLineDash(
	darling_windows_CGContextRef context, double phase, const double* lengths,
	std::size_t count);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetAlpha(
	darling_windows_CGContextRef context, double alpha);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetBlendMode(
	darling_windows_CGContextRef context, darling_windows_CGBlendMode mode);
extern "C" DARLING_CG_GUEST_ABI void CGContextSaveGState(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextRestoreGState(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextClip(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextEOClip(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextClipToRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextClipToMask(
	darling_windows_CGContextRef context, darling_windows_CGRect rect,
	darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI void CGContextResetClip(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI darling_windows_CGAffineTransform CGContextGetCTM(
	darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetCTM(
	darling_windows_CGContextRef context, darling_windows_CGAffineTransform transform);
extern "C" DARLING_CG_GUEST_ABI void CGContextConcatCTM(
	darling_windows_CGContextRef context, darling_windows_CGAffineTransform transform);
extern "C" DARLING_CG_GUEST_ABI void CGContextTranslateCTM(
	darling_windows_CGContextRef context, double tx, double ty);
extern "C" DARLING_CG_GUEST_ABI void CGContextScaleCTM(
	darling_windows_CGContextRef context, double sx, double sy);
extern "C" DARLING_CG_GUEST_ABI void CGContextRotateCTM(
	darling_windows_CGContextRef context, double radians);
extern "C" DARLING_CG_GUEST_ABI void CGContextSetFillColorWithColor(
	darling_windows_CGContextRef context, darling_windows_CGColorRef color);
extern "C" DARLING_CG_GUEST_ABI void CGContextFillRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextClearRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextDrawImage(
	darling_windows_CGContextRef context, darling_windows_CGRect rect,
	darling_windows_CGImageRef image);
extern "C" DARLING_CG_GUEST_ABI void CGContextStrokeRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextBeginPath(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextMoveToPoint(
	darling_windows_CGContextRef context, double x, double y);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddLineToPoint(
	darling_windows_CGContextRef context, double x, double y);
extern "C" DARLING_CG_GUEST_ABI void CGContextClosePath(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextFillEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextStrokeEllipseInRect(
	darling_windows_CGContextRef context, darling_windows_CGRect rect);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddCurveToPoint(
	darling_windows_CGContextRef context, double control1_x, double control1_y,
	double control2_x, double control2_y, double x, double y);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddQuadCurveToPoint(
	darling_windows_CGContextRef context, double control_x, double control_y,
	double x, double y);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddArc(
	darling_windows_CGContextRef context, double center_x, double center_y,
	double radius, double start_angle, double end_angle, int clockwise);
extern "C" DARLING_CG_GUEST_ABI void CGContextAddArcToPoint(
	darling_windows_CGContextRef context, double x1, double y1,
	double x2, double y2, double radius);
extern "C" DARLING_CG_GUEST_ABI void CGContextFillPath(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextEOFillPath(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextStrokePath(darling_windows_CGContextRef context);
extern "C" DARLING_CG_GUEST_ABI void CGContextFlush(darling_windows_CGContextRef context);
