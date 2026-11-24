/* Wintosh original ARM64 Darwin guest fixture; GPL-3.0-only. */
typedef long CFIndex;
typedef int CFNumberType;
typedef struct CFRange { CFIndex location; CFIndex length; } CFRange;
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
extern CFStringRef CFStringCreateWithCharacters(const void *, const unsigned short *, CFIndex);
extern CFStringRef CFStringCreateCopy(const void *, CFStringRef);
extern CFIndex CFStringGetLength(CFStringRef);
extern unsigned char CFStringGetCString(CFStringRef, char *, CFIndex, unsigned int);
extern unsigned short CFStringGetCharacterAtIndex(CFStringRef, CFIndex);
extern unsigned char CFStringGetCharacters(CFStringRef, CFRange, unsigned short *);
extern CFIndex CFStringGetMaximumSizeForEncoding(CFIndex, unsigned int);
extern CFIndex CFStringGetMaximumSizeOfFileSystemRepresentation(CFStringRef);
extern unsigned char CFStringGetFileSystemRepresentation(CFStringRef, char *, CFIndex);
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
extern CFStringRef CFStringCreateWithCString(const void *, const char *,
	unsigned int);
extern CFStringRef CFCopyDescription(const void *);
extern CFTypeRef CFNumberCreate(const void *, CFNumberType, const void *);
extern unsigned char CFNumberGetValue(CFTypeRef, CFNumberType, void *);
extern unsigned char CFNumberIsFloatType(CFTypeRef);
extern long CFNumberCompare(CFTypeRef, CFTypeRef, const void *);
extern CFTypeRef CFDateCreate(const void *, double);
extern double CFDateGetAbsoluteTime(CFTypeRef);
extern long CFDateCompare(CFTypeRef, CFTypeRef, const void *);
extern double CFDateGetTimeIntervalSinceDate(CFTypeRef, CFTypeRef);
extern CFTypeRef CFArrayCreate(const void *, const void *const *, CFIndex,
	const CFArrayCallBacks *);
extern CFTypeRef CFArrayCreateMutable(const void *, CFIndex,
	const CFArrayCallBacks *);
extern void CFArrayAppendValue(CFTypeRef, CFTypeRef);
extern void CFArrayInsertValueAtIndex(CFTypeRef, CFIndex, CFTypeRef);
extern void CFArraySetValueAtIndex(CFTypeRef, CFIndex, CFTypeRef);
extern void CFArrayRemoveValueAtIndex(CFTypeRef, CFIndex);
extern void CFArrayRemoveAllValues(CFTypeRef);
extern void CFArrayReplaceValues(CFTypeRef, CFRange, const void *const *, CFIndex);
extern void CFArrayAppendArray(CFTypeRef, CFTypeRef, CFRange);
extern const void *CFArrayGetValueAtIndex(CFTypeRef, CFIndex);
extern unsigned char CFArrayContainsValue(CFTypeRef, CFRange, CFTypeRef);
extern CFIndex CFArrayGetCountOfValue(CFTypeRef, CFRange, CFTypeRef);
extern CFIndex CFArrayGetFirstIndexOfValue(CFTypeRef, CFRange, CFTypeRef);
extern CFIndex CFArrayGetLastIndexOfValue(CFTypeRef, CFRange, CFTypeRef);
extern void CFArrayGetValues(CFTypeRef, CFRange, const void **);
extern void CFArrayApplyFunction(CFTypeRef, CFRange,
	void (*)(const void *, void *), void *);
extern void CFArraySortValues(CFTypeRef, CFRange,
	int (*)(const void *, const void *, void *), void *);
extern CFIndex CFArrayBSearchValues(CFTypeRef, CFRange, CFTypeRef,
	int (*)(const void *, const void *, void *), void *);
extern CFIndex CFArrayGetCount(CFTypeRef);
extern CFTypeRef CFDictionaryCreate(const void *, const void *const *,
	const void *const *, CFIndex, const CFDictionaryKeyCallBacks *,
	const CFDictionaryValueCallBacks *);
