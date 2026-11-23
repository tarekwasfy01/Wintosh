/* Original Wintosh ARM64 dynamic-image fixture; GPL-3.0-only. */

extern int write(int descriptor, const void *bytes, unsigned long length);

__attribute__((constructor)) static void WintoshDynamicConstructor(void)
{
	static const char marker[] = "ARM64_DYLIB_INIT=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((destructor)) static void WintoshDynamicCxaDestructor(void)
{
	static const char marker[] = "ARM64_DYLIB_CXA=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshDynamicModuleTerminator(void)
{
	static const char marker[] = "ARM64_DYLIB_MOD_TERM=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((used, section("__DATA,__mod_term_func")))
static void (*WintoshDynamicTerminator)(void) = WintoshDynamicModuleTerminator;

long wintosh_dynamic_answer(void)
{
	return 42;
}
