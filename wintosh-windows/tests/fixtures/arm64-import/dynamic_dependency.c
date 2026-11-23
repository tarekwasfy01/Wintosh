/* Original Wintosh ARM64 dynamic dependency fixture; GPL-3.0-only. */

extern int write(int descriptor, const void *bytes, unsigned long length);
extern long wintosh_dynamic_nested_dependency_answer(void);

__attribute__((constructor)) static void WintoshDependencyConstructor(void)
{
	static const char marker[] = "ARM64_DEPENDENCY_INIT=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((destructor)) static void WintoshDependencyCxaDestructor(void)
{
	static const char marker[] = "ARM64_DEPENDENCY_CXA=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshDependencyModuleTerminator(void)
{
	static const char marker[] = "ARM64_DEPENDENCY_MOD_TERM=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((used, section("__DATA,__mod_term_func")))
static void (*WintoshDependencyTerminator)(void) = WintoshDependencyModuleTerminator;

long wintosh_dynamic_dependency_answer(void)
{
	return wintosh_dynamic_nested_dependency_answer() + 15;
}
