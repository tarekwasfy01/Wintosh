# Loader, CPU and rebuild references

| Project | Layer | What it supplies | What remains for macOS apps | Wintosh use |
|---|---|---|---|---|
| [Dynarmic](https://github.com/yuzu-mirror/dynarmic) | ARM32/AArch64 dynamic recompiler | A64 guest execution on x86-64 or AArch64 hosts, user-mode hooks and caller-owned memory callbacks; Windows is listed as a host | Darwin syscalls, Mach services, dyld, Objective-C, Cocoa and app resources; exact instruction/ABI coverage needs fixtures | First candidate to evaluate for ARM64 Mach-O on x64 Windows; 0BSD core, but CMake requires Boost and all bundled dependency licenses need review before integration |
| [Icicle](https://github.com/icicle-emu/icicle-emu) | Multi-architecture CPU/VM framework | AArch64 target triples, p-code VM and caller-provided execution environment | Experimental fuzzing focus, Rust integration, Darwin/Mach-O runtime and GUI services | Alternate CPU-backend research candidate; MIT/Apache-2.0, no local build or guest execution |
| [Unicorn](https://github.com/unicorn-engine/unicorn) | CPU instruction emulation | Multi-architecture CPU execution and register/memory control; Windows supported | Darwin syscalls, Mach services, dyld, Objective-C, Cocoa and app resources; GPL-2.0-only provenance needs compatibility review | Keep as an isolated oracle candidate; do not fold its code into the GPL-3.0 runtime without a license analysis |
| [Qiling](https://github.com/qilingframework/qiling) | OS-aware emulation built over Unicorn | Loader and selected OS syscall models useful for analysis | Validate exact Darwin/Mach-O support per version; GUI, frameworks and arbitrary apps are not implied | Use only as a research oracle and fixture source until reproduced locally |
| [macho-ld](https://github.com/EddieIvan01/macho-ld) | Mach-O loader | Rust in-memory loader concepts and loader fixtures | dyld parity, Objective-C runtime, framework graph and full app services | Compare parser/fixup behavior; existing Wintosh implementation stays independently tested |
| [Swift](https://github.com/swiftlang/swift) | Compiler and language runtime | Official Windows target for recompiling supported Swift code | AppKit/SwiftUI and macOS binary ABI are not supplied by compiling Swift for Windows | Separate source-port route for portable command-line or cross-platform apps |
| [Infinite Mac](https://github.com/mihaip/infinite-mac) | Browser-hosted classic Mac emulator | Browser-delivered historic Macintosh environments | Classic Mac OS app support does not implement modern macOS/Darwin ABI | Historical emulator comparison only |

Upstream checks on 2026-11-23 confirm Dynarmic describes AArch64 guest support,
x86-64/AArch64 host support, Windows support, caller-owned memory callbacks,
and a 0BSD core license. Its README also lists separately licensed bundled
dependencies and warns that it is user-mode emulation, not a system emulator.
Icicle documents `aarch64-linux` targets and dual MIT/Apache licensing, but is
experimental and fuzzing-oriented. Unicorn documents Windows support and
GPL-2.0-only. These are research findings, not local integration or runtime
evidence. No third-party CPU engine has been added to Wintosh.

## Compatibility composition

For an existing Mach-O app, the required path is a conjunction:

`CPU + Mach-O loader + Darwin ABI + dyld + Objective-C + frameworks + resources + host services`.

If any required component is absent, the app cannot be claimed as supported.

For the requested matrix view, let `a_i = 1` when an app needs contract `i`,
and let `T_ij = 1` only when a route preserves contract `i` into contract `j`.
Compose route matrices over the Boolean semiring:

`(A ⊙ B)[i,j] = OR_k (A[i,k] AND B[k,j])`.

The route is viable only if every required contract has an end-to-end path:
`a_i = 1 ⇒ (T_route)[i,i] = 1`. The WSL1 chain has no Darwin-to-Linux syscall
edge by itself: [Microsoft describes WSL1 as a Linux syscall translation layer](https://learn.microsoft.com/en-us/windows/wsl/compare-versions),
while macOS guests require Darwin syscalls, Mach IPC and dyld. Adding WSL therefore
does not fill that missing edge. A more direct x64 Windows route is
`Mach-O loader ⊙ AArch64 CPU backend ⊙ Darwin ABI/services`; Dynarmic is the
current CPU candidate, not an implemented capability. WSL2 supplies a Linux
kernel rather than WSL1's syscall translation layer, but still does not turn a
Mach-O/Darwin program into a Linux ELF program.

Unicorn or Dynarmic contributes CPU execution only. Qiling adds selected OS
models, whose exact Darwin coverage still needs a controlled fixture run. A
Mach-O loader supplies file mapping and relocations, not the frameworks. Swift
and Cocoa ports instead build a new Windows executable from source, which is a
useful but distinct delivery path.

No binaries or source from these projects were added to the Wintosh build.
Each possible integration needs version-pinned behavior tests, architecture
and ABI checks, and license/dependency review before code is reused.
