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

The implementation and source provenance are documented in the repository
license inventory. The upstream Darling project is the reference project:
https://github.com/darlinghq/darling
