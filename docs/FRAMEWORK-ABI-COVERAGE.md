# Framework ABI coverage

This document records a source-level declaration/definition audit for the
minimal Windows framework bridge. It is a coverage check, not a claim of
Darwin behavioral compatibility.

## Current audit

The audit compares `darling_windows_*` declarations in each framework header
with corresponding definitions in the implementation source:

| Header | Declared C-ABI functions | Missing definitions |
| --- | ---: | ---: |
| `wintosh-windows/frameworks/darling_windows_corefoundation.h` | 90 | 0 |
| `wintosh-windows/frameworks/darling_windows_foundation.h` | 7 | 0 |
| `wintosh-windows/frameworks/darling_windows_coregraphics.h` | 58 | 0 |

The audit was performed against the checked-out sources on 2026-10-09 using
the project-prefixed exported names. A zero missing-definition count means
that the declared bridge entry points have implementations; it does not prove
that every argument convention, callback, ownership rule, Unicode behavior,
framework class, or application-level API is compatible with macOS.

Re-run the audit from the repository root with:

```powershell
.\tools\Check-FrameworkAbiCoverage.ps1
```

## Remaining behavioral boundary

The bridge remains a deliberately small C-ABI adapter. CoreFoundation
collection, string, data, number, date, URL, notification, run-loop, and
property-list primitives are covered by smoke tests, while complete Foundation
objects, Unicode normalization/collation, callback/hash semantics, complex
binary property lists, AppKit, and the full Darwin framework graph remain
open.

## Run-loop implementation slice

The current `NSRunLoop` bridge exposes current/main wrappers, its underlying
CF loop, default/common modes, bounded run methods, and ordered selector
delivery. The internal callback queue retains mode-tagged work and leaves it
pending while another mode is active. This covers only scheduled callbacks:
the `CFRunLoopSource`, `CFRunLoopTimer`, and `CFRunLoopObserver` object graphs,
native Windows input-source registration, and run-loop phase observers are
still missing. Apple documents that each thread owns one run loop and that
sources, timers, and observers are activated by their registered modes; current
evidence is source/build level only.

The Mach-O host resolver exports a partial C ABI for run-loop control and
`CFString` creation/read operations plus `CFArray` type/count/index reads. The
guest ABI attributes account for the x86_64 Darwin calling convention. String
conversion is limited to the implementation's UTF-8 representation.
`CFRunLoopPerformBlock` now retains and schedules a guest block in a string or
array of modes and wakes AppKit. The resolver also exports CFNumber, CFDate,
CFBoolean, and CFDictionary create/copy/mutation/enumeration with the built-in
`kCFType` callback tables. Custom callback behavior and null callback tables
are rejected. This has source/build evidence only; guest-side ABI invocation
and status edge cases remain unverified.

References: [Apple NSRunLoop run(mode:before:)](https://developer.apple.com/documentation/foundation/runloop/run%28mode%3Abefore%3A%29),
[CFRunLoop](https://developer.apple.com/documentation/corefoundation/cfrunloop),
and [CFRunLoopPerformBlock](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock%28_%3A_%3A_%3A%29).

The implementation and source provenance are documented in the repository
license inventory. The upstream Darling project is the reference project:
https://github.com/darlinghq/darling
