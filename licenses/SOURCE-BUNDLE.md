# Included source

The source is included with this workspace and must accompany any Stage 1
binary distribution:

- Complete Darling source checkout: `../darling-source`
- Windows port source overlay: `../darling-source/src/native/windows`
- Porting and dependency inventory: `../PORTING-INVENTORY.md`
- License and provenance bundle: this directory

The Windows overlay currently contains the native host adapters and smoke
tests under `darling-source/src/native/windows`, including process, threading,
syscall, socket, Mach/Mach-O, Objective-C, dynamic-loading, broker, terminal,
and Foundation type-encoding families. The exact current file set is the
source tree itself; do not reduce a corresponding-source archive to an old
short list of representative files.

The overlay is intentionally kept inside the Darling source tree so that a
source recipient can inspect the exact integration boundary and rebuild the
Stage 1 process-host proof. The WSL checkout in `../wsl-source` is reference
material only; no WSL implementation is silently relicensed as Darling code.

## Source completeness status

The main Darling repository and downloaded submodule objects are present. One
Heimdal submodule cannot be checked out on NTFS because an upstream filename
contains `:`. That limitation is recorded in
`SOURCE-LICENSE-INVENTORY.csv`; no binary distribution should claim a complete
corresponding-source archive until that checkout exception is resolved or the
affected source is supplied through a filesystem that supports the name.
