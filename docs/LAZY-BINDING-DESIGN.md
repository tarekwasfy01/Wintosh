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

## Provenance

The behavioral target is Apple's Darwin `dyld` lazy-binding contract as exposed
by the Mach-O ABI. The Windows implementation is Wintosh-original adapter
code and does not copy Apple's or Darling's implementation. Darling remains
the reference project: https://github.com/darlinghq/darling
