/* Original Wintosh Objective-C fixture; GPL-3.0-only. */

extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id receiver, SEL selector, ...);

__attribute__((objc_root_class))
@interface WintoshProbe
+ (long)answer;
@end

@implementation WintoshProbe
+ (long)answer
{
	return 42;
}
@end

typedef long (*WintoshSendLong)(id, SEL);

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	Class probe = objc_getClass("WintoshProbe");
	SEL answer = sel_registerName("answer");
	if (probe == (Class)0 || answer == (SEL)0)
		return 1;
	const long value = ((WintoshSendLong)objc_msgSend)((id)probe, answer);
	return value == 42 ? 42 : 1;
}
