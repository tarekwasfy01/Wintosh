# Contract-preserving transformation matrices

Use `tools/porting_matrix.py` with the bounded contracts in
`TRANSFORM-CAPABILITIES.csv`. These rows are a small executable baseline,
not an inventory of all Darwin APIs. The existing primitive matrix remains
the broader porting inventory.

## Instruction-set boundary

The runner's input-resolution stage now accepts either a Mach-O path or an
`.app` directory. For bundles it decodes `Contents/Info.plist` through the
same CoreFoundation XML/binary property-list path used by bundle metadata,
selects `CFBundleExecutable`, and confines that name to one component under
`Contents/MacOS`. Windows drive prefixes and path separators are rejected,
and the selected path is normalized to an absolute path. The selector factor
is therefore `I(app)=M(app)·S(app)·E(app)`, where `M` means usable bundle
metadata or the documented name fallback, `S` means one safe path component,
and `E` means the selected file exists. This improves image selection only; it
does not make framework/resource closure or app behavior true:

`Launch(app) = I(app) * P * M * X * D * F * G`,

where `P/M/X` are parse, map, and execute; `D` is dependency closure, `F` is
framework/API coverage, and `G` is application behavior. An `.app` resolving to
its main Mach-O proves only `I=1`; arbitrary app execution remains unproven.
For the dependency stage, the graph now propagates the ordered runpath context
along each first-load edge:

`S(image) = unique(S(inherited) ++ LC_RPATH(image))`; `S(image)` is passed to each child.

Each node resolves `@rpath/name` against that inherited sequence before the
app's `Contents/Frameworks` and `Contents/PlugIns` fallbacks. The current graph
uses the first encountered context for a shared image, so multi-parent loading
still needs a guest fixture and compatibility corpus before `D` can be treated
as complete.

The dyld image callback bridge tracks the set of mapped guest headers `I_t`.
For a loader transition, it reports additions and removals by set difference:

`Added_t = I_t \ I_(t-1)`; `Removed_t = I_(t-1) \ I_t`.

Add-image registration reports the current snapshot immediately; `dlopen` and
`dlclose` update the tracked set, and removal callbacks run before the dynamic
image is released. Registration and these state transitions are serialized
under a reentrant process lock. This protects callback dispatch from host-side
concurrent unload; it does not make guest access to an image safe if the guest
itself races that access with `dlclose`, nor does it prove complete dyld
initializer-order semantics.

## Mach message header rights

XNU defines a six-word, 24-byte 64-bit header and three independent 5-bit
disposition fields in `msgh_bits`:
`R` for `msgh_remote_port`, `L` for `msgh_local_port`, and `V` for
`msgh_voucher_port`; the complex-message flag is separate. For a send to be
Darwin-compatible, the requested name and disposition must resolve to the
right kind, and each move/copy/make operation must update sender and receiver
right counts atomically with queue insertion. Wintosh previously laid out only
five words, aliasing `msgh_id` with the voucher slot. The ABI structure now
includes `msgh_voucher_port` and `msgh_id` at offsets 16 and 20. The current
message path uses `msgh_remote_port` as its destination and implements selected
`R` and `L` dispositions on local queues and brokered messages. The broker
carries a reply capability separately from body descriptors and materializes
it in the receiving session. Voucher behavior is unsupported, and complex
body port descriptors have a separate, partial transfer path.
Therefore the current header contract is:

| Header contract | Wintosh | XNU reference |
|---|---:|---:|
| Remote destination routing by port name | 1 | 1 |
| Validate supported remote disposition against held right | partial | 1 |
| Transfer reply/local header right on local queue | partial | 1 |
| Voucher right semantics | 0 | 1 |
| Complex body port descriptors (selected types) | partial | 1 |

The local path now checks send/send-once rights for explicit remote
dispositions, preflights combined same-port deltas, reserves moved header
rights as in-transit before queue insertion, and rolls those reservations back
if insertion fails. Receive materializes transit rights, swaps the header
fields and disposition bits, and `mach_msg_destroy` deallocates copied-out
header rights. The broker path now serializes reply-port tokens and selected
reply dispositions, validates them against session rights, and creates a
receiver-local capability when the message arrives. Voucher dispositions
remain unsupported; cross-session lifecycle races and full XNU right semantics
are not established. The local transition tracks
`r_s/r_t/r_r` as sender, in-transit, and receiver counts: a move obeys
`(Q=0,r_s=n,r_t=k) -> (Q=1,r_s=n-1,r_t=k+1)` at send and
`(Q=1,r_t=k+1) -> (Q=0,r_t=k,r_r=m+1)` at receive. Failed insertion rolls
back to the initial state. Body MOVE-SEND, MOVE-SEND-ONCE, and MOVE-RECEIVE
descriptors now use matching transit counts. Concurrent external right
mutation can still race the preflight and count updates. Queue teardown now
releases transit header and body rights, queued OOL storage, and selected
copied or created body rights. Brokered IPC needs
recipient-session capability aliases before cross-session descriptors can be
materialized. Header bits must not be
reported as supported until both local and broker routes satisfy those
invariants.

Mach-O parseability, mapping, and guest execution are separate predicates.
For the architecture boundary inspected on 2026-11-23, `1` means the source
contains that stage, `0` means it explicitly rejects the stage, and `?` means
the stage is not demonstrated on that host:

| Guest Mach-O architecture / stage | Windows x64 | Windows ARM64 | Windows Win32 |
|---|---:|---:|---:|
| x86_64 parse, map, relocation subset | 1 | 1 | 0 |
| x86_64 real entry execution | 1* | 0 | 0 |
| i386 parse, map, relocation subset | 1 | 1 | 1 |
| i386 entry path (source + host build) | 0 | 0 | 1 |
| i386 guest execution verified at runtime | 0 | 0 | 1† |
| i386 Darwin-start + host `_exit` bind verified | 0 | 0 | 1‡ |
| arm64 parse, map, relocation subset | 1 | 1 | 0 |
| arm64 A64 backend compiled and linked | 1¶ | ? | 0 |
| arm64 guest execution verified end-to-end | ? | ? | 0 |

On Windows Win32, the i386 `i386_THREAD_STATE` flavor maps its 16 32-bit
general and segment registers to the host `CONTEXT` for `thread_get_state` and
`thread_set_state`. Other i386 state flavors remain unsupported. The field
order and flavor numbers follow Apple's [XNU i386 thread-state definitions](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/i386/thread_status.h).

`1*` on x64 is limited to Clang builds with the SysV entry bridge. `1†` on
Win32 records a hand-built 32-bit `LC_MAIN` Mach-O with no imports; its entry
returned 42 through the cdecl path. It does not cover compiler-produced
applications, dynamic imports, chained fixups, Darwin-start, or frameworks.
`1‡` records a separate hand-built i386 image that entered through the Darwin
initial stack, resolved one `_exit` bind, and exited with code 42. It does not
cover compiler CRT startup or app dependencies.
`1¶` records the optional Dynarmic A64 backend compiling and linking into the
x64 Release runner. It does not prove guest instruction execution. `?` on the
end-to-end ARM64 row means the runner and A64 backend exist, but no ARM64
Mach-O fixture has run through entry, imports, and termination.

For a Mach-O dependency graph `G=(V,E)`, architecture selection now has one
graph-wide invariant. If `c` is the root CPU type, every loaded image must use
that same slice:

```text
∀v ∈ V: cpu_type(v) = c
V is loadable only if every required image contains slice c
GuestMemory = ⋃v∈V ⋃s∈segments(v) mapped(s)
```

The runner's `--arch arm64` sets `c` explicitly for a fat root image. The
selected type flows through dylib resolution, bindings, initialization order,
dynamic loading, and the A64 segment table. This makes guest-to-guest calls
across mapped ARM64 images addressable by the CPU backend. It does not convert
a guest ARM64 call into a Windows x64 host ABI call; that import bridge remains
a separate required transition.

Dateigestützte `munmap`-Teilbereiche werden als Intervalltransformation
modelliert. Für eine View `V₀ = [b,b+n)`, ein gültiges Unmap-Intervall
`U = [b+ℓ,b+ℓ+k)` und Seitenlängen `ℓ`, `k` gilt:

```text
0 ≤ ℓ, 0 < k, ℓ + k ≤ n
L = [b,b+ℓ)              falls ℓ > 0
R = [b+ℓ+k,b+n)          falls ℓ+k < n
V₁ = L ∪ R = V₀ \ U
offset₁(v) = offset₀ + (v-b)    für jedes v ∈ L ∪ R
```

Page-granular file-backed `MAP_FIXED` on Windows is represented by a larger
allocation-granularity placeholder window. Let `G` be Windows' allocation
granularity, `p` Darwin's page size, `x` the requested guest address, and `s`
the page-rounded view length. The outer reservation starts at the nearest
lower `G` boundary; its prefix and suffix lengths are:

```text
q = G floor(x / G)
r = x - q
t = G ceil((r + s) / G) - (r + s)
O = [q, q + r + s + t)
P = [q, x)                  prefix placeholder, length r
V = [x, x + s)              file view, length s
S = [x + s, q + r + s + t)  suffix placeholder, length t
O = P ∪ V ∪ S
```

The implementation reserves `O` only when the entire outer window is free,
splits and releases `P` and `S`, then replaces the exact `V` placeholder with
the file view. The file offset must satisfy `offset mod p = 0`; `x` need not
satisfy `x mod G = 0`. This removes the Windows allocation-granularity
constraint for addresses in a free envelope without claiming replacement of
arbitrary occupied neighboring mappings.

For a fixed replacement wholly inside one tracked file view, let the original
view be `V₀ = [b,b+n)` and the replacement be `M = [b+ℓ,b+ℓ+k)`. The old
mapping becomes the left and right file intervals; the middle placeholder is
then replaced by the new file interval `N`:

