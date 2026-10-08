/* Minimal CoreGraphics geometry primitives; GPL-3.0-only. */
#pragma once

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

extern "C" darling_windows_CGPoint darling_windows_CGPointMake(double x, double y);
extern "C" darling_windows_CGSize darling_windows_CGSizeMake(double width, double height);
extern "C" darling_windows_CGRect darling_windows_CGRectMake(double x, double y,
	double width, double height);
extern "C" darling_windows_CGRect darling_windows_CGRectStandardize(
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
