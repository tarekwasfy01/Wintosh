# Wintosh prefix-relative preferences subset

Wintosh now provides a small native Windows preferences store for the first
Foundation-family integration step. Its public C ABI is declared in
`wintosh-windows/frameworks/darling_windows_preferences.h` and implemented in
the adjacent source file.

The store is deliberately named `darling_windows_preferences_*`, not
`NSUserDefaults`: it is an adapter subset and does not claim complete Apple
Foundation compatibility. A domain is validated as a bundle-style identifier
(`A-Z`, `a-z`, digits, `.`, `-`, `_`) and is persisted below:

`<WINTOSH_GUEST_PREFIX>/Users/Default/Library/Preferences/<domain>.plist`

The current value families are UTF-8 strings, signed 64-bit integers, and
booleans. Writes use a temporary file followed by replacement/rename and are
serialized per store instance. XML escaping, reload after close, removal, and
missing-key behavior are covered by `wintosh_preferences_smoke`.

This does not yet implement suites, registration domains, argument domains,
defaults notification delivery, managed preferences, binary-plist output, or
the Objective-C `NSUserDefaults` class ABI. Those remain separate work items.

The directory placement follows Apple's documented Library/Preferences role:
[File System Programming Guide](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/MacOSXDirectories/MacOSXDirectories.html).
