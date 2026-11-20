/* CoreGraphics geometry smoke; GPL-3.0-only. */
#include "darling_windows_coregraphics.h"
#include "darling_windows_corefoundation.h"
#include "darling_windows_stdio.h"

#include <cmath>
#include <cstring>
#include <iostream>

int main()
{
	// A tiny valid PNG exercises the CFData-backed ImageIO source and WIC decode.
	const std::uint8_t png[] = {
		0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
		0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
		0x08,0x04,0x00,0x00,0x00,0xb5,0x1c,0x0c,0x02,0x00,0x00,0x00,
		0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x60,0x00,0x00,
		0x00,0x03,0x00,0x01,0x68,0x26,0x59,0x0d,0x00,0x00,0x00,0x00,
		0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
	};
	auto* encoded = darling_windows_CFDataCreate(png, sizeof(png));
	auto source = CGImageSourceCreateWithData(encoded, nullptr);
	auto* decoded = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	if (encoded == nullptr || source == nullptr || CGImageSourceGetCount(source) != 1 ||
		decoded == nullptr || CGImageGetWidth(decoded) != 1 || CGImageGetHeight(decoded) != 1 ||
		darling_windows_host_symbol("_CGImageSourceCreateWithData") !=
			reinterpret_cast<std::uintptr_t>(&CGImageSourceCreateWithData)) return 2;
	CGImageRelease(decoded);
	darling_windows_CFRelease(source);
	darling_windows_CFRelease(encoded);

	auto* rgb_space = CGColorSpaceCreateDeviceRGB();
	if (rgb_space == nullptr || CGColorSpaceGetNumberOfComponents(rgb_space) != 3 ||
		CGColorSpaceGetNumberOfComponents(nullptr) != 0) return 2;
	const double rgba[] = {1.0, 0.25, 0.0, 1.0};
	auto* cg_color = CGColorCreate(rgb_space, rgba);
	std::uint8_t bitmap[16]{};
	auto* bitmap_context = CGBitmapContextCreate(bitmap, 2, 2, 8, 8, rgb_space,
		darling_windows_kCGImageAlphaPremultipliedLast);
	if (cg_color == nullptr || bitmap_context == nullptr ||
		CGBitmapContextGetData(bitmap_context) != bitmap ||
		CGBitmapContextGetWidth(bitmap_context) != 2 ||
		CGBitmapContextGetHeight(bitmap_context) != 2 ||
		CGBitmapContextGetBytesPerRow(bitmap_context) != 8 ||
		darling_windows_host_symbol("_CGBitmapContextCreate") !=
			reinterpret_cast<std::uintptr_t>(&CGBitmapContextCreate) ||
		darling_windows_host_symbol("_CGContextFillRect") !=
			reinterpret_cast<std::uintptr_t>(&CGContextFillRect)) return 2;
	CGContextSetFillColorWithColor(bitmap_context, cg_color);
	CGContextFillRect(bitmap_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (bitmap[12] != 0 || bitmap[13] != 0 || bitmap[14] != 0 || bitmap[15] != 0 ||
		bitmap[8] != 255 || bitmap[9] != 64 || bitmap[10] != 0 || bitmap[11] != 255) return 2;
	CGContextClearRect(bitmap_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (bitmap[8] != 0 || bitmap[9] != 0 || bitmap[10] != 0 || bitmap[11] != 0) return 2;
	CGContextSetRGBStrokeColor(bitmap_context, 0, 0, 1, 1);
	CGContextSetLineWidth(bitmap_context, 1);
	CGContextStrokeRect(bitmap_context, darling_windows_CGRectMake(0, 0, 2, 2));
	for (std::size_t index = 0; index < sizeof(bitmap); index += 4) {
		if (bitmap[index] != 0 || bitmap[index + 1] != 0 ||
			bitmap[index + 2] != 255 || bitmap[index + 3] != 255) return 2;
	}
	if (darling_windows_host_symbol("_CGContextStrokeRect") !=
		reinterpret_cast<std::uintptr_t>(&CGContextStrokeRect)) return 2;
	std::memset(bitmap, 0, sizeof(bitmap));
	CGContextBeginPath(bitmap_context);
	CGContextMoveToPoint(bitmap_context, 0, 0);
	CGContextAddLineToPoint(bitmap_context, 2, 0);
	CGContextAddLineToPoint(bitmap_context, 0, 2);
	CGContextClosePath(bitmap_context);
	CGContextSetRGBFillColor(bitmap_context, 1, 0, 0, 1);
	CGContextFillPath(bitmap_context);
	if (bitmap[8] != 255 || bitmap[9] != 0 || bitmap[10] != 0 || bitmap[11] != 255 ||
		bitmap[12] != 0 || bitmap[13] != 0 || bitmap[14] != 0 || bitmap[15] != 0 ||
		darling_windows_host_symbol("_CGContextFillPath") !=
			reinterpret_cast<std::uintptr_t>(&CGContextFillPath)) return 2;
	auto* snapshot = CGBitmapContextCreateImage(bitmap_context);
	std::uint8_t copied_bitmap[16]{};
	auto* copied_context = CGBitmapContextCreate(copied_bitmap, 2, 2, 8, 8, rgb_space,
		darling_windows_kCGImageAlphaPremultipliedLast);
	if (snapshot == nullptr || copied_context == nullptr ||
		CGImageGetWidth(snapshot) != 2 || CGImageGetHeight(snapshot) != 2 ||
		CGImageGetBytesPerRow(snapshot) != 8 ||
		CGImageGetBitsPerComponent(snapshot) != 8 || CGImageGetBitsPerPixel(snapshot) != 32 ||
		CGImageGetColorSpace(snapshot) != rgb_space ||
		CGImageGetBitmapInfo(snapshot) != darling_windows_kCGImageAlphaPremultipliedLast ||
		darling_windows_host_symbol("_CGBitmapContextCreateImage") !=
			reinterpret_cast<std::uintptr_t>(&CGBitmapContextCreateImage) ||
		darling_windows_host_symbol("_CGContextDrawImage") !=
			reinterpret_cast<std::uintptr_t>(&CGContextDrawImage)) return 2;
	CGContextDrawImage(copied_context, darling_windows_CGRectMake(0, 0, 2, 2), snapshot);
	if (std::memcmp(bitmap, copied_bitmap, sizeof(bitmap)) != 0) return 2;
	CGImageRelease(snapshot);
	CGContextRelease(copied_context);
	std::uint8_t transformed_bitmap[64]{};
	auto* transformed_context = CGBitmapContextCreate(transformed_bitmap, 4, 4, 8, 16,
		rgb_space, darling_windows_kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(transformed_context, 0, 0, 1, 1);
	CGContextSaveGState(transformed_context);
	CGContextTranslateCTM(transformed_context, 3, 0);
	CGContextSetRGBFillColor(transformed_context, 1, 0, 0, 1);
	CGContextRestoreGState(transformed_context);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (transformed_bitmap[(3 * 4) * 4] != 0 ||
		transformed_bitmap[(3 * 4) * 4 + 1] != 0 ||
		transformed_bitmap[(3 * 4) * 4 + 2] != 255 ||
		transformed_bitmap[(3 * 4) * 4 + 3] != 255 ||
		!darling_windows_CGAffineTransformIsIdentity(CGContextGetCTM(transformed_context)) ||
		darling_windows_host_symbol("_CGContextSaveGState") !=
			reinterpret_cast<std::uintptr_t>(&CGContextSaveGState) ||
		darling_windows_host_symbol("_CGContextRestoreGState") !=
			reinterpret_cast<std::uintptr_t>(&CGContextRestoreGState)) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSetRGBFillColor(transformed_context, 1, 0, 0, 1);
	CGContextTranslateCTM(transformed_context, 1, 1);
	CGContextScaleCTM(transformed_context, 2, 2);
	const auto ctm = CGContextGetCTM(transformed_context);
	CGContextSetRGBFillColor(transformed_context, 1, 0, 0, 1);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 1, 1));
	const auto red_pixel = [](const std::uint8_t* pixel) {
		return pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
	};
	if (ctm.a != 2 || ctm.d != 2 || ctm.tx != 2 || ctm.ty != 2 ||
		!red_pixel(transformed_bitmap + (1 * 4 + 2) * 4) ||
		!red_pixel(transformed_bitmap + (1 * 4 + 3) * 4) ||
		!red_pixel(transformed_bitmap + (0 * 4 + 2) * 4) ||
		!red_pixel(transformed_bitmap + (0 * 4 + 3) * 4) ||
		darling_windows_host_symbol("_CGContextGetCTM") !=
			reinterpret_cast<std::uintptr_t>(&CGContextGetCTM) ||
		darling_windows_host_symbol("_CGContextConcatCTM") !=
			reinterpret_cast<std::uintptr_t>(&CGContextConcatCTM)) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSetCTM(transformed_context, darling_windows_CGAffineTransformIdentity());
	CGContextRotateCTM(transformed_context, 0.5 * 3.14159265358979323846);
	CGContextTranslateCTM(transformed_context, 3, 0);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 1, 2));
	if (!red_pixel(transformed_bitmap + (3 * 4 + 1) * 4) ||
		!red_pixel(transformed_bitmap + (3 * 4 + 2) * 4)) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSetCTM(transformed_context, darling_windows_CGAffineTransformIdentity());
	CGContextTranslateCTM(transformed_context, 1, 1);
	CGContextBeginPath(transformed_context);
	CGContextAddRect(transformed_context, darling_windows_CGRectMake(0, 0, 1, 1));
	CGContextFillPath(transformed_context);
	if (!red_pixel(transformed_bitmap + (2 * 4 + 1) * 4)) return 2;
	CGContextSetCTM(transformed_context, darling_windows_CGAffineTransformIdentity());
	if (!darling_windows_CGAffineTransformIsIdentity(CGContextGetCTM(transformed_context))) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSaveGState(transformed_context);
	CGContextClipToRect(transformed_context, darling_windows_CGRectMake(1, 1, 2, 2));
	CGContextSetRGBFillColor(transformed_context, 1, 0, 0, 1);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 4, 4));
	for (std::size_t row = 0; row < 4; ++row) {
		for (std::size_t x = 0; x < 4; ++x) {
			const auto y = 3 - row;
			const bool expected_inside = x >= 1 && x < 3 && y >= 1 && y < 3;
			const auto* pixel = transformed_bitmap + (row * 4 + x) * 4;
			if ((pixel[3] == 255) != expected_inside) return 2;
		}
	}
	CGContextRestoreGState(transformed_context);
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSetRGBFillColor(transformed_context, 0, 1, 0, 1);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 4, 4));
	for (std::size_t index = 0; index < sizeof(transformed_bitmap); index += 4)
		if (transformed_bitmap[index + 3] != 255) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextBeginPath(transformed_context);
	CGContextAddRect(transformed_context, darling_windows_CGRectMake(1, 1, 1, 1));
	CGContextClip(transformed_context);
	CGContextSetRGBFillColor(transformed_context, 0, 0, 1, 1);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 4, 4));
	const auto blue_pixel = [](const std::uint8_t* pixel) {
		return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255;
	};
	if (!blue_pixel(transformed_bitmap + (2 * 4 + 1) * 4) ||
		transformed_bitmap[(3 * 4 + 0) * 4 + 3] != 0 ||
		darling_windows_host_symbol("_CGContextClipToRect") !=
			reinterpret_cast<std::uintptr_t>(&CGContextClipToRect) ||
		darling_windows_host_symbol("_CGContextClip") !=
			reinterpret_cast<std::uintptr_t>(&CGContextClip)) return 2;
	CGContextResetClip(transformed_context);
	std::uint8_t translucent_mask_pixel[] = {0, 0, 0, 128};
	auto* mask_context = CGBitmapContextCreate(translucent_mask_pixel, 1, 1, 8, 4,
		rgb_space, darling_windows_kCGImageAlphaPremultipliedLast);
	auto* translucent_mask = CGBitmapContextCreateImage(mask_context);
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextClipToMask(transformed_context,
		darling_windows_CGRectMake(0, 0, 1, 1), translucent_mask);
	CGContextSetRGBFillColor(transformed_context, 0, 0, 1, 1);
	CGContextFillRect(transformed_context, darling_windows_CGRectMake(0, 0, 4, 4));
	const auto* half_covered = transformed_bitmap + (3 * 4) * 4;
	if (half_covered[0] != 0 || half_covered[1] != 0 ||
		half_covered[2] != 128 || half_covered[3] != 128 ||
		transformed_bitmap[(3 * 4 + 1) * 4 + 3] != 0 ||
		darling_windows_host_symbol("_CGContextClipToMask") !=
			reinterpret_cast<std::uintptr_t>(&CGContextClipToMask)) return 2;
	CGImageRelease(translucent_mask);
	CGContextRelease(mask_context);
	CGContextResetClip(transformed_context);
	std::uint8_t alpha_bitmap[4]{};
	auto* alpha_context = CGBitmapContextCreate(alpha_bitmap, 1, 1, 8, 4, rgb_space,
		darling_windows_kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(alpha_context, 1, 0, 0, 1);
	CGContextSetAlpha(alpha_context, 0.5);
	CGContextSaveGState(alpha_context);
	CGContextSetAlpha(alpha_context, 1);
	CGContextRestoreGState(alpha_context);
	CGContextFillRect(alpha_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (alpha_bitmap[0] != 128 || alpha_bitmap[1] != 0 || alpha_bitmap[2] != 0 ||
		alpha_bitmap[3] != 128 || darling_windows_host_symbol("_CGContextSetAlpha") !=
		reinterpret_cast<std::uintptr_t>(&CGContextSetAlpha)) return 2;
	CGContextRelease(alpha_context);
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextSetRGBFillColor(transformed_context, 1, 0, 0, 1);
	CGContextFillEllipseInRect(transformed_context, darling_windows_CGRectMake(0, 0, 4, 4));
	if (transformed_bitmap[(2 * 4 + 1) * 4 + 3] != 255 ||
		transformed_bitmap[(3 * 4 + 0) * 4 + 3] != 0 ||
		darling_windows_host_symbol("_CGContextFillEllipseInRect") !=
			reinterpret_cast<std::uintptr_t>(&CGContextFillEllipseInRect)) return 2;
	std::memset(transformed_bitmap, 0, sizeof(transformed_bitmap));
	CGContextBeginPath(transformed_context);
	CGContextMoveToPoint(transformed_context, 0, 0);
	CGContextAddCurveToPoint(transformed_context, 0, 2, 2, 2, 2, 0);
	CGContextAddLineToPoint(transformed_context, 0, 0);
	CGContextClosePath(transformed_context);
	CGContextFillPath(transformed_context);
	if (transformed_bitmap[(3 * 4 + 1) * 4 + 3] != 255 ||
		darling_windows_host_symbol("_CGContextAddCurveToPoint") !=
			reinterpret_cast<std::uintptr_t>(&CGContextAddCurveToPoint)) return 2;
	CGContextRelease(transformed_context);
	std::uint8_t arc_bitmap[8 * 8 * 4]{};
	auto* arc_context = CGBitmapContextCreate(arc_bitmap, 8, 8, 8, 8 * 4, rgb_space,
		darling_windows_kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(arc_context, 1, 0, 0, 1);
	CGContextBeginPath(arc_context);
	CGContextMoveToPoint(arc_context, 4, 4);
	CGContextAddArc(arc_context, 4, 4, 2, 0, 0.5 * 3.14159265358979323846, 0);
	CGContextAddLineToPoint(arc_context, 4, 4);
	CGContextClosePath(arc_context);
	CGContextFillPath(arc_context);
	if (arc_bitmap[(3 * 8 + 4) * 4 + 3] != 255 ||
		arc_bitmap[(3 * 8 + 6) * 4 + 3] != 0 ||
		darling_windows_host_symbol("_CGContextAddArc") !=
			reinterpret_cast<std::uintptr_t>(&CGContextAddArc)) return 2;
	std::memset(arc_bitmap, 0, sizeof(arc_bitmap));
	CGContextBeginPath(arc_context);
	CGContextMoveToPoint(arc_context, 1, 4);
	CGContextAddArcToPoint(arc_context, 4, 4, 4, 7, 1);
	CGContextAddLineToPoint(arc_context, 1, 7);
	CGContextClosePath(arc_context);
	CGContextFillPath(arc_context);
	if (arc_bitmap[(3 * 8 + 2) * 4 + 3] != 255 ||
		darling_windows_host_symbol("_CGContextAddArcToPoint") !=
			reinterpret_cast<std::uintptr_t>(&CGContextAddArcToPoint)) return 2;
	CGContextRelease(arc_context);
	std::uint8_t blend_bitmap[4]{};
	auto* blend_context = CGBitmapContextCreate(blend_bitmap, 1, 1, 8, 4, rgb_space,
		darling_windows_kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(blend_context, 0, 0, 1, 1);
	CGContextFillRect(blend_context, darling_windows_CGRectMake(0, 0, 1, 1));
	CGContextSetBlendMode(blend_context, darling_windows_kCGBlendModeMultiply);
	CGContextSetRGBFillColor(blend_context, 1, 0, 0, 1);
	CGContextFillRect(blend_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (blend_bitmap[0] != 0 || blend_bitmap[1] != 0 || blend_bitmap[2] != 0 ||
		blend_bitmap[3] != 255) return 2;
	std::memset(blend_bitmap, 0, sizeof(blend_bitmap));
	CGContextSetBlendMode(blend_context, darling_windows_kCGBlendModeNormal);
	CGContextSetRGBFillColor(blend_context, 0, 0, 1, 1);
	CGContextFillRect(blend_context, darling_windows_CGRectMake(0, 0, 1, 1));
	CGContextSetBlendMode(blend_context, darling_windows_kCGBlendModeSourceIn);
	CGContextSetAlpha(blend_context, 0.5);
	CGContextSetRGBFillColor(blend_context, 1, 0, 0, 1);
	CGContextFillRect(blend_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (blend_bitmap[0] != 128 || blend_bitmap[1] != 0 || blend_bitmap[2] != 0 ||
		blend_bitmap[3] != 128 || darling_windows_host_symbol("_CGContextSetBlendMode") !=
		reinterpret_cast<std::uintptr_t>(&CGContextSetBlendMode)) return 2;
	CGContextSetAlpha(blend_context, 1);
	CGContextSetBlendMode(blend_context, darling_windows_kCGBlendModeCopy);
	CGContextSetRGBFillColor(blend_context, 0, 0, 0, 0);
	CGContextFillRect(blend_context, darling_windows_CGRectMake(0, 0, 1, 1));
	if (blend_bitmap[0] != 0 || blend_bitmap[1] != 0 ||
		blend_bitmap[2] != 0 || blend_bitmap[3] != 0) return 2;
	CGContextRelease(blend_context);
	CGColorRelease(cg_color);
	CGContextRelease(bitmap_context);
	CGColorSpaceRelease(rgb_space);
	std::cout << "COREGRAPHICS_BITMAP_CONTEXT=PASS\n";
	const auto identity = darling_windows_CGAffineTransformIdentity();
	if (!darling_windows_CGAffineTransformIsIdentity(identity) ||
		darling_windows_CGAffineTransformIsIdentity(
			darling_windows_CGAffineTransformMakeTranslation(1, 0))) return 1;
	const auto translation = darling_windows_CGAffineTransformMakeTranslation(4, -3);
	const auto scaling = darling_windows_CGAffineTransformMakeScale(2, 3);
	const auto rotation = darling_windows_CGAffineTransformMakeRotation(0.5 * 3.14159265358979323846);
	const auto translated_point = darling_windows_CGPointApplyAffineTransform({1, 2}, translation);
	const auto scaled_point = darling_windows_CGPointApplyAffineTransform({2, 3}, scaling);
	const auto rotated_point = darling_windows_CGPointApplyAffineTransform({1, 0}, rotation);
	if (translated_point.x != 5 || translated_point.y != -1 ||
		scaled_point.x != 4 || scaled_point.y != 9 ||
		std::abs(rotated_point.x) > 1e-12 || std::abs(rotated_point.y - 1) > 1e-12) {
		std::cerr << "COREGRAPHICS_AFFINE_CONVENIENCE=FAIL\n";
		return 1;
	}
	std::cout << "COREGRAPHICS_AFFINE_CONVENIENCE=PASS\n";
	const auto outer = darling_windows_CGRectMake(0, 0, 10, 10);
	const auto inner = darling_windows_CGRectMake(2, 3, 4, 5);
	const auto reversed = darling_windows_CGRectMake(10, 8, -6, -4);
	const auto bounding = darling_windows_CGRectGetBoundingBox(reversed);
	if (darling_windows_CGRectIsStandardized(reversed) ||
		!darling_windows_CGRectIsStandardized(bounding)) return 1;
	if (bounding.origin.x != 4 || bounding.origin.y != 4 ||
		bounding.size.width != 6 || bounding.size.height != 4) return 1;
	const auto null_rect = darling_windows_CGRectNull();
	if (!darling_windows_CGRectContainsRect(outer, inner) ||
		darling_windows_CGRectContainsRect(inner, outer) ||
		!darling_windows_CGRectIsNull(null_rect) ||
		darling_windows_CGRectIsInfinite(outer) ||
		!darling_windows_CGRectIsInfinite(
			darling_windows_CGRectMake(0, 0, INFINITY, 1))) {
		std::cerr << "COREGRAPHICS_RECT_PREDICATES=FAIL\n";
		return 1;
	}
	std::cout << "COREGRAPHICS_RECT_PREDICATES=PASS\n";
	const auto getter_color = darling_windows_CGColorMakeRGBA(0.1, 0.2, 0.3, 0.4);
	double red = 0, green = 0, blue = 0;
	if (!darling_windows_CGColorGetRed(getter_color, &red) ||
		!darling_windows_CGColorGetGreen(getter_color, &green) ||
		!darling_windows_CGColorGetBlue(getter_color, &blue) ||
		red != 0.1 || green != 0.2 || blue != 0.3 ||
		darling_windows_CGColorGetRed(getter_color, nullptr)) {
		std::cerr << "COREGRAPHICS_COLOR_GETTERS=FAIL\n";
		return 1;
	}
	std::cout << "COREGRAPHICS_COLOR_GETTERS=PASS\n";
	const auto getter_gray = darling_windows_CGColorMakeGray(0.6, 0.7);
	double white_value = 0;
	if (!darling_windows_CGColorGetWhite(getter_gray, &white_value) || white_value != 0.6 ||
		darling_windows_CGColorGetNumberOfComponents(getter_gray) != 2 ||
		darling_windows_CGColorGetWhite(getter_color, &white_value) ||
		darling_windows_CGColorGetNumberOfComponents(getter_color) != 4) {
		std::cerr << "COREGRAPHICS_COLOR_MODEL=FAIL\n";
		return 1;
	}
	std::cout << "COREGRAPHICS_COLOR_MODEL=PASS\n";
	double gray_components[2]{};
	if (!darling_windows_CGColorGetComponents(getter_gray, gray_components, 2) ||
		gray_components[0] != 0.6 || gray_components[1] != 0.7 ||
		darling_windows_CGColorGetComponents(getter_gray, gray_components, 1)) {
		std::cerr << "COREGRAPHICS_COLOR_COMPONENT_MODEL=FAIL\n";
		return 1;
	}
	std::cout << "COREGRAPHICS_COLOR_COMPONENT_MODEL=PASS\n";
	const auto translated = darling_windows_CGAffineTransformTranslate(identity, 3, 4);
	const auto scaled = darling_windows_CGAffineTransformScale(translated, 2, 3);
	const auto rotated = darling_windows_CGAffineTransformRotate(identity, 1.5707963267948966);
	const auto combined = darling_windows_CGAffineTransformConcat(scaled, identity);
	const darling_windows_CGRect rect{{0, 0}, {10, 20}};
	const auto transformed_rect = darling_windows_CGRectApplyAffineTransform(rect, translated);
	bool invertible = false;
	const auto inverse = darling_windows_CGAffineTransformInvert(scaled, &invertible);
	const auto point = darling_windows_CGPointApplyAffineTransform({1, 1}, scaled);
	const auto restored = darling_windows_CGPointApplyAffineTransform(point, inverse);
	const auto intersection = darling_windows_CGRectIntersection(rect, {{5, 5}, {10, 10}});
	const auto united = darling_windows_CGRectUnion(rect, {{20, 20}, {5, 5}});
	const auto inset = darling_windows_CGRectInset(rect, 1, 2);
	const auto offset = darling_windows_CGRectOffset(rect, 2, 3);
	darling_windows_CGRect slice{}, remainder{};
	darling_windows_CGRectDivide(rect, 3, darling_windows_CGMinXEdge, &slice, &remainder);
	const auto color = darling_windows_CGColorMakeRGBA(0.1, 0.2, 0.3, 0.75);
	const auto same_color = darling_windows_CGColorMakeRGBA(0.1, 0.2, 0.3, 0.75);
	const auto negative_rect = darling_windows_CGRectStandardize(
		darling_windows_CGRectMake(10, 20, -5, -6));
	const auto integral_rect = darling_windows_CGRectIntegral({{1.2, 2.8}, {3.1, 4.1}});
	const auto gray = darling_windows_CGColorMakeGray(0.5, 0.25);
	const auto white = darling_windows_CGColorMakeWhite(1);
	const auto clear = darling_windows_CGColorMakeClear();
	const auto vector = darling_windows_CGVectorMake(3, 4);
	bool vector_valid = false;
	const auto normalized = darling_windows_CGVectorNormalize(vector, &vector_valid);
	bool infinite_vector_valid = true;
	const auto infinite_normalized = darling_windows_CGVectorNormalize(
		{INFINITY, 1}, &infinite_vector_valid);
	bool nonfinite_invertible = true;
	(void)darling_windows_CGAffineTransformInvert(
		{NAN, 0, 0, 1, 0, 0}, &nonfinite_invertible);
	const auto point_sum = darling_windows_CGPointAdd({1, 2}, vector);
	const auto point_delta = darling_windows_CGPointSubtract(point_sum, {1, 2});
	double components[4]{};
	const bool ok = translated.tx == 3 && translated.ty == 4 &&
		scaled.a == 2 && scaled.d == 3 &&
		std::abs(rotated.a) < 1e-12 && std::abs(rotated.b - 1) < 1e-12 &&
		combined.tx == scaled.tx && combined.ty == scaled.ty &&
		darling_windows_CGAffineTransformEqualToTransform(identity, identity) &&
		transformed_rect.origin.x == 3 && transformed_rect.origin.y == 4 &&
		invertible && std::abs(restored.x - 1) < 1e-12 && std::abs(restored.y - 1) < 1e-12 &&
		!darling_windows_CGRectIsEmpty(intersection) && intersection.size.width == 5 &&
		darling_windows_CGRectContainsPoint(rect, {5, 5}) && united.size.width == 25 &&
		darling_windows_CGColorGetAlpha(color) == 0.75 &&
		darling_windows_CGColorEqual(color, same_color) && negative_rect.origin.x == 5 &&
		negative_rect.origin.y == 14 && negative_rect.size.width == 5 &&
		negative_rect.size.height == 6 && gray.red == gray.green &&
		integral_rect.origin.x == 1 && integral_rect.origin.y == 2 &&
		integral_rect.size.width == 4 && integral_rect.size.height == 5 &&
		white.red == 1 && white.alpha == 1 && clear.alpha == 0 &&
		darling_windows_CGColorGetComponents(color, components, 4) &&
		components[0] == 0.1 && components[3] == 0.75 && inset.size.width == 8 &&
		inset.size.height == 16 && offset.origin.x == 2 && offset.origin.y == 3 &&
		darling_windows_CGRectGetMinX(rect) == 0 && darling_windows_CGRectGetMidX(rect) == 5 &&
		darling_windows_CGRectGetMaxX(rect) == 10 && darling_windows_CGRectGetMinY(rect) == 0 &&
		 darling_windows_CGRectGetMidY(rect) == 10 && darling_windows_CGRectGetMaxY(rect) == 20;
	const bool geometry_access_ok = darling_windows_CGRectGetWidth(rect) == 10 &&
		darling_windows_CGRectGetHeight(rect) == 20 &&
		darling_windows_CGPointEqualToPoint({1, 2}, {1, 2}) &&
		darling_windows_CGSizeEqualToSize({10, 20}, {10, 20}) &&
		 darling_windows_CGRectEqualToRect(rect, {{0, 0}, {10, 20}});
	const bool divide_ok = slice.size.width == 3 && remainder.origin.x == 3 &&
		remainder.size.width == 7;
	darling_windows_CGRect oversized_slice{}, oversized_remainder{};
	darling_windows_CGRectDivide(rect, 50, darling_windows_CGMaxXEdge,
		&oversized_slice, &oversized_remainder);
	const bool oversized_divide_ok = oversized_slice.origin.x == 0 &&
		oversized_slice.size.width == 10 && oversized_remainder.size.width == 0;
	const bool vector_ok = vector_valid && darling_windows_CGVectorLength(vector) == 5 &&
		darling_windows_CGVectorDot(vector, vector) == 25 &&
		std::abs(normalized.dx - 0.6) < 1e-12 && point_delta.dx == 3 && point_delta.dy == 4 &&
		!infinite_vector_valid && infinite_normalized.dx == 0 && infinite_normalized.dy == 0 &&
		!nonfinite_invertible;
	const bool all_ok = ok && vector_ok && geometry_access_ok && divide_ok && oversized_divide_ok;
	std::cout << "DARWIN_COREGRAPHICS_GEOMETRY=" << (all_ok ? "PASS" : "FAIL") << "\n";
	return all_ok ? 0 : 1;
}
