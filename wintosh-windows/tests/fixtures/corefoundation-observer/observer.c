/* Wintosh original Mach-O guest fixture; GPL-3.0-or-later. */
typedef unsigned long long CFOptionFlags;
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFRunLoopRef;
typedef void *CFStringRef;
typedef void *CFRunLoopObserverRef;
typedef void (*CFRunLoopObserverCallout)(CFRunLoopObserverRef, CFOptionFlags, void *);

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
extern int CFRunLoopRunInMode(CFStringRef mode, double seconds,
	unsigned char returnAfterSource);
extern void CFRelease(CFTypeRef value);
extern int puts(const char *text);

enum {
	CFRunLoopEntry = 1ull << 0,
	CFRunLoopBeforeTimers = 1ull << 1,
	CFRunLoopBeforeSources = 1ull << 2,
	CFRunLoopExit = 1ull << 7,
};

struct CallbackState {
	CFOptionFlags activities;
	int calls;
};
static struct CallbackState repeating;
static struct CallbackState oneShot;

static void repeating_callback(CFRunLoopObserverRef observer,
	CFOptionFlags activity, void *info)
{
	(void)observer;
	(void)info;
	repeating.activities |= activity;
	++repeating.calls;
}
static void one_shot_callback(CFRunLoopObserverRef observer,
	CFOptionFlags activity, void *info)
{
	(void)observer;
	(void)info;
	oneShot.activities |= activity;
	++oneShot.calls;
}

int main(void)
{
	CFStringRef mode = CFStringCreateWithCString(0,
		"kCFRunLoopDefaultMode", 0x08000100u);
	repeating = (struct CallbackState){0, 0};
	oneShot = (struct CallbackState){0, 0};
	const CFOptionFlags phases = CFRunLoopEntry | CFRunLoopBeforeTimers |
		CFRunLoopBeforeSources | CFRunLoopExit;
	CFRunLoopObserverRef repeatingObserver = CFRunLoopObserverCreate(0,
		phases, 1, 10, repeating_callback, 0);
	CFRunLoopObserverRef oneShotObserver = CFRunLoopObserverCreate(0,
		CFRunLoopEntry, 0, 0, one_shot_callback, 0);
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
		oneShot.activities == CFRunLoopEntry && oneShot.calls == 1 &&
		CFRunLoopObserverIsValid(repeatingObserver) &&
		!CFRunLoopObserverIsValid(oneShotObserver);
	CFRunLoopRemoveObserver(loop, repeatingObserver, mode);
	CFRunLoopRemoveObserver(loop, oneShotObserver, mode);
	CFRunLoopObserverInvalidate(repeatingObserver);
	CFRunLoopObserverInvalidate(oneShotObserver);
	CFRelease(repeatingObserver);
	CFRelease(oneShotObserver);
	CFRelease(mode);
	if (!passed)
		return 14;
	puts("COREFOUNDATION_GUEST_OBSERVER=PASS");
	return 0;
}
