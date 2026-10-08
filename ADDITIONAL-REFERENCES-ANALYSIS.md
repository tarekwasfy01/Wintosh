# Additional source analysis

## macho-ld

Checkout: `macho-ld-source/`, commit
`2feda1c49e3fd01a39ddc785cf4428d906fbe0fd`.

`macho-ld` is a Rust in-memory Mach-O loader/executor. Its documented useful
surfaces are executable and dylib loading, universal/fat binaries, chained
fixups and dyld info. It explicitly does not support 32-bit images, binaries
without relocation information, Objective-C runtime initialization or weak
bind handling. This is valuable as a focused parser/loader test reference, but
it is not a complete dyld or Objective-C runtime.

No top-level license file was found in the checkout during this inventory;
license status is therefore `review-required`. No code was copied.

## Classix

Checkout: `classix-source/`, commit
`f7deead2244177468380127de1fca8df23dbacfe`.

Classix is a Mac OS 9 compatibility layer, not modern macOS Darling. Its most
useful design ideas are explicit component boundaries (PEF parser, CFM linker,
PowerPC VM, native bridge), resolver objects that distinguish emulated from
native function addresses, and allocator-owned guest-visible memory. Its
README states that it can start simple PowerPC PEF programs but lacks runtime
CFM features, 68k/XCOFF support and comprehensive tests.

The repository includes GPLv3 licensing and notes that Dolphin interpreter
code is the legal reason for that license. No Classix code was copied into the
Darling Windows runtime.

## VST-ace

Checkout: `vst-ace-source/`, commit
`abbd323e1dfa71647cfa026b86eece064824df09`.

VST-ace is a Linux host/bridge for Windows, macOS and Classic audio plug-ins.
It is not a Darling port, but it is relevant as an integration reference: it
combines a PE loader, Mach-O loader, Objective-C runtime, Classic CFM/PEF
interpreter and platform shims behind one host. Its measured handoff notes
also identify concrete Objective-C runtime gaps such as class/ivar registration
when loading macOS plug-ins.

Because the repository contains third-party plug-ins, reverse-engineering
notes and mixed platform payloads, its license boundary is component-specific.
No plug-in binary, reverse-engineered asset or VST-ace implementation is
copied into this project. It is reference-only.

## Porting consequences

1. Add `macho-ld`-style loader fixtures to the existing Mach-O matrix for fat
   binaries, chained fixups and dyld-info edge cases.
2. Keep Objective-C runtime registration separate from Mach-O mapping; both
   `macho-ld` and VST-ace show that conflating them leaves real gaps.
3. Preserve the native/emulated boundary used by Classix and VST-ace, but use
   Darling's modern `darlingserver` path for full macOS behavior.
