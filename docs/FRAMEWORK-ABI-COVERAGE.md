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
pending while another mode is active. Version-0 and version-1 `CFRunLoopSource`
creation are supported; source-1 Mach ports are polled in 10 ms active-mode
wait slices and one available message is passed to `perform`; returned replies
are sent through the Mach ABI. Custom-allocator reply deallocation and native
multi-port waits remain unsupported. Version-0 source
context/order reads, add/remove/contains, signal, validity, and invalidation
are also exported. Signaled sources enqueue one ordered callback over their
registered modes; the first eligible run loop consumes the pending signal.
`CFRunLoopTimer` create/query/mutate/invalidate and mode registration are
exported. Timers fire on the active loop thread; repeating timers advance their
scheduled date and skip missed intervals. Timer tolerance and native Windows
input-source registration remain missing. `CFRunLoopObserver` create/query/
invalidate and mode registration are exported for entry, before-timer/source,
before/after-wait, and exit callbacks; one-shot observers invalidate after
their first matching callback. Apple documents that sources, timers, and
observers are activated by their registered modes; current evidence is
source/build level only, without guest ABI fixtures.

`CFMachPort` exports the type ID, create/create-with-port, run-loop source,
port/context/validity queries, invalidation, and invalidation-callback accessors.
Its source-1 adapter routes received messages to the CFMachPort callout and
invalidates sources with the wrapper. Created ports are wrapper-owned;
create-with-port rights remain caller-owned. Uniquing and context release are
implemented, but guest callback ABI, edge-case rights accounting, and
message/reply ownership need a Mach-O fixture.

`CFAllocator` exports create/context, allocate/reallocate/deallocate,
preferred-size, type-ID, and per-thread default operations. Custom allocator
contexts retain/release their info and route guest allocation callbacks; the
default allocator uses Wintosh's guest heap. `CFAllocatorCreate` allocates its
own object through the requested backing allocator, including context
allocate/deallocate callbacks for `kCFAllocatorUseContext`. Distinct malloc-zone
behavior, complete behavior for missing callbacks, and guest ABI fixtures
remain open. Guest `CFString` (CString/characters and copy/mutable-copy/mutable
constructors), `CFData`, `CFArray`,
`CFDictionary`, `CFSet`, `CFNumber`, `CFDate`, and `CFMachPort` object blocks
use the supplied allocator and retain it through object destruction. Guest
CFURL string, filesystem-path, filesystem-path-relative-to-base, and
filesystem-representation constructors use the requested allocator for their
URL object blocks. POSIX and Windows path
styles are supported; HFS path conversion is not. The guest resolver also
exports common URL type/string/path/scheme/authority/port/directory queries,
decomposability validation,
query/fragment copies with `charactersToLeaveEscaped`, username/password,
parameter-string and strict-path queries, absolute-URL copy, and filesystem
representation. Hierarchical paths omit their authority and strict paths
report whether they are absolute. The percent-escape
replacement/addition string constructors retain their source if unchanged and
use the requested allocator for changed output. The explicit-encoding
replacement wrapper accepts UTF-8 only; the addition path also supports UTF-8
only. `CFURLGetString` returns a URL-owned cached string. STL-backed
payload storage and other CF constructors still use the host allocator.
Standard allocator constants are exported for Mach-O data-symbol resolution.

The Mach-O host resolver exports a partial C ABI for run-loop control,
`CFString` creation/copy/mutable construction and read operations, selected
CFURL constructors, and CFArray creation, copying, reads,
mutation, and synchronous callback enumeration. CFArray copies its callback
structure and allocator at creation and applies retain/release/equality
callbacks to stored values. Range queries, range append, and index exchange
are exposed. A null callback pointer is treated as a zeroed version-0 table.
Comparator sorting and binary search are exposed; binary search expects the
searched range to be sorted using the same comparator. Mutable operations require an array made by a mutable constructor. The guest
ABI attributes account for the x86_64 Darwin calling convention. String
conversion is limited to the implementation's UTF-8 representation.
`CFRunLoopPerformBlock` now retains and schedules a guest block in a string or
array of modes and wakes AppKit. `CFRunLoopGetTypeID`,
`CFRunLoopCopyCurrentMode`, and `CFRunLoopCopyAllModes` are also exported. The
current-mode query returns null while idle; both copy functions return
caller-owned CoreFoundation objects. The all-modes list is derived from common
modes, active modes, and registered blocks, sources, timers, and observers.
Run-loop handles share the CoreFoundation object header, report the run-loop
type ID through `CFGetTypeID`, and are process/thread-owned immortal objects,
so generic `CFRetain`/`CFRelease` is safe and does not destroy the loop.
`CFRunLoopRunInMode` now maps the internal loop outcome to Apple's result codes
1 (`Finished`), 2 (`Stopped`), 3 (`TimedOut`), and 4 (`HandledSource`), and the
resolver exports `CFRunLoopRun`. Empty worker modes return `Finished`; the
AppKit main loop treats its Windows message queue as an input source. These
additions have build evidence only, without guest ABI fixtures. The
AppKit's queued `NSEvent` lookup now checks that the requested mode is in the
main loop's common-mode set, initialized with default, event-tracking, and
modal-panel modes; raw Win32 messages remain dispatchable in all active modes.
This mode filter has source/build evidence only. The
resolver also exports CFNumber, CFDate,
CFBoolean, and CFDictionary create/copy/mutation/enumeration with the built-in
`kCFType` callback tables. CFSet creation, copying, set algebra and traversal
are also exported with copied callbacks. Both copy callback tables and
allocators, and apply their retain/release/equality functions. CFDictionary
still uses a vector-backed linear lookup. CFSet uses hash buckets and checks
equality only within a matching bucket, giving expected constant-time membership
with well-distributed hashes; pathological collisions remain linear.
Callback table versions other than 0 are rejected. Null
tables become zeroed version-0 structures; custom version-0 tables are
accepted.
This has source/build evidence only; guest-side ABI invocation and status edge
cases remain unverified.

References: [Apple NSRunLoop run(mode:before:)](https://developer.apple.com/documentation/foundation/runloop/run%28mode%3Abefore%3A%29),
[CFRunLoop](https://developer.apple.com/documentation/corefoundation/cfrunloop),
and [CFRunLoopPerformBlock](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock%28_%3A_%3A_%3A%29).

The implementation and source provenance are documented in the repository
license inventory. The upstream Darling project is the reference project:
https://github.com/darlinghq/darling
