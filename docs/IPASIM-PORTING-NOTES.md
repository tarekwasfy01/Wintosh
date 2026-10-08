# ipasim porting notes

The complete ipasim repository is stored under `ipasim-source`, including its
recursive submodules. The main repository is MIT-licensed; dependency
licenses remain with their respective submodule sources and must not be
collapsed into the Darling GPL inventory.

## Relevant source families

| Family | Source | Use in this port | Boundary |
|---|---|---|---|
| Objective-C runtime | `ipasim-source/src/objc`, `deps/objc4`, `deps/libclosure` | Reference for `libobjc` exports, class/selector metadata, blocks, and Windows proxy headers | Apple runtime sources retain their upstream provenance |
| Mach-O/iOS loader | `src/IpaSimulator`, especially `DynamicLoader.cpp`, `LoadedLibrary.cpp`, `MachO.cpp` | Compare loader and Objective-C image-registration behavior with the existing Darling loader | ipasim uses LIEF and Unicorn and targets iOS application emulation |
| API wrapper generation | `src/HeadersAnalyzer`, `src/WrapperIndex.*` | Reference for systematic framework export and wrapper generation | Generated wrappers require their own input-header records |
| Apple framework compatibility | `deps/WinObjC` | Reference for Foundation/UIKit-style Windows adapters | Large separate dependency tree; not copied into the minimal CLI backend |

## First implementation decision

The minimal Darling Windows backend keeps its existing x86-64 Mach-O and
Windows-host ABI. ipasim is used as a reference and source candidate for a
separate Objective-C compatibility family; its ARM/Unicorn execution path and
UWP/XAML app shell are outside the first CLI execution gate.

## Collected revision pins

The top-level clone was fetched recursively on 2026-10-05. Important pins
currently present include `objc4` `4de2b60df12f0e94fbda69b3f6b88c690a4a7737`,
`libclosure` `3694c4d4d355ec65e1d3d0d223fa422ad58cbbcd`, `libdispatch`
`3b7681dabe90f15ae3672db8a8e578cf8f9e908d`, and `WinObjC`
`45d57d87212071fdd8405b5ced4ddef1bfb12eda`.

## Implemented first batch

`darling_windows_objc` now provides a small ABI-safe registry for selectors,
classes, superclass links, and method metadata. It is covered by
`darling_windows_objc_smoke.exe`. The first no-argument `objc_msgSend` and
`objc_msgSendSuper` paths now dispatch through the class hierarchy. Variadic
and typed-argument dispatch still needs a separate calling-convention test
matrix before it can execute arbitrary Objective-C Mach-O code. Three
project-specific typed bridges are now covered: object-with-one-argument,
signed-64-bit-with-one-argument, and void-with-no-argument.

Each registered method now retains its type encoding. The typed bridges reject
an incompatible encoding before calling the implementation, and the smoke
test verifies `@@:@`, `q@:q`, and `v@:` lookup.

The generic tagged-value dispatcher now selects the matching bridge for
`@@:@`, `q@:q`, `B@:B`, `d@:d`, `v@:` and `@@:`. Unsupported encodings or value
kinds return a void result instead of guessing an ABI.

Pointer arguments/returns (`^v@:^v`) and two signed 64-bit arguments/return
(`q@:qq`) are now covered by `darling_objc_invoke1` and
`darling_objc_invoke2`.

The first Cocoa-style struct form is covered as well: a four-double CGRect
representation with encoding `{CGRect={CGPoint=dd}{CGSize=dd}}@:` and a
dedicated Windows-x64-safe return helper.

Class lookup now also exposes a metaclass-compatible lookup and a tested class
method registry. `class_addClassMethod` plus `darling_objc_msgSend_class0`
covers the first factory-style class invocation path.

Standard-style class enumeration is now available through
`objc_getClassList`, including count-only queries and bounded buffer copies.

Protocol metadata is now supported with allocation/registration, required
instance-method descriptors, class conformance, and inherited conformance
lookup. The smoke test covers a registered `DarlingPrintable` protocol.

Object lifetime now has atomic retain/release counts and a thread-local
autorelease pool with `objc_autoreleasePoolPush`/`Pop`. Full Foundation pool
nesting semantics and weak-reference cleanup remain future work.

Weak slots are now registered and cleared on final release through
`objc_initWeak`, `objc_storeWeak`, `objc_loadWeak`, `objc_destroyWeak`,
`objc_copyWeak`, and `objc_moveWeak`. The smoke test also caught and fixed
proper construction of the atomic retain counter in raw instance storage.

A project-specific callback block now supports one object argument, context,
copy, release, and invocation. It is not yet the binary-compatible Apple
Block ABI; the real block layout and descriptor semantics remain separate
work.

