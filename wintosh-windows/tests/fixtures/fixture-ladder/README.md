# Wintosh fixture ladder

Original, small C sources for the Darwin ABI boundary. They contain no Apple
SDK headers or Apple binaries. `build-fixtures.ps1` performs Clang
Darwin-target checks, emits source-derived x86_64 Mach-O objects, and can link
them with LLVM `ld64.lld` for Wintosh execution. The `argv-env`, `memory`,
`qsort-callback`, `file-io`, and `crash` fixtures have dedicated Windows E2E
coverage; the x86_64 `pthread-tlv` fixture remains syntax-/host-TLV-only because
this installed Clang rejects Darwin `__thread` for the local target. Static C
i386 TLV and pthread isolation have a separate runner fixture at
`wintosh-windows/tests/i386_tlv_e2e.ps1`.

Fixtures: `argv-env.c`, `memory.c`, `qsort-callback.c`, `file-io.c`,
`pthread-tlv.c`, `crash.c`, and `lazy-import.c`. Each is an independent gate
fixture. `lazy-import.c` is linked into a real x86_64 Mach-O with LLVM
`ld64.lld`; its two imports execute on Windows through default first-call lazy
binding and the compiler-produced stub helper.

Copyright (C) 2026 Wintosh contributors. Licensed under GPL-3.0-only; see the
repository license inventory.