extern CFIndex CFDictionaryGetCount(CFTypeRef);
extern CFTypeRef CFDictionaryCreateMutable(const void *, CFIndex,
	const CFDictionaryKeyCallBacks *, const CFDictionaryValueCallBacks *);
extern const void *CFDictionaryGetValue(CFTypeRef, const void *);
extern unsigned char CFDictionaryContainsKey(CFTypeRef, const void *);
extern void CFDictionarySetValue(CFTypeRef, const void *, const void *);
extern void CFDictionaryRemoveValue(CFTypeRef, const void *);
extern void CFDictionaryRemoveAllValues(CFTypeRef);
extern CFTypeRef CFDictionaryCreateCopy(const void *, CFTypeRef);
extern CFTypeRef CFDictionaryCreateMutableCopy(const void *, CFIndex, CFTypeRef);
extern unsigned char CFDictionaryGetValueIfPresent(CFTypeRef, const void *, const void **);
extern unsigned char CFDictionaryContainsValue(CFTypeRef, const void *);
extern void CFDictionaryGetKeysAndValues(CFTypeRef, const void **, const void **);
extern void CFDictionaryApplyFunction(CFTypeRef,
	void (*)(const void *, const void *, void *), void *);
extern CFTypeRef CFSetCreate(const void *, const void *const *, CFIndex,
	const CFDictionaryKeyCallBacks *);
extern CFIndex CFSetGetCount(CFTypeRef);
extern CFTypeRef CFSetCreateMutable(const void *, CFIndex,
	const CFDictionaryKeyCallBacks *);
extern unsigned char CFSetContainsValue(CFTypeRef, const void *);
extern void CFSetAddValue(CFTypeRef, const void *);
extern void CFSetRemoveValue(CFTypeRef, const void *);
extern void CFSetRemoveAllValues(CFTypeRef);
extern CFTypeRef CFSetCreateCopy(const void *, CFTypeRef);
extern CFTypeRef CFSetCreateMutableCopy(const void *, CFIndex, CFTypeRef);
extern const void *CFSetGetValue(CFTypeRef, const void *);
extern unsigned char CFSetGetValueIfPresent(CFTypeRef, const void *, const void **);
extern void CFSetGetValues(CFTypeRef, const void **);
extern void CFSetApplyFunction(CFTypeRef, void (*)(const void *, void *), void *);