`_preadv` and `_pwritev` now compose the verified offset-preserving file path
for multiple vectors on both architectures. Overlapped completion and native
locking semantics remain future work.

The Objective-C family is now connected to the host symbol resolver for common
`libobjc` exports, and `libobjc.A.dylib`/`libobjc.dylib` are recognized as
built-in Darwin providers. The dyld/host smoke run verifies the integration.

Mach-O section metadata now includes `section_64` records and slide-correct
`SectionAddress` lookup. The bootstrap scans `__objc_classlist` and
`__objc_nlclslist` after mapping/fixups; class names are extracted from
`class_ro_t` and registered in the Windows Objective-C runtime. A raw
classlist fixture verifies the extraction path.

The scanner now also reads `class_ro_t::baseMethods`, imports selector names,
type encodings, and IMP addresses, and registers them as instance methods.
Malformed or oversized method lists are rejected by bounded checks.

`__objc_catlist` is now registered as well. Category records are resolved to
their target class and both instance and class method lists are imported.

`class_ro_t::baseProtocols` now creates/registers protocol metadata and adds
the protocol to the runtime class. `__objc_selrefs` strings are interned via
`sel_registerName` after image fixups. Both paths have bounded malformed-data
checks and dedicated smoke coverage.

Protocol `protocol_t` method lists are now imported for required and optional
instance/class methods. The runtime exposes `protocol_hasMethodDescription`
so the imported metadata is directly testable.

The classlist scanner now follows each raw class's `isa` pointer, reads the
metaclass `baseMethods`, and installs those entries as class methods. A
factory-style class dispatch fixture verifies the path.

Class-method lookup now walks the class superclass chain. Protocol objects
retain parent protocols, and class conformance recursively recognizes inherited
protocols; both inheritance directions are covered by the runtime smoke test.

Callback blocks are now accepted by the tagged Objective-C dispatcher for the
`@@:@?` method encoding. A method can receive the bridge block and invoke it;
the binary Apple block layout is still intentionally separate.

A binary-shaped Apple block literal is now available with `isa`, flags,
reserved field, invoke pointer, descriptor signature, copy/release, and
invocation helpers. It is validated independently; compiler-generated Clang
block capture/dispose helpers and the complete ABI flags are still pending.

`objc_msgSend` and `objc_msgSendSuper` now accept variadic arguments and select
the supported object, signed-64-bit, BOOL, pointer, and block paths from the
method encoding. Floating-point/aggregate return-register paths remain
separate because Windows x64 uses different return registers.

The Objective-C image scanner now imports `class_ro_t::ivars`; names,
type-encodings, offsets, and superclass lookup are exposed through the Ivar
runtime API. The smoke fixture verifies an image ivar with type `q`.

`class_ro_t::baseProperties` is now parsed into property metadata. Name and
attribute strings are exposed through `class_getProperty`, `property_getName`,
and `property_getAttributes`, with bounded malformed-list checks.

Category metadata now imports instance and class property lists as well, so
category-declared properties participate in the same superclass-aware lookup.
Category protocol lists are imported through the same image scanner path.
Protocol names and bounded global/class protocol-list enumeration are exposed
through `protocol_getName`, `objc_getProtocolList`, and
`class_getProtocolList`.
The matching class-copy APIs now return caller-owned arrays for direct class
ivars, properties, and protocols; the smoke fixture frees and validates all
three arrays.

Additional external references collected for the next porting families:

* `wine64-darwin-source` is a shallow checkout of
  `crioux/wine64-darwin` at `54eeeee041fdadad79e0c5449e4a08d763e58b65`.
  Its top-level `LICENSE` is LGPL-2.1-oriented Wine licensing and its
  `COPYING.LIB` is retained in the source checkout; these files are reference
  provenance only and are not copied into Darling's GPL implementation.
* `puredarwin-source` is a shallow checkout of
  `PureDarwin/PureDarwin` at `88753c478c97b9a08bcdb66cecc68ba5881ff3af`.
  The checkout contains `PUREDARWIN_LICENSE.txt`, `APPLE_LICENSE.txt`, and
  `APPLE_DRIVER_LICENSE.txt`; PureDarwin/Darwin source remains separately
  attributed and is not merged into the Windows backend without per-file
  license review.

The checked reference hashes are Wine `LICENSE` SHA-256
`F7A72381495D51D21B69B3403F4F7BB7744322280475C8D818533A2BB17B9C7D`, Wine
`COPYING.LIB` `090F8EB6BA63E72887C1F89B009FB1656CFDA02D83B6E9805B20468520A5D77F`,
PureDarwin `APPLE_LICENSE.txt`
`CE93889B3DF4F6D0319AA1CC75B194DD633145FB0E20B581AA3DC2C37D8F79F5`,
`APPLE_DRIVER_LICENSE.txt`
`30600A01E7F2A51CA6F8F666C45BDA6F1FDA6872508371B13F160D0F23499FBB`, and
`PUREDARWIN_LICENSE.txt`
`616D7838DC71C5D99A77A2D0682A083433F493F0B6AFC03EBD8F0A99711E2C86`.