```text
0 ≤ ℓ, 0 < k, ℓ + k ≤ n
L = [b,b+ℓ)              if ℓ > 0
R = [b+ℓ+k,b+n)          if ℓ+k < n
V₁ = L ∪ N ∪ R
old_offset(v) = o₀ + (v-b)        for v ∈ L ∪ R
new_offset(v) = oₙ + (v-(b+ℓ))   for v ∈ N
```

The old file view is first split through the same host transaction used by
partial `munmap`; this leaves the target interval as an exact placeholder.
`MapViewOfFile3(MEM_REPLACE_PLACEHOLDER)` then installs the new section view.
The current implementation only admits a target contained in one tracked,
placeholder-backed file view. Host and ARM64 guest regression fixtures replace
the middle page of a three-page view with a different file offset and verify
both retained neighbors. A file-to-anonymous replacement uses the same
placeholder-preserving split, then replaces the target placeholder with a
zero-filled private allocation via `VirtualAlloc2(MEM_REPLACE_PLACEHOLDER)`.
The anonymous allocation is entered into its own host ownership index before
the guest mapping transaction commits. The host and ARM64 fixtures verify the
initial zero bytes and retained file neighbors. Anonymous-to-file,
cross-allocation, image, and multi-view replacements still need separate
ownership transactions, except that the host and ARM64 fixtures now cover
anonymous-to-file replacement when the target equals one complete tracked
anonymous reservation. That case releases the reservation as a unit before
installing the file view; partial anonymous-to-file replacement remains open.
Formally, for anonymous reservation `A = [b,b+n)` and fixed file target
`T = [x,x+s)`, the current reverse transition requires `x=b` and `s=n`:

```text
T = A
Owner_before(A) = anonymous
Owner_after(A) = file(mapping, offset)
```

The replacement is rejected if `T` only covers part of `A`, crosses another
reservation, or includes an unmapped hole.

Mit Seitengröße `p` und Seitenzahlen `a = ℓ/p`, `m = k/p` und
`c = (n-ℓ-k)/p` entspricht die Erhaltung der beiden Restbereiche auf dem
Seitenvektor `x₀` der Selektionsmatrix

```text
Qᵤ = [ Ia  0   0 ]
     [  0   0  Ic ]       x₁ = Qᵤ x₀
```

`Qᵤ` hat `(a+c) × (a+m+c)` Dimensionen; der mittlere Block der `m` entfernten
Seiten wird verworfen. Für `MAP_SHARED` werden die Restbereiche auf dieselben
Dateioffsets zurückgemappt. Für schreibbares `MAP_PRIVATE` sichert die Laufzeit
`x₀` vor dem Entladen und schreibt `Qᵤx₀` in die neu erzeugten privaten Views
zurück. Die Schutzfunktion wird auf dieselben Restintervalle eingeschränkt.

The separate i386 object-to-image experiment is bounded as follows:

| Build transition | Windows Win32 |
|---|---:|
| Clang i386 objects linked into `MH_EXECUTE` | 1§ |
| Mach-O object-symbol resolution in supported sections | 1§ |
| i386 same-section PC-relative relocation preservation | 1§ |
| i386 paired scattered `SECTDIFF` / `LOCAL_SECTDIFF` relocation | 1§ |
| i386 `__mod_init_func` pointer relocation and pre-entry call | 1§ |
| i386 `__mod_term_func` post-entry callback | 1§ |
| i386 C++ `__StaticInit`, `___dso_handle`, and `___cxa_atexit` path | 1§ |
| Cross-object i386 PC-relative external call | 1§ |
| i386 `__DATA` global read/write with dyld slide rebase | 1§ |
| i386 zero-initialized `__common` global read/write | 1§ |
| i386 cross-object global via `__IMPORT,__pointers` | 1§ |
| i386 pointer into `__cstring` and literal read | 1§ |
| Simultaneous i386 `__const` and `__cstring` sections | 1§ |
| i386 host-bound `_puts` and `_printf` calls | 1§ |
| i386 host-bound heap, `_exit`, and `_atexit` lifecycle calls | 1§ |
| Compiler-produced image started by Wintosh | 1§ |

`1§` covers a local same-section call, a two-object `_main` to `_helper` call,
and a single-object mutable `int` global initialized to 40, incremented, read
back, and returned as 42 after the dyld slide rebase, a zero-initialized
`__common` global written and returned as 42, plus a cross-object global
resolved through an indirect pointer slot and returned as 42. For a local
relocation, global and local pointers to `__cstring` literals returned byte
values 65 and 66. Host-bound `_puts` printed a line, and `_printf` formatted
`guest=7` and returned status 3. The linker emits constant bytes in a
non-writable `__TEXT,__const` section. `_malloc`/`_free` allocated, mutated,
and released a guest buffer whose sum returned 42; `_exit(23)` terminated with
status 23; `_atexit` ran a `_puts` callback after normal `main` return 3.
A libc fixture also copied `ABC` with `_memcpy`, checked source and environment
strings with `_strlen`, read `WINTOSH_FIXTURE=42` through `_getenv`, and
returned 42. These are host-backed functions, not a complete Darwin libc.
The same libc guest compiled as PIC also returned 42 after applying paired
scattered section-difference relocations. For object target `A`, minuend `B`,
and stored field value `V`, the linker computes `C = V - (A - B)` and writes
`A' - B' + C` at final placement; a common slide cancels from the difference.
Two separate PIC objects with a cross-object function call and local literal
access also returned 84 in both input orders.
A constructor changed BSS state from 0 to 42 before `main`; the image exposed
`__mod_init_func` and Wintosh ran it before entry. With homogeneous state
`x=[g,1]^T`, the observed initializer is `T=[[1,42],[0,1]]`, so
`T[0,1]^T=[42,1]^T`.
A C++ global object also registered its destructor through `___cxa_atexit`
using a guest-local `___dso_handle`; after `main` returned 42, the destructor
printed and the process retained status 42.
A paired C initializer/terminator image set its global to 42 before entry,
printed `guest terminator` after normal entry return, and preserved status 42.
A common slide `δ` preserves a same-section displacement:
`(S + δ) - (P + 4 + δ) = S - (P + 4)`. For the measured external Clang
relocation, the linker combines the stored addend with the resolved symbol
offset and source-section placement. The two-object output worked with either
input ordering and returned 42 in Wintosh. The indirect-pointer proof covers
one cross-object global form only. Other relocation kinds, compiler CRT,
imported runtime symbols, and frameworks remain unproven. See
[LINKER-RESEARCH.md](LINKER-RESEARCH.md) for the exact link contract.

The ARM64
source path now admits matching-architecture ARM64 code and includes a
Darwin-start transfer, but execution remains `?` because this environment has
not produced an ARM64 runner or executed an ARM64 guest. The Win32 target now
builds the host library and runner and includes an i386 Darwin-start trampoline;
the minimal startup/bind fixture described above exits with code 42.
All 32 host C++ source files and the ARM64 trampoline compile and archive as
33 ARM64 COFF members;
`llvm-readobj` confirms every member has machine type `IMAGE_FILE_MACHINE_ARM64`.
A fresh ARM64 CMake compiler check still fails at link because `msvcrtd.lib` is
missing. The x64 host
cannot execute ARM64 instructions directly; a CPU translation engine is
required. Fat Mach-O selection now prefers the native Windows host slice
(ARM64 on ARM64 Windows, x86_64 on x64 Windows), retaining other recognized
slices for inspection when a native slice is absent. Windows ARM64 excludes
x64 assembly from CMake selection. The full host source compiles to an ARM64
static archive through direct Clang compilation. The ARM64
`main(argc, argv, envp)` entry uses x0-x2, which align for these integer/pointer
parameters. Apple documents differences for aggregates, stack-passed
arguments, variadic functions, and red-zone use; the Objective-C host API
boundary therefore needs its own ARM64 ABI work and guest fixture
([Apple ARM64 guidance](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms),
[Windows ARM64 ABI](https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions?view=msvc-170)).

Apple ARM64 places variadic arguments in stack slots and defines `va_list` as a
byte pointer; Windows ARM64 variadic calls use the Windows register/home-area
convention. A compiler probe confirmed the differing layouts. Native Windows
ARM64 routes guest `objc_msgSend` imports to a small assembly entry that passes
the guest stack pointer to a signature-aware C++ reader. The x64-hosted ARM64
interpreter now uses an ARM64 SVC import trampoline to invoke that same typed
reader with guest state. The interpreter path also adapts selected scalar C
imports. Other host symbols fail closed until an adapter is registered. Neither
route establishes complete Cocoa ABI compatibility.

Apple also uses a 128-byte red zone while Windows reserves only 16 bytes for
its red-zone use. The direct native-entry path does not adapt that contract.

This difference was checked with the installed Clang 22.1.3 by compiling the
same C call to ARM64 assembly for both targets. For
`sink(receiver, selector, double_value, int_value)`, the Darwin output stores
the variadic values in stack slots before `bl _sink`; the Windows output moves
the `double` bits into `x2`, the integer into `x3`, then calls `sink`. The
generated listings are in the ignored local build directory
`build-windows-arm64-clang-verify/abi_probe_{darwin,windows}.s`; this is a
compiler ABI probe, not a runtime guest test.
The fixed-arity CGRect probe also emits `d0-d3` for the four-double HFA on
both targets (`rect_probe_{darwin,windows}.s`), supporting this one aggregate
case only. `return_probe_{darwin,windows}.s` and
`return_classes_{darwin,windows}.s` show matching floating, HFA, and
two-integer return register classes for the sampled signatures.

For a guest call `g` and host implementation `h`, safe dispatch requires both
an argument transform `A` and a return transform `R` that preserve the
signature's Darwin value:

