/* Original Wintosh ARM64 runtime-loaded plugin fixture; GPL-3.0-only. */

extern int write(int descriptor, const void *bytes, unsigned long length);

__attribute__((constructor)) static void WintoshPluginConstructor(void)
{
	static const char marker[] = "ARM64_PLUGIN_INIT=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((destructor)) static void WintoshPluginDestructor(void)
{
	static const char marker[] = "ARM64_PLUGIN_CXA=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

static void WintoshPluginModuleTerminator(void)
{
	static const char marker[] = "ARM64_PLUGIN_MOD_TERM=PASS\n";
	(void)write(1, marker, sizeof(marker) - 1);
}

__attribute__((used, section("__DATA,__mod_term_func")))
static void (*WintoshPluginTerminator)(void) = WintoshPluginModuleTerminator;

long wintosh_dynamic_plugin_answer(void)
{
	return 15;
}
