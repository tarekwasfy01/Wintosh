# macemu reference assessment

Inspected 2026-11-15: https://github.com/kanjitalk755/macemu

The repository contains BasiliskII, SheepShaver, cxmon, and tests. Its README
lists a MinGW x86 JIT target for both emulators and requires SDL 2.0.14 or
later. Windows instructions use MSYS2's i686 toolchain and the respective
`src/Windows` directories. These are upstream build instructions; no local
macemu build or execution is claimed.

For Wintosh, inspect the CPU execution boundary, Windows device adapters,
guest memory access, and debugger integration as architecture references.
This does not establish a Darwin syscall, modern Mach-O, Cocoa, or ARM64
implementation. No edge in the Darwin/Linux/WSL1/Windows matrix is promoted
by the existence of this separate emulator backend.

Next source inspection targets are `BasiliskII/src/Windows`,
`SheepShaver/src/Windows`, their CPU implementations, and `cxmon`.
Before importing any implementation, record the exact commit and applicable
file licenses. No external code or operating-system images were copied.

Source: [repository README](https://github.com/kanjitalk755/macemu/blob/master/README.md).
