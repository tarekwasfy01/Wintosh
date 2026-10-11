# Apple open-source runtime references

This map records official Apple source repositories reviewed for the Wintosh
Darwin runtime.  The repositories are references only; no Apple source or
binary is copied into Wintosh by this document.

## Primary references

| Component | Upstream | Wintosh use | License boundary |
|---|---|---|---|
| `dyld` | https://github.com/apple-oss-distributions/dyld | startup stack, load commands, dependency graph, rebases, bindings, chained fixups and initializer order | inspect each file/header; Apple APSL notices apply where present |
| `libplatform` | https://github.com/apple-oss-distributions/libplatform | low-level locks, atomics, spinlocks, platform and crash/runtime contracts | repository declares Apache-2.0; preserve NOTICE/license if code is ever copied |
| `libpthread` | https://github.com/apple-oss-distributions/libpthread | pthread startup, TLS, cancellation, mutex/condition semantics and thread ABI | inspect source-level notices; do not assume one repository-wide license for every file |
| `objc4` | https://github.com/apple-oss-distributions/objc4 | class/selector registration, metadata, ARC/exception interactions and runtime initialization | preserve per-file Apple notices; use as behavioral reference until a separate license review permits code reuse |
| `xnu` | https://github.com/apple-oss-distributions/xnu | Mach message/task/thread semantics and Darwin syscall contracts | mixed Apple/BSD-origin boundaries; keep reference-only unless component license is individually cleared |
| `Libc` | https://github.com/apple-oss-distributions/Libc | errno, startup environment, POSIX shims, file/process ABI and libc symbol inventory | inspect per-file license headers before any reuse |

Apple's dyld documentation explicitly describes the kernel-to-dyld startup
handoff, including argc/argv/envp and Apple parameters on the initial stack;
that is the next ABI gap for Wintosh's native bootstrap.  Wintosh currently
invokes the mapped entry point directly, so it must add a generated Darwin
startup-frame contract before claiming compatibility with real Apple binaries.

## Ordered implementation plan

1. Convert the dyld startup documentation into a Wintosh-owned startup-frame
   structure and populate it from the existing launch options.
2. Compare `libplatform` and `libpthread` contracts against the existing
   Windows host primitives; add only missing behavior, with independent code.
3. Expand the Objective-C metadata/exception matrix from the current registry
   smoke tests to initializer, class realization and exception unwinding cases.
4. Use XNU headers and MIG descriptions to identify missing Mach calls, while
   keeping the Windows broker as the implementation boundary.
5. Use Libc as a symbol/errno inventory and add missing implementations to the
   host API table, not as an unreviewed source import.

## Current boundary

The current Wintosh tree already has native tests for errno, threads,
signals, Objective-C registration, Mach IPC, dyld parsing and a controlled
x86_64 `.app` fixture.  Those tests prove the local contracts only.  They do
not make Apple's closed framework binaries redistributable, and they do not
yet provide the complete Darwin startup ABI or the full Apple system library
set.