`Dispatch(g,h,s) = A_Darwin→Windows(s) · h · R_Windows→Darwin(s)`

The current ARM64 dispatch transform matrix is consequently:

| Guest call class | Entry layout transform | IMP call transform | Status |
|---|---:|---:|---|
| `main(argc, argv, envp)` integer/pointer args | identity for x0-x2 | not applicable | source implemented; runtime unverified |
| Objective-C message with no explicit args | identity for x0-x1 | identity for basic scalar/object ABI | source compiled; guest unverified |
| Objective-C message with supported variadic args | Darwin stack reader keyed by method encoding | typed fixed-arity host call for scalar/object forms | source compiled; guest unverified |
| `objc_msgSendSuper` / `objc_msgSendSuper2` | ARM64 entries unpack `objc_super` and pass the Darwin stack area | legacy lookup starts at `super_class`; Super2 starts at its superclass | source compiled; guest unverified |
| `float` / `double` returns | encoding-based typed IMP call | assembly restores `s0` / `d0` bits alongside `x0` | source compiled; guest unverified |
| CGRect/CGPoint/CGSize HFA returns | encoding-based typed IMP call | assembly restores `d0-d3`; compiler probes match on both ABIs | source compiled; guest unverified |
| NSRange return | encoding-based typed IMP call | assembly restores `x0-x1`; compiler probes match on both ABIs | source compiled; guest unverified |
| CGRect/HFA arguments | Darwin stack reader | Windows and Apple ARM64 compiler probes both use `d0-d3` for four-double HFA | source compiled; guest unverified |
| CoreFoundation Source0 callbacks | Host-to-guest call places up to eight integer/pointer arguments in `x0-x7` | Active ARM64 interpreter resumes the callback PC with the Darwin GP registers; native-ABI guests keep direct calls | ARM64 and x86_64 guest fixtures pass |
| Selected ARM64 Source0 imports | Guest-to-host adapter maps scalar/pointer arguments to CoreFoundation signatures | Validates guest strings and source-context ranges; adapts create, context retrieval, add/remove, current loop, and release | ARM64 guest fixture passes |
| Other aggregates, indirect returns, and overflow cases | signature-specific | signature-specific | requires fixtures |
| Apple red-zone access | Windows host must preserve/translate | not applicable | missing |

Thus same-ISA execution is a necessary condition, not ABI equivalence:

`Runnable(host, guest) = ISA_match · EntryABI_match · CallABI_match · RuntimeCoverage`.

For ARM64 GUI applications `CallABI_match` remains unknown until unsupported
message signatures, indirect-return/overflow fixtures, red-zone behavior, and
an ARM64 Mach-O guest are verified. The ARM64 entries now preserve `x0-x1` and
`d0-d3` return registers, and compiler probes show matching register classes
for scalar floating returns, CGRect HFA returns, and NSRange integer-pair
returns. The C++ dispatchers and assembly shims compile to COFF, but neither a
linked ARM64 runner nor runtime guest execution has been verified. Do not infer
Cocoa compatibility from the object files.

## NSBezierPath point-array dispatch

For a guest array `P = (p₀, …, pₙ₋₁)` of 16-byte `CGPoint` values, the path
operation is modeled as

`Append(P) = (Move(p₀) if no open subpath else Line(p₀)) · Πᵢ₌₁ⁿ⁻¹ Line(pᵢ)`.

If the current path is closed (its last command is `Close`, `Rect`, or `Oval`),
the first point begins a new subpath. The operation never closes the new path.
Every coordinate must be finite, `0 < n ≤ 1,000,000`, and the guest byte range
`[P, P + 16n)` must be readable before any path command is committed. This
gives the host adapter a bounded memory-copy contract rather than dereferencing
an unchecked Mach-O pointer.

The method encoding is `v@:^{CGPoint=dd}q`: two explicit arguments, a pointer
to points and signed 64-bit `NSInteger` count. The call transform is
architecture-dependent:

| Guest architecture | Guest argument source | Host IMP argument form | Implemented path |
|---|---|---|---|
| x86_64 Darwin SysV | GP slots for pointer and count after `self,_cmd` | typed fixed-arity call; guest IMP preserves SysV | encoding-specific dispatcher |
| Windows ARM64 host | Darwin variadic stack slots | typed call in Windows ARM64 integer registers | ARM64 stack reader and method dispatcher |

`Dispatch(P,n)` is valid only if the encoding selects this signature, both
arguments retain their width/signedness, and the complete point range is
copied. The source implements these edges; a guest fixture has not yet checked
selector dispatch or the resulting path elements.

## NSBezierPath associated-point replacement

Let the expanded element sequence be `E = (e₀, …, eₘ₋₁)`. The associated
point cardinality is

`k(eᵢ) = 3 if kind(eᵢ) = Curve, otherwise 1 for Move, Line, or Close`.

The setter is a partial state transform: `Set(E, i, P)` is defined only for
`0 ≤ i < m`, a supported element kind, and a readable finite point vector `P`
of exactly `k(eᵢ)` points. It preserves `kind(eᵢ)` and every other element.
For a cubic, the public vector is `(control₁, control₂, endpoint)` while the
host command stores `(endpoint, control₁, control₂)`, so its permutation is

`(p₀, p₁, p₂) ↦ (host₂, host₀, host₁)`.

Rectangles and ovals first map to their enumerated Move/Line/Curve/Close
sequence, after which `Set` replaces one member and the sequence becomes the
path's stored representation. The ABI encoding is `v@:^{CGPoint=dd}q`: the
guest passes a point-array pointer followed by a signed 64-bit element index.
The x86_64 dispatcher reads the two SysV GP slots; the Windows ARM64 guest
reader consumes the corresponding Darwin variadic stack slots. This proves
the source-level mapping only; a guest fixture is still needed to verify the
selector ABI and observed geometry.

## NSView rectangle conversion dispatch

For each view `v`, represent its axis-aligned local-to-window mapping as
`Tᵥ(p) = Aᵥp + bᵥ`, where `Aᵥ = diag(sₓ, sᵧ)` composes frame-to-bounds
scales and sign changes from flipped-coordinate transitions. The translation
`bᵥ` composes frame origins and bounds-origin offsets. Conversion from source
`s` into target `t` is

`pₜ = Aₜ⁻¹(Aₛpₛ + bₛ - bₜ)`.

A nil endpoint uses the identity window transform. For an input rectangle `R`,
map all four corners with the point transform and form their axis-aligned
envelope; this preserves the represented region under axis-aligned scales and
flips. Rotated transforms are outside this model.

`convertSize:toView:` and `convertSize:fromView:` apply only the linear part:
`qₜ = |Aₜ⁻¹Aₛqₛ|` componentwise. Translation cancels for a vector, and the
absolute value enforces Cocoa's positive-width/positive-height result even
when a flipped axis reverses direction. A nil endpoint again uses the window
identity transform. The value ABI is `CGSize (CGSize, NSView*)`; x86_64 reads
the two size doubles from XMM registers and the view from a GP register, while
Windows ARM64 reads the aggregate and object from the Darwin variadic stack and
returns a two-double HFA. Apple requires both views to belong to the same
non-nil window and documents the nil endpoint as window coordinates; Wintosh
returns the input unchanged when its view mapping is invalid.

The Objective-C selector signature has return and argument shape
`CGRect (CGRect, NSView*)`. On x86_64 Darwin the selected `objc_msgSend_stret`
path routes the hidden return storage, stack CGRect, and GP-register view
argument; the Super and Super2 variants select their respective starting
classes. The Windows ARM64 guest path reads the two arguments from Darwin
variadic stack slots and writes the CGRect HFA result registers. The compiled
paths do not prove guest correctness until a Mach-O fixture checks both the
coordinates and selector dispatch.

Therefore architecture support composes as

`E(host, guest) = P(host, guest) AND M(host, guest) AND X(host, guest)`,

where `P` is parse support, `M` mapping/relocation support, and `X` execution
support. `P = M = 1` does not imply `E = 1` when the execution edge is zero or
unknown.

For Boolean matrices, composition is

`(A * B)[i,j] = OR_k (A[i,k] AND B[k,j])`.

For the proposed route:

`C_wsl = D_darwin_linux * L_linux_wsl1 * W_wsl1_windows`.

Each intermediate contract must include the same meaning, ABI, memory
ownership, error convention, and architecture. Matching syscall names or
numbers is insufficient. This initial model uses diagonal matrices: each row
preserves exactly one named contract. It intentionally provides no inferred
cross-contract substitutions.

## App activation state transform

The Win32 app-activation signal maps to Cocoa's two-state application activity
model. With column state `x = [inactive, active]^T`, the two idempotent state
transforms are

`T_activate = [[0, 0], [1, 1]]` and
`T_resign = [[1, 1], [0, 0]]`.

Thus `T_activate^2 = T_activate` and `T_resign^2 = T_resign`: duplicate
`WM_ACTIVATEAPP` messages from a process with multiple top-level windows do not
create duplicate lifecycle callbacks. A real transition emits `will`, updates
the state observed by `isActive`, then emits `did`; the four Cocoa notifications
and optional delegate callbacks receive `NSApp` on the application thread.
This maps event meaning and delivery thread, but Windows foreground activation
does not model every AppKit workspace, hide/unhide, screen-change, or modal
activation policy. Source and build evidence currently support this adapter;
guest callback behavior still needs a Mach-O fixture.

Application visibility is a second two-state transform. For
`v = [visible, hidden]^T`, `H_hide = [[0, 0], [1, 1]]` and
`H_unhide = [[1, 1], [0, 0]]`; both are idempotent. Each accepted operation
dispatches its `will` event, applies the window visibility mutation, then
dispatches `did`. Reentrant visibility requests during callbacks are ignored
while the transition is in flight. `unhide:` composes visibility restoration
with a later activation request, while `unhideWithoutActivation` applies only
the visibility transform.

