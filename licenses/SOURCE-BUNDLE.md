# Included source

The published Wintosh repository does not vendor the large upstream source
checkouts. They remain local working references and must be obtained from
their original repositories under the terms listed in the provenance files:

- Complete Darling source: https://github.com/darlinghq/darling
- WSL reference source: https://github.com/microsoft/WSL
- Local Darling checkout, when present: `../darling-source`
- Local Windows port overlay: `../darling-source/src/native/windows`
- Porting and dependency inventory: `../PORTING-INVENTORY.md`
- License and provenance bundle: this directory

The Windows overlay currently contains the native host adapters and smoke
tests under `darling-source/src/native/windows`, including process, threading,
syscall, socket, Mach/Mach-O, Objective-C, dynamic-loading, broker, terminal,
and Foundation type-encoding families. The exact current file set is the
source tree itself; do not reduce a corresponding-source archive to an old
short list of representative files.

The overlay is intentionally kept inside the local Darling source tree so that
developers can inspect the exact integration boundary. It is not part of the
published Wintosh tree. The WSL checkout is reference material only; no WSL
implementation is silently relicensed as Darling code.

## Source completeness status

The local Darling repository and downloaded submodule objects were used during
development. One Heimdal submodule cannot be checked out on NTFS because an
upstream filename contains `:`. That limitation is recorded in
`SOURCE-LICENSE-INVENTORY.csv`. The published Wintosh repository is not a
Darling corresponding-source archive; obtain Darling source upstream.
