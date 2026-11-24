/* Wintosh original Mach-O guest fixture; GPL-3.0-or-later. */
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
extern void CFRelease(CFTypeRef);
extern int puts(const char *);

static int scheduleCalls;
static int cancelCalls;
static int invalidArguments;
static int contextInfo;

static void source_schedule(void *info, CFRunLoopRef loop, const void *mode)
{
	if (info != &contextInfo || loop == 0 || mode == 0) ++invalidArguments;
	++scheduleCalls;
}

static void source_cancel(void *info, CFRunLoopRef loop, const void *mode)
{
	if (info != &contextInfo || loop == 0 || mode == 0) ++invalidArguments;
	++cancelCalls;
}

static void source_perform(void *info)
{
	(void)info;
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
	if (mode == 0 || source == 0) return 10;

	CFRunLoopSourceContext returnedContext;
	CFRunLoopSourceGetContext(source, &returnedContext);
	if (returnedContext.schedule != source_schedule ||
		returnedContext.cancel != source_cancel ||
		returnedContext.info != &contextInfo) return 11;

	CFRunLoopAddSource(loop, source, mode);
	if (scheduleCalls != 1 || cancelCalls != 0 || invalidArguments != 0) return 12;
	CFRunLoopAddSource(loop, source, mode);
	if (scheduleCalls != 1) return 13;
	CFRunLoopRemoveSource(loop, source, mode);
	if (cancelCalls != 1 || invalidArguments != 0) return 14;
	CFRunLoopRemoveSource(loop, source, mode);
	if (cancelCalls != 1) return 15;

	CFRelease(source);
	CFRelease(mode);
	puts("COREFOUNDATION_SOURCE_CALLBACKS=PASS");
	return 0;
}
