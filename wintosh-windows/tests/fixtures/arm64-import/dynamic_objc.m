/* Original Wintosh ARM64 dynamic Objective-C fixture; GPL-3.0-only. */

extern int write(int descriptor, const void *bytes, unsigned long length);

__attribute__((objc_root_class))
@interface WintoshDynamicLoadProbe
+ (void)load;
+ (long)dynamicAnswer;
@end

@implementation WintoshDynamicLoadProbe
+ (void)load
{
	static const char marker[] = "ARM64_DYLIB_OBJC_LOAD=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}
+ (long)dynamicAnswer
{
	return 84;
}
@end

@interface WintoshDynamicLoadProbe (WintoshDynamicCategory)
+ (long)categoryAnswer;
@end

@implementation WintoshDynamicLoadProbe (WintoshDynamicCategory)
+ (void)load
{
	static const char marker[] = "ARM64_DYLIB_CATEGORY_LOAD=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}
+ (long)categoryAnswer
{
	return 126;
}
@end