The Mach-O writer now also provides bounds-checked 32-bit absolute and
PC-relative patch paths used by Darwin text-bind forms. The Dyld fixture checks
both widths and restores the original segment protection after each write.

The Darwin C-runtime error family now exposes a thread-local errno address
through both `_errno` and Darwin's `___error` symbol spelling. Both names are
aliases of the same Windows-side state and are covered by the host ABI smoke
test; this is an original Windows bridge, not copied Wine or PureDarwin code.

Objective-C method metadata now has caller-visible method views with selector,
IMP, and type encoding accessors. `class_getInstanceMethod` and
`class_copyMethodList` are covered by the image fixture; the method views are
Windows-port runtime records and do not copy external implementation code.

`sherlockelf-source` is an additional reference checkout of
`iamtorsten/SherlockElf` at `7183d8a7049693eafe22d83ddc26bf05ffaa2fbe`.
Its repository declares MIT licensing (`LICENSE` SHA-256
`620136EE9F1DF47BDBA742EFF6D7E98BAE41A27C100075EC5C69A6DFC9B076EB`). It is
used only as an analysis/reference source for ELF and experimental Mach-O
inspection; no SherlockElf code is copied into the Darling runtime. The
corresponding license text is retained at
`licenses/LICENSE-sherlockelf-MIT.txt`. Its text matches the source license
line-for-line; the bundled copy uses normalized LF line endings, so its byte
SHA-256 is `D583B0A31CC5D972FEF2DDCCAEF8683CE226984F438F0074C1BBBDC5245133E3`,
while the source file's recorded SHA-256 is
`620136EE9F1DF47BDBA742EFF6D7E98BAE41A27C100075EC5C69A6DFC9B076EB`.

The host resolver now exports the complete currently implemented Objective-C
metadata family, including method, ivar, property, and protocol enumeration
accessors. The smoke fixture checks every resolver spelling used by this
family, while the implementations remain original Windows bridge code.

Protocol method descriptions now retain the raw type-encoding strings from
Mach-O `protocol_t` method lists. `protocol_getMethodDescription` exposes the
selector/type pair, and required, optional, instance, and class method buckets
share the same bounded importer.

The x86-64 relocation family now applies the real Mach-O `SIGNED_1`,
`SIGNED_2`, and `SIGNED_4` forms: all write signed 32-bit fields, with a
1/2/4-byte PC adjustment. The Dyld fixture validates all three forms and
segment-protection restoration. Type 9 TLV records now fail closed with an
explicit diagnostic until the Darwin TLV descriptor/runtime bridge exists.

The main Darwin bootstrap now invokes the relocation engine on the mapped
executable before bind actions and entry execution. This closes the previous
test-only gap for the main image; provider-dylib relocation closure and TLV
descriptor runtime are still pending.

The first TLV runtime layer now initializes `__thread_vars` descriptors in the
bootstrap path and resolves them through template-backed Windows thread-local storage. The
dedicated fixture proves descriptor resolution and thread isolation; the
complete suite is 20/20. Cross-image relocation-to-`__thread_ptrs` wiring and
ARM64 TLVP forms remain pending.

The template-copying part is now implemented: `__thread_data` bytes are copied
per thread and `__thread_bss` capacity is zero-filled. The TLV fixture verifies
the initial value and thread isolation. Relocation slot wiring, complete
multi-variable offsets, destructor callbacks, and ARM64 TLVP remain open.

Local `__thread_vars[i]` to `__thread_ptrs[i]` mapping is now available to the
main-image type-9 relocation path. Cross-image provider slots remain
fail-closed. The bootstrap also defers unreachable cross-image x86-64 PCREL
relocations rather than truncating them; a near-image allocator or thunk
implementation is still needed for complete execution.

The loader now maps the main image first and attempts bounded near-address
reservations for provider dylibs. Reachable cross-image PCREL relocations can
therefore be applied normally; the deferred path remains for allocations that
Windows places outside the signed 32-bit range.

Provider dylibs and bundles without `LC_MAIN` are now valid mapped images. The
bootstrap applies their rebases, relocations, bind opcodes, chained fixups, and
Objective-C registration before executing the main image.

The TLV initialization routine is shared across the main image and provider
images, including their data/bss templates and local pointer slots. Cross-image
TLV symbol metadata remains open. `_tlv_atexit` is now exported through the
host resolver and callbacks execute at thread teardown, covered by the TLV
fixture.

The PureDarwin C++ TLS wrapper `__cxa_thread_atexit` is now implemented and
exported under its Mach-O spelling `___cxa_thread_atexit`; both wrapper paths
are covered by the TLV worker-thread fixture.

