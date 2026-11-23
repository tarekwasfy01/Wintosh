/* Original Wintosh ARM64 nested dependency fixture; GPL-3.0-only. */

extern int write(int descriptor, const void *bytes, unsigned long length);

__attribute__((constructor)) static void WintoshNestedDependencyConstructor(void)
{
	static const char marker[] = "ARM64_NESTED_DEPENDENCY_INIT=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((destructor)) static void WintoshNestedDependencyDestructor(void)
{
	static const char marker[] = "ARM64_NESTED_DEPENDENCY_CXA=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshNestedDependencyModuleTerminator(void)
{
	static const char marker[] = "ARM64_NESTED_DEPENDENCY_MOD_TERM=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((used, section("__DATA,__mod_term_func")))
static void (*WintoshNestedDependencyTerminator)(void) =
	WintoshNestedDependencyModuleTerminator;

long wintosh_dynamic_nested_dependency_answer(void)
{
	return 15;
}
