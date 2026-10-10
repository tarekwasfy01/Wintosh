/* CoreGraphics geometry smoke; GPL-3.0-only. */
#include "darling_windows_coregraphics.h"

#include <cmath>
#include <iostream>

int main()
{
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