Provider TLV exports now bind to their local `__thread_ptrs` slot, and type-9
imports accept the translated cross-image slot. Optimized TLV-LEA cases,
complex graph key sharing, and ARM64 TLVP are still pending.

The loader now recognizes ld64's optimized x86-64 TLV-LEA opcode rewrite and
fails closed instead of treating its descriptor target as an ordinary PCREL
address. A per-thread direct-address bridge is required before those forms can
execute safely.

The loader now executes `__mod_init_func` arrays after image fixups and before
entry execution, including provider dylibs. The dedicated fixture proves a
callback invocation through the mapped array; the complete suite is 21/21.

`__mod_term_func` is also executed in reverse order after the main entry and
before provider mappings are released. The fixture verifies the terminator
callback independently.

Provider startup now uses a dependency-first graph order with cycle detection;
provider teardown follows the reverse order. The existing dyld graph fixtures
remain green at 21/21.

Mach-O `dlopen`/`dlsym`/`dlclose` now has a self-contained image path with
fixups, host imports, initializer/terminator execution, and underscore-tolerant
symbol lookup. The dedicated fixture and complete suite pass at 22/22.
Unresolved non-host Dylib dependencies remain outside this dynamic path.

The dynamic path now consumes the full Dylib binding graph and maps provider
images before the main Mach-O. Provider fixups and initializers execute before
the main image, with reverse teardown on close; unresolved imports remain an
intentional hard failure.

Dynamic Mach-O images and their providers now register Objective-C class,
category, selector-reference, and non-lazy class sections during `dlopen`.

The dynamic path also initializes TLV descriptor/template/pointer sections for
the main image and providers. Optimized TLV-LEA and ARM64 TLVP remain open.

Dynamic provider initialization now follows the dependency-first graph order;
teardown remains reverse-ordered.

Dynamic Mach-O handles now have path-based identity and reference counting;
only the final `dlclose` tears down the image. The dynamic fixture exercises a
double-open and two closes.

The dynamic symbol resolver now covers the main image and all loaded provider
images with underscore-tolerant lookup. Final dynamic teardown executes the
main terminators and then provider terminators in reverse order. The complete
Release smoke suite remains green at 22/22.

The Mach-O graph now also accepts unresolved `N_WEAK_REF` symbols and emits a
NULL binding, while required imports still fail closed. The dyld fixture prints
`DYLD_WEAK_UNDEFINED=PASS` and the complete Release smoke suite remains 22/22.

Cross-image dynamic TLV imports now resolve to the provider's per-thread slot
instead of the static descriptor address, consistent with the process
bootstrap path. Dynamic and complete Release smoke tests remain green.

The Dylib graph now preserves the main executable path while resolving nested
dependencies, correcting `@executable_path` without changing
`@loader_path`. The dyld fixture reports `DYLD_EXECUTABLE_PATH=PASS` and the
complete Release suite remains 22/22.

`dlsym` now accepts Darwin `RTLD_DEFAULT` and searches loaded Mach-O images
before host symbols. The dynamic fixture reports `DARWIN_RTLD_DEFAULT=PASS`;
full Release verification remains 22/22.

Concurrent first-time `dlopen` calls for one normalized Mach-O path now share
an in-flight load state, handle, and reference count; teardown is performed
outside the mutex. `DARWIN_DLOPEN_CONCURRENT=PASS` and the full Release suite
remains 22/22. Same-thread recursive initializer loads remain an explicit
edge case.

Static and dynamic image loading now eagerly applies all parsed lazy-bind
actions, so lazy import slots are usable before entry or initializer code runs.
The fixture keeps `DYLD_LAZY_BIND_APPLY=PASS`; exact Darwin on-demand resolver
stub behavior remains open.

The Darwin `dlopen(NULL, ...)` process-handle behavior is now implemented using
the existing `RTLD_DEFAULT` bridge, including `dlsym` and no-op `dlclose`.
Dynamic and complete Release smoke verification remains green at 22/22.

Objective-C `objc_msgSend` now covers common 32-bit and mixed-width integer
method encodings and void integer arguments. The Objective-C fixture and full
Release smoke suite remain green at 22/22.

Two-argument object and int64 `objc_msgSend` encodings are now dispatched with
stable argument order (`@@:@@` and `q@:qq`). Objective-C and complete Release
verification remain green at 22/22.

The matching `objc_msgSendSuper` paths now support those multi-argument object
and integer encodings against superclass implementations. Full verification
remains green at 22/22.

Associated Object APIs now implement per-key assign/retain/copy policy storage,
replacement cleanup, explicit removal, and cleanup at final object release.
Copy remains retain-only until an NSObject copy protocol exists; tests remain
green at 22/22.

