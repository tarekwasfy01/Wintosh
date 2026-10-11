# libSystem and Framework Gap Matrix

This document separates the Windows host ABI that is already implemented from
the remaining work required for broad Darwin application compatibility. The
project does not claim that a passing resolver or smoke fixture is equivalent
to a complete Apple system library.

## Implemented host families

The current `darling_windows_host_symbol` table and its smoke tests cover the
following families with Windows-backed implementations:

- memory allocation and byte/string primitives;
- environment, process identity, process groups, and basic spawning;
- file descriptors, filesystem metadata, links, locking, and directory APIs;
- sockets, terminal helpers, time, signals, entropy, and scheduling;
- dynamic loading and the Darwin dyld image-query subset;
- Objective-C runtime registration and the Foundation/CoreFoundation/
  CoreGraphics compatibility subsets;
- Mach virtual memory, IPC broker operations, ports, OOL data, and MIG dispatch.
- compiler hardening entry points for `__stack_chk_fail` and its Darwin
  underscore aliases, plus the `__stack_chk_guard` data-symbol aliases;
- BSD libc allocation/tokenization helpers `reallocarray`, `reallocarrayf`,
  and `strsep`, including overflow rejection for array allocation and empty
  delimiter fields, are now Windows-backed and host-symbol-resolvable;
- BSD/Darwin bounded substring search `strnstr` is now Windows-backed and
  host-symbol-resolvable, including limit enforcement and empty-needle behavior;
- C++ static-initialization guard entry points `___cxa_guard_acquire`,
  `___cxa_guard_release`, and `___cxa_guard_abort`, backed by atomic
  Windows-host state transitions; acquire/release/abort behavior is covered
  on both x64 and Win32.
- C++ thread-destructor entry-point aliases `___cxa_thread_atexit`,
  `___cxa_thread_atexit_impl`, and `_cxa_thread_atexit_impl`, mapped to the
  existing TLS destructor machinery; `_tlv_atexit` and `___tlv_atexit` are
  covered alongside them on both target architectures.
- C++ virtual-dispatch failure entry points `___cxa_pure_virtual` and
  `___cxa_deleted_virtual`, resolved to fail-closed Windows handlers with
  both Darwin and Itanium spelling aliases covered by the host smoke test.
- CoreFoundation array traversal through `CFArrayApplyFunction`, including
  range validation and callback execution on x64 and Win32.
- CoreFoundation dictionary traversal through `CFDictionaryApplyFunction`,
  with key/value callback delivery covered on x64 and Win32.
- CoreFoundation set traversal through `CFSetApplyFunction`, with duplicate-
  free value iteration covered on x64 and Win32.
- CoreFoundation container copies `CFArrayCreateCopy`,
  `CFDictionaryCreateCopy`, and `CFSetCreateCopy`, with retained value
  semantics verified on x64 and Win32.
- CoreFoundation immutable copies `CFStringCreateCopy` and
  `CFDataCreateCopy`, including value equality and retained ownership checks
  on x64 and Win32.
- CoreFoundation numeric/date copies `CFNumberCreateCopy` and
  `CFDateCreateCopy`, including integer/real type preservation and value
  equality checks on x64 and Win32.
- CoreFoundation formatted strings through `CFStringCreateWithFormat`,
  including variadic formatting and invalid-format handling on x64 and Win32.
- CoreFoundation byte-backed strings through `CFStringCreateWithBytes`,
  covering the supported ASCII/UTF-8 encoding identifiers and invalid-input
  rejection on x64 and Win32.
- CoreFoundation filesystem URLs through
  `CFURLCreateFromFileSystemRepresentation`, including bounded byte-path
  construction and URL equality on x64 and Win32.
- CoreFoundation URL filesystem extraction through
  `CFURLGetFileSystemRepresentation`, including bounded output and overflow
  rejection on x64 and Win32.
- CoreFoundation URL copying through `CFURLCopyAbsoluteURL`, preserving the
  current filesystem-path URL value on x64 and Win32.
- CoreFoundation URL string extraction through `CFURLGetString`, with URL /
  string equality checked on x64 and Win32.
- CoreFoundation directory-path detection through `CFURLHasDirectoryPath`,
  including file-vs-directory path checks on x64 and Win32.
- CoreFoundation URL type identification through `CFURLGetTypeID`, matching
  generic `CFGetTypeID` results on x64 and Win32.
