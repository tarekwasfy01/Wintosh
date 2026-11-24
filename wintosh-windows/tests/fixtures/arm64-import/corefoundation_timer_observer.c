/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef unsigned long long CFOptionFlags;
typedef void *CFTypeRef;
typedef void *CFStringRef;
typedef void *CFRunLoopRef;
typedef void *CFRunLoopTimerRef;
typedef void *CFRunLoopObserverRef;
typedef struct {
	CFIndex version;
	void *info;
	const void *(*retain)(const void *);
	void (*release)(const void *);
	const void *(*copyDescription)(const void *);
} Context;
_Static_assert(sizeof(Context) == 40, "Darwin run-loop context layout");

extern CFStringRef CFStringCreateWithCString(const void *, const char *, unsigned int);
extern CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFRunLoopTimerRef CFRunLoopTimerCreate(const void *, double, double,
	CFOptionFlags, CFIndex, void (*)(CFRunLoopTimerRef, void *), const Context *);
extern void CFRunLoopAddTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef);
extern void CFRunLoopRemoveTimer(CFRunLoopRef, CFRunLoopTimerRef, CFStringRef);
extern unsigned char CFRunLoopContainsTimer(CFRunLoopRef, CFRunLoopTimerRef,
	CFStringRef);
extern double CFRunLoopTimerGetNextFireDate(CFRunLoopTimerRef);
extern void CFRunLoopTimerSetNextFireDate(CFRunLoopTimerRef, double);
extern double CFRunLoopTimerGetTolerance(CFRunLoopTimerRef);
extern void CFRunLoopTimerSetTolerance(CFRunLoopTimerRef, double);
extern double CFRunLoopTimerGetInterval(CFRunLoopTimerRef);
extern unsigned char CFRunLoopTimerDoesRepeat(CFRunLoopTimerRef);
extern CFIndex CFRunLoopTimerGetOrder(CFRunLoopTimerRef);
extern unsigned char CFRunLoopTimerIsValid(CFRunLoopTimerRef);
extern double CFRunLoopGetNextTimerFireDate(CFRunLoopRef, CFStringRef);
extern CFRunLoopObserverRef CFRunLoopObserverCreate(const void *, CFOptionFlags,
	unsigned char, CFIndex, void (*)(CFRunLoopObserverRef, CFOptionFlags, void *),
	const Context *);
extern CFOptionFlags CFRunLoopObserverGetActivities(CFRunLoopObserverRef);
extern unsigned char CFRunLoopObserverDoesRepeat(CFRunLoopObserverRef);
extern CFIndex CFRunLoopObserverGetOrder(CFRunLoopObserverRef);
extern unsigned char CFRunLoopObserverIsValid(CFRunLoopObserverRef);
extern void CFRunLoopAddObserver(CFRunLoopRef, CFRunLoopObserverRef, CFStringRef);
extern void CFRunLoopRemoveObserver(CFRunLoopRef, CFRunLoopObserverRef, CFStringRef);
extern unsigned char CFRunLoopContainsObserver(CFRunLoopRef,
	CFRunLoopObserverRef, CFStringRef);
extern int CFRunLoopRunInMode(CFStringRef, double, unsigned char);
extern void CFRunLoopPerformBlock(CFRunLoopRef, CFTypeRef, void *);
extern void CFRelease(CFTypeRef);

static int timerCalls;
static int observerCalls;
static int blockCalls;
static int timerInfo;
static int observerInfo;

typedef struct {
	void *isa;
	int flags;
	int reserved;
	void (*invoke)(void *);
	const void *descriptor;
} Block;
typedef struct { unsigned long reserved; unsigned long size; } BlockDescriptor;
static void run_loop_block(void *block)
{
	if (block != 0) ++blockCalls;
}
static const BlockDescriptor blockDescriptor = { 0, sizeof(Block) };
static Block runLoopBlock = { 0, 1 << 28, 0, run_loop_block, &blockDescriptor };

static void timer_callout(CFRunLoopTimerRef timer, void *info)
{
	if (timer != 0 && info == &timerInfo) ++timerCalls;
}

static void observer_callout(CFRunLoopObserverRef observer,
	CFOptionFlags activity, void *info)
{
	if (observer != 0 && (activity & 1) != 0 && info == &observerInfo)
		++observerCalls;
}

int main(void)
{
	Context timerContext;
	timerContext.version = 0;
	timerContext.info = &timerInfo;
	timerContext.retain = 0;
	timerContext.release = 0;
	timerContext.copyDescription = 0;
	Context observerContext;
	observerContext.version = 0;
	observerContext.info = &observerInfo;
	observerContext.retain = 0;
	observerContext.release = 0;
	observerContext.copyDescription = 0;
	CFStringRef mode = CFStringCreateWithCString(0,
		"kCFRunLoopDefaultMode", 0x08000100u);
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	CFRunLoopTimerRef timer = CFRunLoopTimerCreate(0, 0.0, 0.0, 0, 0,
		timer_callout, &timerContext);
	CFRunLoopObserverRef observer = CFRunLoopObserverCreate(0, 1, 0, 0,
		observer_callout, &observerContext);
	if (mode == 0) return 20;
	if (loop == 0) return 21;
	if (timer == 0) return 22;
	if (observer == 0) return 23;
	CFRunLoopTimerSetNextFireDate(timer, 0.25);
	CFRunLoopTimerSetTolerance(timer, 0.125);
	if (CFRunLoopTimerGetNextFireDate(timer) != 0.25 ||
		CFRunLoopTimerGetTolerance(timer) != 0.125 ||
		CFRunLoopTimerGetInterval(timer) != 0.0 ||
		CFRunLoopTimerDoesRepeat(timer) || CFRunLoopTimerGetOrder(timer) != 0 ||
		!CFRunLoopTimerIsValid(timer)) return 26;
	if (CFRunLoopObserverGetActivities(observer) != 1 ||
		CFRunLoopObserverDoesRepeat(observer) ||
		CFRunLoopObserverGetOrder(observer) != 0 ||
		!CFRunLoopObserverIsValid(observer)) return 28;
	CFRunLoopAddTimer(loop, timer, mode);
	CFRunLoopAddObserver(loop, observer, mode);
	if (!CFRunLoopContainsTimer(loop, timer, mode) ||
		!CFRunLoopContainsObserver(loop, observer, mode)) return 24;
	if (CFRunLoopGetNextTimerFireDate(loop, mode) != 0.25) return 27;
	CFRunLoopTimerSetNextFireDate(timer, 0.0);
	CFRunLoopPerformBlock(loop, mode, &runLoopBlock);
	const int result = CFRunLoopRunInMode(mode, 0.01, 0);
	if (result < 1 || timerCalls != 1 || observerCalls != 1 || blockCalls != 1) return 11;
	if (CFRunLoopTimerIsValid(timer) || CFRunLoopObserverIsValid(observer)) return 29;
	CFRunLoopRemoveTimer(loop, timer, mode);
	CFRunLoopRemoveObserver(loop, observer, mode);
	if (CFRunLoopContainsTimer(loop, timer, mode) ||
		CFRunLoopContainsObserver(loop, observer, mode)) return 25;
	CFRelease(timer);
	CFRelease(observer);
	CFRelease(mode);
	return 42;
}
