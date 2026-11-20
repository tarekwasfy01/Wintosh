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
`R` and `L` dispositions on local queues. Voucher behavior is unsupported, and
complex body port descriptors have a separate, partial transfer path.
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
header rights. It rejects
voucher dispositions and brokered move or reply transfers because those routes
do not yet materialize target-session rights. The local transition tracks
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
For the architecture boundary inspected on 2026-11-17, `1` means the source
contains that stage, `0` means it explicitly rejects the stage, and `?` means
the stage is not demonstrated on that host:

| Guest Mach-O architecture / stage | Windows x64 | Windows ARM64 |
|---|---:|---:|
| x86_64 parse, map, relocation subset | 1 | 1 |
| x86_64 real entry execution | 1* | 0 |
| i386 parse, map, relocation subset | 1 | 1 |
| i386 execution | 0 | 0 |
| arm64 parse, map, relocation subset | 1 | 1 |
| arm64 execution | 0 | ? |

`1*` on x64 is limited to Clang builds with the SysV entry bridge. The ARM64
source path now admits matching-architecture ARM64 code and includes a
Darwin-start transfer, but execution remains `?` because this environment has
not produced an ARM64 runner or executed an ARM64 guest. All 32 host C++ source
files and the ARM64 trampoline compile and archive as 33 ARM64 COFF members;
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
convention. A compiler probe confirmed the differing layouts. The ARM64 Mach-O
symbol resolver now routes guest `objc_msgSend` imports to a small assembly
entry that passes the guest stack pointer to a signature-aware C++ reader. The
reader consumes only the slots required by the registered method encoding,
then uses the existing typed dispatcher. Native Windows callers still resolve
to the original Windows-varargs wrapper. This keeps the two call conventions
separate and avoids reading past the guest's supplied arguments.

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
| CF sources/timers/observers | Register, invalidate, retain, and service by mode | Not implemented | Not supplied by WSL1 | Missing |

References: [Apple NSRunLoop `run(mode:before:)`](https://developer.apple.com/documentation/foundation/runloop/run%28mode%3Abefore%3A%29),
[CFRunLoop mode contract](https://developer.apple.com/documentation/corefoundation/cfrunloop),
and [CFRunLoopPerformBlock](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock%28_%3A_%3A_%3A%29). This is a callback-queue contract, not yet the full run-loop object graph.
