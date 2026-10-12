# Lazy Mach-O binding on Windows

This document records the next implementation boundary for Wintosh's
Darwin-shaped loader. The native Clang path now has a session-backed
`dyld_stub_binder` entry, but the complete on-demand Mach-O E2E gate remains
open.

## Current state

The Mach-O parser records lazy bind actions, and `MachOImage::ApplyLazyBindAction`
can apply one validated action to a mapped image when the resolved provider
addresses are already available. The bootstrap and dynamic-image paths use
`ApplyLazyBindActions` before entering guest code. This is the tested eager
compatibility path.

The exported `dyld_stub_binder` bridge is fail-closed unless a validated
per-image session is active. The bootstrap now keeps the main-image session
alive across guest entry, while missing context still returns zero and the
assembly entry aborts deterministically instead of guessing a target.

## Required context boundary

The next implementation must introduce an explicit per-image session carrying:

1. the `MachOImage` identity and its `Mapping`;
2. the resolved binding table for the image's dependency graph;
3. a validated lazy-pointer/action lookup;
4. synchronization and lifetime ownership that outlives the guest stub call;
5. a thread-local activation token only while guest code is executing.

The binder entry must receive or recover a validated action identity from the
Darwin stub convention, resolve exactly one action, patch only that action's
slot, and return the resolved target using the host's SysV trampoline. Missing
context, an invalid action, an expired session, or an unresolved non-weak
symbol must remain deterministic errors.

## Verification gate

To complete the real binder gate, add a fixture with two lazy imports and prove
that the first call patches only slot A, the second call patches only slot B,
repeated calls reuse the patched targets, and an invalid action cannot modify
memory. The existing eager lazy-bind tests must remain green. The old
fail-closed bridge test must remain present until this gate passes.

## Implemented groundwork

`MachOLazyBindingSession` now keeps one `MachOImage`, its `Mapping`, and its
resolved binding table together. The bootstrap path uses this session for
provider and main-image eager lazy binding, and keeps the main-image session
active across guest entry so the guarded external stub binder can resolve
on-demand actions. Full action-index recovery from an unmodified Mach-O guest
stub remains open.

The dyld smoke now exercises `MachOLazyBindingSession::ApplyOne` for a real
lazy action and verifies the patched slot, so the session boundary is covered
by executable test code.

The activation test also covers nested scopes: the inner guard restores the
outer session, and the outer guard restores an empty context on exit.

The session also now provides a scoped thread-local `Activate()` guard and
`Current()` lookup. The dyld smoke verifies activation, nesting cleanup, and
absence of leakage after scope exit. This supplies the context lookup half of
the future binder boundary; it does not yet recover the Darwin action index.

The session now also maps a validated mapped slot address to exactly one lazy
action through `FindActionForSlot`. The dyld smoke checks the expected action
index and rejects a neighboring non-slot address before applying it. Remaining
binder work is recovering that slot address from the real stub-call register
convention and passing it into this lookup.

`ApplyForSlot` now combines the validated lookup and one bind application and
returns false when no lazy action owns the supplied slot. This remains useful
for architecture-specific trampoline validation; the active x86_64 binder
uses the corresponding lazy-info lookup.

Each parsed bind action now retains its byte offset in the originating bind
stream. `FindActionForLazyInfo` uses that provenance to map the Darwin binder's
lazy-info offset back to exactly one lazy action; the dyld smoke checks the
matching offset and rejects its neighbor. This is the first real bridge from
the Darwin binder metadata to Wintosh's action table.

The ABI bridge now exposes `darling_windows_abi_resolve_lazy_binding`. It is
fail-closed when no session is active or the offset is unknown, and otherwise
applies the action and returns the resolved target. The dyld smoke exercises
both the valid active-session path and the invalid-neighbor path.

The assembler fixture now calls this real session resolver rather than a
hardcoded target whenever a session is active. Its end-to-end smoke passes the
parsed lazy-info offset through the Darwin metadata stack and verifies the
resolved target receives the original guest arguments. The production
`dyld_stub_binder` export is now guarded by that active session and remains
fail-closed outside it; a real Mach-O guest lazy-stub fixture is still needed
for the final on-demand proof.

`DecodeDarwinX86_64StubFrame` now validates and names the three metadata words
(`image_cache`, `lazy_binding_info`, and `return_address`) in the confirmed
Darwin order. Its null/zero rejection is covered by the dyld smoke fixture.
The metadata parser is supplemented by the Clang x86_64 trampoline, which
preserves guest registers and jumps to the resolved target; the parser itself
still does not recover context from arbitrary guest memory.

## Confirmed x86_64 Darwin stub contract

The historical Apple dyld `dyld_stub_binder.s` source documents the x86_64
entry stack as follows: at the entry stack pointer, the stub's return address
is at `sp+16` after the binder prologue, the lazy-binding-info offset is at
`sp+8`, and the image-loader-cache pointer is at `sp+0`. The binder preserves
the general-purpose and XMM registers before calling the resolver and restores
them before jumping to the resolved target. Wintosh records this as an ABI
contract and implementation target; it does not redistribute Apple's
assembler. The source reference is
https://github.com/opensource-apple/dyld/blob/master/src/dyld_stub_binder.s.

The remaining implementation task is therefore a Wintosh-owned x86_64
trampoline that preserves the guest register/XMM state, decodes the two
metadata words, resolves the lazy-info offset to a session action, and jumps
to the returned target. Until a fixture proves that full round trip, the
export remains fail-closed.

The repository now contains a Wintosh-owned x86_64 trampoline at
`wintosh-windows/abi/darling_windows_stub_trampoline_x64.S`. It saves the
guest GPR/XMM argument state, calls the active-session resolver, restores the
state, and jumps to the returned target. The native Clang build exports this
entry as `darling_windows_abi_dyld_stub_binder`; when no binding session is
active, resolution returns zero and the entry aborts through the explicit
fail-closed diagnostic. No synthetic target is used in that path.

The dyld smoke now invokes an explicit assembler fixture that lays out the
three metadata words, enters the probe with `jmp`, and verifies that the
resolved target sees the original SysV integer arguments after the trampoline
restores them. This proves the register-preservation/jump mechanics and the
session-backed lazy-info-to-target handoff. It does not yet keep a session
active around the full arbitrary guest execution lifetime, so general
real-world lazy binding in an unmodified `.app` is not claimed yet.

## Provenance

The behavioral target is Apple's Darwin `dyld` lazy-binding contract as exposed
by the Mach-O ABI. The Windows implementation is Wintosh-original adapter
code and does not copy Apple's or Darling's implementation. Darling remains
the reference project: https://github.com/darlinghq/darling