## AppKit launch and NIB dependency chain

Launching a GUI app requires a separate path from loading its Mach-O image.
The current AppKit host resolves `NSPrincipalClass` and reads `NSMainNibFile`
from the main bundle. `+[NSBundle loadNibNamed:owner:]` can also select a
named NIB in the main bundle and pass a File's Owner explicitly. The modern
instance route uses `NSNib` (`initWithNibNamed:bundle:` or
`initWithNibData:bundle:` followed by `instantiateWithOwner:topLevelObjects:`), while
`-[NSBundle loadNibNamed:owner:topLevelObjects:]` also returns top-level
objects inferred from the classic archive's object and OID maps. These routes
look up a flat `.nib` resource or the
`keyedobjects.nib` member of a NIB package, then ask the keyed unarchiver to
materialize the archive. For classic `NSIBObjectData` roots it walks the root,
object values, connection records, and visible-window references before
running connectors, `awakeFromNib`, and window ordering. Apple defines bundle
nib loading as unarchiving the contents and linking them to an owner. The full
dependency chain is therefore:

The similarly named `loadNibNamed:owner:options:` API belongs to UIKit's
`NSBundle` additions and returns top-level objects using UIKit proxy options;
it is not the AppKit method surface. Wintosh keeps UIKit nib semantics outside
this macOS path.

`E_nib = B * N * U * G * C * A * V * R`,

where `B` is bundle/resource lookup, `N` is main or named NIB selection, `U` is
keyed-archive decoding (including preserving binary-plist UID references),
`G` is class/object-graph construction, `C` is outlet/action connection,
`A` is `awakeFromNib` delivery, and `V` is AppKit object-to-Win32 view
materialization, and `R` is ownership/retention of the instantiated object
graph. Each factor uses the matrix's `{0, partial, 1}` evidence states and
composition is AND for this single required route. `B=1`; `N`, `U`, `G`, `V`,
`C`, `A`, and `R` are partial. Since required archive, object, proxy, and
framework behavior remains incomplete, the composed route is `E_nib=0`; it can become
nonzero only after every missing stage is implemented and evidenced. XML/binary
property-list support alone is not keyed unarchiving. Wintosh now
provides `NSKeyedUnarchiver +unarchiveObjectWithData:` for a subset of
`NSKeyedArchiver` Foundation graphs: UID references, strings, numbers, data,
dates, arrays, dictionaries, and null values. It caches UID placeholders before
recursive construction, supporting cycles in arrays, dictionaries, and custom
classes while preserving identity. Custom classes with `initWithCoder:` can read object,
class-checked object, bool, signed/unsigned 32- and 64-bit integer, float, and
double fields through a temporary decoder context; superclass and conditional
decoding remain absent. The host `NSView -initWithCoder:` additionally reads
`NSFrame`, optional `NSBounds`, `NSSubviews`, hidden bits in `NSvFlags`, and
`NSNextKeyView` using NSString-based keyed geometry values. A keyed
`NSCustomView` placeholder resolves `NSClassName`, creates a loaded `NSView`
subclass through `initWithFrame:`, and restores selected bounds, subviews,
hidden state, and next-key-view links. `NSViewTemplate` and
`NSTextViewTemplate` validate that their archived class is an `NSView` subclass,
cache the placeholder, and call the real class's `initWithCoder:` under the
archive context, allowing inherited `NSView` decoding to restore those selected
fields. Custom coder state remains limited to the decoder's supported key types
and no real NIB fixture currently covers these template substitutions.

Window templates are a separate unresolved graph node. A representative
Interface Builder XIB encodes `NSWindowTemplate` with `NSWindowStyleMask`,
`NSWindowRect`, `NSWindowTitle`, `NSWindowClass`, `NSWTFlags`, and
`NSWindowView`. The current decoder maps `NSWindowTemplate` to the requested
registered `NSWindow` subclass (falling back to `NSWindow`), restores frame,
style, title, minimum/maximum content size, then attaches an archived `NSView`
content tree. The archived window frame is restored after creation because
`NSWindow` initialization accepts the content rect. Plain `NSView` decoding
also accepts the common `NSFrameSize` field. This is still only an
object-graph slice: outlet and control target/action connectors are applied
after successful graph decoding. `NSMenu`/`NSMenuItem` and `NSCustomObject`
have selected decoding paths. `NSMenuTemplate` now substitutes to the archived
registered `NSMenu` subclass (or base `NSMenu`) and reuses its existing coder
for title, items, and main-menu registration. The private Apple menu-template
field layout has no real-NIB fixture yet, so this source path is build-verified
but not established as runtime-compatible. `NSOidsKeys`/`NSOidsValues` now seed classic
negative proxy IDs before graph materialization: `-1` resolves to the current
nib owner, `-2` to the First Responder nil-target sentinel, and `-3` to
`NSApp`. Main-nib loading sets the owner to the shared application instance, so
File's Owner connections target the application. Apple documents custom named
placeholders other than File's Owner as unsupported in OS X NIBs; UIKit's named
external-object replacement is a separate API surface. Complete top-level
ownership semantics remain partial.
Binding connectors, complete class substitution, and full toolbar/template
properties remain absent. Bundle resource lookup now checks language-specific
`.lproj` resource paths in Windows UI language order, then the bundle's
development region and `Base`, before unlocalized resources. This enables
localized NIB path selection but does not establish full localization
compatibility. The implemented lifecycle
calls `awakeFromNib` on decoded objects and then orders windows listed in
`NSVisibleWindows`; when the archive has no usable list, it falls back to all
constructed windows. File's Owner receives `awakeFromNib` after other decoded
objects when it implements the selector; other callback ordering is not
specified. Other window visibility flags are not decoded precisely.
Modern instance APIs associate retained decoded archive objects with the
returned top-level array and release them when that array dies. This keeps
outlet ivars alive but is broader than Cocoa's top-level strong-reference
ownership rules. Legacy and main-nib paths still retain the decoded graph for
the application lifetime. Visible-window state is isolated per load so a
nested NIB load during `awakeFromNib` does not overwrite its caller's window
list.

The next implementation boundary is additional archived object types,
validated NIB fixtures, and an application compatibility corpus. Apple documents a
broader order: instantiate encoded objects, reconnect
outlets/actions/bindings, call `awakeFromNib`, and show configured windows.
The implemented loader handles only a subset of classic keyed NIB archives;
storyboards, newer archive variants, complete localization behavior, and a
real-app compatibility corpus are still missing.
Apple documents `NSKeyedUnarchiver` as restoring keyed object
graphs, and Apple's cross-platform Foundation source demonstrates archive
object tables, UID dereferencing, decoding contexts, and class validation.
GNUstep's separate XIB5 unarchiver is a useful reference for the additional
UI-specific layer; it is a large specialized implementation, not a drop-in
binary macOS NIB decoder. These references provide structure only; no external
source code was copied.

| Stage | Wintosh evidence | State |
|---|---|---:|
| Bundle and resource lookup (`B`) | `NSBundle`/`CFBundle` main bundle and resource APIs, including Windows UI language `.lproj` lookup followed by development-region/Base and unlocalized fallbacks | partial |
| Main/named NIB selection (`N`) | `NSMainNibFile`, deprecated `+[NSBundle loadNibNamed:owner:]`, `-[NSBundle loadNibNamed:owner:topLevelObjects:]`, and `NSNib` name/data initialization plus instantiation; named initializer preloads flat NIB/package data and data initializer retains supplied NSData | partial |
| Keyed archive (`U`) | `unarchiveObjectWithData:` decodes selected Foundation classes, caches repeated UIDs before recursion, and supports restricted custom `initWithCoder:` field decoding; full NSCoder policy/delegates remain absent | partial |
| Object graph (`G`) | Classic `NSIBObjectData` root/object/connector/visible-window/OID fields plus selected Foundation, NSView/NSCustomView/NSViewTemplate/NSTextViewTemplate, NSCustomObject, NSMenu/NSMenuItem/NSMenuTemplate, and NSWindowTemplate decoding; private template fields and unsupported classes lack real-NIB fixture coverage | partial |
| Connections (`C`) | Reachable `NSNibOutletConnector` setter or object-ivar outlet connections and `NSNibControlConnector` target/action links run after graph decoding; bindings and unsupported proxy archive forms remain absent | partial |
| `awakeFromNib` (`A`) | Calls `awakeFromNib` on decoded objects after connectors and then on File's Owner; other callback order is unspecified | partial |
| Native views (`V`) | Partial NSWindow/NSView/control surface on Win32; NSImage/NSImageView and NSData-backed NSBitmapImageRep use ImageIO/WIC and active CGContext; ICNS uses the largest embedded PNG; archived NSReps can select decoded bitmap reps, while other reps and vector formats remain absent | partial |
| Object lifetime (`R`) | Modern top-level arrays retain the decoded archive graph and release it when the array dies; this retains more than Cocoa's top-level strong references, while legacy/main-nib loads still retain the graph for process lifetime | partial |