Superclass dispatch now supports the multi-argument object/int64 encodings
already supported by `objc_msgSend`. The complete Release suite remains green
at 22/22.

Superclass pointer and callback-block dispatch are now covered as well. The
Objective-C fixture and complete Release suite remain green at 22/22.

Mach-O export re-exports now retain dependency ordinal/target metadata and bind
umbrella-Dylib imports recursively to the actual target provider. The dyld
fixture reports `DYLD_REEXPORT=PASS`; full Release verification remains 22/22.

Direct dynamic lookup through an umbrella-Dylib handle now traverses that
re-export chain as well (`DYLD_REEXPORT_DLSYM=PASS`).

The Darwin filesystem ABI family now has `stat`, `lstat`, and `fstat` bridges,
with `stat64`/`lstat64`/`fstat64` lookup aliases. The Windows implementation
reports file type, size, link count, identity, block count, and Windows-derived
timestamps. The dedicated filesystem fixture and all 22 Release smoke tests
pass. uid/gid remain compatibility zeros, and lstat does not yet provide
Darwin no-follow symlink behavior for Windows reparse points.

The filesystem fixture additionally checks all three `*64` aliases through the
host symbol resolver.

`fstatat` and `fstatat64` now support `AT_FDCWD` and the accepted
`AT_SYMLINK_NOFOLLOW` flag. Directory-FD-relative calls return `ENOTSUP` until
the descriptor table preserves directory paths/handles; the full 22-test
Release suite remains green.

The Directory-FD limitation has since been removed: opened directory paths
are retained and relative `fstatat` resolution is tested end-to-end. The
`F_DUPFD` growth bug discovered by the repeated test was corrected, with
AddressSanitizer host-API verification 10/10 and three complete Release runs
at 22/22. Reparse-point no-follow semantics remain unsupported.

No-follow handling now opens paths with `FILE_FLAG_OPEN_REPARSE_POINT` and
maps reparse points to Darwin symlink mode bits. The current account lacks
symlink creation privilege, therefore the real reparse fixture is explicitly
`SKIP` while the complete 22-test Release suite passes; this needs one
privileged/Developer-Mode Windows verification.

The open bridge now maps Darwin `O_CREAT`, `O_EXCL`, `O_TRUNC`, and
`O_APPEND` values to Windows, including persistent append behavior and
exclusive creation. The filesystem fixture verifies the complete
create/truncate/append/exclusive sequence; all three Release runs pass 22/22.

`O_DIRECTORY` and `O_NOFOLLOW` now reach real Windows validation paths;
regular-file `O_NONBLOCK` is intentionally a no-op and created handles are
non-inheritable for `O_CLOEXEC` compatibility. Directory acceptance/rejection
and the complete 22/22 suite pass. Pipe nonblocking and full `fcntl` exec
flag state are still pending.

`statfs` and `fstatfs` now report Windows volume capacity, free space, serial,
filesystem name, and mount/source paths, with `*64` symbol aliases. The
fixture checks both path and descriptor forms; all three complete Release runs
remain 22/22. Darwin-specific mount flags and filesystem accounting fields are
still compatibility mappings.

`getfsstat`/`getfsstat64` now provide the current Windows volume with the BSD
count/buffer contract and tested alias resolution. The complete suite remains
22/22 across three runs; multi-volume/mount-namespace enumeration and Darwin
filesystem accounting remain open.

`getfsstat` now enumerates all logical Windows drive roots; the current test
host exposed 11 entries and the current volume was found among them. Count
query and alias checks pass, as do three 22/22 Release runs. This remains a
drive-root approximation, not a complete Darwin mount namespace.

`fcntl` descriptor state is now implemented for `FD_CLOEXEC`, `O_APPEND`, and
regular-file `O_NONBLOCK`; anonymous pipes reject unsupported nonblocking with
`ENOTSUP`. The targeted fixtures and three complete 22/22 Release runs pass.

The Mach-O family now recognizes ARM64 thin and fat-image metadata, including
the ARM64 thread-state PC layout. A synthetic ARM64 `LC_MAIN` image opens and
reports its entry point; ARM64 execution and architecture-specific relocation
or dyld fixup application remain fail-closed until an ARM64 execution backend
exists. Three complete Release runs remain 22/22.

The first executable ARM64 relocation family is now covered: external
`UNSIGNED`, `BRANCH26`, `PAGE21`, `PAGEOFF12`, and GOT page relocations are
applied with segment-relative address resolution. The smoke fixture proves the
branch/page family; authenticated/TLV/ADDEND and chained-fixup variants remain open,
and no ARM64 instructions are executed on the x86_64 host.

The non-authenticated ARM64 `DYLD_CHAINED_PTR_64` rebase path is now covered
by the fixture (`MACHO_ARM64_CHAINED_FIXUP=PASS`). ARM64e authentication,
kernel/shared-cache pointer formats, and their signing semantics remain open.

