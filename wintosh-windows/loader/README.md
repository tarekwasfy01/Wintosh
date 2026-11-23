# Loader

Mach-O, dyld, install-name, UUID, binding and dynamic-library support used by
the Wintosh Windows host layer.

Classic `LC_DYLD_INFO` lazy-bind streams contain concatenated per-symbol
opcode sequences. Their `BIND_OPCODE_DONE` markers end the current symbol
sequence, so parsing continues through the remaining stream and records the
sequence's starting offset for the lazy resolver. Non-lazy and weak-bind streams
still stop at their terminal `DONE` marker. This matches Apple's legacy dyld
`eachLazyBind` implementation; the lazy path has not yet been exercised by a
real macOS binary.

Import resolution now preserves the client image and two-level dylib ordinal
from the Mach-O symbol description. Undefined-symbol records, bind opcodes, and
chained imports merge by symbol and lookup ordinal so bind-only and lazy symbols
also receive providers. Bind actions, chained imports, indirect symbol slots,
and relocation symbol vectors resolve within that client image's
binding set, avoiding accidental selection of a same-named export from an
unrelated dependency. `MH_FORCE_FLAT` and images without `MH_TWOLEVEL` use the
flat fallback path, searching the main image before dependencies in recursive
load-command order; a strong definition overrides an earlier weak definition,
while the first weak definition is retained if no strong one exists. Undefined
`N_REF_TO_WEAK` symbols also use flat lookup, separately from `N_WEAK_REF`'s
missing-symbol allowance. The dependency graph is mapped even when a dylib
contributes no direct undefined-symbol imports, so its initializers and
Objective-C sections are still visited. Weak flags are read from nlist
descriptions and the exports trie, and `N_ABS` symbols bypass image-slide
adjustment. Export-trie addresses are normalized from image-relative offsets
to preferred VM addresses, including `STUB_AND_RESOLVER` resolver addresses.
The process bootstrap and dynamic-image loader invoke referenced resolver
functions through the Darwin-compatible zero-argument ABI and reapply retained
relocation and bind fixups from saved link-time site contents. Resolver
calls run only after every image completes its initial rebase and bind pass,
in dependency order derived from resolver-bearing imports. Detected cycles fail
before execution. Some weak-coalescing and
re-export edge cases remain incomplete.
Re-export ordinals now follow the dependency paths already resolved for each
image, including inherited `LC_RPATH` context; absent built-in dependencies can
forward through the host symbol table. The flag and ordinal layouts follow
Apple's
[Mach-O loader](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h)
and [nlist](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/nlist.h)
headers.

Chained import tables accept `DYLD_CHAINED_IMPORT`, `DYLD_CHAINED_IMPORT_ADDEND`,
and `DYLD_CHAINED_IMPORT_ADDEND64`. The 64-bit form uses its 16-bit signed
library ordinal, 32-bit name offset, and 64-bit addend; the 32-bit addend is
sign-extended before binding. Ordinal decoding follows the field widths and
reserved ranges in Apple's [chained-fixups format](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h).

The dylib graph carries the inherited `LC_RPATH` search context from the main
image through each dependency edge, adding each loaded image's own runpaths.
Nested `@rpath` dependencies can therefore use a search root declared by the
app or an ancestor dylib. Traversal preserves load-command order when a shared
image is first reached. Multi-parent runpath-context behavior still needs
runtime fixture coverage.

While a guest is running, the dyld image-query subset reports the main Mach-O
mapping and the dependency providers mapped by bootstrap, then appends images
mapped through `dlopen` with their in-memory Mach-O headers and slides. Windows
PE modules stay out of that guest image list; header queries point at the
mapped `__TEXT` segment when present. The standalone host-call path
still exposes native modules for host diagnostics. dyld add/remove callbacks,
and synchronized notifications during concurrent image unload, are not yet
implemented.

Classic x86_64 images without chained fixups now keep lazy bind actions and
`__la_symbol_ptr` slots unresolved during startup. The stub-helper trampoline
passes the image-cache address and lazy-stream offset; the host selects the
session by mapped image range and resolves the import on first call. Sessions
remain active across initializers, guest entry and terminators, including
calls from guest-created host threads. Dynamic classic x86_64 images keep
thread-neutral sessions registered until `dlclose`. The pointer-import walk
uses each section's Mach-O `reserved1` index, so non-lazy GOT entries do not
shift subsequent imports onto unrelated symbols. ARM64, 32-bit and
chained-fixup images remain eager. `WINTOSH_FORCE_EAGER_BIND=1` remains a
launch-time fallback for classic lazy imports when all symbols resolve.
`WINTOSH_TRACE_LAZY_BIND=1` reports image classification, slot contents and
resolver calls while debugging. A lazy-info offset selects and applies every
action in its sequence through the sequence boundary.
