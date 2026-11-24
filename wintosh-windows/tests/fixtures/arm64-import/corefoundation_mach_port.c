/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFStringRef;
typedef void *CFRunLoopRef;
typedef void *CFRunLoopSourceRef;
typedef const void *CFMachPortRef;
typedef struct {
	unsigned int bits;
	unsigned int size;
	unsigned int remotePort;
	unsigned int localPort;
	unsigned int voucherPort;
	int id;
} MachMessage;
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
extern int CFRunLoopRunInMode(CFStringRef, double, unsigned char);
extern unsigned char CFRunLoopContainsSource(CFRunLoopRef, CFRunLoopSourceRef,
	CFStringRef);
extern unsigned char CFRunLoopSourceIsValid(CFRunLoopSourceRef);
extern void CFRelease(CFTypeRef);
extern int mach_msg(MachMessage *, unsigned int, unsigned int, unsigned int,
	unsigned int, unsigned int, unsigned int);

static int callbackInfo;
static int retainCalls;
static int releaseCalls;
static int invalidationCalls;
static int messageCalls;
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
	if (port != 0 && message != 0 && size >= (CFIndex)sizeof(MachMessage) &&
		info == &callbackInfo) {
		++messageCalls;
	}
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
	MachMessage outgoing = { 19, sizeof(MachMessage), CFMachPortGetPort(port),
		0, 0, 0x57494e54 };
	if (mach_msg(&outgoing, 1, sizeof(outgoing), 0, 0, 0, 0) != 0)
		return 26;
	if (CFRunLoopRunInMode(mode, 0.0, 1) != 4) return 27;
	if (messageCalls != 1) return 28;
	CFMachPortInvalidate(port);
	if (CFMachPortIsValid(port) || invalidationCalls != 1 || releaseCalls != 1 ||
		CFRunLoopSourceIsValid(source) ||
		CFRunLoopContainsSource(loop, source, mode)) return 25;
	CFRunLoopRemoveSource(loop, source, mode);
	CFRelease(source);
	CFRelease((CFTypeRef)port);
	CFRelease(mode);
	return 42;
}