The ARM64 local-pointer rebase path is now covered as well
(`MACHO_ARM64_LOCAL_RELOCATION=PASS`), using the existing pointer value plus
the image slide. `ARM64_RELOC_ADDEND`, subtractor, and authenticated-pointer
records remain unimplemented.

The explicit ARM64 addend record is now paired and sign-extended correctly;
the fixture reports `MACHO_ARM64_ADDEND=PASS`. Subtractor and authenticated
pointer records remain unimplemented.

ARM64 subtractor/unsigned pairs are now applied and tested
(`MACHO_ARM64_SUBTRACTOR=PASS`). Authenticated-pointer relocations and their
PAC metadata are still not implemented.

Authenticated-pointer metadata is now decoded and validated before the loader
fails closed (`MACHO_ARM64_AUTH_METADATA=PASS`). Windows still lacks the
Apple PAC key/signing backend, so the pointer is never treated as authenticated.

The classic ARM64 pointer-to-GOT forms are now applied and tested
(`MACHO_ARM64_POINTER_TO_GOT=PASS`): 64-bit absolute and 32-bit PC-relative.
Full dynamic GOT construction and authenticated ARM64e GOT entries remain open.

The ARM64 TLVP page relocation pair is now applied and tested
(`MACHO_ARM64_TLV_RELOCATIONS=PASS`). Complete Darwin TLV descriptor and
per-thread resolver integration is still outstanding.

Dynamic Darwin-TLV registrations now have explicit teardown. The Windows host
tracks live thread-local maps, removes templates and per-thread objects during
dynamic-image destruction, clears descriptors, and reports
`DARWIN_TLV_DESTROY=PASS`. The remaining boundaries are Apple ABI thunk
compatibility, ARM64 instruction execution, PAC signing/authentication, and
the complete Darwin mount namespace.

The dyld bind parser now handles `ADD_ADDR_ULEB`, signed special dylib
ordinals, weak-import flags, scaled binds, and repeated skip binds across
normal, weak, and lazy streams. The synthetic image verifies four bound slots
and a repeated skip sequence with `DYLD_BIND_OPCODES=PASS`; chained-fixup
format variants and ARM64 execution remain open.

The chained-fixup loader now accepts both `DYLD_CHAINED_PTR_64` and
`DYLD_CHAINED_PTR_64_OFFSET`; the offset form is tested by
`MACHO_CHAINED_PTR_64_OFFSET=PASS` and is based on the bundled PureDarwin/XNU
`mach-o/fixup-chains.h` definitions. ARM64e/PAC, 32-bit Mach-O headers, and
cache/kernel pointer formats remain open.

`DYLD_CHAINED_PTR_ARM64E` authenticated metadata is now decoded and rejected
closed without a PAC backend; `MACHO_ARM64E_CHAINED_AUTH_METADATA=PASS`
verifies the boundary. PAC signing/authentication, ARM64e offset/userland
variants, and execution remain unimplemented.

The non-authenticated ARM64e userland offset format 9 is now applied and
tested with `MACHO_ARM64E_USERLAND_REBASE=PASS`; formats 7 and 10 are decoded
with their four-byte stride. Userland24 format 12, kernel/cache behavior, and
PAC execution remain open.

Format 12 (`DYLD_CHAINED_PTR_ARM64E_USERLAND24`) now uses the 24-bit ordinal
layout and is tested by `MACHO_ARM64E_USERLAND24_REBASE=PASS`. Authenticated
format-12 binds, kernel/cache formats, 32-bit Mach-O, and PAC execution remain
open.

Both legacy `LC_DYLD_INFO` and modern `LC_DYLD_INFO_ONLY` are now accepted;
the legacy path is covered through binding, reexport, and DLSYM with
`DYLD_INFO_LEGACY=PASS`.

The parser now normalizes 32-bit x86 Mach-O headers, segments, sections, and
`nlist` symbols; a synthetic dylib reports `MACHO_32BIT_METADATA=PASS`.
Execution is explicitly rejected on the x86_64 host and reports
`MACHO_32BIT_EXECUTION_GUARD=PASS`; 32-bit relocations and execution remain
unimplemented.

External i386 `GENERIC_RELOC_VANILLA` absolute and PC-relative relocations are
now applied with 32-bit addends and verified by `MACHO_32BIT_RELOCATIONS=PASS`.
Scattered/SECTDIFF/PAIR, prebound-lazy-pointer, remaining local records, and
execution remain open.

Scattered `GENERIC_RELOC_SECTDIFF`/`LOCAL_SECTDIFF` plus validated
`GENERIC_RELOC_PAIR` records are now applied as a symbol difference with the
in-place addend; they are covered by the same 32-bit relocation fixture.
Prebound-lazy-pointer, remaining local forms, and execution remain open.

