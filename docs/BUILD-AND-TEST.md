# Build and test guide

The repository contains generated Visual Studio projects for the current
native adapter snapshot. The commands below are PowerShell commands executed
from the repository root.

## Prerequisites

- Visual Studio with C++ desktop tools and MSBuild.
- Windows x64 host for the x64 build.
- A 32-bit build toolchain for the Win32 build.
- The source trees and license bundle present in the checkout.

## Build

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild .\build-windows-msvc\ALL_BUILD.vcxproj /p:Configuration=Release /p:Platform=x64 /m:4
& $msbuild .\build-windows-win32\ALL_BUILD.vcxproj /p:Configuration=Release /p:Platform=Win32 /m:4
```

If the Visual Studio installation is elsewhere, set `$msbuild` to the actual
path. A successful command must finish with exit code 0; merely finding a
generated project is not a build result.

## Focused Foundation gate

```powershell
& .\build-windows-msvc\Release\darling_windows_foundation_smoke.exe
& .\build-windows-win32\Release\darling_windows_foundation_smoke.exe
```

Expected output includes `FOUNDATION_TYPE_ENCODING_ABI=PASS` for both targets.

## Regression gate

Run every `darling_windows_*_smoke.exe` under both Release directories and
record the exit code and output. The filesystem and host-API smoke tests may
require permissions that are unavailable to a restricted account; if they are
excluded, record that exclusion rather than counting it as a pass.

## Evidence rules

- Source present is not compiled.
- Compiled is not runtime-tested.
- A smoke test is not application compatibility.
- A native Windows adapter is not full Darwin kernel behavior.
- A reference checkout is not copied code and does not change licensing.
