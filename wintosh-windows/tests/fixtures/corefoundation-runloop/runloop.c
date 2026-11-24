/* Wintosh original Mach-O guest fixture; GPL-3.0-or-later. */
typedef unsigned long long CFOptionFlags;
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFRunLoopRef;
typedef void *CFStringRef;
typedef void *CFRunLoopObserverRef;
typedef void *CFRunLoopTimerRef;
typedef void *CFRunLoopSourceRef;
typedef void (*CFRunLoopObserverCallout)(CFRunLoopObserverRef, CFOptionFlags, void *);
typedef struct CFRunLoopObserverContext {
	CFIndex version;
	void *info;
	const void *(*retain)(const void *);
	void (*release)(const void *);
	const void *(*copyDescription)(const void *);
} CFRunLoopObserverContext;
typedef struct CFRunLoopTimerContext {
	CFIndex version;
	void *info;
	const void *(*retain)(const void *);
	void (*release)(const void *);
	const void *(*copyDescription)(const void *);
} CFRunLoopTimerContext;
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
_Static_assert(sizeof(CFRunLoopObserverContext) == 40, "Darwin observer context layout");
_Static_assert(__builtin_offsetof(CFRunLoopObserverContext, info) == 8,
	"Darwin observer info offset");
_Static_assert(sizeof(CFRunLoopSourceContext) == 80, "Darwin source context layout");

extern CFStringRef CFStringCreateWithCString(const void *allocator,
	const char *bytes, unsigned int encoding);
extern CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFRunLoopObserverRef CFRunLoopObserverCreate(const void *allocator,
	CFOptionFlags activities, unsigned char repeats, CFIndex order,
	CFRunLoopObserverCallout callout, const void *context);
extern unsigned char CFRunLoopObserverIsValid(CFRunLoopObserverRef observer);
extern void CFRunLoopObserverInvalidate(CFRunLoopObserverRef observer);
extern void CFRunLoopAddObserver(CFRunLoopRef loop,
	CFRunLoopObserverRef observer, CFStringRef mode);
extern void CFRunLoopRemoveObserver(CFRunLoopRef loop,
	CFRunLoopObserverRef observer, CFStringRef mode);
extern CFRunLoopTimerRef CFRunLoopTimerCreate(const void *allocator,
	double fireDate, double interval, CFOptionFlags flags, CFIndex order,
	void (*callout)(CFRunLoopTimerRef, void *),
	const CFRunLoopTimerContext *context);
extern double CFRunLoopTimerGetNextFireDate(CFRunLoopTimerRef timer);
extern double CFRunLoopTimerGetInterval(CFRunLoopTimerRef timer);
extern unsigned char CFRunLoopTimerDoesRepeat(CFRunLoopTimerRef timer);
extern double CFRunLoopTimerGetTolerance(CFRunLoopTimerRef timer);
extern void CFRunLoopTimerSetTolerance(CFRunLoopTimerRef timer, double tolerance);
extern unsigned char CFRunLoopTimerIsValid(CFRunLoopTimerRef timer);
extern void CFRunLoopTimerInvalidate(CFRunLoopTimerRef timer);
extern void CFRunLoopAddTimer(CFRunLoopRef loop,
	CFRunLoopTimerRef timer, CFStringRef mode);
extern void CFRunLoopRemoveTimer(CFRunLoopRef loop,
	CFRunLoopTimerRef timer, CFStringRef mode);
extern CFRunLoopSourceRef CFRunLoopSourceCreate(const void *allocator,
	CFIndex order, const CFRunLoopSourceContext *context);
extern void CFRunLoopSourceGetContext(CFRunLoopSourceRef source,
	CFRunLoopSourceContext *context);
extern unsigned char CFRunLoopSourceIsValid(CFRunLoopSourceRef source);
extern void CFRunLoopSourceSignal(CFRunLoopSourceRef source);
extern void CFRunLoopSourceInvalidate(CFRunLoopSourceRef source);
extern unsigned char CFRunLoopContainsSource(CFRunLoopRef loop,
	CFRunLoopSourceRef source, CFStringRef mode);
extern void CFRunLoopAddSource(CFRunLoopRef loop,
	CFRunLoopSourceRef source, CFStringRef mode);
extern void CFRunLoopRemoveSource(CFRunLoopRef loop,
	CFRunLoopSourceRef source, CFStringRef mode);
extern int CFRunLoopRunInMode(CFStringRef mode, double seconds,
	unsigned char returnAfterSource);
extern void CFRelease(CFTypeRef value);
extern int puts(const char *text);

