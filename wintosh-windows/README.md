# Wintosh Windows implementation

This directory contains the published Wintosh-native Windows implementation.
It is separate from the local Darling checkout: Wintosh keeps its own adapter
source here while linking to Darling as the upstream behavior and license
reference.

## Layout

- `core/` — bootstrap, runtime and broker integration.
- `platform/` — Win32-backed files, memory, sockets, threads, signals, time,
  syscalls, terminal and standard-I/O adapters.
- `abi/` — local Mach, Objective-C and thread-local-storage ABI primitives.
- `loader/` — Mach-O, dyld and dynamic-library parsing/loading adapters.
- `runtime/` — executable runner entry points.
- `frameworks/` — Foundation-compatible ABI primitives.
- `process/` — process and process-group abstractions.
- `tests/` — focused smoke tests for each implementation family.

## User-facing command line

The main entry point is `wintosh.exe` (built from `runtime/`). It accepts a
Mach-O image followed by its arguments:

```text
wintosh.exe <mach-o> [args...]
```

The current release contains the native Windows loader/bootstrap path and is
still an experimental compatibility layer; it does not yet provide complete
Darwin framework or arbitrary macOS application compatibility.

The native runner does not require WSL. WSL is documented separately as an
optional reference/backend experiment; the Windows ABI, Mach broker, loader,
and smoke tests in this directory execute through Win32/MSVC directly.

### CLI examples

Run a supported Mach-O image and pass arguments to its entry point:

```powershell
.\bin\wintosh.exe .\Applications\Example.app\Contents\MacOS\Example
.\bin\wintosh.exe .\bin\example.macho --verbose input.txt
.\bin\wintosh.exe --prefix .\runtime --rpath .\runtime\lib \
    --env WINTOSH_MODE=portable .\bin\example.macho
.\bin\wintosh.exe --inspect .\bin\example.macho
```

The options are `--help`, `--version`, `--prefix <directory>`, repeated
`--rpath <directory>`, repeated `--env KEY=VALUE`, `--inspect`, and `--` to
end options. `--inspect` prints the selected architecture, Mach-O type,
segments, and every declared dylib dependency without executing the image.
The CLI inherits the current Windows environment, uses the Mach-O image's
parent directory as the initial Wintosh prefix, and forwards the image path
plus all following arguments to the native Darwin bootstrap. A missing image
prints the usage text and returns exit code `64`; loader or bootstrap errors
are reported on stderr and return exit code `1`; a successful image exit code
is forwarded (clamped to the Windows process range when necessary).

The release CLI is currently a native loader/bootstrap entry point, not yet a
complete macOS application runtime. It requires a supported Mach-O format and
architecture, and does not yet provide full Darling framework coverage,
automatic `.app` discovery, or universal execution of arbitrary macOS apps.

The current loader baseline includes dyld image enumeration, dynamic Mach-O
`dladdr` symbol lookup, initializer/terminator ordering, and token-scoped
ownership cleanup for newly registered Objective-C classes and protocols.
Mach-O category registration, category methods/properties/protocols, and
initializer/terminator ordering are covered by the current Objective-C/dyld
smokes. Selector-reference teardown, complete dyld namespace semantics, and
arbitrary application compatibility remain outside this release boundary.

The detailed build and evidence rules are in `../docs/BUILD-AND-TEST.md`.

## Installation layout

The CMake install target places the product entry points in `bin/`:

```powershell
cmake --install build --config Release --prefix dist
.\dist\bin\wintosh.exe --help
```

The smoke executables are intentionally not installed as product binaries;
they remain available from the build directory for verification and CI.
The CPack ZIP contains the same product-only `bin/` layout:

```powershell
cpack --config build/CPackConfig.cmake -B build
```