- CoreFoundation type-ID accessors for strings, data, arrays, dictionaries,
  sets, numbers, and dates, each matching `CFGetTypeID` on x64 and Win32.
- CoreFoundation singleton type IDs for `CFBoolean` and `CFNull`, also
  matching generic `CFGetTypeID` results on x64 and Win32.

These are native Windows compatibility primitives. They are not a copied or
relicensed Apple libSystem binary.

Foundation `NSOffsetRange` and `NSInsetRange` now provide overflow-safe range
transforms for the existing `NSRange` ABI on x64 and Win32.
`NSInsetRange` now saturates location addition at the host size limit; the
boundary and oversized-inset path are covered by the Foundation smoke test.

Mutable CoreFoundation container constructors `CFArrayCreateMutable`,
`CFDictionaryCreateMutable`, and `CFSetCreateMutable` are now available with
negative-capacity rejection and subsequent mutation through the existing
container APIs. Capacity is a non-binding reservation hint in this Windows
bridge; allocator callbacks and full CoreFoundation object semantics remain
outside this minimal ABI.

Mutable-copy constructors `CFArrayCreateMutableCopy`,
`CFDictionaryCreateMutableCopy`, and `CFSetCreateMutableCopy` now preserve
independent retained container state and pass mutation checks on x64 and
Win32. The capacity argument remains a non-binding reservation hint.

`CFRunLoopGetMain` now exposes the process main run loop alongside
`CFRunLoopGetCurrent`; the current minimal single-process model keeps the
main loop Windows-backed and distinct from worker-thread run-loop state.

`CFDateGetTimeIntervalSinceReferenceDate` now exposes the same Darwin
reference-date value as `CFDateGetAbsoluteTime`, with x64 and Win32 coverage.
`CFDateCreateWithTimeIntervalSinceNow` now derives the Darwin 2001 reference
date from the Windows system clock and is covered for future relative dates.
`CFURLCreateWithString` now creates a URL object from a CoreFoundation string
for the absolute/simple URL form used by this bridge; base-URL resolution is
now also handles a simple relative path against a base URL. Full RFC
normalization and escaping remain unimplemented; one or more leading `../`
segments are handled for simple path bases.
Simple `/./` segments and a trailing `/.` are also collapsed in this path
form; full RFC normalization remains unimplemented.
Bracketed IPv6 authorities are now recognized by `CFURLCopyHostName` and
`CFURLGetPortNumber`, including `[addr]:port`.
`CFURLCopyNetLocation` now returns the authority component for simple
`scheme://authority` URLs.
`CFURLCopyUserName` and `CFURLCopyPassword` now expose simple user-info
components before the authority host.
Valid `%XX` escapes in those two user-info components are decoded by the
Windows bridge; malformed escapes remain literal.

## Still required for broad compatibility

