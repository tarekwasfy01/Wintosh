#include "darling_windows_preferences.h"
#include <filesystem>
#include <iostream>
#include <string>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "wintosh-preferences-smoke";
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root, ec);
    _putenv_s("WINTOSH_GUEST_PREFIX", root.string().c_str());
    auto store = darling_windows_preferences_open("com.wintosh.smoke");
    if (!store) { std::cout << "OPEN=FAIL\n"; return 1; }
    if (!darling_windows_preferences_set_string(store, "Name", "Wintosh & Darling")) { std::cout << "SET_STRING=FAIL\n"; return 1; }
    if (!darling_windows_preferences_set_integer(store, "Count", 42)) { std::cout << "SET_INTEGER=FAIL\n"; return 1; }
    if (!darling_windows_preferences_set_boolean(store, "Enabled", true)) { std::cout << "SET_BOOLEAN=FAIL\n"; return 1; }
    darling_windows_preferences_close(store);
    store = darling_windows_preferences_open("com.wintosh.smoke");
    char name[64]{}; std::int64_t count{}; bool enabled{};
    const bool ok = store && darling_windows_preferences_get_string(store, "Name", name, sizeof name) &&
        std::string(name) == "Wintosh & Darling" && darling_windows_preferences_get_integer(store, "Count", &count) && count == 42 &&
        darling_windows_preferences_get_boolean(store, "Enabled", &enabled) && enabled;
    darling_windows_preferences_close(store);
    const bool path_ok = std::filesystem::exists(root / "Users" / "Default" / "Library" / "Preferences" / "com.wintosh.smoke.plist");
    std::filesystem::remove_all(root, ec);
    std::cout << "DARWIN_PREFERENCES_PERSISTENCE=" << (ok && path_ok ? "PASS" : "FAIL") << "\n";
    return ok && path_ok ? 0 : 1;
}