`GENERIC_RELOC_PB_LA_PTR` now applies its unslid `r_value` plus the mapping
slide, matching the bundled cctools prebinding implementation; it is verified
by `MACHO_32BIT_PB_LA_PTR=PASS`.

Current checkpoint (2026-10-05): i386 `VANILLA`, scattered
`SECTDIFF`/`LOCAL_SECTDIFF` plus `PAIR`, and `PB_LA_PTR` are implemented and
covered by synthetic fixtures. i386 `GENERIC_RELOC_TLV` now resolves the
local `__thread_ptrs` slot and reports `MACHO_32BIT_TLV_RELOCATION=PASS`.
Execution remains deliberately fail-closed in the x64 process; the separate
Win32 process now provides native i386 execution. Release validation is 22 smoke
binaries over 3 runs with 66/66 passes. `sherlockelf-source` remains a
MIT-licensed analysis reference; its source is included for provenance, but no
code was copied into the Darling Windows runtime.

The separate MSVC Win32 build now executes a real i386 Mach-O entry stub and
reports `MACHO_32BIT_EXECUTION=PASS`; the x64 build retains the explicit guard.
Win32 dynamic loading, `__mod_init_func`/`__mod_term_func`, dyld mapping, and
Objective-C metadata are covered by architecture-specific fixtures. The full
Win32 smoke suite is 22/22, while the x64 Release suite remains 66/66 across
three runs.

Execution gating is now architecture-aware for a future `_M_ARM64` build:
ARM64 images are accepted only there, while x64 and Win32 reject incompatible
Mach-O CPU families. The current Visual Studio installation lacks an ARM64
target toolset, so native ARM64 execution remains unverified.

The Win32 bootstrap now completes the complete synthetic i386 executable path,
including mapping, rebasing, TLV section setup, relocations, initializers, and
entry execution; `DARWIN_BOOTSTRAP_32BIT=PASS` is verified. The Win32 suite is
22/22 and the x64 Release suite is 66/66 over three runs after cleanup of all
temporary diagnostics.

The socket family now also has a real Windows AF_UNIX stream path based on the
Windows SDK `sockaddr_un`; the local bind/connect/accept roundtrip reports
`WINDOWS_UNIX_SOCKET_REQUEST=PING` and `WINDOWS_UNIX_SOCKET_RESPONSE=PONG` on
both targets. Datagram/SEQPACKET and ancillary descriptor passing remain
unimplemented.

The descriptor family now distinguishes `F_DUPFD` from
`F_DUPFD_CLOEXEC` (command 1030): ordinary duplication clears the close-on-exec
bit and the CLOEXEC variant sets it. This follows the retained WSL reference
test `wsl-source/test/linux/unit_tests/dup.c` and passes on both Win32 and x64.
Pipe nonblocking now covers the read side: an empty pipe returns Darwin
`EAGAIN`, and buffered bytes are consumed without blocking; the syscall fixture
reports `DARWIN_SYSCALL_PIPE_NONBLOCK=PASS` on both targets. Windows write-side
pipe capacity semantics and inheritance-aware spawn file-actions remain open.

Duplicating a descriptor now shares `O_NONBLOCK` status with the source while
keeping `FD_CLOEXEC` independent, matching the Darwin/Unix descriptor model;
the syscall fixture verifies this on x64 and Win32.

The process bridge now uses the retained PureDarwin/XNU `spawn_internal.h`
layout instead of rejecting all file actions. `PSFA_OPEN`, `PSFA_CLOSE`,
`PSFA_DUP2`, and `PSFA_CHDIR` work for child standard descriptors, with the
fixture proving redirection, duplication, close, and directory change via
`DARWIN_POSIX_SPAWN_FILE_ACTIONS=PASS` on both targets. `PSFA_INHERIT` and
`PSFA_FCHDIR` are now also covered for standard descriptors. Fileport
duplication, descriptors above 2, and unsupported spawn attributes remain
rejected explicitly. `POSIX_SPAWN_CLOEXEC_DEFAULT` is implemented by the
inherited-handle list and verified by `DARWIN_POSIX_SPAWN_ATTRIBUTES=PASS`;
both post-change suites remain 22/22.

The AF_UNIX stream implementation remains verified on x64 and Win32. A direct
Winsock capability probing confirms that the current Windows host rejects
native `AF_UNIX` with `SOCK_DGRAM`. The Darling host now emulates that family
with a path-register-backed loopback UDP transport; the smoke test verifies
PING/PONG and reports `WINDOWS_UNIX_DGRAM=EMULATED` on both targets. Native
external AF_UNIX datagram interoperability, SEQPACKET, abstract namespaces,
and ancillary descriptor passing remain future work. The complete Release
suites still pass 22/22 on both targets.

