# Wintosh Windows implementation

This directory contains the published Wintosh-native Windows implementation.
It is separate from the local Darling checkout: Wintosh keeps its own adapter
source here while linking to Darling as the upstream behavior and license
reference.

## Layout

- `core/` — bootstrap, runtime and broker integration.
- `platform/` — Win32-backed files, memory, sockets, threads, signals, time,
  syscalls, terminal and standard-I/O adapters.
- `abi/` — local Mach, Objective-C and thread-local-storage ABI primitives.
- `loader/` — Mach-O, dyld and dynamic-library parsing/loading adapters.
- `runtime/` — executable runner entry points.
- `frameworks/` — Foundation-compatible ABI primitives.
- `process/` — process and process-group abstractions.
- `tests/` — focused smoke tests for each implementation family.

## User-facing command line

The main entry point is `wintosh.exe` (built from `runtime/`). It accepts a
Mach-O image followed by its arguments:

```text
wintosh.exe <mach-o> [args...]
```

The current release contains the native Windows loader/bootstrap path and is
still an experimental compatibility layer; it does not yet provide complete
Darwin framework or arbitrary macOS application compatibility.

The detailed build and evidence rules are in `../docs/BUILD-AND-TEST.md`.
