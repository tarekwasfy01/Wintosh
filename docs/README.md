# Wintosh


Wintosh is an experimental native Windows port of selected Darling host and
Darwin compatibility primitives. The project explores how parts of the
Darling execution model can be mapped to Win32/Win64 while keeping source
provenance, license boundaries, and runtime evidence explicit.

This repository is not yet a complete macOS-on-Windows runtime. It is a
working porting laboratory and a documented foundation for future work.

## Relationship to Darling

The project is inspired by and developed against the [Darling project](https://github.com/darlinghq/darling).
Darling remains the primary reference for Darwin userland, Mach-O loading,
Mach interfaces, Objective-C runtime behavior, and framework contracts. The
The source is not vendored into this repository. Use the [upstream Darling
repository](https://github.com/darlinghq/darling) for the complete source and
its current license terms; the local analysis checkout is intentionally kept
outside the public Wintosh tree.

## What works today

The current Windows-native host layer contains verified x64 and Win32
primitive families, including process and thread helpers, file descriptors and
selected syscalls, sockets and resolver calls, pthread attributes and TLS,
time and memory helpers, Mach-O parsing, dyld-related parsing, Objective-C
runtime primitives, dynamic loading, broker/terminal helpers, local Mach port
and VM adapters, and a first Foundation type-encoding adapter.

The Foundation smoke test currently covers scalar types, qualifiers, pointers,
arrays, structures, unions, and resolver aliases. The current evidence is
recorded in `../PRIMITIVE-MATRIX.csv` and `PORTING-INVENTORY.md`; a passing smoke
test is not a claim of full Darwin compatibility.

## What is next

The major remaining work is the full Foundation/CoreFoundation object model,
framework initialization, complete Mach/MIG kernel semantics, cross-process
Mach transport, the real Darling server and loader boundary, broader Darwin
frameworks, and end-to-end execution of real Mach-O applications. These areas
must be implemented and tested family by family.

## Repository guide

- `../wintosh-windows/` — published Wintosh-native Windows overlay and smoke tests.
- `../darling-source/src/native/windows/` — local comparison copy; the upstream
  source checkout is not vendored here.
- `../wsl-source/` — local-only WSL reference checkout; it is not published here.
- `../licenses/` — collected licenses, notices, and the source-license inventory.
- `../PRIMITIVE-MATRIX.csv` / `PRIMITIVE-MATRIX.md` — implementation and test map.
- `SOURCE-EXTRACTION-MATRIX.md` — cross-project source and idea extraction map.
- `PORTING-INVENTORY.md` — detailed progress log and remaining gaps.
- `MACH-OOL-OWNERSHIP-DESIGN.md` — contract for the next native OOL ownership
  implementation and its explicit safety boundary.
- `DOCUMENTATION-INDEX.md` — complete documentation map.
- `docs/porting/README.md` — ordered porting-notes index.
- `STATUS.md` — evidence-based current status.
- `BUILD-AND-TEST.md` — reproducible build and test procedure.
- `CURRENT-STATE.md` / `CURRENT-STATE-MANIFEST.csv` — complete workspace
  contents and release-role inventory.
- `../tools/` — probe and helper scripts.

## Building the current native targets

The CMake project is in `../wintosh-windows`; generated Visual Studio projects
are written to `build-windows-msvc` and `build-windows-win32`. A Visual
Studio/CMake installation is required. The
Foundation smoke binaries are:

```powershell
cmake -S .\wintosh-windows -B .\build-windows-msvc -G "Visual Studio 18 2026" -A x64
cmake -S .\wintosh-windows -B .\build-windows-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build .\build-windows-msvc --config Release
cmake --build .\build-windows-win32 --config Release
& .\build-windows-msvc\Release\wintosh_foundation_smoke.exe
& .\build-windows-win32\Release\wintosh_foundation_smoke.exe
```

The repository deliberately distinguishes source presence, compilation,
smoke-test execution, and real application compatibility.

## License summary

This repository contains multiple license boundaries. Darling is GPL-3.0 and
its corresponding source and notices must remain available. The WSL checkout
is MIT-licensed reference material. Other checked-out projects have their own
licenses. See `../licenses/README.md`,
`../licenses/SOURCE-LICENSE-INVENTORY.csv`, and
`THIRD-PARTY-NOTICES.md` before redistribution.

New Wintosh-specific adapter code that is genuinely independent of Darling
derivative code is offered under the MIT-compatible notice in
`LICENSE-WINTOSH-ORIGINAL-MIT.txt`. Files derived from or integrated with
Darling remain subject to the applicable Darling GPL terms; the Wintosh notice
does not relicense them.

## Status

Experimental, source-available, and under active porting. Contributions and
careful compatibility reports are welcome, especially when they include the
Windows version, architecture, build command, and test output.
