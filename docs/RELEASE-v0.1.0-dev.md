# Wintosh v0.1.0-dev

Wintosh v0.1.0-dev is the first public developer preview of an experimental
native Windows compatibility layer inspired by the
[Darling project](https://github.com/darlinghq/darling).

This release publishes the current Wintosh implementation, its CMake build,
focused smoke tests, license notices, provenance records, porting matrices,
and the documented development roadmap.

## Included in this preview

- Native Windows x64/Win32 adapter source under `wintosh-windows/`.
- Organized core, platform, ABI, loader, runtime, framework, process, and test
  families.
- Win32-backed process, file, memory, socket, thread, synchronization, signal,
  time, terminal, and selected syscall primitives.
- Local Mach port/VM ABI-shaped adapters.
- Mach-O/dyld parsing and loader-related primitives.
- Objective-C runtime and Foundation type-encoding primitives.
- CMake configuration with separate smoke-test targets.
- Contributor guidance for GitHub user
  [@tarekwasfy01](https://github.com/tarekwasfy01).

## Verified evidence

The current CMake project configures successfully with Visual Studio 18 2026
for x64. The Foundation smoke target builds and runs successfully with:

`FOUNDATION_TYPE_ENCODING_ABI=PASS`

This is a developer-preview evidence gate, not a complete application-runtime
claim. The full smoke-test matrix and both architecture builds remain ongoing.

## Not included yet

- Complete Foundation/CoreFoundation/AppKit implementation.
- Full Darwin Mach kernel and MIG semantics.
- Complete cross-process Mach transport.
- Full Darling server and arbitrary Mach-O application execution on Windows.
- A claim of general macOS application compatibility.

## Source and licensing

The large upstream Darling, WSL, and other reference checkouts are not vendored
in this repository. They remain documented as upstream references. License and
provenance material is retained under `licenses/`. Darling-derived boundaries
remain subject to the applicable Darling GPL-3.0 terms; the Wintosh-original
license applies only to clearly identified original Wintosh code.

The release tree includes the following legal files:

- `LICENSE-WINTOSH-ORIGINAL-MIT.txt` — Wintosh-original code boundary.
- `licenses/LICENSE-darling-GPL-3.0.txt` — Darling GPL-3.0 license text.
- `licenses/LICENSE-wsl-MIT.txt` and `licenses/NOTICE-wsl.txt` — WSL notice
  material.
- `licenses/SOURCE-LICENSE-INVENTORY.csv` — recognized source license files.
- `licenses/EXTERNAL-COMPONENT-PROVENANCE.csv` and
  `licenses/EXTERNAL-COMPONENT-REVIEW.md` — component provenance and open
  review status.
- `licenses/README.md`, `licenses/SOURCE-BUNDLE.md`, and
  `docs/THIRD-PARTY-NOTICES.md` — redistribution instructions and notices.

The repository does not relicense Darling or any other third-party source.
The original upstream terms remain authoritative.

## Feedback

Please include the Windows version, architecture, CMake generator, compiler
version, exact target, and complete output when reporting a problem. The
project is experimental and contributions are welcome.
