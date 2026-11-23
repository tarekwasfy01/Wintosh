/* Original Wintosh Objective-C fixture; GPL-3.0-only. */

extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id receiver, SEL selector, ...);

typedef struct {
	double x;
	double y;
} WintoshPair;

typedef struct {
	double a;
	double b;
	double c;
	double d;
} WintoshQuad;

typedef struct {
	unsigned long long a;
	unsigned long long b;
	unsigned long long c;
} WintoshTriple;

__attribute__((objc_root_class))
@interface WintoshProbe
+ (long)answer;
+ (long)add:(long)left to:(long)right;
+ (double)increment:(double)value;
+ (id)echo:(id)value;
+ (long)readValue:(const long *)value;
+ (long)sumSeven:(long)a second:(long)b third:(long)c fourth:(long)d
	 fifth:(long)e sixth:(long)f seventh:(long)g;
+ (WintoshPair)shiftPair:(WintoshPair)value;
+ (WintoshQuad)shiftQuad:(WintoshQuad)value;
+ (WintoshTriple)advanceTriple:(WintoshTriple)value;
@end

@implementation WintoshProbe
+ (long)answer
{
	return 42;
}
+ (long)add:(long)left to:(long)right
{
	return left + right;
}
+ (double)increment:(double)value
{
	return value + 1.0;
}
+ (id)echo:(id)value
{
	return value;
}
+ (long)readValue:(const long *)value
{
	return *value + 2;
}
+ (long)sumSeven:(long)a second:(long)b third:(long)c fourth:(long)d
	 fifth:(long)e sixth:(long)f seventh:(long)g
{
	return a + b + c + d + e + f + g;
}
+ (WintoshPair)shiftPair:(WintoshPair)value
{
	return (WintoshPair){value.x + 1.0, value.y + 2.0};
}
+ (WintoshQuad)shiftQuad:(WintoshQuad)value
{
	return (WintoshQuad){value.a + 1.0, value.b + 2.0,
		value.c + 3.0, value.d + 4.0};
}
+ (WintoshTriple)advanceTriple:(WintoshTriple)value
{
	return (WintoshTriple){value.a + 1, value.b + 2, value.c + 3};
}
@end

typedef long (*WintoshSendLong)(id, SEL);
typedef long (*WintoshSendLongPair)(id, SEL, long, long);
typedef double (*WintoshSendDouble)(id, SEL, double);
typedef id (*WintoshSendObject)(id, SEL, id);
typedef long (*WintoshSendLongPointer)(id, SEL, const long *);
typedef long (*WintoshSendLongSeven)(id, SEL, long, long, long, long, long,
	long, long);
typedef WintoshPair (*WintoshSendPair)(id, SEL, WintoshPair);
typedef WintoshQuad (*WintoshSendQuad)(id, SEL, WintoshQuad);
typedef WintoshTriple (*WintoshSendTriple)(id, SEL, WintoshTriple);

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	Class probe = objc_getClass("WintoshProbe");
	SEL answer = sel_registerName("answer");
	SEL add = sel_registerName("add:to:");
	SEL increment = sel_registerName("increment:");
	SEL echo = sel_registerName("echo:");
	SEL read_value = sel_registerName("readValue:");
	SEL sum_seven = sel_registerName(
		"sumSeven:second:third:fourth:fifth:sixth:seventh:");
	SEL shift_pair = sel_registerName("shiftPair:");
	SEL shift_quad = sel_registerName("shiftQuad:");
	SEL advance_triple = sel_registerName("advanceTriple:");
	if (probe == (Class)0 || answer == (SEL)0 || add == (SEL)0 ||
		increment == (SEL)0 || echo == (SEL)0 || read_value == (SEL)0 ||
		sum_seven == (SEL)0 || shift_pair == (SEL)0 || shift_quad == (SEL)0 ||
		advance_triple == (SEL)0)
		return 1;
	const long value = ((WintoshSendLong)objc_msgSend)((id)probe, answer);
	const long sum = ((WintoshSendLongPair)objc_msgSend)((id)probe, add, 20, 22);
	const double incremented = ((WintoshSendDouble)objc_msgSend)(
		(id)probe, increment, 41.0);
	const id echoed = ((WintoshSendObject)objc_msgSend)((id)probe, echo, (id)probe);
	const long pointed_value = 40;
	const long dereferenced = ((WintoshSendLongPointer)objc_msgSend)(
		(id)probe, read_value, &pointed_value);
	const long seven = ((WintoshSendLongSeven)objc_msgSend)((id)probe, sum_seven,
		1, 2, 3, 4, 5, 6, 21);
	const WintoshPair pair = ((WintoshSendPair)objc_msgSend)(
		(id)probe, shift_pair, (WintoshPair){20.0, 20.0});
	const WintoshQuad quad = ((WintoshSendQuad)objc_msgSend)(
		(id)probe, shift_quad, (WintoshQuad){10.0, 10.0, 10.0, 10.0});
	const WintoshTriple triple = ((WintoshSendTriple)objc_msgSend)(
		(id)probe, advance_triple, (WintoshTriple){10, 10, 10});
	return value == 42 && sum == 42 && incremented == 42.0 &&
		echoed == (id)probe && dereferenced == 42 && seven == 42 &&
		pair.x == 21.0 && pair.y == 22.0 && quad.a == 11.0 &&
		quad.b == 12.0 && quad.c == 13.0 && quad.d == 14.0 &&
		triple.a == 11 && triple.b == 12 && triple.c == 13 ? 42 : 1;
}
