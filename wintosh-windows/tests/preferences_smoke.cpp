/*
 * Stage 2 Darwin preferences boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_preferences.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

namespace { int observer_calls = 0; int reentrant_reads = 0; int self_removing_calls = 0; int late_observer_calls = 0; int destroying_observer_calls = 0; int external_observer_calls = 0; std::string observer_key; void Observer(const char* key, void*) { ++observer_calls; observer_key = key ? key : ""; } void LateObserver(const char*, void*) { ++late_observer_calls; } void ReentrantObserver(const char* key, void* context) { char value[64]{}; ++reentrant_reads; auto store = static_cast<darling_windows_preferences_ref>(context); darling_windows_preferences_get_string(store, key, value, sizeof value); if (reentrant_reads == 1) darling_windows_preferences_add_observer(store, LateObserver, nullptr); } void SelfRemovingObserver(const char*, void* context) { ++self_removing_calls; darling_windows_preferences_remove_observer(static_cast<darling_windows_preferences_ref>(context), SelfRemovingObserver, context); } void DestroyingObserver(const char*, void* context) { ++destroying_observer_calls; darling_windows_preferences_close(static_cast<darling_windows_preferences_ref>(context)); } void ExternalObserver(const char*, void*) { ++external_observer_calls; } }

int main() {
    const auto root = std::filesystem::temp_directory_path() / "wintosh-preferences-smoke";
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root, ec);
    _putenv_s("WINTOSH_GUEST_PREFIX", root.string().c_str());
    auto store = darling_windows_preferences_open("com.wintosh.smoke");
    if (!store) { std::cout << "OPEN=FAIL\n"; return 1; }
    const bool observer_added = darling_windows_preferences_add_observer(store, Observer, nullptr);
    const bool reentrant_observer_added = darling_windows_preferences_add_observer(store, ReentrantObserver, store);
    const bool self_removing_observer_added = darling_windows_preferences_add_observer(store, SelfRemovingObserver, store);
    if (!darling_windows_preferences_set_string(store, "Name", "Wintosh & Darling")) { std::cout << "SET_STRING=FAIL\n"; return 1; }
    if (!darling_windows_preferences_set_integer(store, "Count", 42)) { std::cout << "SET_INTEGER=FAIL\n"; return 1; }
    if (!darling_windows_preferences_set_boolean(store, "Enabled", true)) { std::cout << "SET_BOOLEAN=FAIL\n"; return 1; }
    const bool notifications_ok = observer_added && reentrant_observer_added && self_removing_observer_added && observer_calls >= 3 && reentrant_reads >= 3 && self_removing_calls == 1 && late_observer_calls == 2 && observer_key == "Enabled" &&
        darling_windows_preferences_remove_observer(store, Observer, nullptr) && darling_windows_preferences_remove_observer(store, ReentrantObserver, store) && darling_windows_preferences_remove_observer(store, LateObserver, nullptr);
    auto* destroying_store = darling_windows_preferences_open("com.wintosh.callback-destruction");
    const bool destroying_store_ok = destroying_store &&
        darling_windows_preferences_add_observer(destroying_store, DestroyingObserver, destroying_store) &&
        darling_windows_preferences_set_string(destroying_store, "Value", "deferred") &&
        destroying_observer_calls == 1;
    darling_windows_preferences_close(store);
    store = darling_windows_preferences_open("com.wintosh.smoke");
    char name[64]{}; std::int64_t count{}; bool enabled{};
    const bool ok = store && darling_windows_preferences_get_string(store, "Name", name, sizeof name) &&
        std::string(name) == "Wintosh & Darling" && darling_windows_preferences_get_integer(store, "Count", &count) && count == 42 &&
        darling_windows_preferences_get_boolean(store, "Enabled", &enabled) && enabled;
    darling_windows_preferences_close(store);
    const bool path_ok = std::filesystem::exists(root / "Users" / "Default" / "Library" / "Preferences" / "com.wintosh.smoke.plist");
    auto watcher = darling_windows_preferences_open("com.wintosh.smoke");
    const bool external_observer_added = watcher && darling_windows_preferences_add_observer(watcher, ExternalObserver, nullptr);
    bool initial_changed{}; const bool initial_poll = watcher && darling_windows_preferences_poll_external(watcher, &initial_changed) && !initial_changed;
    char queued_key[64]{}; bool queued_event{}; bool queue_drained = watcher;
    while (queue_drained && darling_windows_preferences_drain_external_event(watcher, queued_key, sizeof queued_key, &queued_event) && queued_event) {}
    auto external_writer = darling_windows_preferences_open("com.wintosh.smoke");
    const bool external_write = external_writer && darling_windows_preferences_set_string(external_writer, "QueueA", "one") &&
        darling_windows_preferences_set_string(external_writer, "External", "updated") &&
        darling_windows_preferences_set_string(external_writer, "QueueB", "two");
    darling_windows_preferences_close(external_writer);
    bool external_changed{}; char external_value[64]{};
    char first_event[64]{}, second_event[64]{}, third_event[64]{}; bool first_has_event{}, second_has_event{}, third_has_event{};
    const bool external_queue = watcher && darling_windows_preferences_drain_external_event(watcher, first_event, sizeof first_event, &first_has_event) &&
        darling_windows_preferences_drain_external_event(watcher, second_event, sizeof second_event, &second_has_event) &&
        darling_windows_preferences_drain_external_event(watcher, third_event, sizeof third_event, &third_has_event) && first_has_event && second_has_event && third_has_event &&
        std::string(first_event) == "QueueA" && std::string(second_event) == "External" && std::string(third_event) == "QueueB";
    const bool external_wait = watcher && darling_windows_preferences_wait_external(watcher, 1000, &external_changed) && external_changed && external_observer_calls >= 3 &&
        darling_windows_preferences_get_string(watcher, "External", external_value, sizeof external_value) && std::string(external_value) == "updated";
    darling_windows_preferences_close(watcher);
    auto suite = darling_windows_preferences_open_suite("com.wintosh.smoke", "shared");
    const bool suite_ok = suite && darling_windows_preferences_set_string(suite, "Shared", "suite-value");
    darling_windows_preferences_close(suite);
    auto fallback = darling_windows_preferences_open("com.wintosh.fallback");
    char fallback_value[64]{};
    const bool registration_ok = fallback && darling_windows_preferences_register_string(fallback, "Default", "registered") &&
        darling_windows_preferences_get_string(fallback, "Default", fallback_value, sizeof fallback_value) &&
        std::string(fallback_value) == "registered";
    std::int64_t registered_count{}; bool registered_enabled{};
    const bool registration_types_ok = fallback && darling_windows_preferences_register_integer(fallback, "RegisteredCount", 9) &&
        darling_windows_preferences_register_boolean(fallback, "RegisteredEnabled", true) &&
        darling_windows_preferences_get_integer(fallback, "RegisteredCount", &registered_count) && registered_count == 9 &&
        darling_windows_preferences_get_boolean(fallback, "RegisteredEnabled", &registered_enabled) && registered_enabled;
    darling_windows_preferences_close(fallback);
    std::filesystem::remove_all(root, ec);
    std::cout << "DARWIN_PREFERENCES_PERSISTENCE=" << (ok && path_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_EXTERNAL_POLL=" << (initial_poll && queue_drained && external_write && external_queue && external_wait ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_NOTIFICATIONS=" << (notifications_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_CALLBACK_DESTRUCTION=" << (destroying_store_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_DOMAINS=" << (suite_ok && registration_ok && registration_types_ok ? "PASS" : "FAIL") << "\n";
    const char* arguments[] = { "app", "--Mode=command-line", "--Count=i:7", "--Enabled=b:1" };
    auto argument_store = darling_windows_preferences_open_arguments("com.wintosh.arguments", 4, arguments);
    char mode[64]{}; std::int64_t argument_count{}; bool argument_enabled{};
    const bool arguments_ok = argument_store && darling_windows_preferences_get_string(argument_store, "Mode", mode, sizeof mode) &&
        std::string(mode) == "command-line" && darling_windows_preferences_get_integer(argument_store, "Count", &argument_count) && argument_count == 7 &&
        darling_windows_preferences_get_boolean(argument_store, "Enabled", &argument_enabled) && argument_enabled;
    const bool arguments_read_only = argument_store && !darling_windows_preferences_set_string(argument_store, "Mode", "mutated") &&
        !darling_windows_preferences_remove(argument_store, "Mode") && !darling_windows_preferences_synchronize(argument_store);
    darling_windows_preferences_close(argument_store);
    std::cout << "DARWIN_PREFERENCES_ARGUMENTS=" << (arguments_ok && arguments_read_only ? "PASS" : "FAIL") << "\n";
    const auto managed_dir = root / "Library" / "Managed Preferences";
    std::filesystem::create_directories(managed_dir, ec);
    std::ofstream managed_file(managed_dir / "com.wintosh.managed.plist");
    managed_file << "<plist><dict><key>Policy</key><string>enforced</string><key>Limit</key><integer>3</integer><key>Locked</key><true></true></dict></plist>";
    managed_file.close();
    auto application_policy = darling_windows_preferences_open("com.wintosh.managed");
    const bool application_policy_ok = application_policy && darling_windows_preferences_set_string(application_policy, "Policy", "application");
    darling_windows_preferences_close(application_policy);
    auto managed = darling_windows_preferences_open_managed("com.wintosh.managed");
    char policy[64]{}; std::int64_t limit{}; bool locked{};
    const bool managed_ok = managed && darling_windows_preferences_get_string(managed, "Policy", policy, sizeof policy) &&
        std::string(policy) == "enforced" && darling_windows_preferences_get_integer(managed, "Limit", &limit) && limit == 3 &&
        darling_windows_preferences_get_boolean(managed, "Locked", &locked) && locked &&
        !darling_windows_preferences_set_string(managed, "Policy", "changed") && !darling_windows_preferences_synchronize(managed);
    darling_windows_preferences_close(managed);
    auto effective = darling_windows_preferences_open_effective("com.wintosh.managed");
    char effective_policy[64]{};
    const bool effective_ok = effective && darling_windows_preferences_get_string(effective, "Policy", effective_policy, sizeof effective_policy) &&
        std::string(effective_policy) == "enforced" && !darling_windows_preferences_set_string(effective, "Policy", "changed");
    darling_windows_preferences_close(effective);
    const auto recovery_path = root / "Users" / "Default" / "Library" / "Preferences" / "com.wintosh.recovery.plist";
    std::filesystem::create_directories(recovery_path.parent_path(), ec);
    std::ofstream recovery_main(recovery_path);
    recovery_main << "<plist><dict><key>Recovered</key><string>old</string></dict></plist>";
    recovery_main.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    std::ofstream recovery_tmp(recovery_path.wstring() + L".tmp");
    recovery_tmp << "<plist><dict><key>Recovered</key><string>yes</string></dict></plist>";
    recovery_tmp.close();
    std::filesystem::remove(recovery_path, ec);
    auto recovery = darling_windows_preferences_open("com.wintosh.recovery");
    char recovered_value[16]{};
    const bool recovery_ok = recovery && darling_windows_preferences_get_string(recovery, "Recovered", recovered_value, sizeof recovered_value) &&
        std::string(recovered_value) == "yes";
    darling_windows_preferences_close(recovery);
    std::cout << "DARWIN_PREFERENCES_MANAGED=" << (managed_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_EFFECTIVE=" << (application_policy_ok && effective_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "DARWIN_PREFERENCES_RECOVERY=" << (recovery_ok ? "PASS" : "FAIL") << "\n";
    return ok && path_ok && initial_poll && queue_drained && external_write && external_queue && external_wait && notifications_ok && destroying_store_ok && suite_ok && registration_ok && registration_types_ok && arguments_ok && arguments_read_only && managed_ok && application_policy_ok && effective_ok && recovery_ok ? 0 : 1;
}
