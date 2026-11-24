/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef void *CFTypeRef;
typedef void *CFStringRef;
typedef void *CFRunLoopRef;
typedef void *CFRunLoopSourceRef;
typedef struct CFArrayCallBacks {
	CFIndex version;
	const void *(*retain)(const void *, const void *);
	void (*release)(const void *, const void *);
	const void *(*copyDescription)(const void *);
	unsigned char (*equal)(const void *, const void *);
} CFArrayCallBacks;
typedef struct CFDictionaryKeyCallBacks {
	CFIndex version;
	const void *(*retain)(const void *, const void *);
	void (*release)(const void *, const void *);
	const void *(*copyDescription)(const void *);
	unsigned char (*equal)(const void *, const void *);
	unsigned long (*hash)(const void *);
} CFDictionaryKeyCallBacks;
typedef struct CFDictionaryValueCallBacks {
	CFIndex version;
	const void *(*retain)(const void *, const void *);
	void (*release)(const void *, const void *);
	const void *(*copyDescription)(const void *);
} CFDictionaryValueCallBacks;
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
extern CFTypeRef CFArrayCreate(const void *, const void *const *, CFIndex,
	const CFArrayCallBacks *);
extern CFIndex CFArrayGetCount(CFTypeRef);
extern CFTypeRef CFDictionaryCreate(const void *, const void *const *,
	const void *const *, CFIndex, const CFDictionaryKeyCallBacks *,
	const CFDictionaryValueCallBacks *);
extern CFIndex CFDictionaryGetCount(CFTypeRef);
extern CFTypeRef CFSetCreate(const void *, const void *const *, CFIndex,
	const CFDictionaryKeyCallBacks *);
extern CFIndex CFSetGetCount(CFTypeRef);

static int scheduleCalls;
static int cancelCalls;
static int performCalls;
static int contextInfo;
static int arrayValueA;
static int arrayValueB;
static int arrayRetainCalls;
static int arrayReleaseCalls;
static int dictionaryKeyRetainCalls;
static int dictionaryKeyReleaseCalls;
static int dictionaryValueRetainCalls;
static int dictionaryValueReleaseCalls;
static int dictionaryEqualCalls;
static int setRetainCalls;
static int setReleaseCalls;
static int setEqualCalls;
static int setHashCalls;
static int dictionaryKeyA, dictionaryKeyB;
static int dictionaryValueA, dictionaryValueB;
static int setValueA, setValueB;

static const void *array_retain(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &arrayValueA || value == &arrayValueB) ++arrayRetainCalls;
	return value;
}

static void array_release(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &arrayValueA || value == &arrayValueB) ++arrayReleaseCalls;
}

static const void *dictionary_key_retain(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &dictionaryKeyA || value == &dictionaryKeyB)
		++dictionaryKeyRetainCalls;
	return value;
}
static void dictionary_key_release(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &dictionaryKeyA || value == &dictionaryKeyB)
		++dictionaryKeyReleaseCalls;
}
static unsigned char dictionary_key_equal(const void *left, const void *right)
{
	++dictionaryEqualCalls;
	return left == right;
}
static unsigned long dictionary_key_hash(const void *value)
{
	return (unsigned long)(unsigned long long)value;
}
static const void *dictionary_value_retain(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &dictionaryValueA || value == &dictionaryValueB)
		++dictionaryValueRetainCalls;
	return value;
}
static void dictionary_value_release(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &dictionaryValueA || value == &dictionaryValueB)
		++dictionaryValueReleaseCalls;
}
static const void *set_retain(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &setValueA || value == &setValueB) ++setRetainCalls;
	return value;
}
static void set_release(const void *allocator, const void *value)
{
	(void)allocator;
	if (value == &setValueA || value == &setValueB) ++setReleaseCalls;
}
static unsigned char set_equal(const void *left, const void *right)
{
	++setEqualCalls;
	return left == right;
}
static unsigned long set_hash(const void *value)
{
	(void)value;
	++setHashCalls;
	return 1;
}

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
	const void *arrayValues[] = { &arrayValueA, &arrayValueB };
	CFArrayCallBacks arrayCallbacks = { 0, array_retain, array_release, 0, 0 };
	CFTypeRef array = CFArrayCreate(0, arrayValues, 2, &arrayCallbacks);
	if (array == 0 || CFArrayGetCount(array) != 2 || arrayRetainCalls != 2)
		return 17;
	CFRelease(array);
	if (arrayReleaseCalls != 2) return 18;
	const void *dictionaryKeys[] = { &dictionaryKeyA, &dictionaryKeyB };
	const void *dictionaryValues[] = { &dictionaryValueA, &dictionaryValueB };
	CFDictionaryKeyCallBacks keyCallbacks = { 0, dictionary_key_retain,
		dictionary_key_release, 0, dictionary_key_equal, dictionary_key_hash };
	CFDictionaryValueCallBacks valueCallbacks = { 0, dictionary_value_retain,
		dictionary_value_release, 0 };
	CFTypeRef dictionary = CFDictionaryCreate(0, dictionaryKeys, dictionaryValues,
		2, &keyCallbacks, &valueCallbacks);
	if (dictionary == 0 || CFDictionaryGetCount(dictionary) != 2 ||
		dictionaryKeyRetainCalls != 2 || dictionaryValueRetainCalls != 2 ||
		dictionaryEqualCalls != 1) return 19;
	CFRelease(dictionary);
	if (dictionaryKeyReleaseCalls != 2 || dictionaryValueReleaseCalls != 2)
		return 20;
	const void *setValues[] = { &setValueA, &setValueB };
	CFDictionaryKeyCallBacks setCallbacks = { 0, set_retain, set_release, 0,
		set_equal, set_hash };
	CFTypeRef set = CFSetCreate(0, setValues, 2, &setCallbacks);
	if (set == 0 || CFSetGetCount(set) != 2 || setRetainCalls != 2 ||
		setHashCalls != 2 || setEqualCalls != 1) return 21;
	CFRelease(set);
	if (setReleaseCalls != 2) return 22;
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
