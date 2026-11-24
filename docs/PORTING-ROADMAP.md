# Wintosh porting roadmap

This roadmap keeps the native Windows port aligned with the family matrix. A
family is not called complete merely because a synthetic smoke test passes;
the named runtime gate and the semantic boundary must both be recorded.

## Next implementation order

1. **Darwin startup and Mach-O execution** — preserve the x86_64 relocation,
   startup and lazy-binding gates, then broaden ARM64 application coverage.
   An optional Dynarmic A64 backend now runs ARM64 Mach-O on x64 Windows with
   selected UNIX SVCs, dyld imports, Objective-C startup callbacks and
   CoreFoundation fixtures. Remaining: pin and reproducibly build Dynarmic
   from project metadata, review all built dependency licenses, implement more
   dyld/kernel contracts, and validate real app bundles. Windows ARM64 native
   execution still lacks equivalent end-to-end coverage. WSL1 does not supply
   the missing Darwin-to-Linux syscall edge. The controlled x86_64 binder
   trampoline is verified separately; end-to-end lazy-stub behavior still
   needs a real Mach-O runtime fixture.
   The detailed backend composition, memory invariants, and staged guest-to-host
   trap contract are recorded in
   [`AARCH64-X64-BACKEND.md`](porting/AARCH64-X64-BACKEND.md).
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

The current Clang build registers 78 CTest cases. The most recent focused
CoreFoundation run passed 4/4, including ARM64 allocator callbacks using the
imported `kCFAllocatorUseContext` data symbol. This is not a claim that all 78
tests or arbitrary macOS applications pass. Verified families and limits are
recorded in [`PRIMITIVE-MATRIX.csv`](../PRIMITIVE-MATRIX.csv) and
[`AARCH64-X64-BACKEND.md`](porting/AARCH64-X64-BACKEND.md).

The current branch is still a partial compatibility layer. The exact status of
each family is in [`PRIMITIVE-MATRIX.csv`](../PRIMITIVE-MATRIX.csv), while
source provenance and licenses remain in [`licenses/`](../licenses/README.md).