enum {
	CFRunLoopEntry = 1ull << 0,
	CFRunLoopBeforeTimers = 1ull << 1,
	CFRunLoopBeforeSources = 1ull << 2,
	CFRunLoopExit = 1ull << 7,
	CFRunLoopRunHandledSource = 4,
};

struct CallbackState {
	CFOptionFlags activities;
	int calls;
	int context_matches;
};
static struct CallbackState repeating;
static struct CallbackState oneShot;
struct TimerState {
	int calls;
	int context_matches;
};
static struct TimerState timerState;
struct SourceState {
	int calls;
	int context_matches;
	int retains;
	int releases;
};
static struct SourceState sourceState;

static void observer_callback(CFRunLoopObserverRef observer,
	CFOptionFlags activity, void *info)
{
	(void)observer;
	if (info == &repeating) {
		repeating.activities |= activity;
		++repeating.calls;
		repeating.context_matches = 1;
	} else if (info == &oneShot) {
		oneShot.activities |= activity;
		++oneShot.calls;
		oneShot.context_matches = 1;
	}
}
static void one_shot_callback(CFRunLoopObserverRef observer,
	CFOptionFlags activity, void *info)
{
	(void)observer;
	oneShot.activities |= activity;
	++oneShot.calls;
	oneShot.context_matches = info != 0;
}
static void timer_callback(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	++timerState.calls;
	timerState.context_matches = info != 0;
}
static void source_perform(void *info)
{
	++sourceState.calls;
	sourceState.context_matches = info == &sourceState;
}
static const void *source_retain(const void *info)
{
	if (info != &sourceState) return 0;
	++sourceState.retains;
	return info;
}
static void source_release(const void *info)
{
	if (info == &sourceState) ++sourceState.releases;
}

