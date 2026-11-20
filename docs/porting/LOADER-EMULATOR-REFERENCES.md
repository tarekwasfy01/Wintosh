# Loader, CPU and rebuild references

| Project | Layer | What it supplies | What remains for macOS apps | Wintosh use |
|---|---|---|---|---|
| [Unicorn](https://github.com/unicorn-engine/unicorn) | CPU instruction emulation | A CPU execution engine and register/memory control | Darwin syscalls, Mach services, dyld, Objective-C, Cocoa and app resources | Consider as an isolated guest CPU backend if native x86_64/ARM64 execution proves insufficient |
| [Qiling](https://github.com/qilingframework/qiling) | OS-aware emulation built over Unicorn | Loader and selected OS syscall models useful for analysis | Validate exact Darwin/Mach-O support per version; GUI, frameworks and arbitrary apps are not implied | Use only as a research oracle and fixture source until reproduced locally |
| [macho-ld](https://github.com/EddieIvan01/macho-ld) | Mach-O loader | Rust in-memory loader concepts and loader fixtures | dyld parity, Objective-C runtime, framework graph and full app services | Compare parser/fixup behavior; existing Wintosh implementation stays independently tested |
| [Swift](https://github.com/swiftlang/swift) | Compiler and language runtime | Official Windows target for recompiling supported Swift code | AppKit/SwiftUI and macOS binary ABI are not supplied by compiling Swift for Windows | Separate source-port route for portable command-line or cross-platform apps |
| [Infinite Mac](https://github.com/mihaip/infinite-mac) | Browser-hosted classic Mac emulator | Browser-delivered historic Macintosh environments | Classic Mac OS app support does not implement modern macOS/Darwin ABI | Historical emulator comparison only |

Upstream checks on 2026-11-15 confirm Unicorn describes itself as a CPU
emulator, macho-ld as an in-memory Mach-O loader, and Infinite Mac as a
classic Macintosh environment. These repository scopes support the layer
classification above. Qiling remains a candidate analysis framework; its
exact Darwin behavior is unverified here. No local checkout was found for any
of these four sources, and no app fixture was run through them.

## Compatibility composition

For an existing Mach-O app, the required path is a conjunction:

`CPU + Mach-O loader + Darwin ABI + dyld + Objective-C + frameworks + resources + host services`.

If any required component is absent, the app cannot be claimed as supported.
Unicorn contributes CPU execution; it does not provide the other terms.
Qiling adds OS models, whose exact Darwin coverage still needs a controlled
fixture run. A Mach-O loader supplies file mapping and relocations, not the
frameworks. Swift and Cocoa ports instead build a new Windows executable from
source, which is a useful but distinct delivery path.

No binaries or source from these projects were added to the Wintosh build.
Each possible integration needs version-pinned behavior tests, architecture
and ABI checks, and license/dependency review before code is reused.
