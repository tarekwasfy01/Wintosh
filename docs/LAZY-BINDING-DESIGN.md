# Mach-O lazy binding

This page describes the current implementation boundary for classic Mach-O
lazy binding in Wintosh. It does not claim that arbitrary macOS applications
run on Windows.

## Current startup behavior

For classic x86_64 images without chained fixups, bootstrap preserves lazy bind
actions and `__la_symbol_ptr` slots during initial binding. Non-lazy binds,
rebases, export resolvers, and other startup fixups still run before image
initializers.

A Wintosh-owned x86_64 trampoline receives the Darwin stub metadata, saves the
integer and XMM argument registers, resolves the lazy-bind stream offset, then
restores arguments and jumps to the target. A process-wide registry keeps
per-image sessions alive through initializers, guest entry, and terminators.
The image-cache pointer selects an image when multiple active images have the
same lazy-bind offset. When only one active session contains the requested
offset, the offset uniquely identifies that session. Invalid or ambiguous
requests return zero; the assembly path then fails closed. Resolver exceptions
are caught before returning through the assembly frame.

ARM64, 32-bit, chained-fixup images, and dynamic loading remain eager. The
MSVC path does not execute this foreign SysV trampoline and remains fail-closed
for real Darwin guest entry.

## Evidence and limits

The host x64 executable and ARM64 host library build successfully. The
repository contains synthetic parser and trampoline smokes, including a
single-import round trip, but they have not been run against this revision.
There is no end-to-end fixture produced by Apple's linker or an unmodified
third-party Mach-O executable verifying the mapped `__dyld_private` value,
multiple unresolved imports, or concurrent first calls. Ordinary macOS
`.app` compatibility is therefore not established.

The resolver serializes lazy binding while it selects a session and patches a
slot. This prevents two simultaneous first calls from racing the writable
protection change. The active-session registry is removed before its sessions
and binding tables are destroyed.

## Next loader work

1. Add or adapt a linker-produced x86_64 Mach-O fixture with at least two lazy
   imports and verify each call patches only its corresponding slot.
2. Verify repeated and concurrent first calls, unresolved weak and strong
   imports, and provider-image lazy calls.
3. Exercise the XSAVE and FXSAVE trampoline branches with an ABI fixture.
4. Implement the distinct ARM64 stub-helper protocol and assess chained-fixup
   lazy imports before deferring those formats.
5. Implement lazy resolution for dynamic loading rather than marking all
   dynamically loaded images eager.

## Contract references

Apple's historical dyld x86_64 binder documents the metadata words at the
entry stack (`ImageLoader` cache pointer, lazy-bind offset, return address) and
preserves guest argument registers around resolution:
[dyld_stub_binder.s](https://github.com/opensource-apple/dyld/blob/master/src/dyld_stub_binder.s).
LLVM's Mach-O linker places `__dyld_private` in the image-loader-cache input
section used by the stub-helper:
[LLD SyntheticSections.cpp](https://github.com/llvm/llvm-project/blob/main/lld/MachO/SyntheticSections.cpp).
The xstate area size comes from CPUID leaf `0xD`, and XGETBV is gated by the
XSAVE/OSXSAVE feature bits described in the
[Intel Software Developer's Manual](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).

The implementation is Wintosh-owned adapter code. It does not copy Apple's
assembler or dyld implementation.


The x86_64 trampoline now checks CPUID/OSXSAVE and uses XGETBV plus XSAVE/XRSTOR
for the user state enabled by the host OS; older systems use FXSAVE/FXRSTOR.
This expands preserved state beyond XMM registers, but only the assembler build
has been verified. No runtime ABI fixture has exercised the XSAVE and fallback
branches yet.
