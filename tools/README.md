# Wintosh porting tools

These scripts support the native Windows port without bundling upstream
Darling source code.

## Darwin_Computa through WSL1

`Invoke-DarlingWslBackend.ps1 -Mode DarwinComputa` routes one Mach-O command
through WSL1, Darwin_Computa/Boxedwine, and Darling. The paired
`run-darwin-computa-once.sh` waits for the guest process exit, forwards its
output and exit status to the PowerShell caller, then stops only the Boxedwine
process it started.
`DSERVER_INIT` accepts a path only, so this mode does not yet pass guest argv.
Pass `-WindowsAppPath <bundle.app>` and optionally
`-GuestBundleExecutable <name>` to copy a Windows app bundle temporarily into
Darling's guest-visible `Applications` directory and run its `Contents/MacOS`
executable. If the executable name is
omitted, the wrapper reads `CFBundleExecutable` from XML or binary Info.plist
using Python `plistlib`, then falls back to the bundle name. The wrapper removes
the temporary staging directory when the run ends.
Before execution it also materializes a requested symlinked system executable
from `darling.zip` or the staged app bundle into the writable Darling rootfs
overlay so its Mach-O target can be launched by the current direct-init path.
See [the backend contract and verified limits](../docs/DARLING-WSL-BACKEND.md).

## Mathematical capability report

`python tools/porting_matrix.py` composes Boolean transformation bounds for
the native and Darwin/Linux/WSL1/Windows routes. Supply CTest JUnit results
to enable bounded native evidence; without results all implemented rows
remain unproven. See [the model and commands](../docs/porting/MATHEMATICAL-MODEL.md).

## MIG inventory

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Scan-MigDefinitions.ps1 `
  -SourceRoot .\darling-source `
  -OutputCsv .\build\mig-definitions.csv
```

The inventory records paths, SHA-256 values, detected subsystem/routine names,
and an explicit upstream-license review marker. It does not copy, transform,
or relicense upstream files.

## Stub registration fragment

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Generate-MigRegistry.ps1 `
  -InventoryCsv .\build\mig-definitions.csv `
  -OutputCpp .\build\generated-mig-registry.cpp
```

Generated entries are empty integration stubs with local IDs. They are not
Darwin implementations and must be replaced or completed after reviewing the
corresponding upstream definitions and licenses.

## Bounded i386 Mach-O linker

`python tools/link_i386_macho.py --output app.macho main.o helper.o` accepts
little-endian i386 `MH_OBJECT` files. It combines each input's `__TEXT,__text`,
optional `__TEXT,__StaticInit` code, separate optional `__TEXT,__cstring` and
`__const` sections, and optional `__DATA,__data` and zerofill sections plus
`__DATA,__mod_init_func` / `__mod_term_func`, resolves external
definitions between those sections, and supports 32-bit
`GENERIC_RELOC_VANILLA` PC-relative and absolute references plus paired
scattered `GENERIC_RELOC_SECTDIFF` and `GENERIC_RELOC_LOCAL_SECTDIFF` fields
used by PIC code. Zerofill and
`__common` storage are placed in the zero-initialized data segment; absolute
pointers receive dyld type-2 rebases for the image slide. It resolves bounded
`__IMPORT,__pointers` slots against supplied objects and emits local function
stubs plus bind opcodes for unresolved PC-relative calls. Those calls resolve
only if Wintosh's host-symbol table exports the requested Darwin name; this
does not load Darwin shared libraries. Constant-section bytes are emitted in
a `__TEXT,__const` section in the non-writable `__TEXT` segment. Other
relocation forms, dylibs, CRT, and frameworks remain outside this prototype.
It emits a segmented `MH_EXECUTE` and drops unsupported
metadata such as unwind sections. Two-object call and data fixtures, plus
single-object initialized and zero-initialized global fixtures, ran in Wintosh
and returned 42. `__cstring` pointer and direct references returned 65 and 66.
Host-bound `_puts` and `_printf` calls also ran; the formatted guest returned
3. A heap guest called `_malloc`/`_free` and returned 42, `_exit(23)` terminated
with status 23, and `_atexit` ran a cleanup callback that called `_puts` after
`main` returned 3. A libc guest also copied `ABC` with `_memcpy`, checked
string lengths, read `WINTOSH_FIXTURE=42` through `_getenv`, and returned 42.
The same fixture compiled as PIC returned 42 as well. These are host exports,
not a complete libSystem. Separate PIC `main` and `helper` objects linked and
ran in either input order, returning 84 after a local literal access.
A constructor guest whose `__mod_init_func` set a global before `main` returned
42, proving that the emitted section reaches Wintosh's initializer phase.
A linked `__mod_term_func` fixture ran after `main`, printed `guest terminator`,
and preserved the constructor guest's status 42.
A mixed read-only guest containing both `__const` and `__cstring` ran and
returned 40 after dereferencing the CString pointer.
A no-standard-library C++ guest also constructed a global object before `main`
and ran its `___cxa_atexit` destructor afterward, printing `cxx destructor`
while preserving status 42. `___dso_handle` is represented by a guest-local
zero-fill cell. This is limited C++ object-lifecycle evidence, not a full C++
runtime. A compiler-generated `_Thread_local` integer used an i386
`GENERIC_RELOC_TLV`, `__thread_data`, `__thread_vars`, and `__thread_ptrs`;
Wintosh initialized its Darwin TLV descriptor through an i386 register-to-stack
bridge and returned 42 after changing the initialized variable from 40 to 42
and adding it to a zero-initialized TLS variable. The reusable check is
`wintosh-windows/tests/i386_tlv_e2e.ps1`; it requires Clang, Python,
and a Win32 runner. The same check also runs a `pthread_create`/`pthread_join`
worker: it receives the initial value 40, changes its copy to 77, and leaves
the main thread's value at 9. This proves one static C TLV image on two threads,
not multiple images or dynamic loading. A separate C++ fixture now exercises
thread-local construction and destructor registration on a worker; run it with
`wintosh-windows/tests/i386_cpp_tlv_e2e.ps1` and the same Clang, Python, and
Win32 runner inputs. It expects guest exit 42 after `pthread_join` observes the
worker's destructor completion. This remains a single i386 ABI case, not broad
C++ runtime compatibility.
See
[LINKER-RESEARCH.md](../docs/porting/LINKER-RESEARCH.md) for the exact contract.

The scripts and this documentation are original Wintosh tooling. Upstream
Darling, WSL, and other referenced projects retain their original licenses;
see the repository license inventory before distributing derived artifacts.
