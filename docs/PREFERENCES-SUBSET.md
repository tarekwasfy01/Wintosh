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
serialized per store instance and by a named Windows mutex for the target
domain file. XML escaping, reload after close, removal, and
missing-key behavior are covered by `wintosh_preferences_smoke`. The adapter
also has a suite-domain path (`open_suite`) and an in-memory registration
domain (`register_string`, `register_integer`, `register_boolean`): persisted
application values take precedence over registration defaults, and registration
values disappear when the store is closed.

An argument domain can be opened with `open_arguments`. It is read-only and
accepts `--Key=Value`
or `Key=Value` tokens; string values use the normal form, while integer and
boolean values can use the explicit `i:<number>` and `b:0`/`b:1` forms. The
resolution order is persisted application value, argument value, then
registration default.

Managed values are opened with `open_managed` from
`<WINTOSH_GUEST_PREFIX>/Library/Managed Preferences/<domain>.plist`. This
domain is read-only and takes precedence for callers that explicitly select
it; writes and synchronization are rejected by the same central write guard.
`open_effective` provides a read-only merged view in which managed keys
override application keys, allowing policy precedence to be tested without
making ordinary application stores read-only.

Observers can be registered and removed through the C ABI. A successful
persisted set operation reports the changed key synchronously to observers on
that store instance; callbacks must not re-enter the store. The smoke test
covers registration, delivery, and removal.

`poll_external` provides an explicit cross-process polling primitive based on
the preference file modification time. It reloads changed application values
on demand. Successful writes also signal a named Windows event per domain, and
`signal_external` exposes that signal for broker integrations; the current
`wait_external(timeout_ms)` now provides a bounded blocking wait on that
event before reloading the file; immediate callback delivery and a durable
multi-event queue are still separate work.

Successful writes also append the changed key to a per-domain `.events` sidecar.
`darling_windows_preferences_drain_external_event` removes and returns the
oldest queued key, preserving write order across processes. The event handle is
held for the lifetime of an open store, so a waiter cannot miss a signal merely
because the writer opened and closed its own handle. This remains a Wintosh
adapter rather than the complete Darwin notification-center ABI: payload
values, crash-proof transaction journals, and cross-machine delivery remain
outside this subset.

If the process is interrupted after the temporary plist is flushed but before
the destination is replaced, opening the domain can recover the `.tmp` content.
Normal replacement uses `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`; the smoke
fixture exercises the orphaned-temporary-file recovery path.

This does not yet implement argument-domain mutation/removal, crash-proof
multi-file notification transactions, or callback-safe reentrancy,
defaults notification delivery, managed preferences, binary-plist output, or
the Objective-C `NSUserDefaults` class ABI. Those remain separate work items.

The directory placement follows Apple's documented Library/Preferences role:
[File System Programming Guide](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/MacOSXDirectories/MacOSXDirectories.html).
