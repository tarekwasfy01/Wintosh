/* Original Wintosh Objective-C fixture; GPL-3.0-only. */

extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id receiver, SEL selector, ...);

__attribute__((objc_root_class))
@interface WintoshProbe
+ (long)answer;
+ (long)add:(long)left to:(long)right;
+ (double)increment:(double)value;
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
@end

typedef long (*WintoshSendLong)(id, SEL);
typedef long (*WintoshSendLongPair)(id, SEL, long, long);
typedef double (*WintoshSendDouble)(id, SEL, double);

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	Class probe = objc_getClass("WintoshProbe");
	SEL answer = sel_registerName("answer");
	SEL add = sel_registerName("add:to:");
	SEL increment = sel_registerName("increment:");
	if (probe == (Class)0 || answer == (SEL)0 || add == (SEL)0 ||
		increment == (SEL)0)
		return 1;
	const long value = ((WintoshSendLong)objc_msgSend)((id)probe, answer);
	const long sum = ((WintoshSendLongPair)objc_msgSend)((id)probe, add, 20, 22);
	const double incremented = ((WintoshSendDouble)objc_msgSend)(
		(id)probe, increment, 41.0);
	return value == 42 && sum == 42 && incremented == 42.0 ? 42 : 1;
}
