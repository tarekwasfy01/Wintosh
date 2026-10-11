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

These are native Windows compatibility primitives. They are not a copied or
relicensed Apple libSystem binary.

## Still required for broad compatibility

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