The CoreFoundation string family now also includes the numeric accessors
`CFStringGetIntValue` and `CFStringGetDoubleValue`, with strict trailing-input
validation and bounded integer conversion on x64 and Win32.
`CFStringGetCharacters` now exposes UTF-16 code units, including surrogate
pairs for non-BMP code points, with range validation on both targets.
`CFStringGetCharacterAtIndex` uses the same validated UTF-16 indexing.
`CFStringFind` now supports backwards searches, including combined
case-insensitive/backwards flag handling.
The additive `CFStringFindWithOptions` entry point now applies the search to a
validated UTF-16 range and returns locations relative to the original string.
Options-based prefix and suffix checks are also available through additive
`CFStringHasPrefixWithOptions` and `CFStringHasSuffixWithOptions` entry points;
their case-insensitive behavior remains ASCII-focused.
The additive `CFStringCompareWithOptions` entry point now provides the
case-insensitive comparison subset without changing the existing two-argument
bridge ABI.
Its returned locations and lengths are now expressed in UTF-16 units,
including for searches after non-BMP characters.
Empty backward searches now resolve to the UTF-16 end position, matching the
directional search contract.
`CFStringCreateWithPascalString` now accepts the supported ASCII/UTF-8
encoding identifiers, performs bounded length decoding, and rejects null or
unsupported-encoding inputs.
`CFStringCreateWithCharacters` now converts validated UTF-16 input, including
surrogate pairs, for CoreServices/CarbonCore-style Unicode path handling.
`CFStringCreateArrayBySeparatingStrings` now creates retained CFString
segments, including trailing empty segments.
`CFStringCreateWithFileSystemRepresentation` now handles bounded UTF-8 path
bytes and rejects embedded NUL data.
`CFStringCreateWithCStringNoCopy` is available for the relevant encoding IDs;
on Windows it deliberately copies the input and therefore does not retain
foreign allocator ownership.
`CFStringGetSystemEncoding` reports the port's explicit UTF-8 system encoding
identifier for stable CoreServices string conversion.
`CFStringGetMaximumSizeForEncoding` now provides conservative overflow-safe
UTF-8/ASCII buffer bounds for CoreFoundation clients.
`CFStringGetMaximumSizeOfFileSystemRepresentation` and
`CFStringGetFileSystemRepresentation` now provide bounded NUL-terminated path
conversion for LaunchServices-style callers.
`CFStringFindAndReplace` now combines range-bounded lookup and replacement for
the common CoreAudio/DirectoryService normalization path.
Its lookup is now restricted to the supplied UTF-16 range even when an earlier
matching substring exists outside that range.
`CFStringReplaceRange` now converts its location and length from UTF-16 units
to UTF-8 storage offsets, including surrogate-pair-safe replacement.
`CFStringLowercase` and `CFStringUppercase` provide safe ASCII case conversion
for mutable strings; non-ASCII UTF-8 bytes are preserved until full
locale-aware Unicode mappings are added.
`CFStringCapitalize` applies the corresponding ASCII word-boundary behavior;
locale-specific Unicode titlecasing remains outside the current bridge.
`CFStringGetBytes` now uses the same UTF-16 range model while returning the
UTF-8 byte count required by the Windows bridge.
`CFStringCreateMutableCopy` now returns an independent mutable string; its
capacity argument is treated as a non-binding reservation hint on Windows.
`CFStringCreateMutable` now creates an empty mutable string with the same
capacity-hint behavior and rejects negative capacities.
`CFStringAppend` now appends another CFString without exposing the underlying
storage.
The URL family also includes last-component extraction and parent-path copies,
covering path operations used by Darling's CoreAudio helpers.
`CFURLCopyPath` is also available for extracting the path portion before query
or fragment delimiters.
`CFURLCopyQueryString` and `CFURLCopyFragment` now expose the corresponding
URL components without their delimiters and return null when absent.
`CFURLCopyScheme` and `CFURLCopyResourceSpecifier` complete the basic URL
decomposition for scheme-based callers.
`CFURLCopyHostName` and `CFURLGetPortNumber` cover simple `scheme://authority`
URLs for network-facing callers; full URL normalization and IPv6 authority
parsing remain open.
It now also provides path-extension extraction and extension removal for
filesystem URLs.

The CoreFoundation string family now also includes UTF-16-indexed
`CFStringCreateWithSubstring`, with surrogate-pair-safe boundaries and
fail-closed out-of-range handling on x64 and Win32. This covers the substring
operation used by Darling's CoreAudio file-format helpers.

1. A packaged Darwin-compatible libSystem surface with stable library/image
   identity and complete symbol-level ABI coverage.
2. Correct startup and teardown ordering for libSystem, dyld, Objective-C,
   TLS/TLV, `atexit`, and framework initializers in real applications.
3. More complete libc, pthread, locale, resolver, Unicode, security, and
   filesystem edge semantics beyond the current tested subset.
4. Real framework modules and API behavior for AppKit, UIKit, CoreServices,
   CFNetwork, Security, Metal, and other application-facing frameworks.
5. Architecture coverage beyond native x86/x64 execution: ARM64 Mach-O is
   parsed and relocated in selected paths, but native ARM64 code execution on
   x86/x64 Windows is not implemented.
6. Compatibility testing against real, legally redistributable Mach-O command
   line programs and framework clients rather than only synthetic fixtures.

## Evidence boundary

The current Win32 and x64 CTest matrices each pass 46/46 tests. The dyld CLI
fixture additionally proves dependency discovery through `DYLD_LIBRARY_PATH`,
indirect import-slot patching, and entry execution. Those results prove the
loader and selected ABI primitives; they do not prove that arbitrary macOS
applications can run.

## Provenance and licensing

Ported implementation files carry the project license notice. External
inspiration and source provenance remain catalogued in
`licenses/EXTERNAL-COMPONENT-PROVENANCE.csv` and
`licenses/SOURCE-LICENSE-INVENTORY.csv`; the corresponding license texts are
kept in `licenses/`. Darling remains the upstream behavioral reference and is
not silently presented as original Wintosh code.
