# Build and test guide

The repository contains the standalone Wintosh implementation under
`wintosh-windows/`. The commands below are PowerShell commands executed from
the repository root.

## Prerequisites

- Visual Studio with C++ desktop tools and MSBuild.
- Windows x64 host for the x64 build.
- A 32-bit build toolchain for the Win32 build.
- The source trees and license bundle present in the checkout.

## Build

```powershell
cmake -S .\wintosh-windows -B .\build-windows-msvc -G "Visual Studio 18 2026" -A x64
cmake -S .\wintosh-windows -B .\build-windows-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build .\build-windows-msvc --config Release
cmake --build .\build-windows-win32 --config Release
```

If the installed CMake generator has another name, select the matching Visual
Studio generator. A successful command must finish with exit code 0; merely
finding a generated project is not a build result.

## Clang/SysV Mach-O gate

The native real-guest path is built separately with clang-cl. The project uses
LLVM's integrated assembler when MASM `ml64` is unavailable:

```powershell
cmake -S .\wintosh-windows -B .\build-windows-clang -G Ninja `
  -DCMAKE_CXX_COMPILER='C:/Program Files/LLVM/bin/clang-cl.exe' `
  -DCMAKE_RC_COMPILER='C:/Program Files/LLVM/bin/llvm-rc.exe' `
  -DCMAKE_LINKER='C:/Program Files/LLVM/bin/lld-link.exe' `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_FLAGS='/EHsc /clang:-fexceptions'
cmake --build .\build-windows-clang --target wintosh
```

The verified gate uses the supplied test fixtures from the external
ABI-bridge task archive, not redistributed Apple binaries. The Chained and
Classic fixtures each print `hello from guest`, return `0` without an extra
argument, and return `7` with one extra argument. The MSVC build must not be
used for this gate; it fails closed before entering real guest code.

## Focused Foundation gate

```powershell
& .\build-windows-msvc\Release\wintosh_foundation_smoke.exe
& .\build-windows-win32\Release\wintosh_foundation_smoke.exe
```

Expected output includes `FOUNDATION_TYPE_ENCODING_ABI=PASS` for both targets.

## Regression gate

Run every `wintosh_*_smoke.exe` under both Release directories and
record the exit code and output. The filesystem and host-API smoke tests may
require permissions that are unavailable to a restricted account; if they are
excluded, record that exclusion rather than counting it as a pass.

## Evidence rules

- Source present is not compiled.
- Compiled is not runtime-tested.
- A smoke test is not application compatibility.
- A native Windows adapter is not full Darwin kernel behavior.
- A reference checkout is not copied code and does not change licensing.
