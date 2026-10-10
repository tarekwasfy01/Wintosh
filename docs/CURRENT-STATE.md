# Complete current workspace state

This file closes the gap between the working directory and the release
documentation. The machine-readable inventory is
[CURRENT-STATE-MANIFEST.csv](../CURRENT-STATE-MANIFEST.csv).

## Contents confirmed on 2026-10-08

- 13 complete reference/source checkouts remain available locally, but are not
  committed to the public Wintosh repository.
- The local primary Darling checkout contains 31,720 files.
- The published `wintosh-windows` overlay contains 83 implementation, header,
  CMake, and smoke-test files; the full Darling checkout remains local-only.
- The license bundle contains 12 files, including Darling GPL text, WSL
  license/notice text, source inventories, and provenance review material.
- Generated x64 and Win32 build trees are present locally and are explicitly
  classified as generated local artifacts, not corresponding source.
- All release documentation is present and linked from
  [DOCUMENTATION-INDEX.md](DOCUMENTATION-INDEX.md).

## Execution boundary

The supported architecture target is the native Windows adapter and runner;
WSL is optional reference/backend infrastructure, not a runtime prerequisite.
On the current host, direct WSL probing returns `Wsl/E_ACCESSDENIED`, so no
WSL-backed Darling execution is claimed or required for the native smoke-test
suite.

The native `wintosh.exe` Release target was built and verified with
`--help` and `--version` (`Wintosh 0.1.1`). No standalone Mach-O fixture is
bundled in the current workspace, so `--inspect` and actual Mach-O execution
remain unclaimed until a licensed test image is supplied.

## What is included in a source release

Include the Windows port source, all documentation, and the `licenses`
directory. Do not vendor the large upstream reference checkouts; link to the
original repositories and keep every applicable license and notice here. A
source release must not silently replace a third-party license with the
Wintosh-original MIT notice.

## What is not claimed

This manifest proves that the current files are present in this workspace. It
does not prove that all 150 Darling external components are license-cleared,
that the NTFS-incompatible Heimdal checkout is complete, that the global build
passes, or that arbitrary macOS applications run on Windows. Those boundaries
remain in [STATUS.md](STATUS.md) and [RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md).
