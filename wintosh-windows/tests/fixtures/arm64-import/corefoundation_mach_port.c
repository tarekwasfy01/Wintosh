/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFStringRef;
typedef void *CFRunLoopRef;
typedef void *CFRunLoopSourceRef;
typedef const void *CFMachPortRef;
typedef struct {
	CFIndex version;
	void *info;
	const void *(*retain)(const void *);
	void (*release)(const void *);
	const void *(*copyDescription)(const void *);
} CFMachPortContext;

extern CFStringRef CFStringCreateWithCString(const void *, const char *, unsigned int);
extern CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFMachPortRef CFMachPortCreate(const void *,
	void (*)(CFMachPortRef, void *, CFIndex, void *),
	const CFMachPortContext *, unsigned char *);
extern CFRunLoopSourceRef CFMachPortCreateRunLoopSource(const void *,
	CFMachPortRef, CFIndex);
extern void CFMachPortGetContext(CFMachPortRef, CFMachPortContext *);
extern unsigned int CFMachPortGetPort(CFMachPortRef);
extern unsigned char CFMachPortIsValid(CFMachPortRef);
extern void CFMachPortInvalidate(CFMachPortRef);
extern void CFMachPortSetInvalidationCallBack(CFMachPortRef,
	void (*)(CFMachPortRef, void *));
extern void (*CFMachPortGetInvalidationCallBack(CFMachPortRef))(
	CFMachPortRef, void *);
extern void CFRunLoopAddSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
extern void CFRunLoopRemoveSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
extern unsigned char CFRunLoopContainsSource(CFRunLoopRef, CFRunLoopSourceRef,
	CFStringRef);
extern unsigned char CFRunLoopSourceIsValid(CFRunLoopSourceRef);
extern void CFRelease(CFTypeRef);

static int callbackInfo;
static int retainCalls;
static int releaseCalls;
static int invalidationCalls;
static const void *retain_context(const void *info)
{
	if (info == &callbackInfo) ++retainCalls;
	return info;
}
static void release_context(const void *info)
{
	if (info == &callbackInfo) ++releaseCalls;
}
static void mach_message(CFMachPortRef port, void *message, CFIndex size, void *info)
{
	(void)port; (void)message; (void)size; (void)info;
}
static void mach_invalidated(CFMachPortRef port, void *info)
{
	if (port != 0 && info == &callbackInfo) ++invalidationCalls;
}

int main(void)
{
	CFMachPortContext context = {
		0, &callbackInfo, retain_context, release_context, 0 };
	unsigned char shouldFreeInfo = 0xff;
	CFMachPortRef port = CFMachPortCreate(0, mach_message, &context,
		&shouldFreeInfo);
	if (port == 0 || shouldFreeInfo != 0 || !CFMachPortIsValid(port) ||
		CFMachPortGetPort(port) == 0 || retainCalls != 1) return 20;
	CFMachPortContext returned;
	CFMachPortGetContext(port, &returned);
	if (returned.version != 0 || returned.info != &callbackInfo) return 21;
	CFMachPortSetInvalidationCallBack(port, mach_invalidated);
	if (CFMachPortGetInvalidationCallBack(port) != mach_invalidated) return 22;
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	CFStringRef mode = CFStringCreateWithCString(0,
		"kCFRunLoopDefaultMode", 0x08000100u);
	CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(0, port, 3);
	if (loop == 0 || mode == 0 || source == 0) return 23;
	CFRunLoopAddSource(loop, source, mode);
	if (!CFRunLoopContainsSource(loop, source, mode)) return 24;
	CFMachPortInvalidate(port);
	if (CFMachPortIsValid(port) || invalidationCalls != 1 || releaseCalls != 1 ||
		CFRunLoopSourceIsValid(source) ||
		CFRunLoopContainsSource(loop, source, mode)) return 25;
	CFRunLoopRemoveSource(loop, source, mode);
	CFRelease(source);
	CFRelease(port);
	CFRelease(mode);
	return 42;
}