References: [Apple nib object life cycle](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/LoadingResources/CocoaNibs/CocoaNibs.html), [Apple NSBundle NIB loading and top-level ownership](https://developer.apple.com/documentation/foundation/nsbundle/loadnibnamed%3Aowner%3Atoplevelobjects%3A?language=objc), [Apple NSNib](https://developer.apple.com/documentation/appkit/nsnib), [Apple NSNibConnector](https://developer.apple.com/documentation/appkit/nsnibconnector), [Apple compiled NIB packaging notes](https://leopard-adc.pepas.com/releasenotes/DeveloperTools/RN-InterfaceBuilder-Previous/index.html), [Chromium Interface Builder archive sample](https://chromium.googlesource.com/chromium/src/+/df73e8c2f83cf17d7c737482b225d42db83f258e/chrome/app/nibs/HungRendererDialog.xib), [sample XIB window-template fields](https://zenn.dev/okunokentaro/articles/01ejkz0012hp10vv9dkxbjv5hg), [GNUstep window-template decoder and NIB connection lifecycle](https://sources.debian.org/src/gnustep-gui/0.28.0-3/Source/GSNibLoading.m/), [Apple NSCoder geometry decoding](https://developer.apple.com/documentation/Foundation/NSCoder?changes=la&language=objc), [Apple NSKeyedUnarchiver](https://developer.apple.com/documentation/Foundation/NSKeyedUnarchiver), and [swift-corelibs-foundation geometry coding](https://github.com/swiftlang/swift-corelibs-foundation/blob/main/Foundation/NSGeometry.swift).

The URL-based `NSNib` initializer remains unsupported: Apple deprecated it in
macOS 10.8 and documents an assertion for macOS 13; use name/bundle or data/
bundle initialization for current apps ([macOS 13 AppKit release notes](https://developer.apple.com/documentation/macos-release-notes/appkit-release-notes-for-macos-13)).

## Dispatch queue path

The Objective-C block runtime already copies and releases compiler-generated
Darwin blocks. Wintosh now composes that ownership path with queue execution:

`E_dispatch = B * Q * T * O`,

where `B` is block ownership/invocation, `Q` is queue ordering, `T` is host
thread/message-loop scheduling, and `O` is once-only initialization. The queue
term now decomposes as

`Q = Q_serial + Q_private-concurrent + Q_barrier`.

`Q_private-concurrent` bounds worker count to `min(8, max(2, hardware_threads))`;
`Q_barrier` runs a barrier task only after earlier tasks finish and blocks later
tasks until it completes. Main-queue work is posted to the AppKit thread and
drained from its Win32 message loop; global work uses a bounded concurrent pool
and global-queue barriers remain ordinary work. Groups, sources, timers,
queue-specific data, and counted semaphores are implemented as partial
contracts. `dispatch_walltime` maps its absolute wall-clock target `W_target`
onto the monotonic scheduler as `D = M_now + max(0, W_target - W_now)`; later
wall-clock changes do not retarget `D`. Remaining gaps include QoS and
target-queue attributes, exact same-queue synchronous edge cases, timer leeway,
Mach-clock conversion, and a guest block fixture. This does not establish full
application-level dispatch compatibility.

Mach receive-source readiness is modeled as `R(t) = q(port, t)`, where `q` is
the number of messages currently queued on a live receive right. A 10 ms host
poll samples `R`; if `R > 0`, the source schedules a callback and exposes the
sampled count. Sampling never dequeues a Mach message, so the callback's guest
`mach_msg` call remains the only consumer. This is level-triggered behavior:
messages left queued can trigger another callback after the previous callback
returns. Destroying the receive right terminates polling through source
cancellation. For a send source, `S(t)` is the transition where the watched
port's receive right is destroyed. Wintosh retains one send reference and
registers `MACH_NOTIFY_DEAD_NAME`; the local Mach notification message drives
one `DISPATCH_MACH_SEND_DEAD` event. This event path is local to the current
Mach namespace and does not yet observe remote broker-side port death.

## WSL observation boundary

The recorded unrestricted host inspection on 2026-11-15 found one stopped
Ubuntu WSL2 distribution and no WSL1 distribution. In the 2026-11-16 Codex
session, `wsl --status` and `wsl --list --verbose` returned
`Wsl/E_ACCESSDENIED`; querying optional Windows features returned an elevated-
rights requirement. Those latter results do not establish whether WSL or either
feature changed since the earlier observation, so the historical WSL2 record
remains the last successful host measurement. No conversion or feature change
was attempted.

Each matrix has lower and upper bounds. A missing edge is `[0,0]`; an
unknown edge is `[0,1]`. A native implemented edge with a passing named CTest
gate is `[1,1]` **only for that gate's documented scope**; without evidence it
is `[0,1]`. A WSL route currently has no integrated proof gates, so its lower
bound remains zero even if a row is manually labeled implemented.
The upper bound is a hypothesis, not proof that implementation is possible.

For requirement vector `r`, acceptance requires

`AND_i (NOT r[i] OR C_lower[i,i]) = 1`.

Unknown requirements are errors. Unknown edges, skipped tests, missing tests,
failed tests, and duplicate test names with any failure cannot produce proof.
A report without `--junit` has no demonstrated native capabilities.
The report describes the supplied test run, not every machine or future
source revision. Rerun CTest after changing the implementation.

## Reproducible commands

From the repository root, after building the native test executables and
runner (replace `ctest` with its installed full path if needed):

```powershell
ctest --test-dir build-windows-msvc -C Release `
  -R '^wintosh_(tlv|thread|dyld|macho)_smoke$' `
  --output-on-failure --output-junit matrix-evidence.xml
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
python tools/porting_matrix.py `
  --junit build-windows-msvc/matrix-evidence.xml `
  --output build-windows-msvc/porting-matrix.json
python -m unittest discover -s tools/tests -p test_porting_matrix.py
```

Use repeated `--require <capability>` to evaluate a bounded requirement set.
By default all modeled rows are required. Passing all requirements means only
that the listed contracts passed; omitted application requirements remain
unassessed. This tool never generates adapters or executes commands from
matrix data. Its successful exit means the report was generated, not that
all compatibility requirements were met; inspect `all_required_demonstrated`.
For a command-chain gate, pass `--check native` or
`--check darwin_linux_wsl1_windows`: exit 1 means at least one required
contract is missing or unproven; malformed input exits 2.

WSL availability alone does not prove a Darling loader, Mach services,
Objective-C runtime, or macOS frameworks work. Microsoft's
[WSL comparison](https://learn.microsoft.com/en-us/windows/wsl/compare-versions)
distinguishes WSL1's compatibility layer from WSL2's real Linux kernel and
full syscall compatibility. The command-forwarding prototype in
`tools/Invoke-DarlingWslBackend.ps1` supplies no Darwin-to-Linux proof.

The next WSL task is to record the actual distribution/version, run a bounded
Darling Mach-O fixture there, capture missing contracts, and add route-specific
test gates. An installed WSL distribution must be measured before any WSL1
execution claim is added.

## Observed host and evidence, 2026-11-15

The native MSVC x64 CTest run passed all four modeled fixture/adapter gates.
The seven matrix regression tests passed, and CLI checks confirmed that
removing JUnit evidence removes native proof and that native passes do not
enable the WSL route.

An unrestricted `wsl.exe --list --verbose` found `Ubuntu`, stopped, version
**2**. No WSL1 distribution was listed. Attempting a read-only kernel/Darling
inventory inside Ubuntu failed before executing Linux commands with
`Wsl/Service/CreateInstance/CreateVm/HCS/HCS_E_HYPERV_NOT_INSTALLED`.
WSL also reported unavailable virtualization and `ERROR_INVALID_HANDLE`
while querying nested virtualization. Darling installation could therefore
not be inspected. The initial sandbox access denial alone was not used to
infer the host's WSL configuration. No distribution conversion or Windows
feature change was performed.

## Run-loop mode scheduling

Apple's contract is mode-selective: a source is serviced only while the run
loop executes in a mode to which that source was registered. A common-mode
registration expands to each mode in that loop's common set. Wintosh now
applies the same filter to queued callbacks. Let `M(w)` be the mode set for
work item `w`, `C(L)` the common-mode set for loop `L`, and `m` its active
mode:

`Eligible(w,L,m) = (m ∈ M(w)) ∨ (Common ∈ M(w) ∧ m ∈ C(L))`

For a one-source turn, Wintosh selects the earliest eligible queue item with
the lowest declared order; FIFO insertion order breaks ties. A draining turn
extracts only eligible items, sorts them by order, and leaves all other modes
pending. The current default common set is `{kCFRunLoopDefaultMode}` until
`CFRunLoopAddCommonMode` extends it.

| Contract layer | Darwin expectation | Wintosh implementation | WSL1 comparison | Evidence |
|---|---|---|---|---|
| Mode membership | Work runs only in registered modes | Mode-tagged callbacks; default and common modes | WSL1 offers Linux compatibility but does not define Cocoa run-loop modes | Clang Release build; no guest fixture |
| Ordered selector queue | Process target selector on target thread in requested modes/order | `NSRunLoop.performSelector:target:argument:order:modes:` | No direct WSL1 equivalent | Source/build only |
| Main AppKit loop | Cocoa loop dispatches AppKit input and Foundation work | Existing Win32 message loop pumps default-mode CF callback work | WSL1 cannot supply AppKit behavior | Source/build only |
| CF source version 0 | Register by mode, signal, retain, invalidate, and service in order; schedule/cancel callouts bracket actual membership transitions | Mode-tagged callback queue; first eligible loop consumes a signal; duplicate add/remove operations do not repeat callouts | Not supplied by WSL1 | x86_64 callback ABI and ARM64 interpreter callback/import paths pass focused guest fixtures |
| CF source version 1 | Register a Mach port and perform received messages in order | `getPort` is queried on registration and each turn; incoming messages are polled on 10 ms wait slices | Not supplied by WSL1 | Clang Release build; reply-message handling and guest fixture remain open |
| CF timer | Fire at deadline, repeat on cadence, invalidate after one shot | Timer registry per loop and mode; repeating fire date advances over missed intervals | Not supplied by WSL1 | Clang Release build; no guest fixture |
| CF observer | Report run-loop phases in registered modes; one-shot invalidates once | Ordered callbacks for entry, before timers/sources, before/after wait, and exit | Not supplied by WSL1 | Clang Release build; no guest fixture |

For a source `s`, registered loops `L(s)` and generation `g`,
`Signal(s,g)` queues at most one delivery token per loop across the union of
its registered modes. Delivery is enabled only if `valid(s) ∧ signaled(s) ∧
generation(s)=g`; the first eligible loop atomically clears `signaled(s)`, so
remaining mode/loop tokens become no-ops. This models version-0 manual
signaling. For a concrete loop/mode pair, let `R(s,L,m)` be its registration
bit. A successful transition `0→1` commits membership and then calls
`schedule(info,L,m)` once; an attempted `1→1` add has no callout. A successful
transition `1→0` removes membership and then calls `cancel(info,L,m)` once; an
attempted `0→0` removal has no callout. Callouts run after the internal locks
are released, so guest code can safely re-enter CoreFoundation. The guest
fixture checks both callback pointers through `GetContext`, validates the
arguments, and verifies idempotence on repeated add/remove. Version-1 sources
use `getPort` to poll the registered Mach port,
receive one ready message, and call `perform` in source order. The polling
bound is `Δt ≤ 10 ms` while an active-mode version-1 source is registered;
reply messages returned by `perform` are sent through the Mach message ABI and
then deallocated using the callback's allocator. A native multi-port wait set
remains unsupported.

For an ARM64 guest callback address `f` with up to eight integer/pointer
arguments `a`, the active interpreter bridge is
`InvokeA64(f,a) = RunGuest(PC=f, X[0..n)=a)`. The callback runner uses the
current guest execution seed and resumes the guest after the callback returns.
CoreFoundation Source0 schedule, cancel, and perform callouts use this route
when an ARM64 guest invoker is active; native-ABI guests retain direct calls.
The ARM64 end-to-end fixture exercises the selected host imports for string
creation, current-run-loop lookup, source creation/context retrieval,
add/remove, and release. This verifies only those signatures; it does not
establish general ARM64 CoreFoundation import coverage or guest callbacks
dispatched after their active execution context has ended.
The source/port registry enforces one source per port for each live source.
For an allocator `a` with context `c`, allocation is `A(a,n,h) = c.allocate(n,h,c.info)`;
deallocation is `D(a,p) = c.deallocate(p,c.info)`. When `a = null`, both
operations use Wintosh's guest heap. `CFAllocatorCreate` retains context info
once and releases it with the allocator; the per-thread default holds a CF
reference. Source-1 reply handling now sends the reply and applies `D(a,p)`.
Standard allocator constants are resolved as Mach-O data symbols; malloc-zone
currently shares system-heap behavior. `CFAllocatorCreate` allocates its own
object through its requested backing allocator and supports the context
callbacks when `kCFAllocatorUseContext` is selected. Consistent use of custom
allocators by every CF constructor remains outside the current implementation.
Guest `CFString` (CString/characters), `CFData`, `CFArray`, `CFDictionary`,
`CFSet`, `CFNumber`, `CFDate`, and `CFMachPort` object blocks use the allocator
passed to their constructor and retain it until `Release(object)`. Their
variable-length STL payloads still use the host heap, so full allocator
control of collection and byte-buffer storage is not yet true.
The ARM64 string imports now include CString/UTF-16 construction, shallow copy,
UTF-16 length and indexed/range reads, bounded CString and filesystem
representation conversion, and maximum encoded-size queries. For output
`(p,n)`, the bridge requires the full writable
interval `[p,p+n)` before calling the host-backed implementation; character
arrays use `2·length` bytes. The fixture round-trips the ASCII string “Darwin”
through UTF-8 bytes and UTF-16 code units and checks its six-code-unit length.
The ARM64 `CFData` bridge now maps immutable/mutable creation and copying,
length, range reads, append, replacement, and resizing. For byte sequence `D`
and range `[i,i+ℓ)`, replacement with `R` is
`D'=D[0:i]·R·D[i+ℓ:|D|]`; append is `D'=D·R`, and resize to `m` truncates or
zero-extends to length `m`. Every read/write span is checked before crossing
the guest/host boundary. The fixture validates the exact bytes after these
transitions and the independently copied result. Raw byte-pointer getters are
mapped to the ARM64 guest address space for the data object's live backing
vector. Before append/replace/resize, the old mapping is removed; afterward a
previously exposed pointer range is re-registered with its prior read/write
permission. Object release removes an active mapping. The fixture dereferences
both const and mutable byte pointers, writes through the mutable pointer, and
then observes the changed byte through `CFDataGetBytes`.
The ARM64 URL bridge now covers filesystem-path and byte-representation
construction, path-component appending, absolute copying, borrowed URL-string
access, filesystem-path copying, byte extraction, and filesystem-representation
output. For base path `P` and component `c`, appending is modeled as
`append(P,c,d)=P'` where separators are normalized and directory flag `d=1`
ensures a trailing `/`. Each variable-length input/output is range-checked in
guest memory. The fixture checks the exact byte string for
`/tmp/Wintosh.app/Contents/` and the NUL-terminated filesystem form.
For a CFMachPort wrapper `p`, source delivery is `Deliver(p,m) = Valid(p) ∧
Receive(GetPort(p),m)`, with the callback `Callout(p,m,size,info)` invoked once
for each received message. `Invalidate(p)` performs the state transition
`Valid: 1→0`, invalidates every registered source, invokes the optional
invalidation callback, releases retained context once, and destroys only a
port allocated by `CFMachPortCreate`; rights supplied by
`CFMachPortCreateWithPort` remain caller-owned. Wrappers are unique by Mach
port name. This models the API ownership boundary, while ABI callback details
and complete message/reply rights transfer remain fixture-unverified.
References: [Apple CFRunLoopSource](https://developer.apple.com/documentation/corefoundation/cfrunloopsource),
[CFRunLoopSourceContext1.getPort](https://developer.apple.com/documentation/corefoundation/cfrunloopsourcecontext1/getport),
[source context](https://developer.apple.com/documentation/corefoundation/cfrunloopsourcecontext),
[CFRunLoop mode contract](https://developer.apple.com/documentation/corefoundation/cfrunloop),
and [CFRunLoopPerformBlock](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock%28_%3A_%3A_%3A%29).

For a run loop `L` waiting until deadline `d` with next timer deadline `t`,
the host wait bound is `w = min(d,t)` when a timer exists, otherwise `w = d`.
For the AppKit main thread, `Wait(L,w)` is implemented by
`MsgWaitForMultipleObjectsEx` over the Win32 message queue; one dispatched
message is an input-source delivery. Thus
`RunInMode(L, m, d, returnAfterSource=true)` returns after the first delivered
message. The other threads wait on `L`'s condition variable, whose predicate
is `stopped ∨ pendingBlock(m) ∨ dueTimer(m)`. A stop transition sets
`stopped := true` and signals the condition variable; on the main loop it also
posts a thread message so that the Win32 wait becomes observable. This models
the AppKit main-thread queue; source-1 ports use 10 ms polling, without reply
messages or a native multi-port wait set. Arbitrary native handles remain
unsupported. The Win32 wait contract follows
[`MsgWaitForMultipleObjectsEx`](https://learn.microsoft.com/en-us/windows/desktop/api/Winuser/nf-winuser-msgwaitformultipleobjectsex),
while Apple describes input sources in its [CFRunLoop contract](https://developer.apple.com/documentation/corefoundation/cfrunloop).

The guest result mapping is `result(L) = 1` when the active mode has no
registered source/timer (or a non-main empty mode is queried), `2` after
`CFRunLoopStop`, `3` when the deadline expires, and `4` when
`returnAfterSourceHandled` is true and a source/block or main-thread input is
dispatched. `CFRunLoopRun()` repeats default-mode timed-out turns until the
loop is stopped or has no worker input source. The current result codes follow
Apple's [`CFRunLoopRunInMode` contract](https://developer.apple.com/documentation/corefoundation/cfrunloopruninmode%28_%3A_%3A_%3A%29);
guest-level ABI calls are build-verified but not fixture-verified.

The waiting predicate is `W(L,q) = waiting(L) ∧ running(L) ∧ (q ≠ owner(L))`:
it is true only while the host is blocked on the worker condition variable or
the AppKit thread's Win32 message queue, and only for a caller on another
thread. This preserves Apple's documented false result for querying the
current thread's run loop. See [CFRunLoopIsWaiting](https://developer.apple.com/documentation/corefoundation/cfrunloopiswaiting%28_%3A%29).

Mutation wake-up is `notify(L) = condition(L) ∨ (L = main ∧ postWin32(L))`.
Adding/removing a source or observer, adding a common mode, and scheduling a
block now post the AppKit wake message as well as notifying worker waiters.
This ensures the main loop reevaluates its mode after cross-thread changes.

For queued AppKit events, eligibility is `eligible(e,m) = m ∈ C(main)` where
`C(main)` is the main run loop's registered common-mode set. Retrieval in an
unregistered mode leaves the event queued; Win32-native input messages remain
host messages and are dispatched independently of this Cocoa event predicate.

For a timer with next fire time `t`, current absolute time `n`, and positive
interval `Δ`, a due repeating timer advances to
`t' = t + (floor((n - t) / Δ) + 1)Δ`. This retains the original cadence and
avoids a burst of callbacks after the host thread was delayed. A timer with
`Δ ≤ 0` fires once and becomes invalid. Timer callbacks run on the associated
run-loop thread; tolerance-based coalescing and wall-clock-change handling are
not modeled. See [Apple CFRunLoopTimerCreate](https://developer.apple.com/documentation/corefoundation/cfrunlooptimercreate%28_%3A_%3A_%3A_%3A_%3A_%3A_%3A%29)
and [CFRunLoopTimerSetNextFireDate](https://developer.apple.com/documentation/corefoundation/cfrunlooptimersetnextfiredate%28_%3A_%3A%29).

An observer `o` with activity mask `A(o)`, repeat flag `r(o)`, order
`ord(o)`, active mode `m`, and current phase `a` is eligible iff
`valid(o) ∧ registered(o,m) ∧ (A(o) ∧ a ≠ 0)`. Eligible observers run in
ascending `ord(o)`; if `r(o)=false`, the observer is invalidated after its
first matching callback, even when its mask contains multiple phases. The
current phase set is `{entry, beforeTimers, beforeSources, beforeWaiting,
afterWaiting, exit}`. Native event-source phases and remaining activity-mask
bits are not implemented. See [Apple CFRunLoopObserverCreate](https://developer.apple.com/documentation/corefoundation/cfrunloopobservercreate%28_%3A_%3A_%3A_%3A_%3A_%3A%29)
and [CFRunLoopActivity](https://developer.apple.com/documentation/corefoundation/cfrunloopactivity).

Run-loop mode introspection is represented as a snapshot. For a loop `L`,
`CopyCurrentMode(L) = null` when its activation stack is empty and otherwise
returns a newly owned string for the top mode. `CopyAllModes(L)` returns a
newly owned array over the deduplicated union of common modes, active modes,
and modes referenced by queued blocks or registered sources, timers, and
observers. The common-mode pseudo-name itself is excluded because it is a
registration set, not an executable mode. See Apple's
[`CFRunLoopCopyCurrentMode`](https://developer.apple.com/documentation/corefoundation/cfrunloopcopycurrentmode%28_%3A%29)
and [`CFRunLoopCopyAllModes`](https://developer.apple.com/documentation/corefoundation/cfrunloopcopyallmodes%28_%3A%29) contracts.
The run-loop references embed the generic CF header, but their ownership stays
with the runtime: `retain(L) = L` and `release(L)` is a no-op. This lets generic
`CFRetain`, `CFRelease`, and `CFGetTypeID` operate without converting the
thread-local or main-loop handle into a caller-owned allocation.

## CoreFoundation scalar bridge

For a numeric guest value `x` requested in target type `T`, conversion is
defined by `Q_T(x) = trunc(x)` for integer targets and `Q_T(x) = round_T(x)`
for floating targets. An integer conversion succeeds only when `x` is finite
and `min(T) ≤ Q_T(x) ≤ max(T)`; the output buffer is unchanged on failure.
The implementation's matrix maps Darwin number tags to host storage as follows:

| Darwin tags | Input/output width | Canonical host storage | Status |
|---|---:|---|---|
| SInt8 / Char | 8-bit signed | signed 64-bit | Implemented |
| SInt16 / Short | 16-bit signed | signed 64-bit | Implemented |
| SInt32 / Int | 32-bit signed | signed 64-bit | Implemented |
| SInt64 / Long / LongLong / CFIndex / NSInteger | 64-bit signed | signed 64-bit | Implemented; ARM64 import-tested |
| Float32 / Float | 32-bit float | double | Implemented |
| Float64 / Double / CGFloat | 64-bit float | double | Implemented; ARM64 import-tested |

This bridge keeps values usable by property-list and collection code but does
not claim CFNumber's exact internal-type preservation or all architecture and
overflow corner cases. `CFDate` stores Darwin absolute time as a double and
compares in reference-date seconds. These contracts follow [Apple CFNumber](https://developer.apple.com/documentation/corefoundation/cfnumber),
[CFDate](https://developer.apple.com/documentation/corefoundation/cfdatecreate%28_%3A_%3A%29),
and [CFDictionary](https://developer.apple.com/documentation/corefoundation/cfdictionary).

The ARM64 import adapter now routes `CFNumberCreate`, `CFNumberGetValue`,
`CFNumberIsFloatType`, and `CFNumberCompare`. Before scalar reads or writes it
maps the Darwin tag to width `w(T)∈{1,2,4,8}` and requires a guest range
`[p,p+w(T))` with the appropriate read/write permission. The guest fixture
checks signed-integer-to-64-bit extraction, integer/real classification, and
cross-representation ordering. Numeric values remain canonicalized to signed
64-bit integers or doubles in the host object model.

The ARM64 `CFDate` bridge reads `CFDateCreate`'s `double` argument from `v0`
and returns absolute-time and interval results through `v0`; object handles and
comparison results use `x0`. Dates are scalar points `t∈ℝ` on Darwin's
reference-time axis. `compare(t₁,t₂)=sign(t₁-t₂)` and
`interval(t₁,t₂)=t₁-t₂`; the ARM64 fixture checks both identities with distinct
fractional values so accidental integer-register transport is observable.

For dictionary collection semantics the supported callback matrix is explicit:

| Key callbacks | Value callbacks | Wintosh behavior |
|---|---|---|
| Any version-0 key/value tables | Any version-0 key/value tables | Tables are copied; null pointers become zeroed tables |
| Key retain/release/equal callbacks | Value retain/release callbacks | Invoked on create/copy/mutate/destroy and key lookup; null fields mean pointer equality or no ownership callback |
| Hash/description callbacks | Description callback | Stored in the per-dictionary table; hash callback is not used by the current vector-backed lookup |

The key invariant is `∀(kᵢ,vᵢ),(kⱼ,vⱼ) ∈ D, i≠j ⇒ ¬equal(kᵢ,kⱼ)`.
The vector implementation enforces that invariant with linear scans, `O(n)`
per lookup or mutation; a hash callback is preserved but not yet used to meet
CoreFoundation's expected hash-table performance.

Mutable operations are enabled only for dictionaries created by
`CFDictionaryCreateMutable` or `CFDictionaryCreateMutableCopy`; mutation calls
on immutable guest objects are ignored. `CFDictionaryApplyFunction` adapts the
guest callback synchronously and holds the internal snapshot through the pass.
The ARM64 import bridge now dispatches mutable construction, lookup,
contains-key, set/add/replace/remove, and remove-all calls. If the guest state is
`D=(K,V)`, insertion is `D' = D ∪ {(k,v)}` when `k` is new; otherwise the
equal key's associated value is replaced. Removal is `D' = D \ {(k,v)}` for
the pair whose key compares equal. Every transition is routed through the
guest callback table, so ownership counts satisfy
`retains - releases = live stored representatives` per collection.
Dictionary copy and mutable-copy imports now clone the entry state while
preserving the callback tables: `D₁=copy(D₀)` initially has the same key/value
mapping, but `mutate(D₀)` leaves `D₁` unchanged. `GetValueIfPresent` writes its
out-parameter only after finding the key; `GetKeysAndValues` writes exactly
`|D|` key slots and `|D|` value slots, after the ARM64 bridge validates both
guest output spans.

The ARM64 set bridge covers mutable construction, membership, add/set/replace,
remove, and remove-all. Its state transition is `S'=S∪{x}` for add,
`S'=(S\{y | y~x})∪{x}` for set, and `S'=S\{y | y~x}` for remove, where `~`
is callback equality. The ARM64 guest fixture checks duplicate-add idempotence,
lookup, replacement, emptying, and collection release. These import paths do
not imply full CoreFoundation coverage: allocator semantics, all remaining
collection APIs, and broad application compatibility are still separate work.
ARM64 set copy and mutable-copy operations preserve the callback table and
create independent membership state. For `S₁=copy(S₀)`, later mutation of
`S₀` does not change `S₁`; `GetValues` and `GetValueIfPresent` provide bounded
guest-memory exports of the current set representatives.

`CFSet` uses the set invariant `∀x,y ∈ S: x ≠ y` under the set's equality
callback (pointer identity when it is null). Add is
idempotent, Set replaces an equal member's stored representative, and Replace
has no effect when no equal member exists. The exposed set algebra is
`Union(S,T)=S∪T`, `Intersect(S,T)=S∩T`, and `Subtract(S,T)=S\T`; each mutating
operation requires a mutable set. Wintosh indexes each value in a bucket by
its callback hash and checks equality only inside that bucket. Membership and
insertion are expected `O(1)` with well-distributed hashes and degrade to
`O(n)` under collisions. Callback tables are copied per set, and
retain/release/equality callbacks are applied on insertion, removal, lookup,
copy, and destruction; a null table has no retain/release and pointer equality.
Hash and description callbacks are preserved; hash callbacks determine the
bucket index used for lookup and mutation.
Formally, `Bₕ={x∈S | hash(x)=h}` and `find(x)` searches only
`{y∈B_hash(x) | equal(y,x)}`. The callback contract therefore requires equal
values to produce equal hashes, as in Apple's [CFSet callback structure](https://developer.apple.com/documentation/corefoundation/cfsetcallbacks).
Null elements are rejected. These semantics
follow [Apple CFSet](https://developer.apple.com/documentation/corefoundation/cfset),
[CFSetCallBacks](https://developer.apple.com/documentation/corefoundation/cfsetcallbacks),
and [CFSetCreateMutable](https://developer.apple.com/documentation/corefoundation/cfsetcreatemutable%28_%3A_%3A_%29).

`CFArray` is modeled as an ordered sequence `A=(a₀,…,aₙ₋₁)`, so duplicate
values and order are preserved. Replacing range `[i,i+ℓ)` by sequence `B`
produces `A'=(a₀,…,aᵢ₋₁)·B·(aᵢ₊ℓ,…,aₙ₋₁)`. Append and insert are the special
cases `i=n, ℓ=0` and `0≤i≤n, ℓ=0`; setting index `i` uses `ℓ=1, |B|=1`.
Range append is `A'=A·B` for a selected source subsequence `B`; index exchange
is the permutation `A'[i]=A[j]`, `A'[j]=A[i]` with all other positions fixed.
Comparator sorting orders a selected interval by `cmp(aᵢ,aⱼ)<0`. Binary search
on an already sorted interval returns an equal element's index, or the lower
bound `min{i | cmp(aᵢ,x)≥0}` as the insertion position. Both use the guest
comparator and context; search takes `O(log n)` comparisons. See Apple's
[CFArraySortValues](https://developer.apple.com/documentation/corefoundation/cfarraysortvalues%28_%3A_%3A_%3A_%3A%29)
and [CFArrayBSearchValues](https://developer.apple.com/documentation/corefoundation/cfarraybsearchvalues%28_%3A_%3A_%3A_%3A_%3A%29).
Only arrays constructed as mutable accept these transformations. Each array
stores its own copied callback structure: retain maps an inserted value to its
stored representative, release runs when that representative leaves the
collection, and equality defines membership/search. A null callback pointer is
the zeroed version-0 table (no retain/release and pointer equality). See Apple's
[CFArrayCreateMutable](https://developer.apple.com/documentation/corefoundation/cfarraycreatemutable%28_%3A_%3A_%29)
and [`kCFTypeArrayCallBacks`](https://developer.apple.com/documentation/corefoundation/kcftypearraycallbacks).

The ARM64 import layer now exposes the array operations needed to construct and
mutate a guest-owned sequence: `CFArrayCreateMutable`, `CFArrayGetValueAtIndex`,
`CFArrayGetValues`, `CFArrayContainsValue`, append, insert, set, remove-at-index,
and remove-all. The bridge represents a `CFRange` as `(location,length)` in the
Darwin ARM64 argument registers and validates bulk output spans before writing
guest memory. For a valid sequence `A` with `n` elements, insertion at `i`
obeys `0≤i≤n` and `A'=A[0:i]·[x]·A[i:n]`; removal obeys `0≤i<n` and
`A'=A[0:i]·A[i+1:n]`; replacing index `i` preserves `n`. The ARM64 fixture
checks these transforms, range membership and extraction, and the ownership
conservation equation `retains - releases = |A|` after destruction. For range
replacement of `[i,i+ℓ)` by `R`, the resulting state is
`A'=A[0:i]·R·A[i+ℓ:n]`; append-array is the special case `i=n,ℓ=0` with `R`
selected from the source array. Both operations and the count/first/last index
queries now have ARM64 import adapters and fixture coverage. Copy APIs are
bridged too. Comparator-based sort/search and Array, Dictionary, and Set
enumeration now route guest function pointers through the active ARM64
interpreter: `apply(x)` maps each selected element to the callback, while sort
uses `cmp(aᵢ,aⱼ)` and binary search uses at most `⌈log₂(n+1)⌉` comparator
probes plus the equality probe. The fixture checks ascending order, exact-match
index, enumeration cardinality, and accumulated callback state for all three
collection types. Array exchange and the remaining array surface still need
ARM64 coverage.

## ARM64 CoreFoundation run-loop callbacks

The AArch64 import bridge covers the CoreFoundation run-loop operations used
to register and exercise guest source, timer, and observer callbacks. Guest
callbacks run by entering the active AArch64 interpreter with an event-specific
argument state; a guest virtual address is never called as a Windows function
pointer. The callback runner restores the suspended import state on return.

The callback argument vectors are `(source, info)` for a source, `(timer,
info)` for a timer, and `(observer, activity, info)` for an observer. ARM64
general-purpose and floating-point argument registers are allocated
independently. Thus `CFRunLoopTimerCreate(allocator, fireDate, interval,
flags, order, callout, context)` receives `fireDate` and `interval` in `v0` and
`v1`; the remaining arguments occupy `x0` through `x4`. `CFRunLoopRunInMode`
receives its `seconds` value in `v0`, and its mode and boolean flag in `x0` and
`x1`. Observer creation uses `x0` through `x5`. These mappings follow the
prototype types and AArch64 calling convention, rather than assigning a
register from the argument's ordinal position.

`CFRunLoopPerformBlock` retains the guest block and queues its invocation for
the requested mode. On ARM64 the Blocks ABI supplies the block literal in
`x0`; the active Objective-C guest method invoker resumes its invoke function
in the interpreter. The ARM64 fixture verifies a global block is performed
once during the selected mode alongside timer and observer delivery.

The focused ARM64 fixtures cover timer and observer creation, registration,
invalidation/removal, mode execution, perform-block ownership, and synchronous
guest callback dispatch. They do not establish full Darwin run-loop
compatibility: timer rescheduling and tolerance, nested or concurrent run
loops, every callback-context ownership case, and AppKit event delivery remain
separate work.

Membership queries use the same three-register map for source, timer, and
observer forms: `x0=loop`, `x1=object`, `x2=mode`; the one-byte Darwin result is
returned through `x0`. Fixtures assert membership transitions
`0 → 1 → 0` around add/remove for all three registration kinds. This verifies
the ABI adapter and the mode-specific membership predicate together.

Timer date and tolerance setters read their `double` value from `v0`; their
getters, the interval getter, and `CFRunLoopGetNextTimerFireDate` return the
double in `v0`. The fixture writes distinct fractional values and reads them
back, so the test checks the FP argument and result paths instead of only the
integer register bridge. Timer creation continues to map `fireDate`/`interval`
to `v0`/`v1` while `flags`, `order`, `callout`, and `context` use `x1`–`x4`.

Timer and observer scalar accessors use `x0` for their object and return
validity/repeat bytes, activity masks, or signed order through `x0`. Guest
fixtures verify initial properties and the one-shot lifecycle transition:
both timer and non-repeating observer become invalid after their callback.

Run-loop control imports use the Darwin register contract: current/main loop
getters take no arguments; mode-copy imports take the loop in `x0`; stop, wake,
and waiting queries take the loop in `x0`; common-mode registration takes the
loop and mode in `x0`/`x1`. The ARM64 fixture covers main/current lookup, wake,
waiting-state query, adding a common mode, copying all modes and reading its
array count. It also queries the active mode from inside a guest observer
callback, covering a nested ARM64 import while the interpreter is re-entered.

Timer and observer context getters take the object in `x0` and a writable
Darwin context record in `x1`. The adapter checks the complete 40-byte guest
range before writing it; the fixture verifies the returned version and info
pointer for both records, complementing the existing source-context coverage.

Source signal/invalidate operations take the source in `x0`; source validity
and order return in `x0`. The ARM64 source fixture now checks the source's
validity/order, signals it, runs the selected mode until the perform callback
has executed, removes it, and invalidates it. This follows the lifecycle
`valid ∧ registered ∧ signaled → perform → ¬registered → invalid` and checks
the bridge across multiple imports, not only one isolated call.

Source creation, context retrieval, and add/remove now use the same ARM64
CoreFoundation import dispatcher as timer and observer operations. Context
input/output ranges are checked there before host code reads or writes guest
memory, so nested guest callbacks and top-level imports follow one signature
mapping.

The ARM64 CFMachPort adapters now cover create, context/port/validity queries,
invalidation callback access, invalidation, and creation of a Source-1 run-loop
source. Guest invalidation callbacks receive `(port, info)` through the active
interpreter callback bridge. Port invalidation transitions the wrapper to
invalid and invalidates its registered run-loop sources. The fixture verifies
that lifecycle and callback path. It also sends a message to the port with
`mach_msg`, runs the active mode, and asserts that the guest message callout
receives exactly one message with the original context. The x64-host ARM64
import adapter validates guest message ranges and forwards `mach_msg` using
Darwin's seven-register argument map; it is also available to nested guest
callbacks. Before callback execution, the host-owned receive buffer is
temporarily registered as a readable/writable guest mapping and removed after
the callback returns, allowing the guest to inspect the actual message bytes.
This proves the current local send → queued receive → Source-1 perform → guest
callout path. Message-ID and payload preservation are asserted by the fixture;
broker-backed cross-process delivery remains a separate acceptance case.

Context retain/release callbacks for run-loop sources, timers, observers,
allocators, and CFMachPorts now share the callback dispatch rule
`dispatch(f) = guest(f)` when `f` lies in executable guest memory, and
`dispatch(f) = host(f)` otherwise. This address predicate prevents internal
host callbacks from being misinterpreted as guest instructions. A retain
callback returns its retained pointer in AArch64 `x0`; the bridge carries that
result back to CoreFoundation, while a release callback is void. If no active
guest dispatcher claims an address, the native host callback remains the
fallback. The ARM64 timer/observer and Mach-port fixtures assert the resulting
retain/release counts and retained pointer lifecycle. This establishes only
these context callbacks. Collection retain/release/equality callbacks and Set
hash callbacks use a typed register-vector bridge as well: callback arguments
map positionally to `x0…x(n−1)`, pointer/integer results return in `x0`, and
non-guest function addresses use the native fallback. The ARM64 fixture creates
an array with guest retain/release callbacks and checks `retain_count = 2`,
`release_count = 2` across create/count/destroy. Dictionary/Set retain, release,
equality, and hash callbacks also have ARM64 fixtures; copy-description callbacks
are now exercised through `CFCopyDescription`; other callback-bearing
CoreFoundation APIs remain to be audited.

The same ARM64 import dispatcher now accepts `CFDictionaryCreate` and
`CFSetCreate` after checking their pointer-vector and callback-table guest
ranges. For a two-key dictionary, insertion compares each new key with the
existing key, so the fixture verifies `key_equal_calls = 1`, two key retains,
two value retains, and matching release counts at destruction. The Set fixture
forces one hash collision and checks `hash_calls = 2`, `equal_calls = 1`, and
two retain/release pairs. The count imports return through `x0`. These cases
exercise both the argument vector and callback result vector for guest
collections; mutable replacement/removal remain uncovered. `CFCopyDescription`
now formats arrays, dictionaries, and sets from
the callback-produced CFStrings, returns a new CFString, and releases each
temporary callback result after conversion. The fixture checks two description
calls for array elements, two each for dictionary keys and values, and two for
set members. Set output is sorted by rendered element text for deterministic
results.
