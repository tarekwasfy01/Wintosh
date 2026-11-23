/* Original Wintosh Objective-C fixture; GPL-3.0-only. */

extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id receiver, SEL selector, ...);
extern unsigned long strlen(const char *value);
extern void *malloc(unsigned long bytes);
extern void *realloc(void *allocation, unsigned long bytes);
extern void free(void *allocation);
extern int write(int descriptor, const void *bytes, unsigned long length);
extern int atexit(void (*function)(void));
extern int __cxa_atexit(void (*function)(void *), void *argument, void *dso);
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern const char *dlerror(void);

typedef long (*WintoshDynamicAnswer)(void);
typedef long (*WintoshDynamicObjcSend)(id, SEL);

static void WintoshCxaAtexitMarker(void *bytes)
{
	static const char marker[] = "ARM64_CXA_ATEXIT=PASS\n";
	(void)bytes;
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshAtexitMarker(void)
{
	static const char marker[] = "ARM64_ATEXIT=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshTerminatorMarker(void)
{
	static const char marker[] = "ARM64_TERMINATOR=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((used, section("__DATA,__mod_term_func")))
static void (*WintoshModuleTerminator)(void) = WintoshTerminatorMarker;

typedef struct {
	double x;
	double y;
} WintoshPair;

typedef struct {
	double a;
	double b;
	double c;
	double d;
} WintoshQuad;

typedef struct {
	unsigned long long a;
	unsigned long long b;
	unsigned long long c;
} WintoshTriple;

static int wintosh_initialize_order;
static int wintosh_load_stage;
static int wintosh_constructor_count;
static int wintosh_base_load_count;
static int wintosh_probe_load_count;
static int wintosh_category_load_count;

__attribute__((constructor)) static void WintoshImageConstructor(void)
{
	++wintosh_constructor_count;
	wintosh_load_stage = wintosh_load_stage == 0 ? 1 : -1;
}

__attribute__((objc_root_class))
@interface WintoshBase
+ (long)inheritedAnswer;
@end

@implementation WintoshBase
+ (void)load
{
	++wintosh_base_load_count;
	wintosh_load_stage = wintosh_load_stage == 1 ? 2 : -1;
}
+ (void)initialize
{
	wintosh_initialize_order = wintosh_initialize_order == 0 ? 1 : -1;
}
+ (long)inheritedAnswer
{
	return 42;
}
@end

@interface WintoshProbe : WintoshBase
+ (long)answer;
+ (long)add:(long)left to:(long)right;
+ (double)increment:(double)value;
+ (id)echo:(id)value;
+ (long)readValue:(const long *)value;
+ (long)sumSeven:(long)a second:(long)b third:(long)c fourth:(long)d
	 fifth:(long)e sixth:(long)f seventh:(long)g;
+ (WintoshPair)shiftPair:(WintoshPair)value;
+ (WintoshQuad)shiftQuad:(WintoshQuad)value;
+ (WintoshTriple)advanceTriple:(WintoshTriple)value;
+ (long)nestedAnswer;
+ (long)lengthPlus:(const char *)value;
+ (long)heapRoundTrip;
+ (long)initializedOrder;
+ (long)loadedOrder;
@end

@implementation WintoshProbe
+ (void)load
{
	++wintosh_probe_load_count;
	wintosh_load_stage = wintosh_load_stage == 2 ? 3 : -1;
}
+ (void)initialize
{
	wintosh_initialize_order = wintosh_initialize_order == 1 ? 2 : -1;
}
+ (long)initializedOrder
{
	return wintosh_initialize_order;
}
+ (long)loadedOrder
{
	return ((wintosh_constructor_count & 3) << 6) |
		((wintosh_base_load_count & 3) << 4) |
		((wintosh_probe_load_count & 3) << 2) |
		(wintosh_category_load_count & 3) |
		(wintosh_load_stage == 4 ? 16 : 0) +
		(wintosh_load_stage > 0 ? wintosh_load_stage * 10 : 90);
}
+ (long)answer
{
	return 42;
}
+ (long)add:(long)left to:(long)right
{
	return left + right;
}
+ (double)increment:(double)value
{
	return value + 1.0;
}
+ (id)echo:(id)value
{
	return value;
}
+ (long)readValue:(const long *)value
{
	return *value + 2;
}
+ (long)sumSeven:(long)a second:(long)b third:(long)c fourth:(long)d
	 fifth:(long)e sixth:(long)f seventh:(long)g
{
	return a + b + c + d + e + f + g;
}
+ (WintoshPair)shiftPair:(WintoshPair)value
{
	return (WintoshPair){value.x + 1.0, value.y + 2.0};
}
+ (WintoshQuad)shiftQuad:(WintoshQuad)value
{
	return (WintoshQuad){value.a + 1.0, value.b + 2.0,
		value.c + 3.0, value.d + 4.0};
}
+ (WintoshTriple)advanceTriple:(WintoshTriple)value
{
	return (WintoshTriple){value.a + 1, value.b + 2, value.c + 3};
}
+ (long)nestedAnswer
{
	SEL nested_selector = sel_registerName("answer");
	typedef long (*WintoshNestedSend)(id, SEL);
	return ((WintoshNestedSend)objc_msgSend)((id)self, nested_selector);
}
+ (long)lengthPlus:(const char *)value
{
	return (long)strlen(value) + 35;
}
+ (long)heapRoundTrip
{
	volatile long *value = (volatile long *)malloc(sizeof(long));
	if (value == (volatile long *)0)
		return 0;
	*value = 40;
	volatile long *resized = (volatile long *)realloc(
		(void *)value, 2 * sizeof(long));
	if (resized == (volatile long *)0) {
		free((void *)value);
		return 0;
	}
	value = resized;
	value[1] = 2;
	const long result = value[0] + value[1];
	free((void *)value);
	return result;
}
@end

@interface WintoshProbe (WintoshCategory)
+ (long)categoryAnswer;
@end

@implementation WintoshProbe (WintoshCategory)
+ (void)load
{
	++wintosh_category_load_count;
	wintosh_load_stage = wintosh_load_stage == 3 ? 4 : -1;
}
+ (long)categoryAnswer
{
	return 42;
}
@end

typedef long (*WintoshSendLong)(id, SEL);
typedef long (*WintoshSendLongString)(id, SEL, const char *);
typedef long (*WintoshSendLongPair)(id, SEL, long, long);
typedef double (*WintoshSendDouble)(id, SEL, double);
typedef id (*WintoshSendObject)(id, SEL, id);
typedef long (*WintoshSendLongPointer)(id, SEL, const long *);
typedef long (*WintoshSendLongSeven)(id, SEL, long, long, long, long, long,
	long, long);
typedef WintoshPair (*WintoshSendPair)(id, SEL, WintoshPair);
typedef WintoshQuad (*WintoshSendQuad)(id, SEL, WintoshQuad);
typedef WintoshTriple (*WintoshSendTriple)(id, SEL, WintoshTriple);

int main(int argc, char **argv, char **envp)
{
	if (atexit(WintoshAtexitMarker) != 0)
		return 3;
	if (__cxa_atexit(WintoshCxaAtexitMarker, (void *)0, (void *)0) != 0)
		return 4;
	(void)argc;
	(void)argv;
	(void)envp;
	if (argc < 2 || argv[1] == (char *)0)
		return 5;
	void *dynamic_image = dlopen(
		"@rpath/libarm64-dynamic-import.dylib", 2);
	if (dynamic_image == (void *)0) {
		const char *error = dlerror();
		if (error != (const char *)0)
			(void)write(2, error, strlen(error));
		return 6;
	}
	void *dynamic_image_second_reference = dlopen(
		"@rpath/libarm64-dynamic-import.dylib", 2);
	if (dynamic_image_second_reference != dynamic_image)
		return 10;
	WintoshDynamicAnswer dynamic_answer =
		(WintoshDynamicAnswer)dlsym(dynamic_image, "wintosh_dynamic_answer");
	WintoshDynamicAnswer dynamic_answer_second_reference =
		(WintoshDynamicAnswer)dlsym(dynamic_image_second_reference,
			"wintosh_dynamic_answer");
	if (dynamic_answer == (WintoshDynamicAnswer)0 || dynamic_answer() != 42 ||
		dynamic_answer_second_reference != dynamic_answer ||
		dynamic_answer_second_reference() != 42)
		return 7;
	Class dynamic_probe = objc_getClass("WintoshDynamicLoadProbe");
	SEL dynamic_answer_selector = sel_registerName("dynamicAnswer");
	SEL dynamic_category_selector = sel_registerName("categoryAnswer");
	WintoshDynamicObjcSend dynamic_send = (WintoshDynamicObjcSend)objc_msgSend;
	if (dynamic_probe == (Class)0 || dynamic_answer_selector == (SEL)0 ||
		dynamic_category_selector == (SEL)0 ||
		dynamic_send((id)dynamic_probe, dynamic_answer_selector) != 84 ||
		dynamic_send((id)dynamic_probe, dynamic_category_selector) != 126)
		return 9;
	if (dlclose(dynamic_image) != 0 ||
		dynamic_answer_second_reference() != 42 ||
		dynamic_send((id)dynamic_probe, dynamic_answer_selector) != 84)
		return 8;
	if (dlclose(dynamic_image_second_reference) != 0 ||
		objc_getClass("WintoshDynamicLoadProbe") != (Class)0)
		return 11;
	Class probe = objc_getClass("WintoshProbe");
	SEL answer = sel_registerName("answer");
	SEL inherited_answer = sel_registerName("inheritedAnswer");
	SEL category_answer = sel_registerName("categoryAnswer");
	SEL add = sel_registerName("add:to:");
	SEL increment = sel_registerName("increment:");
	SEL echo = sel_registerName("echo:");
	SEL read_value = sel_registerName("readValue:");
	SEL sum_seven = sel_registerName(
		"sumSeven:second:third:fourth:fifth:sixth:seventh:");
	SEL shift_pair = sel_registerName("shiftPair:");
	SEL shift_quad = sel_registerName("shiftQuad:");
	SEL advance_triple = sel_registerName("advanceTriple:");
	SEL nested_answer = sel_registerName("nestedAnswer");
	SEL length_plus = sel_registerName("lengthPlus:");
	SEL heap_round_trip = sel_registerName("heapRoundTrip");
	SEL initialized_order = sel_registerName("initializedOrder");
	SEL loaded_order = sel_registerName("loadedOrder");
	if (probe == (Class)0 || answer == (SEL)0 || inherited_answer == (SEL)0 ||
		category_answer == (SEL)0 ||
		add == (SEL)0 ||
		increment == (SEL)0 || echo == (SEL)0 || read_value == (SEL)0 ||
		sum_seven == (SEL)0 || shift_pair == (SEL)0 || shift_quad == (SEL)0 ||
		advance_triple == (SEL)0 || nested_answer == (SEL)0 ||
		length_plus == (SEL)0 || heap_round_trip == (SEL)0 ||
		initialized_order == (SEL)0 || loaded_order == (SEL)0)
		return 1;
	const long value = ((WintoshSendLong)objc_msgSend)((id)probe, answer);
	const long initialized = ((WintoshSendLong)objc_msgSend)((id)probe,
		initialized_order);
	const long loaded = ((WintoshSendLong)objc_msgSend)((id)probe, loaded_order);
	if (initialized != 2)
		return 2;
	if (loaded != 125)
		return (int)loaded;
	const long inherited = ((WintoshSendLong)objc_msgSend)(
		(id)probe, inherited_answer);
	const long category = ((WintoshSendLong)objc_msgSend)(
		(id)probe, category_answer);
	const long sum = ((WintoshSendLongPair)objc_msgSend)((id)probe, add, 20, 22);
	const double incremented = ((WintoshSendDouble)objc_msgSend)(
		(id)probe, increment, 41.0);
	const id echoed = ((WintoshSendObject)objc_msgSend)((id)probe, echo, (id)probe);
	const long pointed_value = 40;
	const long dereferenced = ((WintoshSendLongPointer)objc_msgSend)(
		(id)probe, read_value, &pointed_value);
	const long seven = ((WintoshSendLongSeven)objc_msgSend)((id)probe, sum_seven,
		1, 2, 3, 4, 5, 6, 21);
	const WintoshPair pair = ((WintoshSendPair)objc_msgSend)(
		(id)probe, shift_pair, (WintoshPair){20.0, 20.0});
	const WintoshQuad quad = ((WintoshSendQuad)objc_msgSend)(
		(id)probe, shift_quad, (WintoshQuad){10.0, 10.0, 10.0, 10.0});
	const WintoshTriple triple = ((WintoshSendTriple)objc_msgSend)(
		(id)probe, advance_triple, (WintoshTriple){10, 10, 10});
	const long nested = ((WintoshSendLong)objc_msgSend)((id)probe, nested_answer);
	const long nested_length = ((WintoshSendLongString)objc_msgSend)(
		(id)probe, length_plus, "Wintosh");
	const long heap_value = ((WintoshSendLong)objc_msgSend)(
		(id)probe, heap_round_trip);
	return value == 42 && inherited == 42 &&
		category == 42 && sum == 42 &&
		incremented == 42.0 &&
		echoed == (id)probe && dereferenced == 42 && seven == 42 &&
		pair.x == 21.0 && pair.y == 22.0 && quad.a == 11.0 &&
		quad.b == 12.0 && quad.c == 13.0 && quad.d == 14.0 &&
		triple.a == 11 && triple.b == 12 && triple.c == 13 && nested == 42 &&
		nested_length == 42 && heap_value == 42 ? 42 : 1;
}
