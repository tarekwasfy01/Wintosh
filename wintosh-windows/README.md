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

The detailed build and evidence rules are in `../docs/BUILD-AND-TEST.md`.
