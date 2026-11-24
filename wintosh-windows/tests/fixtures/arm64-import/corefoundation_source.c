/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFStringRef;
typedef void *CFRunLoopRef;
typedef void *CFRunLoopSourceRef;
typedef struct CFRunLoopSourceContext {
	CFIndex version;
	void *info;
	const void *(*retain)(const void *);
	void (*release)(const void *);
	const void *(*copyDescription)(const void *);
	unsigned char (*equal)(const void *, const void *);
	unsigned long (*hash)(const void *);
	void (*schedule)(void *, CFRunLoopRef, const void *);
	void (*cancel)(void *, CFRunLoopRef, const void *);
	void (*perform)(void *);
} CFRunLoopSourceContext;
_Static_assert(sizeof(CFRunLoopSourceContext) == 80, "Darwin source context layout");

extern CFStringRef CFStringCreateWithCString(const void *, const char *, unsigned int);
extern CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFRunLoopSourceRef CFRunLoopSourceCreate(const void *, CFIndex,
	const CFRunLoopSourceContext *);
extern void CFRunLoopSourceGetContext(CFRunLoopSourceRef, CFRunLoopSourceContext *);
extern void CFRunLoopAddSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
extern void CFRunLoopRemoveSource(CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
extern unsigned char CFRunLoopContainsSource(CFRunLoopRef, CFRunLoopSourceRef,
	CFStringRef);
extern long CFRunLoopSourceGetOrder(CFRunLoopSourceRef);
extern unsigned char CFRunLoopSourceIsValid(CFRunLoopSourceRef);
extern void CFRunLoopSourceSignal(CFRunLoopSourceRef);
extern void CFRunLoopSourceInvalidate(CFRunLoopSourceRef);
extern int CFRunLoopRunInMode(CFStringRef, double, unsigned char);
extern void CFRelease(CFTypeRef);

static int scheduleCalls;
static int cancelCalls;
static int performCalls;
static int contextInfo;

static void source_schedule(void *info, CFRunLoopRef loop, const void *mode)
{
	if (info != &contextInfo || loop == 0 || mode == 0) return;
	++scheduleCalls;
}

static void source_cancel(void *info, CFRunLoopRef loop, const void *mode)
{
	if (info != &contextInfo || loop == 0 || mode == 0) return;
	++cancelCalls;
}

static void source_perform(void *info)
{
	if (info == &contextInfo) ++performCalls;
}

int main(void)
{
	CFRunLoopSourceContext context;
	context.version = 0;
	context.info = &contextInfo;
	context.retain = 0;
	context.release = 0;
	context.copyDescription = 0;
	context.equal = 0;
	context.hash = 0;
	context.schedule = source_schedule;
	context.cancel = source_cancel;
	context.perform = source_perform;
	CFStringRef mode = CFStringCreateWithCString(0,
		"kCFRunLoopDefaultMode", 0x08000100u);
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	CFRunLoopSourceRef source = CFRunLoopSourceCreate(0, 0, &context);
	if (mode == 0 || loop == 0 || source == 0) return 10;
	CFRunLoopSourceContext returnedContext;
	CFRunLoopSourceGetContext(source, &returnedContext);
	if (returnedContext.info != &contextInfo ||
		returnedContext.schedule != source_schedule ||
		returnedContext.cancel != source_cancel) return 11;
	if (!CFRunLoopSourceIsValid(source) || CFRunLoopSourceGetOrder(source) != 0)
		return 14;
	CFRunLoopAddSource(loop, source, mode);
	if (scheduleCalls != 1 || cancelCalls != 0 ||
		!CFRunLoopContainsSource(loop, source, mode)) return 12;
	CFRunLoopSourceSignal(source);
	if (CFRunLoopRunInMode(mode, 0.01, 1) < 1 || performCalls != 1) return 15;
	CFRunLoopRemoveSource(loop, source, mode);
	if (scheduleCalls != 1 || cancelCalls != 1 ||
		CFRunLoopContainsSource(loop, source, mode)) return 13;
	CFRunLoopSourceInvalidate(source);
	if (CFRunLoopSourceIsValid(source)) return 16;
	CFRelease(source);
	CFRelease(mode);
	return 42;
}