int main(void)
{
	CFStringRef mode = CFStringCreateWithCString(0,
		"kCFRunLoopDefaultMode", 0x08000100u);
	repeating = (struct CallbackState){0, 0, 0};
	oneShot = (struct CallbackState){0, 0, 0};
	CFRunLoopObserverContext repeatingContext = {0, &repeating, 0, 0, 0};
	CFRunLoopObserverContext oneShotContext = {0, &oneShot, 0, 0, 0};
	const CFOptionFlags phases = CFRunLoopEntry | CFRunLoopBeforeTimers |
		CFRunLoopBeforeSources | CFRunLoopExit;
	CFRunLoopObserverRef repeatingObserver = CFRunLoopObserverCreate(0,
		phases, 1, 10, observer_callback, &repeatingContext);
	CFRunLoopObserverRef oneShotObserver = CFRunLoopObserverCreate(0,
		CFRunLoopEntry, 0, 0, one_shot_callback, &oneShotContext);
	if (mode == 0)
		return 11;
	if (repeatingObserver == 0)
		return 12;
	if (oneShotObserver == 0)
		return 13;
	CFRunLoopRef loop = CFRunLoopGetCurrent();
	CFRunLoopAddObserver(loop, repeatingObserver, mode);
	CFRunLoopAddObserver(loop, oneShotObserver, mode);
	(void)CFRunLoopRunInMode(mode, 0.01, 0);
	const int passed = repeating.activities == phases && repeating.calls == 4 &&
		repeating.context_matches && oneShot.activities == CFRunLoopEntry &&
		oneShot.calls == 1 && oneShot.context_matches == 1 &&
		CFRunLoopObserverIsValid(repeatingObserver) &&
		!CFRunLoopObserverIsValid(oneShotObserver);
	if (repeating.activities != phases) return 21;
	if (repeating.calls != 4) return 22;
	if (!repeating.context_matches) return 23;
	if (oneShot.activities != CFRunLoopEntry) return 24;
	if (oneShot.calls != 1) return 25;
	if (!oneShot.context_matches) return 26;
	if (!CFRunLoopObserverIsValid(repeatingObserver)) return 27;
	if (CFRunLoopObserverIsValid(oneShotObserver)) return 28;
	CFRunLoopRemoveObserver(loop, repeatingObserver, mode);
	CFRunLoopRemoveObserver(loop, oneShotObserver, mode);
	CFRunLoopObserverInvalidate(repeatingObserver);
	CFRunLoopObserverInvalidate(oneShotObserver);
	CFRelease(repeatingObserver);
	CFRelease(oneShotObserver);
	timerState = (struct TimerState){0, 0};
	CFRunLoopTimerContext timerContext = {0, &timerState, 0, 0, 0};
	CFRunLoopTimerRef timer = CFRunLoopTimerCreate(0, 0.0, 0.0,
		0, 0, timer_callback, &timerContext);
	if (timer == 0)
		return 32;
	CFRunLoopTimerSetTolerance(timer, 0.01);
	if (CFRunLoopTimerGetTolerance(timer) != 0.01 ||
		CFRunLoopTimerGetNextFireDate(timer) != 0.0 ||
		CFRunLoopTimerDoesRepeat(timer)) return 36;
	CFRunLoopTimerSetTolerance(timer, -1.0);
	if (CFRunLoopTimerGetTolerance(timer) != 0.01) return 37;
	CFRunLoopAddTimer(loop, timer, mode);
	(void)CFRunLoopRunInMode(mode, 1.0, 0);
	if (timerState.calls != 1) return 33;
	if (!timerState.context_matches) return 34;
	if (CFRunLoopTimerIsValid(timer)) return 35;
	CFRunLoopRemoveTimer(loop, timer, mode);
	CFRunLoopTimerInvalidate(timer);
	CFRelease(timer);
	timerState = (struct TimerState){0, 0};
	CFRunLoopTimerRef repeatingTimer = CFRunLoopTimerCreate(0,
		0.0, 0.005, 0, 0, timer_callback, &timerContext);
	if (repeatingTimer == 0) return 38;
	CFRunLoopTimerSetTolerance(repeatingTimer, 0.002);
	if (CFRunLoopTimerGetTolerance(repeatingTimer) != 0.002 ||
		CFRunLoopTimerGetInterval(repeatingTimer) != 0.005 ||
		!CFRunLoopTimerDoesRepeat(repeatingTimer)) return 39;
	CFRunLoopAddTimer(loop, repeatingTimer, mode);
	(void)CFRunLoopRunInMode(mode, 0.03, 0);
	if (timerState.calls < 2 || !timerState.context_matches ||
		!CFRunLoopTimerIsValid(repeatingTimer)) return 40;
	CFRunLoopRemoveTimer(loop, repeatingTimer, mode);
	CFRunLoopTimerInvalidate(repeatingTimer);
	CFRelease(repeatingTimer);
	sourceState = (struct SourceState){0, 0};
	CFRunLoopSourceContext sourceContext;
	sourceContext.version = 0;
	sourceContext.info = &sourceState;
	sourceContext.retain = 0;
	sourceContext.release = 0;
	sourceContext.copyDescription = 0;
	sourceContext.equal = 0;
	sourceContext.hash = 0;
	sourceContext.schedule = 0;
	sourceContext.cancel = 0;
	sourceContext.perform = source_perform;
	CFRunLoopSourceRef source = CFRunLoopSourceCreate(0, 0, &sourceContext);
	if (source == 0) return 41;
	CFRunLoopAddSource(loop, source, mode);
	if (!CFRunLoopContainsSource(loop, source, mode)) return 42;
	CFRunLoopSourceSignal(source);
	CFRunLoopSourceSignal(source);
	const int sourceRunResult = CFRunLoopRunInMode(mode, 0.03, 1);
	if (sourceRunResult != CFRunLoopRunHandledSource) return 51;
	if (sourceState.calls != 1) return 45;
	if (!sourceState.context_matches) return 46;
	CFRunLoopRemoveSource(loop, source, mode);
	if (CFRunLoopContainsSource(loop, source, mode))
		return 48;
	CFRunLoopSourceInvalidate(source);
	if (CFRunLoopSourceIsValid(source)) return 44;
	CFRelease(source);
	sourceState.retains = 0;
	sourceState.releases = 0;
	CFRunLoopSourceContext retainedContext;
	retainedContext.version = 0;
	retainedContext.info = &sourceState;
	retainedContext.retain = source_retain;
	retainedContext.release = source_release;
	retainedContext.copyDescription = 0;
	retainedContext.equal = 0;
	retainedContext.hash = 0;
	retainedContext.schedule = 0;
	retainedContext.cancel = 0;
	retainedContext.perform = source_perform;
	CFRunLoopSourceRef retainedSource = CFRunLoopSourceCreate(0, 0,
		&retainedContext);
	if (retainedSource == 0 || sourceState.retains != 1) return 52;
	CFRunLoopSourceContext returnedContext;
	CFRunLoopSourceGetContext(retainedSource, &returnedContext);
	if (returnedContext.info != &sourceState ||
		returnedContext.retain != source_retain ||
		returnedContext.release != source_release) return 56;
	CFRelease(retainedSource);
	if (sourceState.releases != 1) return 53;
	CFRelease(mode);
	if (!passed)
		return 14;
	puts("COREFOUNDATION_GUEST_OBSERVER=PASS");
	puts("COREFOUNDATION_GUEST_TIMER=PASS");
	puts("COREFOUNDATION_GUEST_SOURCE=PASS");
	return 0;
}
