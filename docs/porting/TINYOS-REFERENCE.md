# TinyOS reference assessment

## Finding

`tinyos/tinyos-main` is the TinyOS source tree for low-power wireless embedded
devices. Its README describes the software as a component OS for sensor-network
nodes and documents Linux and macOS development setup. That is not evidence
that the TinyOS runtime implements the APIs of those desktop operating systems,
or that it can execute their applications.

The repository's Make 3 system is a host-side build orchestrator. A build
selects a concrete hardware target, then needs nesC and that target's compiler
toolchain. The source tree separates interfaces, system components, chip code,
platforms, and sensor boards. Make 3 also aims to support out-of-tree TinyOS
trees and new targets without changing the main source tree.

## Relevance to Wintosh

| TinyOS concept | Transfer to Wintosh | Why |
|---|---|---|
| Interface/component boundaries | Architectural pattern only | Wintosh needs explicit Darwin, Mach-O, Objective-C, Foundation, AppKit, and Win32 contracts; TinyOS interfaces model embedded components, not Cocoa. |
| Hardware target / platform split | Useful analogy for backend matrix | A Wintosh host backend (Win32, WSL1-assisted, or future host) should be distinguished from the guest ABI and macOS framework contract it must implement. |
| Out-of-tree platform support | Future plugin/build-system design | Optional host/guest adapters can be isolated without changing core ABI code. This does not provide any Darwin implementation. |
| Make 3's explicit target/dependency model | Build-system reference | Wintosh already uses CMake and has compiler-specific assembly selection; TinyOS does not supply a compatible macOS runtime or Windows bridge. |
| TinyOS application/runtime code | Not reusable for this goal | It targets constrained wireless microcontrollers and has no Mach-O loader, Darwin syscalls, Objective-C runtime, or AppKit. |

## Capability matrix

Let a requested execution route be the tuple `(h, a, k, r, f)`: Windows host
`h`, Mach-O architecture `a`, Darwin kernel contract `k`, Objective-C runtime
`r`, and framework set `f`. A route is viable only when every layer exists:

`V(h,a,k,r,f) = H(h) · L(a) · K(k) · O(r) · F(f)`

Here each factor is `1` only when implemented and verified; unknown or missing
factors are `0` for a support claim. TinyOS contributes no implementation to
`L`, `K`, `O`, or `F`:

| Layer | Wintosh requirement | TinyOS contribution | Value for macOS app execution |
|---|---|---|---|
| `H` | Host process, threads, files, windows | Build tooling runs on supported development hosts | Indirect; does not implement Windows host services |
| `L` | Mach-O parsing, relocations, dyld images | None | `0` |
| `K` | Darwin/Mach syscalls and IPC | TinyOS embedded scheduler and hardware abstractions | `0`; different kernel contract |
| `O` | Objective-C dispatch, ARC/runtime metadata | nesC components and wiring | `0`; different language/runtime model |
| `F` | Foundation, AppKit, graphics, services | Sensor/network libraries | `0` |

TinyOS's useful transferable idea is the explicit target graph. In Make 3,
hardware targets compose platform, microcontroller rules, optional sensor
boards, and build extras; dependencies determine build order. Wintosh can use
the same *shape* for its own build configuration, but the nodes must describe
host ABI, guest architecture, runtime backend, and framework coverage. For
example, a build target can be modeled as a directed acyclic graph
`G=(V,E)`, where each edge `u→v` means `v` requires `u`; a successful build
requires a topological ordering of `G`. This helps build the runtime reliably,
but does not make any unsupported execution factor equal to `1`.

TOSSIM is likewise a simulator for TinyOS sensor-network programs. It is not a
general operating-system emulator and cannot supply a Darwin syscall layer or
run macOS binaries. No TinyOS source is copied into the runtime.

The correct transfer is a design constraint, not source transplantation:

`HostSupport(h) AND GuestContract(g) AND FrameworkCoverage(f)`

must all hold before Wintosh can claim a `(host, macOS app)` route. The fact
that TinyOS can be developed from more than one host OS must not be read as
“TinyOS runs every OS” or “TinyOS runs apps from those OSes.” Its README
documents Debian/Ubuntu and macOS development setup; Make 3 requires GNU Make,
nesC, and a target-specific compiler toolchain. The Make 3 documentation does
not list Windows as a supported host. Host portability of a build tool is a
separate property from guest ABI compatibility:

`BuildsOn(h) ⇏ ExecutesGuest(g)`

TinyOS's documented host setup affects only its build environment; it does not
make `GuestContract(macOS)` true. No TinyOS code is copied into Wintosh.

## Upstream status checked 2026-11-17

GitHub marks `tinyos/tinyos-main` archived and read-only. Its README points to
`tp-freeforall/prod` as the active development tree, but GitHub also marks that
repository archived. The `tinyos` organization currently lists only three
repositories, all archived (`tinyos-main`, `nesc`, and `tinyos-release`). The
README's transition plan is therefore stale as a current-development pointer;
this check did not find a live canonical successor under the `tinyos`
organization. Recheck before relying on future upstream changes. The archived
repository's README describes TinyOS as an OS for embedded wireless devices
and documents Debian/Ubuntu and macOS developer setup. Make 3 builds selected
hardware targets using nesC and the target compiler; its documentation does not
claim universal host-OS support.

## Sources

- [tinyos-main repository and README](https://github.com/tinyos/tinyos-main)
- [TinyOS Make 3 documentation](https://github.com/tinyos/tinyos-main/blob/master/support/make/README.md)
- [TinyOS source tree layout](https://github.com/tinyos/tinyos-main/tree/master/tos)
- [Debian development setup](https://github.com/tinyos/tinyos-main/blob/master/doc/00c_Setting_Up_Debian_Development)
- [tp-freeforall/prod repository](https://github.com/tp-freeforall/prod)
- [TinyOS organization repositories](https://github.com/orgs/tinyos/repositories)
