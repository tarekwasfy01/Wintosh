# Aequora reference check

Checked 2026-11-17. The supplied `irshadlab/aequora` repository contains Part
32, a desktop-runtime architecture for the
Aequora synchronization engine, not a Darwin emulator, Mach-O loader, or macOS
runtime. It does not add an execution route to Wintosh's capability matrix.
The current README's Linux/Windows/macOS desktop-runtime claims refer to
Aequora's own Rust synchronization client and its OS integration; they do not
mean it can load or run macOS binaries on Windows.

The Part 32 details confirm four transferable design patterns, all at the
desktop-runtime boundary: thin OS adapters, optional process separation,
versioned local IPC, and named conformance gates. It proposes Windows named
pipes and a protocol version, message kind, and payload length; it also calls
for an agent build ID and registry generation during version negotiation.
Wintosh already has the analogous named-pipe Mach broker. Its WIPC envelope
currently uses a fixed version field (version 6 in the codec) and rejects any
other version; the session-open operation is not a version negotiation. It
does not exchange build IDs or registry generations. The transferable
follow-up is an explicit handshake and capability report. A shared transport
does not implement Darwin Mach rights, ports, or message semantics.

## Transfer matrix

Treat each row as a contract-preserving map. `1` means the source provides a
directly reusable design pattern; `0` means it does not provide that
implementation. `?` means the idea still needs a Wintosh-specific design or
evidence. `A` is Aequora Part 32; `W` is Wintosh; `E` is macOS application
execution capability.

| Contract | A: desktop architecture | A → W pattern transfer | A → E implementation edge |
|---|---:|---:|---:|
| Thin platform adapters | 1 | 1 | 0 |
| Optional in-process/agent modes | 1 | 1 | 0 |
| Versioned Windows named-pipe transport | 1 | 1 (WIPC has a fixed version; negotiation is absent) | 0 |
| Darwin Mach message/rights semantics | 0 | ? | 0 |
| Mach-O loading, dyld, Objective-C ABI | 0 | 0 | 0 |
| AppKit, CoreGraphics, macOS frameworks | 0 | 0 | 0 |
| Application execution on Windows | 0 | 0 | 0 |

For the executable contracts `d = (Mach, Mach-O/dyld/ObjC, AppKit/graphics,
macOS framework behavior)`, Part 32 has source vector `a_d = (0,0,0,0)`.
Thus for any Boolean transfer matrix `T`, `T a_d = 0`: the documented patterns
have no executable Darwin implementation edge. They can improve how Wintosh
isolates and negotiates services, but cannot extend the set of Mac applications
that run.

| Aequora Part 32 pattern | Wintosh analogue | Transfer decision | Compatibility evidence still required |
|---|---|---|---|
| Thin per-platform adapters around a shared core | `platform/`, `frameworks/`, and `loader/` boundaries | Reuse the boundary rule; no Aequora implementation code applies | Guest ABI and framework conformance for each exported API |
| Optional sidecar/agent mode | Named-pipe Mach broker | Reuse only for process ownership and transport structure | Mach rights, task namespaces, wire ABI, and cross-process lifetime semantics |
| Versioned local IPC | WIPC v6 envelope, session/capability tokens | Partial: fixed-version framing exists; negotiation and peer metadata do not | Version negotiation, build/registry identity, and compatibility behavior remain separately gated |
| Explicit platform conformance suite | `PRIMITIVE-MATRIX.csv` plus smoke gates | Extend claims only when evidence exists | No smoke gate alone establishes full Darwin equivalence |

This is an architecture transfer matrix, not a claim that Aequora runs Mac
applications or implements Darwin services. Part 32 describes product
architecture and proposed integration patterns; its presence in the wiki is
not proof that those patterns are implemented in a reusable library.

No Aequora source code was downloaded or copied. Reviewed:
[Aequora Part 32: Desktop Runtime Architecture](https://github.com/irshadali5/aequora/blob/main/wiki/32-linux-windows-macos-desktop-runtime-architecture.md)
and [Aequora repository](https://github.com/irshadali5/aequora).
