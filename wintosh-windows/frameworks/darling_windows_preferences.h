/* Wintosh prefix-relative preferences subset; MIT, see LICENSE-WINTOSH-ORIGINAL-MIT.txt. */
#pragma once
#include <cstddef>
#include <cstdint>
struct darling_windows_preferences;
using darling_windows_preferences_ref = darling_windows_preferences*;
using darling_windows_preferences_observer = void (*)(const char* key, void* context);
extern "C" darling_windows_preferences_ref darling_windows_preferences_open(const char* domain);
extern "C" darling_windows_preferences_ref darling_windows_preferences_open_arguments(const char* domain, int argc, const char* const* argv);
extern "C" darling_windows_preferences_ref darling_windows_preferences_open_managed(const char* domain);
extern "C" darling_windows_preferences_ref darling_windows_preferences_open_effective(const char* domain);
extern "C" darling_windows_preferences_ref darling_windows_preferences_open_suite(const char* domain, const char* suite);
extern "C" void darling_windows_preferences_close(darling_windows_preferences_ref store);
extern "C" bool darling_windows_preferences_get_string(darling_windows_preferences_ref, const char*, char*, std::size_t);
extern "C" bool darling_windows_preferences_register_string(darling_windows_preferences_ref, const char*, const char*);
extern "C" bool darling_windows_preferences_register_integer(darling_windows_preferences_ref, const char*, std::int64_t);
extern "C" bool darling_windows_preferences_register_boolean(darling_windows_preferences_ref, const char*, bool);
extern "C" bool darling_windows_preferences_get_integer(darling_windows_preferences_ref, const char*, std::int64_t*);
extern "C" bool darling_windows_preferences_get_boolean(darling_windows_preferences_ref, const char*, bool*);
extern "C" bool darling_windows_preferences_set_string(darling_windows_preferences_ref, const char*, const char*);
extern "C" bool darling_windows_preferences_set_integer(darling_windows_preferences_ref, const char*, std::int64_t);
extern "C" bool darling_windows_preferences_set_boolean(darling_windows_preferences_ref, const char*, bool);
extern "C" bool darling_windows_preferences_remove(darling_windows_preferences_ref, const char*);
extern "C" bool darling_windows_preferences_synchronize(darling_windows_preferences_ref);
extern "C" bool darling_windows_preferences_add_observer(darling_windows_preferences_ref, darling_windows_preferences_observer, void* context);
extern "C" bool darling_windows_preferences_remove_observer(darling_windows_preferences_ref, darling_windows_preferences_observer, void* context);
extern "C" bool darling_windows_preferences_poll_external(darling_windows_preferences_ref, bool* changed);
extern "C" bool darling_windows_preferences_signal_external(darling_windows_preferences_ref);
extern "C" bool darling_windows_preferences_wait_external(darling_windows_preferences_ref, unsigned timeout_ms, bool* changed);
extern "C" bool darling_windows_preferences_drain_external_event(darling_windows_preferences_ref, char* key, std::size_t capacity, bool* has_event);
