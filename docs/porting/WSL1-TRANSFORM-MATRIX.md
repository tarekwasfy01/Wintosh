# WSL1-to-Windows transformation matrix

This document records where WSL1 is useful as an implementation reference for
the native Wintosh backend. It is an architectural map, not a claim that WSL1
implements Darwin or macOS behavior. WSL source remains external reference
material under its own MIT license; no WSL code is copied into the native
adapter by this matrix.

## Reusable implementation patterns

| Darling/Wintosh family | WSL1 reference areas | Native Windows mapping | Boundary |
|---|---|---|---|
| Unix path and filesystem translation | `wsl-source/src/linux/init/drvfs.*`, `wslpath.cpp`, `src/linux/plan9/p9fs.*` | UTF-8/UTF-16 path conversion, `CreateFileW`, `MoveFileExW`, directory handles | DrvFs semantics are not Darwin vnode semantics |
| Process startup and environment | `src/linux/init/binfmt.cpp`, `src/linux/init/init.cpp` | PE/Mach-O dispatch, environment snapshots, process/job objects | WSL binfmt is Linux-loader policy, not dyld |
| Socket and network forwarding | `src/linux/init/NetworkManager.cpp`, `src/windows/common/ConsommeNetworking.*`, `DnsResolver.*` | Winsock descriptor model and resolver bridge | Linux socket flags and Darwin flags still need explicit translation |
| IPC channels | `src/shared/inc/SocketChannel.h`, `src/windows/inc/wslrelay.h` | Named pipes and the existing Mach broker envelope | WSL relay framing is not Mach port-right semantics |
| Syscall filtering/dispatch | `src/linux/init/SecCompDispatcher.cpp`, `src/linux/inc/seccomp_defs.h` | Host-symbol dispatch and fail-closed unsupported calls | seccomp numbers cannot be reused as Darwin syscall numbers |
| Console and terminal | `src/windows/common/ConsoleState.*`, `src/linux/plan9/p9io.*` | Console handles, input modes, PTY-compatible state | Windows console behavior differs from a Darwin TTY |
| DNS/name services | `src/windows/common/DnsResolver.*`, `src/linux/init/DnsTunnelingManager.*` | Winsock resolver and explicit error conversion | No claim of full mDNS/Bonjour compatibility |

## Provenance and licensing

The WSL checkout is retained in `wsl-source/` for source inspection and is
covered by `licenses/LICENSE-wsl-MIT.txt` and `licenses/NOTICE-wsl.txt`.
Darling behavior remains referenced separately under
`licenses/LICENSE-darling-GPL-3.0.txt`. Wintosh implementation files retain
their own project notice; source provenance is catalogued in
`licenses/EXTERNAL-COMPONENT-PROVENANCE.csv` and
`licenses/SOURCE-LICENSE-INVENTORY.csv`.

## Current status

The matrix identifies reusable patterns but does not itself add runtime
support. The native Windows adapter currently has verified process, file,
socket, Mach IPC, loader, and selected CoreFoundation families. Remaining
work includes broader libSystem parity, full Darwin flag/error semantics,
CoreServices/CFNetwork/Security, framework APIs, and ARM64 execution.
