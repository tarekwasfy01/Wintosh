# Wintosh

Wintosh is an experimental native Windows compatibility runtime for selected
Darwin and macOS user-space behavior. Its goal is to make progressively more
Mach-O command-line and GUI software usable on Windows by implementing the
required loader, ABI, runtime, system-call, and framework layers.

> **Status:** early development. Wintosh is not a macOS virtual machine, does
> not provide a complete Darwin kernel, and cannot currently run arbitrary
> macOS `.app` bundles. Compatibility is feature-by-feature and must be
> demonstrated with guest binaries; implemented host functions alone do not
> establish application compatibility.

## What is in the repository

- `wintosh-windows/` — native Windows runtime, Mach-O and dyld work, Objective-C
  ABI support, Darwin-like services, and framework implementations.
- `docs/` — current implementation notes, known gaps, research references, and
  the porting model.
- `tools/` — scripts for reporting and validating the capability matrix.
- `PRIMITIVE-MATRIX.csv` — machine-readable inventory of transformation
  primitives and evidence status.

Darling and other upstream projects are behavioral references; their source
checkouts and Apple system images are not bundled as runtime dependencies.

## Build on Windows

Requirements: CMake 3.20+, a Windows 10/11 SDK, and either Visual Studio with
MSVC or a supported Clang toolchain. The project currently targets 64-bit
Windows (x64 and, with Clang, ARM64); see the CMake configuration and release
workflow for the exact toolchain-specific assembly paths.

Example with Visual Studio (x64):

```powershell
cmake -S wintosh-windows -B build-windows-msvc -A x64
cmake --build build-windows-msvc --config Release
```

Example with Ninja and Clang (x64):

```powershell
cmake -S wintosh-windows -B build-windows-clang -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows-clang
```

Build configuration and available targets can vary by generator and host SDK.
The CI packaging workflow is [`.github/workflows/wintosh-release.yml`](.github/workflows/wintosh-release.yml).

## Compatibility roadmap

The work is layered: executable format and loader → calling conventions and
Objective-C runtime → Darwin process and system interfaces → Foundation and
CoreFoundation → graphics and AppKit → application-level validation. A failure
in any required layer can prevent an app from starting. WSL1 is useful as a
research/compatibility comparison, but it does not turn Darwin binaries into
Windows programs and is not itself a solution for macOS frameworks.

For the implemented scope and unverified areas, use the [current status](docs/STATUS.md).
For architecture and subsystem notes, start with the [Windows runtime guide](wintosh-windows/runtime/README.md).
For the mathematical model and reference material, see the [porting index](docs/porting/README.md).
The machine-readable matrix and its validation utility are described in the
[primitive matrix documentation](docs/PRIMITIVE-MATRIX.md) and [tools guide](tools/README.md).

## Contributing and evidence

Keep claims tied to reproducible evidence. Matrix rows distinguish an
implemented primitive from one proven against a guest binary or application.
Update the relevant status and subsystem documentation when behavior changes.
Do not add proprietary macOS system files, firmware, or personal data to the
repository.

## License

See [`LICENSE-WINTOSH-ORIGINAL-MIT.txt`](LICENSE-WINTOSH-ORIGINAL-MIT.txt) and
per-component license notices under [`licenses/`](licenses/).
