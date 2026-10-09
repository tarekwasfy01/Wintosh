# Cross-source extraction matrix

This is the consolidated extraction record over every checked-out source tree.
It records what each project actually contributes to the Windows port;
it is intentionally family-level rather than pretending that every source
file has been legally or semantically cleared for reuse.

| Source | Primary material extracted | Direct port value | Boundary / open issue | Action in this workspace |
|---|---|---|---|---|
| Darling | modern macOS userland, dyld, Objective-C, frameworks, darlingserver | canonical ABI and behavior target | GPL/component boundaries; huge framework surface | native adapter plus optional WSL/backend path |
| WSL | Windows-hosted Linux integration and syscall/VM boundary | execution substrate reference | WSL availability and Linux distribution state | source catalogued; runtime probe returns `20` on access denial |
| PureDarwin/XNU | Darwin headers, kernel contracts, Mach/BSD semantics | ABI/error oracle | Apple/XNU provenance and kernel-only behavior | reference only; no Apple code copied |
| iPAsimulator | iOS/Mach-O/framework emulation patterns | app-driven framework prioritization | iOS is not macOS; dependency licenses | reference notes and tests only |
| wine64-darwin | Wine/Darwin loader and compatibility experiments | host/shim separation | historical project age and mixed licenses | reference only |
| SherlockElf | ELF/Mach-O inspection and loader analysis | binary-format diagnostics | tool-specific license and scope | reference only |
| touchHLE | HLE framework shims, app-driven API coverage | Foundation/UIKit prioritization | MPL source, GPL binary distribution, dependency licenses | reference only |
| DirectHW | privileged Darwin hardware driver/framework | kernel/user boundary | signed Windows driver and security model | no user-mode hardware emulation |
| Cocotron | historical Foundation/AppKit/Core* API inventory | framework layering and API grouping | old Xcode/Apple assumptions; nested components | no source copied; framework row remains missing |
| Darwin_Computa | Boxedwine64 emulated Linux kernel + real Darling/darlingserver | highest-value modern architecture | GPLv2 plus Boxedwine/Darling/rootfs boundaries | WSL/backend contract added |
| macho-ld | in-memory Mach-O/dylib/fat/chained-fixup loader | loader edge-case fixtures | no verified top-level license; lacks ObjC init/weak bind/32-bit | extend Mach-O smoke matrix |
| Classix | PEF/CFM/PowerPC VM/native resolver separation | explicit guest/native boundary | GPLv3; Mac OS 9 only; limited tests | architecture reference only |
| VST-ace | mixed PE/Mach-O/CFM hosts and shims | integration and Objective-C gap evidence | third-party binaries/reverse-engineering corpus | no binaries/code copied |

## Port batches derived from the extraction

1. **Loader:** fat Mach-O, chained fixups, dyld info, symbol binding,
   Objective-C registration and weak-bind behavior must be tested separately.
2. **Kernel boundary:** native Windows primitives remain a compatibility
   backend; modern full Darling should use a Linux/WSL or emulated-Linux
   backend running real `darlingserver`.
3. **Frameworks:** implement Foundation/CoreFoundation in small ABI families
   on top of the existing Objective-C runtime; do not import a whole historic
   Cocotron tree.
4. **Guest/native transitions:** preserve explicit address-space and resolver
   boundaries; do not treat a host function pointer as a guest ABI proof.
5. **Provenance:** every imported family requires a source row, license row,
   x64/Win32 or guest-runtime test, and an explicit remaining-gap field.

## Current evidence boundary

The native Windows adapter has verified low-level smoke coverage, including
Mach VM and local port sets. The Darwin_Computa README's claims of real Darling
CLI execution are upstream claims and have not been reproduced locally. The
WSL probe is implemented but this host returns `Wsl/E_ACCESSDENIED`.

## Checkout and license accounting

The named source checkouts represented here are `darling-source`,
`wsl-source`, `puredarwin-source`, `ipasim-source`, `wine64-darwin-source`,
`sherlockelf-source`, `touchhle-source`, `directhw-source`,
`cocotron-source`, `darwin-computa-source`, `macho-ld-source`,
`classix-source`, and `vst-ace-source`. Their original license files remain
inside those checkouts where available. The Darling-specific external audit is
tracked separately in `licenses/EXTERNAL-COMPONENT-PROVENANCE.csv`: it has
150 rows, with 147 still requiring manual license review. The smaller
`licenses/SOURCE-LICENSE-INVENTORY.csv` records recognized license and notice
files, so its row count must not be confused with the full component count.

No source analysis entry grants permission to copy code. Before a family is
ported, its provenance, applicable license, copyright notices, and whether the
implementation is original or derivative must be recorded in the porting
inventory.

Run the reproducible license-file and inventory preflight from the repository
root with:

```powershell
.\tools\Check-LicensePreflight.ps1
```

The preflight also scans the 90 current `wintosh-windows` C/C++ source and
header files for a license, copyright, or GPL notice in their first 14 lines;
the current result is 90 covered and 0 missing notices. This is a notice
coverage check, not a substitute for component-by-component license review.
The release workflow runs this preflight before configuring CMake.
