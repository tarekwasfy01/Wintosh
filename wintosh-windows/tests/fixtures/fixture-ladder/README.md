# Wintosh fixture ladder

Original, small C sources for the Darwin ABI boundary. They contain no Apple
SDK headers or Apple binaries. `build-fixtures.ps1` performs Clang
Darwin-target checks, emits source-derived x86_64 Mach-O objects, and can link
them with LLVM `ld64.lld` for Wintosh execution. The `argv-env`, `memory`,
`qsort-callback`, `file-io`, and `crash` fixtures have dedicated Windows E2E
coverage; `pthread-tlv` remains syntax-/host-TLV-only because this installed
Clang rejects Darwin `__thread` for the local target.

Fixtures: `argv-env.c`, `memory.c`, `qsort-callback.c`, `file-io.c`,
`pthread-tlv.c`, and `crash.c`. Each is an independent gate fixture.

Copyright (C) 2026 Wintosh contributors. Licensed under GPL-3.0-only; see the
repository license inventory.
