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

## Newly reviewed references

### JetBrains format-ripper

Repository: https://github.com/JetBrains/format-ripper
License: Apache-2.0.  The project parses and identifies PE, ELF, Mach-O,
Fat-Mach-O, MSI, DMG and related formats and also checks signatures and
hashes.  It is useful as an independent format-validation oracle for
Wintosh's Mach-O parser, especially universal-binary boundaries and malformed
header handling.  It is not a loader or runtime, so no code is copied.

### panthera-speech

Repository: https://github.com/tgeczy/panthera-speech
License: MIT according to the repository metadata; engine data and other
third-party payloads remain separate and are not included.  This is the most
directly relevant new reference: it demonstrates a hand-written Windows
Mach-O loader, direct symbol-slot filling, a native Windows host boundary and
the practical ABI split between 32-bit Darwin/i386 and x64 Windows.  Its
loader architecture should guide a later isolated `libSystem`/engine-host
adapter, but its engine-specific code and extracted Apple assets must not be
copied into Wintosh.

### cthpw103 mach-o

Repository: https://github.com/cthpw103/mach-o
License: Apple Public Source License 2.0.  The repository is a small
cross-platform Mach-O port reference rather than a complete runtime.  It is
useful for comparing portable structure definitions and endian/format
handling, but APSL-covered code must remain isolated from the GPL Wintosh
implementation unless a separate compatibility decision is made.  No code is
copied.

### Venerable OS

Repository: https://github.com/Sovereign-Immortal/venerable-os
License: Apache-2.0.  This is primarily an Arch/Hyprland workstation and
launcher integration project, not a Mach-O loader.  Its useful idea for
Wintosh is the explicit cross-platform launch matrix (`win`, `mac`, Linux)
and a user-facing launcher boundary; its Linux desktop configuration is out
of scope for the native Windows port.  No code is copied.

### Resulting Wintosh work items

1. Add format-ripper-derived independent fixtures for fat Mach-O, signature
   metadata and malformed bounds, while keeping the implementation original.
2. Add a documented isolated engine/guest ABI layer inspired by
   panthera-speech's symbol-slot approach; do not mix it with the core Mach
   loader or ship Apple engine data.
3. Keep the cthpw103/APSL material reference-only unless a future legal review
   explicitly permits a separated APSL component.
4. Extend the CLI documentation with a platform-launch matrix concept without
   importing Venerable OS's desktop configuration.
