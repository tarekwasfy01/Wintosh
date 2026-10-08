/* Minimal CoreGraphics geometry primitives; GPL-3.0-only. */
#include "darling_windows_coregraphics.h"

#include <cmath>
#include <algorithm>
#include <limits>

extern "C" darling_windows_CGPoint darling_windows_CGPointMake(double x, double y)
{
	return {x, y};
}

extern "C" darling_windows_CGSize darling_windows_CGSizeMake(double width, double height)
{
	return {width, height};
}

extern "C" darling_windows_CGRect darling_windows_CGRectMake(double x, double y,
	double width, double height)
{
	return {{x, y}, {width, height}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectStandardize(
	darling_windows_CGRect rect)
{
	if (rect.size.width < 0) { rect.origin.x += rect.size.width; rect.size.width = -rect.size.width; }
	if (rect.size.height < 0) { rect.origin.y += rect.size.height; rect.size.height = -rect.size.height; }
	return rect;
}

extern "C" darling_windows_CGVector darling_windows_CGVectorMake(double dx, double dy)
{
	return {dx, dy};
}

extern "C" darling_windows_CGPoint darling_windows_CGPointAdd(
	darling_windows_CGPoint point, darling_windows_CGVector vector)
{
	return {point.x + vector.dx, point.y + vector.dy};
}

extern "C" darling_windows_CGVector darling_windows_CGPointSubtract(
	darling_windows_CGPoint left, darling_windows_CGPoint right)
{
	return {left.x - right.x, left.y - right.y};
}

extern "C" double darling_windows_CGVectorDot(darling_windows_CGVector left,
	darling_windows_CGVector right)
{
	return left.dx * right.dx + left.dy * right.dy;
}

extern "C" double darling_windows_CGVectorLength(darling_windows_CGVector vector)
{
	return std::hypot(vector.dx, vector.dy);
}

extern "C" darling_windows_CGVector darling_windows_CGVectorNormalize(
	darling_windows_CGVector vector, bool* valid)
{
	const double length = darling_windows_CGVectorLength(vector);
	if (length == 0) {
		if (valid != nullptr) *valid = false;
		return {0, 0};
	}
	if (valid != nullptr) *valid = true;
	return {vector.dx / length, vector.dy / length};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMake(
	double a, double b, double c, double d, double tx, double ty)
{
	return {a, b, c, d, tx, ty};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformIdentity()
{
	return {1, 0, 0, 1, 0, 0};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeTranslation(
	double tx, double ty)
{
	return {1, 0, 0, 1, tx, ty};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeScale(
	double sx, double sy)
{
	return {sx, 0, 0, sy, 0, 0};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformMakeRotation(
	double radians)
{
	const double cosine = std::cos(radians);
	const double sine = std::sin(radians);
	return {cosine, sine, -sine, cosine, 0, 0};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformTranslate(
	darling_windows_CGAffineTransform transform, double tx, double ty)
{
	transform.tx += transform.a * tx + transform.c * ty;
	transform.ty += transform.b * tx + transform.d * ty;
	return transform;
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformScale(
	darling_windows_CGAffineTransform transform, double sx, double sy)
{
	transform.a *= sx;
	transform.b *= sx;
	transform.c *= sy;
	transform.d *= sy;
	return transform;
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformRotate(
	darling_windows_CGAffineTransform transform, double radians)
{
	const double cosine = std::cos(radians);
	const double sine = std::sin(radians);
	return darling_windows_CGAffineTransformConcat(transform,
		{cosine, sine, -sine, cosine, 0, 0});
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformConcat(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right)
{
	return {
		left.a * right.a + left.c * right.b,
		left.b * right.a + left.d * right.b,
		left.a * right.c + left.c * right.d,
		left.b * right.c + left.d * right.d,
		left.a * right.tx + left.c * right.ty + left.tx,
		left.b * right.tx + left.d * right.ty + left.ty};
}

extern "C" darling_windows_CGPoint darling_windows_CGPointApplyAffineTransform(
	darling_windows_CGPoint point, darling_windows_CGAffineTransform transform)
{
	return {transform.a * point.x + transform.c * point.y + transform.tx,
		transform.b * point.x + transform.d * point.y + transform.ty};
}

extern "C" darling_windows_CGRect darling_windows_CGRectApplyAffineTransform(
	darling_windows_CGRect rect, darling_windows_CGAffineTransform transform)
{
	const darling_windows_CGPoint corners[] = {
		rect.origin,
		{rect.origin.x + rect.size.width, rect.origin.y},
		{rect.origin.x, rect.origin.y + rect.size.height},
		{rect.origin.x + rect.size.width, rect.origin.y + rect.size.height}};
	const auto first = darling_windows_CGPointApplyAffineTransform(corners[0], transform);
	double min_x = first.x, max_x = first.x, min_y = first.y, max_y = first.y;
	for (int index = 1; index < 4; ++index) {
		const auto point = darling_windows_CGPointApplyAffineTransform(corners[index], transform);
		min_x = (std::min)(min_x, point.x); max_x = (std::max)(max_x, point.x);
		min_y = (std::min)(min_y, point.y); max_y = (std::max)(max_y, point.y);
	}
	return {{min_x, min_y}, {max_x - min_x, max_y - min_y}};
}

extern "C" darling_windows_CGAffineTransform darling_windows_CGAffineTransformInvert(
	darling_windows_CGAffineTransform transform, bool* invertible)
{
	const double determinant = transform.a * transform.d - transform.b * transform.c;
	if (std::abs(determinant) < 1e-15) {
		if (invertible != nullptr) *invertible = false;
		return darling_windows_CGAffineTransformIdentity();
	}
	if (invertible != nullptr) *invertible = true;
	const double inverse = 1.0 / determinant;
	return {transform.d * inverse, -transform.b * inverse, -transform.c * inverse,
		transform.a * inverse,
		(transform.c * transform.ty - transform.d * transform.tx) * inverse,
		(transform.b * transform.tx - transform.a * transform.ty) * inverse};
}

extern "C" bool darling_windows_CGAffineTransformEqualToTransform(
	darling_windows_CGAffineTransform left, darling_windows_CGAffineTransform right)
{
	return left.a == right.a && left.b == right.b && left.c == right.c &&
		left.d == right.d && left.tx == right.tx && left.ty == right.ty;
}

extern "C" bool darling_windows_CGRectIsEmpty(darling_windows_CGRect rect)
{
	return rect.size.width <= 0 || rect.size.height <= 0;
}

extern "C" darling_windows_CGRect darling_windows_CGRectNull()
{
	const double null_value = std::numeric_limits<double>::quiet_NaN();
	return {{null_value, null_value}, {null_value, null_value}};
}

extern "C" bool darling_windows_CGRectIsNull(darling_windows_CGRect rect)
{
	return std::isnan(rect.origin.x) && std::isnan(rect.origin.y) &&
		std::isnan(rect.size.width) && std::isnan(rect.size.height);
}

extern "C" bool darling_windows_CGRectIsInfinite(darling_windows_CGRect rect)
{
	return std::isinf(rect.origin.x) || std::isinf(rect.origin.y) ||
		std::isinf(rect.size.width) || std::isinf(rect.size.height);
}

extern "C" darling_windows_CGRect darling_windows_CGRectIntegral(
	darling_windows_CGRect rect)
{
	const double min_x = std::floor(rect.origin.x);
	const double min_y = std::floor(rect.origin.y);
	const double max_x = std::ceil(rect.origin.x + rect.size.width);
	const double max_y = std::ceil(rect.origin.y + rect.size.height);
	return {{min_x, min_y}, {max_x - min_x, max_y - min_y}};
}

extern "C" bool darling_windows_CGRectContainsPoint(darling_windows_CGRect rect,
	darling_windows_CGPoint point)
{
	return !darling_windows_CGRectIsEmpty(rect) && point.x >= rect.origin.x &&
	point.y >= rect.origin.y && point.x <= rect.origin.x + rect.size.width &&
	point.y <= rect.origin.y + rect.size.height;
}

extern "C" bool darling_windows_CGRectContainsRect(darling_windows_CGRect rect,
	darling_windows_CGRect candidate)
{
	return darling_windows_CGRectContainsPoint(rect, candidate.origin) &&
		darling_windows_CGRectContainsPoint(rect,
		{candidate.origin.x + candidate.size.width,
		candidate.origin.y + candidate.size.height});
}

extern "C" darling_windows_CGRect darling_windows_CGRectIntersection(
	darling_windows_CGRect left, darling_windows_CGRect right)
{
	const double x1 = (std::max)(left.origin.x, right.origin.x);
	const double y1 = (std::max)(left.origin.y, right.origin.y);
	const double x2 = (std::min)(left.origin.x + left.size.width,
		right.origin.x + right.size.width);
	const double y2 = (std::min)(left.origin.y + left.size.height,
		right.origin.y + right.size.height);
	if (x2 <= x1 || y2 <= y1) return {{0, 0}, {0, 0}};
	return {{x1, y1}, {x2 - x1, y2 - y1}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectUnion(
	darling_windows_CGRect left, darling_windows_CGRect right)
{
	if (darling_windows_CGRectIsEmpty(left)) return right;
	if (darling_windows_CGRectIsEmpty(right)) return left;
	const double x1 = (std::min)(left.origin.x, right.origin.x);
	const double y1 = (std::min)(left.origin.y, right.origin.y);
	const double x2 = (std::max)(left.origin.x + left.size.width,
		right.origin.x + right.size.width);
	const double y2 = (std::max)(left.origin.y + left.size.height,
		right.origin.y + right.size.height);
	return {{x1, y1}, {x2 - x1, y2 - y1}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectInset(
	darling_windows_CGRect rect, double dx, double dy)
{
	return {{rect.origin.x + dx, rect.origin.y + dy},
		{rect.size.width - 2 * dx, rect.size.height - 2 * dy}};
}

extern "C" darling_windows_CGRect darling_windows_CGRectOffset(
	darling_windows_CGRect rect, double dx, double dy)
{
	rect.origin.x += dx;
	rect.origin.y += dy;
	return rect;
}

extern "C" double darling_windows_CGRectGetMinX(darling_windows_CGRect rect) { return rect.origin.x; }
extern "C" double darling_windows_CGRectGetMidX(darling_windows_CGRect rect)
{ return rect.origin.x + rect.size.width / 2; }
extern "C" double darling_windows_CGRectGetMaxX(darling_windows_CGRect rect)
{ return rect.origin.x + rect.size.width; }
extern "C" double darling_windows_CGRectGetMinY(darling_windows_CGRect rect) { return rect.origin.y; }
extern "C" double darling_windows_CGRectGetMidY(darling_windows_CGRect rect)
{ return rect.origin.y + rect.size.height / 2; }
extern "C" double darling_windows_CGRectGetMaxY(darling_windows_CGRect rect)
{ return rect.origin.y + rect.size.height; }
extern "C" double darling_windows_CGRectGetWidth(darling_windows_CGRect rect)
{ return rect.size.width; }
extern "C" double darling_windows_CGRectGetHeight(darling_windows_CGRect rect)
{ return rect.size.height; }
extern "C" bool darling_windows_CGPointEqualToPoint(darling_windows_CGPoint left,
	darling_windows_CGPoint right)
{ return left.x == right.x && left.y == right.y; }
extern "C" bool darling_windows_CGSizeEqualToSize(darling_windows_CGSize left,
	darling_windows_CGSize right)
{ return left.width == right.width && left.height == right.height; }
extern "C" bool darling_windows_CGRectEqualToRect(darling_windows_CGRect left,
	darling_windows_CGRect right)
{ return darling_windows_CGPointEqualToPoint(left.origin, right.origin) &&
	darling_windows_CGSizeEqualToSize(left.size, right.size); }

extern "C" void darling_windows_CGRectDivide(darling_windows_CGRect rect,
	double amount, darling_windows_CGRectEdge edge, darling_windows_CGRect* slice,
	darling_windows_CGRect* remainder)
{
	if (slice == nullptr && remainder == nullptr) return;
	const double clamped = (std::max)(0.0, amount);
	if (slice != nullptr) *slice = {{0, 0}, {0, 0}};
	if (remainder != nullptr) *remainder = rect;
	switch (edge) {
	case darling_windows_CGMinXEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.width));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y},
			{used, rect.size.height}};
		if (remainder != nullptr) { remainder->origin.x += used; remainder->size.width -= used; }
		}
		break;
	case darling_windows_CGMinYEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.height));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y},
			{rect.size.width, used}};
		if (remainder != nullptr) { remainder->origin.y += used; remainder->size.height -= used; }
		}
		break;
	case darling_windows_CGMaxXEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.width));
		if (slice != nullptr) *slice = {{rect.origin.x + rect.size.width - clamped, rect.origin.y},
			{used, rect.size.height}};
		if (slice != nullptr) slice->origin.x = rect.origin.x + rect.size.width - used;
		if (remainder != nullptr) remainder->size.width -= used;
		}
		break;
	case darling_windows_CGMaxYEdge:
		{
		const double used = (std::min)(clamped, (std::max)(0.0, rect.size.height));
		if (slice != nullptr) *slice = {{rect.origin.x, rect.origin.y + rect.size.height - clamped},
			{rect.size.width, used}};
		if (slice != nullptr) slice->origin.y = rect.origin.y + rect.size.height - used;
		if (remainder != nullptr) remainder->size.height -= used;
		}
		break;
	}
	if (remainder != nullptr) {
		if (remainder->size.width < 0) remainder->size.width = 0;
		if (remainder->size.height < 0) remainder->size.height = 0;
	}
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeRGBA(
	double red, double green, double blue, double alpha)
{
	return {red, green, blue, alpha};
}

extern "C" double darling_windows_CGColorGetAlpha(darling_windows_CGColor color)
{
	return color.alpha;
}

extern "C" bool darling_windows_CGColorGetComponents(darling_windows_CGColor color,
	double* components, int capacity)
{
	if (components == nullptr || capacity < 4) return false;
	components[0] = color.red;
	components[1] = color.green;
	components[2] = color.blue;
	components[3] = color.alpha;
	return true;
}

extern "C" bool darling_windows_CGColorEqual(darling_windows_CGColor left,
	darling_windows_CGColor right)
{
	return left.red == right.red && left.green == right.green &&
		left.blue == right.blue && left.alpha == right.alpha;
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeGray(double white, double alpha)
{
	return {white, white, white, alpha};
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeWhite(double alpha)
{
	return darling_windows_CGColorMakeGray(1, alpha);
}

extern "C" darling_windows_CGColor darling_windows_CGColorMakeClear()
{
	return {0, 0, 0, 0};
}
