# Wintosh porting roadmap

This roadmap keeps the native Windows port aligned with the family matrix. A
family is not called complete merely because a synthetic smoke test passes;
the named runtime gate and the semantic boundary must both be recorded.

## Next implementation order

1. **Darwin startup and Mach-O execution** — extend the existing x86_64 Clang
   path from the chained/classic fixtures to additional real relocation,
   lazy-binding, and startup-stack fixtures. Gate: `wintosh_macho_printf_e2e`
   plus the isolated license-clear startup trampoline and the explicit ARM64
   fail-closed gate. Remaining: native ARM64 execution, true on-demand
   `dyld_stub_binder`, full dyld protocol parity, arbitrary Apple frameworks.
2. **Foundation/CoreFoundation** — grow the existing C ABI in cohesive value,
   collection, plist, run-loop, and notification families. Gate each family in
   `corefoundation_smoke` and keep Apple/Cocotron provenance separate from
   Wintosh-original implementation. Remaining: Objective-C class ABI and full
   Unicode/Foundation behavior.
3. **Mach IPC and task semantics** — expand the broker only where the wire
   layout and ownership rules are tested together. Gate: Mach ABI and broker
   smokes. Remaining: complete per-task namespaces, native handle adoption,
   full wire ABI, vouchers, and VM lifetime parity.
4. **Thread and scheduler semantics** — continue pthread cancellation,
   process-shared/robust synchronization, and Mach policy readback. Gate:
   `host_api_smoke`, `pthread_sync_smoke`, and `mach_abi_smoke`. Windows
   priority mapping must remain labeled as an adapter, not real-time Darwin
   scheduling.
5. **License closure** — review the remaining external component records
   before copying any reference implementation. The provenance CSV and
   component-specific notices remain authoritative; no Apple binary framework
   is redistributed by this project.

## Current verified baseline

The native Clang Release suite has 60/60 passing tests. The verified subset
includes x86_64 Mach-O execution, fixed-arity and narrow variadic bridges,
Darwin-like prefix paths, Mach IPC adapters, CoreFoundation primitives,
Preferences persistence/managed views/cross-process FIFO events and recovery,
reentrant observer delivery, and selected pthread synchronization behavior.

The current branch is still a partial compatibility layer. The exact status of
each family is in [`PRIMITIVE-MATRIX.csv`](../PRIMITIVE-MATRIX.csv), while
source provenance and licenses remain in [`licenses/`](../licenses/README.md).
