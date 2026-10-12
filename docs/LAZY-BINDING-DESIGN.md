# Lazy Mach-O binding on Windows

This document records the next implementation boundary for Wintosh's
Darwin-shaped loader. It does not claim that on-demand `dyld_stub_binder`
execution is implemented.

## Current state

The Mach-O parser records lazy bind actions, and `MachOImage::ApplyLazyBindAction`
can apply one validated action to a mapped image when the resolved provider
addresses are already available. The bootstrap and dynamic-image paths use
`ApplyLazyBindActions` before entering guest code. This is the tested eager
compatibility path.

The exported `dyld_stub_binder` bridge remains fail-closed. It currently has
no image, mapping, lazy-action index, or provider-resolution context, so
silently guessing a target would risk patching the wrong image or address.

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

Before enabling the real binder, add a fixture with two lazy imports and prove
that the first call patches only slot A, the second call patches only slot B,
repeated calls reuse the patched targets, and an invalid action cannot modify
memory. The existing eager lazy-bind tests must remain green. The old
fail-closed bridge test must remain present until this gate passes.

## Implemented groundwork

`MachOLazyBindingSession` now keeps one `MachOImage`, its `Mapping`, and its
resolved binding table together. The bootstrap path uses this session for
provider and main-image eager lazy binding, and `ApplyOne` is the intended
future entry point for on-demand binding. The external stub binder is not
enabled yet; action-index recovery from the Darwin stub convention remains
open.

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
returns false when no lazy action owns the supplied slot. This is the direct
handoff primitive for a future architecture-specific Darwin stub trampoline;
it does not decode registers or claim that `dyld_stub_binder` is complete.

Each parsed bind action now retains its byte offset in the originating bind
stream. `FindActionForLazyInfo` uses that provenance to map the Darwin binder's
lazy-info offset back to exactly one lazy action; the dyld smoke checks the
matching offset and rejects its neighbor. This is the first real bridge from
the Darwin binder metadata to Wintosh's action table.

`DecodeDarwinX86_64StubFrame` now validates and names the three metadata words
(`image_cache`, `lazy_binding_info`, and `return_address`) in the confirmed
Darwin order. Its null/zero rejection is covered by the dyld smoke fixture.
This remains a metadata parser; it does not yet preserve guest registers or
jump to the resolved target.

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

The repository now contains an isolated Wintosh-owned probe trampoline at
`wintosh-windows/abi/darling_windows_stub_trampoline_x64.S`. It saves the
guest GPR/XMM argument state, calls a SysV resolver probe, restores the state,
and jumps to the returned target. It is compiled and linked in the native
Clang build but is not installed as `dyld_stub_binder`; a hand-built fixture
that supplies the Darwin metadata stack and verifies the post-jump registers
is still required before activation.

The dyld smoke now invokes an explicit assembler fixture that lays out the
three metadata words, enters the probe with `jmp`, and verifies that the
resolved target sees the original SysV integer arguments after the trampoline
restores them. This proves the register-preservation/jump mechanics only; it
does not yet resolve a real lazy-binding-info stream to a Mach-O slot.

## Provenance

The behavioral target is Apple's Darwin `dyld` lazy-binding contract as exposed
by the Mach-O ABI. The Windows implementation is Wintosh-original adapter
code and does not copy Apple's or Darling's implementation. Darling remains
the reference project: https://github.com/darlinghq/darling