static int scheduleCalls;
static int cancelCalls;
static int performCalls;
static int contextInfo;
static int arrayValueA;
static int arrayValueB;
static int arrayRetainCalls;
static int arrayReleaseCalls;
static int arrayDescriptionCalls;
static int arrayApplyCalls;
static int arrayApplySum;
static int arrayCompareCalls;
static int dictionaryApplyCalls;
static int dictionaryApplySum;
static int setApplyCalls;
static int setApplySum;
static int dictionaryKeyRetainCalls;
static int dictionaryKeyReleaseCalls;
static int dictionaryValueRetainCalls;
static int dictionaryValueReleaseCalls;
static int dictionaryEqualCalls;
static int dictionaryKeyDescriptionCalls;
static int dictionaryValueDescriptionCalls;
static int setRetainCalls;
static int setReleaseCalls;
static int setEqualCalls;
static int setHashCalls;
static int setDescriptionCalls;
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
static const void *array_copy_description(const void *value)
{
	if (value == &arrayValueA || value == &arrayValueB) ++arrayDescriptionCalls;
	return CFStringCreateWithCString(0, "array-item", 0x08000100u);
}
static void array_apply(const void *value, void *context)
{
	if (context != 0 && value != 0) {
		++arrayApplyCalls;
	*(int *)context += *(const int *)value;
		arrayApplySum += *(const int *)value;
	}
}
static int array_compare(const void *left, const void *right, void *context)
{
	(void)context;
	++arrayCompareCalls;
	const int leftValue = *(const int *)left;
	const int rightValue = *(const int *)right;
	return leftValue < rightValue ? -1 : leftValue > rightValue ? 1 : 0;
}
static void dictionary_apply(const void *key, const void *value, void *context)
{
	if (context != 0 && key != 0 && value != 0) {
		++dictionaryApplyCalls;
		*(int *)context += *(const int *)key + *(const int *)value;
		dictionaryApplySum += *(const int *)key + *(const int *)value;
	}
}
static void set_apply(const void *value, void *context)
{
	if (context != 0 && value != 0) {
		++setApplyCalls;
		*(int *)context += *(const int *)value;
		setApplySum += *(const int *)value;
	}
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
static const void *dictionary_key_description(const void *value)
{
	if (value == &dictionaryKeyA || value == &dictionaryKeyB)
		++dictionaryKeyDescriptionCalls;
	return CFStringCreateWithCString(0, "dictionary-key", 0x08000100u);
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
static const void *dictionary_value_description(const void *value)
{
	if (value == &dictionaryValueA || value == &dictionaryValueB)
		++dictionaryValueDescriptionCalls;
	return CFStringCreateWithCString(0, "dictionary-value", 0x08000100u);
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
static const void *set_description(const void *value)
{
	if (value == &setValueA || value == &setValueB) ++setDescriptionCalls;
	return CFStringCreateWithCString(0, "set-value", 0x08000100u);
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
	const unsigned int utf8Encoding = 0x08000100u;
	CFStringRef sourceString = CFStringCreateWithCString(0, "Darwin", utf8Encoding);
	char stringBuffer[16] = { 0 };
	unsigned short characters[6] = { 0 };
	if (sourceString == 0 || CFStringGetLength(sourceString) != 6 ||
		CFStringGetCharacterAtIndex(sourceString, 0) != 'D' ||
		!CFStringGetCString(sourceString, stringBuffer, sizeof(stringBuffer), utf8Encoding) ||
		stringBuffer[0] != 'D' || stringBuffer[5] != 'n' ||
		!CFStringGetCharacters(sourceString, (CFRange){ 0, 6 }, characters) ||
		characters[0] != 'D' || characters[5] != 'n' ||
		CFStringGetMaximumSizeForEncoding(6, utf8Encoding) < 6) return 61;
	CFStringRef characterString = CFStringCreateWithCharacters(0, characters, 6);
	CFStringRef copiedString = CFStringCreateCopy(0, sourceString);
	if (characterString == 0 || copiedString == 0 ||
		!CFStringGetCString(characterString, stringBuffer, sizeof(stringBuffer), utf8Encoding) ||
		stringBuffer[0] != 'D' ||
		!CFStringGetCString(copiedString, stringBuffer, sizeof(stringBuffer), utf8Encoding) ||
		stringBuffer[5] != 'n') return 62;
	char fileSystemBuffer[16] = { 0 };
	if (CFStringGetMaximumSizeOfFileSystemRepresentation(sourceString) != 7 ||
		!CFStringGetFileSystemRepresentation(sourceString, fileSystemBuffer,
			sizeof(fileSystemBuffer)) || fileSystemBuffer[5] != 'n') return 63;
	CFRelease(characterString);
	CFRelease(copiedString);
	CFRelease(sourceString);
	CFTypeRef laterDate = CFDateCreate(0, 123.25);
	CFTypeRef earlierDate = CFDateCreate(0, 120.0);
	const double dateDelta = CFDateGetTimeIntervalSinceDate(laterDate, earlierDate);
	if (laterDate == 0 || earlierDate == 0 ||
		CFDateGetAbsoluteTime(laterDate) != 123.25 ||
		CFDateCompare(laterDate, earlierDate, 0) <= 0 ||
		dateDelta < 3.249 || dateDelta > 3.251) return 60;
	CFRelease(laterDate);
	CFRelease(earlierDate);
	int integerValue = 40;
	double realValue = 40.5;
	CFTypeRef integerNumber = CFNumberCreate(0, 9, &integerValue);
	CFTypeRef realNumber = CFNumberCreate(0, 6, &realValue);
	long long convertedInteger = 0;
	if (integerNumber == 0 || realNumber == 0 ||
		!CFNumberGetValue(integerNumber, 4, &convertedInteger) ||
		convertedInteger != 40 || CFNumberIsFloatType(integerNumber) ||
		!CFNumberIsFloatType(realNumber) ||
		CFNumberCompare(integerNumber, realNumber, 0) >= 0) return 59;
	CFRelease(integerNumber);
	CFRelease(realNumber);
	arrayValueA = 2;
	arrayValueB = 1;
	dictionaryKeyA = 3;
	dictionaryKeyB = 8;
	dictionaryValueA = 5;
	dictionaryValueB = 4;
	setValueA = 9;
	setValueB = 10;
	const void *arrayValues[] = { &arrayValueA, &arrayValueB };
	CFArrayCallBacks arrayCallbacks = { 0, array_retain, array_release,
		array_copy_description, 0 };
	CFTypeRef array = CFArrayCreate(0, arrayValues, 2, &arrayCallbacks);
	if (array == 0 || CFArrayGetCount(array) != 2 || arrayRetainCalls != 2)
		return 17;
	CFTypeRef arrayDescription = CFCopyDescription(array);
	if (arrayDescription == 0 || arrayDescriptionCalls != 2) return 23;
	CFRelease(arrayDescription);
	CFRelease(array);
	if (arrayReleaseCalls != 2) return 18;
	CFTypeRef mutableArray = CFArrayCreateMutable(0, 0, &arrayCallbacks);
	if (mutableArray == 0) return 36;
	CFArrayAppendValue(mutableArray, &arrayValueA);
	CFArrayInsertValueAtIndex(mutableArray, 0, &arrayValueB);
	const void *arraySnapshot[2] = { 0, 0 };
	CFArrayGetValues(mutableArray, (CFRange){ 0, 2 }, arraySnapshot);
	if (CFArrayGetCount(mutableArray) != 2 || arraySnapshot[0] != &arrayValueB ||
		arraySnapshot[1] != &arrayValueA ||
		!CFArrayContainsValue(mutableArray, (CFRange){ 0, 2 }, &arrayValueA))
		return 37;
	CFArraySetValueAtIndex(mutableArray, 1, &arrayValueB);
	if (CFArrayGetValueAtIndex(mutableArray, 1) != &arrayValueB) return 38;
	if (CFArrayGetCountOfValue(mutableArray, (CFRange){ 0, 2 }, &arrayValueB) != 2 ||
		CFArrayGetFirstIndexOfValue(mutableArray, (CFRange){ 0, 2 }, &arrayValueB) != 0 ||
		CFArrayGetLastIndexOfValue(mutableArray, (CFRange){ 0, 2 }, &arrayValueB) != 1)
		return 41;
	const void *replacementValues[] = { &arrayValueA };
	CFArrayReplaceValues(mutableArray, (CFRange){ 0, 1 }, replacementValues, 1);
	if (CFArrayGetValueAtIndex(mutableArray, 0) != &arrayValueA ||
		CFArrayGetCount(mutableArray) != 2) return 42;
	CFTypeRef sourceArray = CFArrayCreate(0, arrayValues, 2, &arrayCallbacks);
	if (sourceArray == 0) return 43;
	CFArrayAppendArray(mutableArray, sourceArray, (CFRange){ 1, 1 });
	CFRelease(sourceArray);
	if (CFArrayGetCount(mutableArray) != 3 ||
		CFArrayGetValueAtIndex(mutableArray, 2) != &arrayValueB) return 44;
	CFArraySortValues(mutableArray, (CFRange){ 0, 3 }, array_compare, 0);
	if (CFArrayGetValueAtIndex(mutableArray, 0) != &arrayValueB ||
		CFArrayGetValueAtIndex(mutableArray, 1) != &arrayValueB ||
		CFArrayGetValueAtIndex(mutableArray, 2) != &arrayValueA ||
		arrayCompareCalls == 0) return 54;
	if (CFArrayBSearchValues(mutableArray, (CFRange){ 0, 3 }, &arrayValueA,
		array_compare, 0) != 2) return 55;
	int appliedSum = 0;
	CFArrayApplyFunction(mutableArray, (CFRange){ 0, 3 }, array_apply, &appliedSum);
	if (arrayApplyCalls != 3 || appliedSum != 4 || arrayApplySum != 4) return 56;
	CFArrayRemoveValueAtIndex(mutableArray, 0);
	CFArrayAppendValue(mutableArray, &arrayValueA);
	CFArrayRemoveAllValues(mutableArray);
	if (CFArrayGetCount(mutableArray) != 0) return 39;
	CFRelease(mutableArray);
	if (arrayRetainCalls != arrayReleaseCalls) return 40;
	const void *dictionaryKeys[] = { &dictionaryKeyA, &dictionaryKeyB };
	const void *dictionaryValues[] = { &dictionaryValueA, &dictionaryValueB };
	CFDictionaryKeyCallBacks keyCallbacks = { 0, dictionary_key_retain,
		dictionary_key_release, dictionary_key_description, dictionary_key_equal,
		dictionary_key_hash };
	CFDictionaryValueCallBacks valueCallbacks = { 0, dictionary_value_retain,
		dictionary_value_release, dictionary_value_description };
	CFTypeRef dictionary = CFDictionaryCreate(0, dictionaryKeys, dictionaryValues,
		2, &keyCallbacks, &valueCallbacks);
	if (dictionary == 0 || CFDictionaryGetCount(dictionary) != 2 ||
		dictionaryKeyRetainCalls != 2 || dictionaryValueRetainCalls != 2 ||
		dictionaryEqualCalls != 1) return 19;
	CFTypeRef dictionaryDescription = CFCopyDescription(dictionary);
	if (dictionaryDescription == 0 || dictionaryKeyDescriptionCalls != 2 ||
		dictionaryValueDescriptionCalls != 2) return 24;
	CFRelease(dictionaryDescription);
	CFRelease(dictionary);
	if (dictionaryKeyReleaseCalls != 2 || dictionaryValueReleaseCalls != 2)
		return 20;
	CFTypeRef mutableDictionary = CFDictionaryCreateMutable(0, 0,
		&keyCallbacks, &valueCallbacks);
	if (mutableDictionary == 0) return 26;
	CFDictionarySetValue(mutableDictionary, &dictionaryKeyA, &dictionaryValueA);
	if (CFDictionaryGetCount(mutableDictionary) != 1 ||
		!CFDictionaryContainsKey(mutableDictionary, &dictionaryKeyA) ||
		CFDictionaryGetValue(mutableDictionary, &dictionaryKeyA) != &dictionaryValueA)
		return 27;
	CFDictionarySetValue(mutableDictionary, &dictionaryKeyA, &dictionaryValueB);
	if (CFDictionaryGetValue(mutableDictionary, &dictionaryKeyA) != &dictionaryValueB)
		return 28;
	CFDictionaryRemoveValue(mutableDictionary, &dictionaryKeyA);
	if (CFDictionaryGetCount(mutableDictionary) != 0) return 29;
	CFDictionarySetValue(mutableDictionary, &dictionaryKeyA, &dictionaryValueA);
	CFDictionaryRemoveAllValues(mutableDictionary);
	if (CFDictionaryGetCount(mutableDictionary) != 0) return 30;
	CFDictionarySetValue(mutableDictionary, &dictionaryKeyA, &dictionaryValueB);
	int dictionaryApplyTotal = 0;
	CFDictionaryApplyFunction(mutableDictionary, dictionary_apply,
		&dictionaryApplyTotal);
	if (dictionaryApplyCalls != 1 || dictionaryApplyTotal != 7 ||
		dictionaryApplySum != 7) return 57;
	const void *dictionaryOutputKey = 0, *dictionaryOutputValue = 0;
	if (!CFDictionaryGetValueIfPresent(mutableDictionary, &dictionaryKeyA,
		&dictionaryOutputValue) || dictionaryOutputValue != &dictionaryValueB ||
		!CFDictionaryContainsValue(mutableDictionary, &dictionaryValueB)) return 45;
	CFDictionaryGetKeysAndValues(mutableDictionary, &dictionaryOutputKey,
		&dictionaryOutputValue);
	if (dictionaryOutputKey != &dictionaryKeyA ||
		dictionaryOutputValue != &dictionaryValueB) return 46;
	CFTypeRef dictionaryCopy = CFDictionaryCreateCopy(0, mutableDictionary);
	CFTypeRef mutableDictionaryCopy = CFDictionaryCreateMutableCopy(0, 0,
		mutableDictionary);
	if (dictionaryCopy == 0 || mutableDictionaryCopy == 0 ||
		CFDictionaryGetCount(dictionaryCopy) != 1 ||
		CFDictionaryGetCount(mutableDictionaryCopy) != 1) return 47;
	CFDictionaryRemoveValue(mutableDictionary, &dictionaryKeyA);
	if (CFDictionaryGetCount(dictionaryCopy) != 1 ||
		CFDictionaryGetCount(mutableDictionaryCopy) != 1) return 48;
	CFRelease(dictionaryCopy);
	CFRelease(mutableDictionaryCopy);
	CFDictionaryRemoveAllValues(mutableDictionary);
	CFRelease(mutableDictionary);
	if (dictionaryKeyRetainCalls != dictionaryKeyReleaseCalls ||
		dictionaryValueRetainCalls != dictionaryValueReleaseCalls) return 49;
	const void *setValues[] = { &setValueA, &setValueB };
	CFDictionaryKeyCallBacks setCallbacks = { 0, set_retain, set_release, 0,
		set_equal, set_hash };
	setCallbacks.copyDescription = set_description;
	CFTypeRef set = CFSetCreate(0, setValues, 2, &setCallbacks);
	if (set == 0 || CFSetGetCount(set) != 2 || setRetainCalls != 2 ||
		setHashCalls != 2 || setEqualCalls != 1) return 21;
	CFTypeRef setDescription = CFCopyDescription(set);
	if (setDescription == 0 || setDescriptionCalls != 2) return 25;
	CFRelease(setDescription);
	CFRelease(set);
	if (setReleaseCalls != 2) return 22;
	CFTypeRef mutableSet = CFSetCreateMutable(0, 0, &setCallbacks);
	if (mutableSet == 0) return 31;
	CFSetAddValue(mutableSet, &setValueA);
	if (CFSetGetCount(mutableSet) != 1 ||
		!CFSetContainsValue(mutableSet, &setValueA)) return 32;
	CFSetAddValue(mutableSet, &setValueA);
	if (CFSetGetCount(mutableSet) != 1) return 33;
	CFSetRemoveValue(mutableSet, &setValueA);
	if (CFSetGetCount(mutableSet) != 0) return 34;
	CFSetAddValue(mutableSet, &setValueA);
	CFSetRemoveAllValues(mutableSet);
	if (CFSetGetCount(mutableSet) != 0) return 35;
	CFSetAddValue(mutableSet, &setValueA);
	int setApplyTotal = 0;
	CFSetApplyFunction(mutableSet, set_apply, &setApplyTotal);
	if (setApplyCalls != 1 || setApplyTotal != 9 || setApplySum != 9) return 58;
	const void *setOutput[1] = { 0 };
	const void *setOutputValue = 0;
	CFSetGetValues(mutableSet, setOutput);
	if (setOutput[0] != &setValueA || CFSetGetValue(mutableSet, &setValueA) !=
		&setValueA || !CFSetGetValueIfPresent(mutableSet, &setValueA,
		&setOutputValue) || setOutputValue != &setValueA) return 50;
	CFTypeRef setCopy = CFSetCreateCopy(0, mutableSet);
	CFTypeRef mutableSetCopy = CFSetCreateMutableCopy(0, 0, mutableSet);
	if (setCopy == 0 || mutableSetCopy == 0 || CFSetGetCount(setCopy) != 1 ||
		CFSetGetCount(mutableSetCopy) != 1) return 51;
	CFSetRemoveValue(mutableSet, &setValueA);
	if (CFSetGetCount(setCopy) != 1 || CFSetGetCount(mutableSetCopy) != 1)
		return 52;
	CFRelease(setCopy);
	CFRelease(mutableSetCopy);
	CFSetRemoveAllValues(mutableSet);
	CFRelease(mutableSet);
	if (setRetainCalls != setReleaseCalls) return 53;
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
