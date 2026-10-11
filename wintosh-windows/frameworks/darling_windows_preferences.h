/* Wintosh prefix-relative preferences subset; MIT, see LICENSE-WINTOSH-ORIGINAL-MIT.txt. */
#pragma once
#include <cstddef>
#include <cstdint>
struct darling_windows_preferences;
using darling_windows_preferences_ref = darling_windows_preferences*;
extern "C" darling_windows_preferences_ref darling_windows_preferences_open(const char* domain);
extern "C" void darling_windows_preferences_close(darling_windows_preferences_ref store);
extern "C" bool darling_windows_preferences_get_string(darling_windows_preferences_ref, const char*, char*, std::size_t);
extern "C" bool darling_windows_preferences_get_integer(darling_windows_preferences_ref, const char*, std::int64_t*);
extern "C" bool darling_windows_preferences_get_boolean(darling_windows_preferences_ref, const char*, bool*);
extern "C" bool darling_windows_preferences_set_string(darling_windows_preferences_ref, const char*, const char*);
extern "C" bool darling_windows_preferences_set_integer(darling_windows_preferences_ref, const char*, std::int64_t);
extern "C" bool darling_windows_preferences_set_boolean(darling_windows_preferences_ref, const char*, bool);
extern "C" bool darling_windows_preferences_remove(darling_windows_preferences_ref, const char*);
extern "C" bool darling_windows_preferences_synchronize(darling_windows_preferences_ref);