The filesystem bridge was corrected so `lstat` no longer follows a Windows
reparse point; it now uses `FILE_FLAG_OPEN_REPARSE_POINT`, matching the already
supported `fstatat` no-follow path. The fixture also checks
`AT_SYMLINK_NOFOLLOW` when Windows allows symlink creation. In the current
restricted account that branch is `DARWIN_LSTAT_REPARSE=SKIP`; the ordinary
filesystem and complete x64/Win32 22/22 suites remain green.

`POSIX_SPAWN_START_SUSPENDED` (`0x0080`) is now mapped to
`CREATE_SUSPENDED`. The fixture resumes the created process's main thread and
then verifies its exit status on both architectures. Other process-group,
signal-mask/default, identity, exec-replacement, fileport, and private Darwin
attributes remain explicitly unsupported. The complete serial Release suites
pass 22/22 on x64 and Win32.

Objective-C autorelease pools now use stable heap-backed frames. This fixes
nested-pool token invalidation caused by vector growth; the fixture drains two
nested pools and verifies weak-reference clearing after final release on both
architectures. Foundation-level ownership and concurrent weak-table semantics
remain future work.

`POSIX_SPAWN_SETSID` is now accepted through a documented Windows emulation
with `CREATE_NEW_PROCESS_GROUP`; the process fixture reports
`DARWIN_POSIX_SPAWN_SETSID=EMULATED` on x64 and Win32. It supplies internal
process-group separation but does not provide Darwin's native session-ID,
controlling-terminal, or arbitrary process-group semantics.

Darwin `SOCK_SEQPACKET` is now available inside the Windows host bridge through
a path-registered loopback TCP transport with explicit packet length framing.
The fixture verifies one complete PING/PONG packet exchange and reports
`WINDOWS_UNIX_SEQPACKET=EMULATED` on both architectures. This does not claim
native external AF_UNIX/SEQPACKET interoperability; ancillary descriptor
passing remains open.

The Windows bridge now exports `_objc_retainBlock`, `_objc_releaseBlock`,
`_Block_copy`, and `_Block_release` for its supported heap-shaped Apple block
records. The fixture resolves and exercises these aliases on both targets.
Captured-variable copy/dispose helpers and full libclosure-compatible stack,
global, and descriptor semantics remain unimplemented.

The bridge now handles the basic standard stack-block literal path: descriptor
size is honored, copy/dispose helpers run, heap copies are reference-counted,
and global blocks are returned unchanged. The fixture verifies this on both
architectures. Byref cells, nested descriptors, compiler-specific layout
variants, and complete libclosure behavior remain future work.

`POSIX_SPAWN_SETPGROUP` is now accepted for the Darwin `pgroup == 0` form and
is emulated with a Windows process group on both supported architectures.
Nonzero Unix process-group assignment remains rejected because Windows has no
equivalent child-group identifier operation in this bridge.

`_Block_object_assign` and `_Block_object_dispose` now cover ordinary object
and block capture ownership and are resolved by the host symbol table on both
architectures. Basic Byref forwarding/lifetime is now covered as well: a
stack cell is promoted once and shared by subsequent assignments. Weak
capture semantics, nested Byref graphs, and compiler-specific variants remain
unimplemented and continue to fail closed.

Weak block-field flags are explicitly non-retaining now. The remaining weak
slot registration and weak-Byref teardown semantics still require the native
Objective-C weak ABI.

Cross-process Darwin `SIGKILL` is now emulated with `TerminateProcess` and is
covered by the process fixture on both targets. Graceful cross-process signal
delivery and Unix signal-mask/queue semantics remain outside the Windows
bridge.

Thread-local blocked/pending masks are now implemented for signals raised
through the Darling bridge; pending delivery occurs when the mask is removed.
Native external signal delivery, cross-thread routing, and queued `siginfo`
remain unsupported.

The host resolver now exposes `_sigaction`, `_sigprocmask`, and `_sigpending`
with the four-word Darwin signal-set ABI. Basic local handler and mask
operations are tested on both architectures; `SA_SIGINFO`, external native
delivery, and queued `siginfo` remain outside the bridge.

Basic `SA_SIGINFO` dispatch is now available for signals raised through the
bridge; the handler receives null placeholders because Windows does not yet
provide the Darwin kernel's `siginfo_t`/ucontext payload. External populated
events remain unsupported.

`_pread` and `_pwrite` are now available through the host resolver with
position-preserving regular-file semantics on both architectures. Overlapped
I/O, positioned vectors, and native filesystem-lock behavior remain future
work.

`_flock` now maps shared/exclusive/nonblocking/unlock operations to Windows
range locks and is covered by the filesystem fixture on both architectures.
Advisory inheritance and native Darwin lock edge cases remain future work.
