# Wintosh current status

## Reading this status

This file records implemented behavior and known limits; it is not a claim of
complete macOS compatibility. “Implemented” means host-side code exists;
validation still requires the guest fixture or application evidence stated in
each section. Begin with the [repository README](../README.md) for build
entry points and the [documentation index](DOCUMENTATION-INDEX.md) for the full
map.

Status date: 2026-11-23

### NSView affine coordinate transforms and rotation (2026-11-23)

NSView coordinate chains now compose full affine matrices, including bounds
scaling/flips and the `frameRotation`/`boundsRotation` properties in degrees.
Point, size, and rectangle conversions, view drawing CTMs, dirty-rectangle
mapping, native-control placement, scroll visibility, and scrollbar placement
use the same transforms. Mouse and wheel hit-testing now inversely maps window
locations into each candidate view's bounds, so rotated views receive input at
their rendered position. Native Win32 controls remain unrotated, but their
`SetWindowRgn` clipping now intersects polygon regions for all ancestor clip
views, including rotated ones. `visibleRect` and scroll targeting still use
axis-aligned bounds for rotated clip views. The x64 executable and ARM64 host
library compile; no guest Mach-O fixture has exercised rotated view behavior
or input delivery.

### NSView drawing and child clipping (2026-11-23)

`wantsDefaultClipping` now defaults to true and clips only the view's own
`drawRect:` call to its bounds. `clipsToBounds` controls whether child views
are clipped and defaults to false, matching current AppKit behavior; `NSClipView`
continues to clip its children regardless. Native child controls use matching
Win32 regions, including polygon clips for rotated clipping ancestors. This
follows Apple's [`wantsDefaultClipping`](https://developer.apple.com/documentation/appkit/nsview/wantsdefaultclipping)
and [`clipsToBounds`](https://developer.apple.com/documentation/appkit/nsview/clipstobounds)
semantics. The legacy pre-macOS-14 `clipsToBounds` default is not selected
dynamically. The x64 executable and ARM64 host library compile; no guest
drawing fixture has verified out-of-bounds drawing or clipping.

`needsToDrawRect:` now intersects its argument with the dirty rectangle for the
view whose `drawRect:` is currently running; it returns false outside that
view's drawing callback. This supports AppKit's usual in-draw culling query
([Apple documentation](https://developer.apple.com/documentation/appkit/nsview/needstodraw%28_%3A%29)).

### AppKit NSAffineTransform subset (2026-11-23)

`NSAffineTransform` now creates an identity matrix and supports ordered
append/prepend composition, translation, uniform and per-axis scaling, rotation
by degrees or radians, inversion for nonsingular matrices, identity queries,
and affine transformation of `CGPoint`, `CGSize`, and `CGRect` values. A
rectangle result is the axis-aligned bounds of its four transformed corners.
Matrix state is held per Objective-C object and removed by its runtime
finalizer. Point and size values use explicit x86_64 guest ABI paths;
`setTransformStruct:` reads the six-double structure from the guest stack, and
`transformStruct` returns it through stret. `initWithTransform:` copies the
matrix. The local Clang Release build compiles and links this implementation;
no Mach-O guest fixture verifies selector dispatch or numeric results. `set`
replaces the active graphics-context CTM and `concat` composes the matrix with
it. `transformBezierPath:` creates a transformed path, and
`NSBezierPath -transformUsingAffineTransform:` mutates an existing path;
rectangles and ovals expand to line/cubic elements before transformation.
Platform-specific
singular-matrix exceptions remain unimplemented.

### Foundation NSURL and relative CFURL integration (2026-11-21)

The AppKit `NSURL` host object now retains the CoreFoundation URL that backs
it, instead of storing only a file path. It implements `initWithString:`,
`initWithString:relativeToURL:`, `relativeString`, `baseURL`, and `absoluteURL`,
plus the corresponding `URLWithString:` class constructors. File URLs are
created from filesystem paths with URL escaping. File URL construction and
path-component appending include the `isDirectory` variants and preserve
directory trailing slashes. `initFileURLWithPath:relativeToURL:` and
`fileURLWithPath:relativeToURL:` resolve encoded relative filesystem paths
against a file URL base while preserving the relative URL form. Path-component
and extension copy operations now
call the CFURL adapters, so they keep URL schemes and query/fragment suffixes
and preserve a relative URL's base when supported by the CFURL copy operation.
`path`, `lastPathComponent`, and `pathExtension` read
from the stored CFURL for URLs created from strings. Drive-letter file URLs
are translated back to Windows drive paths for host filesystem use.

The local Visual Studio 2026 Release `wintosh` target compiles and links after
these changes. This is build evidence only: no guest Mach-O exercised the new
Objective-C selectors. URL parsing and RFC resolution remain partial; security-
scoped access, resource values, bookmark data, and complete macOS URL behavior
are not implemented.

### Foundation run-loop modes and NSRunLoop bridge (2026-11-21)

The CoreFoundation callback queue now stores requested run-loop modes and
supports a per-loop common-mode set (with `kCFRunLoopDefaultMode` included by
default). Mode-specific turns leave callbacks for other modes queued; callbacks
scheduled for multiple modes run once in the first eligible mode. Ordered
`NSRunLoop` selector requests run in increasing `order` and are queued on the
target thread's loop. `NSRunLoop` now exposes `currentRunLoop`, `mainRunLoop`,
`getCFRunLoop`, `defaultMode`, `commonModes`, `run`, `runUntilDate:`,
`runMode:beforeDate:`, and `acceptInputForMode:beforeDate:`. The AppKit main
loop pumps default-mode callbacks and is woken when a selector is queued.

Mach-O symbol resolution exposes a partial public CoreFoundation C ABI:
run-loop current/main/run/stop/wakeup/common-mode operations, plus
`CFStringGetTypeID`, `CFStringCreateWithCString`, `CFStringGetLength`,
`CFStringGetCString`, UTF-16 character access, filesystem-representation
queries, and `CFArray` type/count/index/range/contains reads. String conversions
are limited to the runtime's UTF-8 representation and do not cover all Darwin
encodings. `CFRunLoopPerformBlock` accepts a string or array of modes, retains
and queues the guest block, and wakes the AppKit main thread. The resolver now
also exports `CFRunLoopGetTypeID`, `CFRunLoopCopyCurrentMode`, and
`CFRunLoopCopyAllModes`. The mode-copy calls follow Create/Copy ownership;
run loops share the generic CF object header and are immortal handles, so
`CFRetain`, `CFRelease`, and `CFGetTypeID` safely accept them.
Defined modes are collected from common modes, active modes, and registered
blocks, sources, timers, and observers. Mode names are currently bounded to
4095 UTF-8 bytes. Guest import fixtures and edge-case status behavior still
need coverage. Guest exports now
also include CFNumber creation/conversion/comparison, CFDate creation/comparison,
CFBoolean reads, and CFDictionary creation, copying, lookup, mutation and
enumeration with copied key/value callback tables. CFSet creation, copying,
membership/value queries, enumeration, set algebra and mutation are exposed
with copied callback tables, including `kCFTypeSetCallBacks`. CFSet uses
hash buckets for expected constant-time lookup. CFArray now also has creation,
mutable copies,
append/insert/remove/replace/set, remove-all, and synchronous callback
enumeration through `kCFTypeArrayCallBacks`, plus range queries, append-array,
and index exchange. Mutable operations are guarded by
the mutability of the guest object. Callback structures and the allocator are
stored per array; NULL means a version-0 table with null fields, and custom
retain/release/equal
callbacks are invoked for storage, destruction, and lookup. Other callback
fields are retained in the copied table for ABI fidelity.
Comparator-based range sorting and binary search are also exported; binary
search assumes the searched range is already sorted by the same ordering.

Version-0 `CFRunLoopSource` objects support create/context/order queries, mode
registration/removal, signal delivery, validity, and invalidation. Version-1
sources can register a Mach port through `getPort`; the active loop polls it on
10 ms slices, receives one available message, and invokes `perform` in source
order. Returned replies are sent through the Mach message ABI and deallocated
through the allocator passed to `perform`, including context-backed custom
allocators created by this adapter. Native multi-port waiting remains open.
One source per registered port is enforced. Guest
`CFRunLoopTimer` creation, context/order/interval/fire-date queries, repeat
semantics, next-fire-date mutation, mode registration/removal, and invalidation
are also wired into the run loop. Due timers fire on the active loop thread;
repeating timers advance on their scheduled cadence and skip missed intervals.
`CFRunLoopObserver` creation, activity/context/order queries, mode
registration/removal, validity, and invalidation are also exported. Callbacks
run at entry, before timers/sources, before/after waiting, and exit; one-shot
observers invalidate after their first matching activity. The Windows x64
Clang Release build compiles and links these APIs. Guest ABI behavior remains
unverified. While waiting on the AppKit main thread, CFRunLoop now waits on
and dispatches the Win32 message queue; non-main loops retain the condition-
variable wait. A dispatched message can end a `returnAfterSource` turn, and a
cross-thread stop wakes the main queue. The guest `CFRunLoopRunInMode` maps
outcomes to CoreFoundation's `Finished=1`, `Stopped=2`, `TimedOut=3`, and
`HandledSource=4`; `CFRunLoopRun` is exported and returns for an empty worker
mode or a stop. Mach source-1 reply messages, unique-port checks, a native
multi-port wait set, arbitrary Windows-handle registration, timer tolerance,
the remaining observer phases, recursive activation
semantics, and guest ABI fixture verification remain open.
`CFRunLoopIsWaiting` is exported and reports a blocked loop only to a caller
on another thread, matching Apple's contract; this has build evidence only.
Cross-thread block, source, common-mode, and observer changes also post a
Win32 wake message when targeting the AppKit main loop, so its native wait
reevaluates changed scheduler state.

`CFMachPort` now wraps an existing receive right or allocates a receive/send
right pair, retains its context, exposes port/context/validity and invalidation
callbacks, and creates a version-1 run-loop source that dispatches received
messages to the CFMachPort callout. Invalidating the wrapper invalidates its
registered sources and releases context info once. Wrappers are uniqued by
port name. Guest callback ABI behavior and message/reply ownership still need
a Mach-O fixture. Existing rights passed to `CFMachPortCreateWithPort` remain
caller-owned; only ports created by `CFMachPortCreate` are destroyed with the
wrapper.

The `CFAllocator` subset now supports custom context creation, context reads,
allocation, reallocation, deallocation, preferred-size queries, and a retained
per-thread default allocator. Default allocations use the guest heap. The
default, system-default, malloc, malloc-zone, null, and use-context constants
are exported. `CFAllocatorCreate` allocates its own object through the
requested backing allocator; `kCFAllocatorUseContext` uses the supplied
context's allocate and deallocate callbacks. Distinct malloc-zone behavior
remains outside this subset. Guest `CFString` (CString/characters and
copy/mutable-copy/mutable constructors), `CFData`,
`CFArray`, `CFDictionary`, `CFSet`, `CFNumber`, `CFDate`, and `CFMachPort`
constructors now allocate their object blocks through the requested allocator
and retain that allocator until destruction. Container, string, and data
payload buffers still use the host C++ allocator; other CF constructors remain
to be converted. Guest `CFURLCreateWithString`, `CFURLCreateWithFileSystemPath`,
and `CFURLCreateFromFileSystemRepresentation` now follow the same object
allocator rule. The string and byte relative-to-base constructors resolve
relative paths against a base URL. `CFURLCreateWithBytes` accepts UTF-8 URL
bytes and an optional base URL; other encodings are unsupported.
`CFURLCreateAbsoluteURLWithBytes` also routes through that resolution path;
compatibility mode preserves the base URL's final path component for query-,
parameter-, and fragment-only references. Compatibility-mode handling for the
remaining alternate RFC resolution rules is still partial.
`CFURLCanBeDecomposed` validates scheme syntax
and percent escapes for the supported URL representation. The guest ABI also
resolves common CFURL type, string, path,
scheme, authority, port, directory, absolute-URL, and filesystem-representation
queries. `CFURLGetString` caches its borrowed string until URL destruction.
Relative guest URLs created with a base now retain that base and their original
relative string. `CFURLGetBaseURL` returns the borrowed base, while
`CFURLGetString`, `CFURLGetBytes`, and component ranges use the relative string;
path and filesystem operations continue using the resolved host string.
`CFURLCopyAbsoluteURL` returns a flattened URL without a base. Relative query,
fragment, and parameter references now resolve against the corresponding base
URL resource, and absolute-path references retain the base scheme and authority.
`CFURLGetBytes` returns the UTF-8 bytes backing the URL, reports the required
length for a null buffer, and returns `-1` without writing when the supplied
buffer is too small.
`CFURLGetByteRangeForComponent` reports byte offsets for scheme, authority,
path, resource specifier, user info, host, port, parameters, query, and fragment,
including optional ranges with adjacent separators. Offsets refer to the same
UTF-8 byte sequence returned by `CFURLGetBytes`; malformed and unusual
authority forms remain outside the supported subset.
`CFURLCreateCopyAppendingPathComponent` appends a UTF-8 component before any
query or fragment, escapes component delimiters, preserves the resource suffix,
and adds the trailing slash when `isDirectory` is true. Relative append/delete
copies retain their base and independently transform the relative string.
`CFURLCreateCopyAppendingPathExtension` now adds an escaped UTF-8 extension to
the final non-directory path component, before parameters, query, and fragment;
URLs with an empty or directory path are copied unchanged.
The guest ABI now also exports `CFURLCreateCopyDeletingLastPathComponent` and
`CFURLCreateCopyDeletingPathExtension`; both preserve the applicable URL
resource suffix and allocate the copied URL through the supplied allocator.
Query and fragment copies honor `charactersToLeaveEscaped`: null preserves
percent escapes, while a string selectively preserves escapes for matching
decoded bytes. Username and password copies are also exported.
Hierarchical URL path extraction now separates the authority and resource
suffix before returning the path; strict-path queries report whether that path
is absolute. Parameter strings are extracted from the final path segment.
`CFURLCreateStringByReplacingPercentEscapes` uses the requested allocator,
returns a retained input when no escape is replaced, and rejects replacement
results that are not well-formed UTF-8. Its adding-escapes counterpart now
escapes non-URL UTF-8 bytes and caller-forced legal URL characters, while
preserving caller-allowed characters. The explicit-encoding replacement
variant delegates to the UTF-8 implementation and returns null for any other
encoding.
URL path-style conversion is limited to POSIX and Windows styles; HFS paths
remain unsupported. Source-1 reply memory uses this deallocator path.

### Foundation dates and AppKit event queue (2026-11-17)

`NSDate` now provides current, Unix-epoch, reference-date, distant-past/future,
and date-relative construction, comparison, and interval queries. Unix seconds
convert to the Cocoa reference epoch using 978307200 seconds. AppKit now
supports `NSApplication.currentEvent`, event polling with
`nextEventMatchingMask:untilDate:inMode:dequeue:`, `postEvent:atStart:`, and
`discardEventsMatchingMask:beforeEvent:`. Mouse, keyboard, and wheel events can
be queued or sent directly; `NSApplication.run` drains posted events and a
thread message wakes it when another thread posts. Deadline waits continue
across Windows' bounded wait intervals. Run-loop mode filtering is not yet
complete: queued `NSEvent`s are delivered only when the requested mode is in
the main loop's common-mode set, which starts with default, event-tracking,
and modal-panel modes; raw Win32 message dispatch remains shared across modes.
Clang and MSVC Release builds link; no guest fixture has exercised
event ordering, queue ownership, or deadline behavior.

### Darwin sysctl guest identity and CPU topology (2026-11-17)

Name-based `hw.optional.*` queries now expose host-supported x86_64 SSE2, SSE3,
AVX2, AVX-512F and ARM64 NEON, ARMv8.1 atomics, CRC32, and ARM crypto feature
flags. Windows provides an aggregate ARMv8 crypto capability, so the AES/SHA1/
SHA256 names are aliases for that aggregate rather than independent detection.
Features belonging to another guest architecture are not registered. This
improves optimized-code-path selection, but runtime behavior still needs a
Darwin guest fixture and the full XNU feature catalog remains incomplete.

Fixed `hw.machine`/`hw.cputype` so Windows ARM64 reports ARM64 rather than
being collapsed into x86_64 by `_WIN64`. Added `hw.cpusubtype`, logical and
physical active/maximum processor counts, and `CTL_KERN` selectors for
`kern.ostype`, `kern.osrelease`, `kern.version`, and `kern.osversion`. The
`sysctlbyname` name table also now exposes `hw.byteorder`, `hw.pagesize`,
`hw.cachelinesize`, `hw.machine_arch`, and `kern.version`, keeping these queries aligned with the
existing numeric `CTL_HW` MIB path. The MIB selectors follow Apple's public
XNU definitions (`CTL_HW=6`, `HW_NCPU=3`, `HW_CACHELINE=16`,
`HW_AVAILCPU=25`, `HW_L1ICACHESIZE=17`, `HW_L1DCACHESIZE=18`,
`HW_L2CACHESIZE=20`, `HW_L3CACHESIZE=22`). `hw.cachelinesize` reads the largest reported level-one
data/unified cache line across Windows processor groups, with a 64-byte
fallback when Windows reports no cache topology; `WINTOSH_DARWIN_CACHELINESIZE`
can select a power-of-two value from 16 through 4096. The legacy numeric
`HW_CACHELINE` MIB returns an `int`; the ARM64 name-based key returns a
64-bit value to match XNU's quad-typed ARM64 selector. XNU's registration and
selector behavior is visible in its
[kernel MIB implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_mib.c). The
name-based and numeric MIB queries now also expose L1 instruction/data and L2/L3
cache sizes from Windows cache topology. For each level, Wintosh reports the
smallest matching cache descriptor; this is a conservative approximation of
Apple's least-performant-core generic cache selector, not a full Apple
performance-level model. Apple documents the cache keys in its
[system capability guide](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities). The
Darwin page-size profile now defaults to 4 KiB on x86_64 and 16 KiB on ARM64;
`WINTOSH_DARWIN_PAGESIZE` may select either supported value. `getpagesize`,
`getpagesizes`, `_SC_PAGESIZE`, `hw.pagesize`, and Mach `host_page_size` read
the same profile. Anonymous and file-backed mappings round lengths and check
guest-page alignment; Windows allocation-granularity limits can still prevent
some fixed file mappings. Processor `sysconf` values now use the same
processor-group-aware counts as the Darwin sysctl surface. Apple documents
that macOS page sizes differ by architecture in its
[Apple-silicon porting guidance](https://developer.apple.com/documentation/Apple-Silicon/addressing-architectural-differences-in-your-macos-code).
Apple also recommends querying `hw.cachelinesize` for architecture-sensitive
alignment ([system capability guide](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities)).
The default guest identity is macOS 14.0.0 / Darwin 23.0.0 / build 23A344, with
launch-environment overrides (including runner `--env`) `WINTOSH_MACOS_VERSION`,
`WINTOSH_DARWIN_OSRELEASE`, `WINTOSH_DARWIN_OSVERSION`, and
`WINTOSH_DARWIN_MODEL`, plus `WINTOSH_DARWIN_PAGESIZE` and
`WINTOSH_DARWIN_CACHELINESIZE` for memory topology.
This makes the current guest profile internally
coherent but does not implement all Darwin sysctl nodes. Apple documents the
CPU-count aliases and physical/logical semantics in its
[system capability guide](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities);
the selector meanings follow [XNU's public sysctl headers](https://github.com/apple-oss-distributions/xnu/blob/main/libkern/libkern/sysctl.h).
Clang and MSVC x64 Release `wintosh` builds compiled and linked; the ARM64
Ninja build compiled the changed sources but cannot link because this
environment lacks `msvcrt.lib`, `oldnames.lib`, and `msvcprt.lib`. No guest
Mach-O fixture has verified page-size queries or mapping alignment.

### CoreFoundation executable architecture queries (2026-11-17)

Added the Apple C entry points `CFBundleCopyExecutableArchitectures` and
`CFBundleCopyExecutableArchitecturesForURL`. They inspect thin, fat32, and
fat64 Mach-O headers and return retained CFArray objects containing CFNumbers
with raw CPU type IDs. The URL form accepts a bundle directory or an
unbundled Mach-O file. Invalid/missing executables and unsupported URLs return
`NULL`. These APIs report image slices only; they do not imply that the loader
can execute every reported architecture. Apple documents the same C APIs and
the CFNumber-array return contract ([bundle API](https://developer.apple.com/documentation/corefoundation/cfbundlecopyexecutablearchitectures%28_%3A%29),
[URL API](https://developer.apple.com/documentation/corefoundation/cfbundlecopyexecutablearchitecturesforurl%28_%3A%29)).
Both Clang and MSVC Release `wintosh` builds compiled and linked successfully;
no runtime fixture has exercised these APIs.

### NSBundle file URL APIs (2026-11-17)

The bridge also exposes `principalClass` and `classNamed:`. Principal-class
lookup and named-class lookup now call `CFBundleLoadExecutable` first, then
read `NSPrincipalClass` or query the Objective-C runtime registry. This uses
the existing `dlopen`-backed dynamic Mach-O adapter, so the lookup supports
only the architectures and image features already handled there; it is not a
general plug-in loader. A first successful load posts
`NSBundleDidLoadNotification`; `userInfo[NSLoadedClasses]` contains the class
names newly registered across that load. This is a runtime-wide before/after
class-list difference, so it does not establish per-image ownership when
multiple images load concurrently. `preflightAndReturnError:` exposes the
existing Mach-O plus host-architecture preflight; `loadAndReturnError:` runs
the dynamic loader. They now return `NSExecutableNotLoadableError` for an
unreadable executable, `NSExecutableArchitectureMismatchError` when no host
slice exists, and `NSExecutableLoadError` for general dependency/load failures.
More specific linker errors remain open. `executableArchitectures` reads CPU types from thin, fat32,
or fat64 Mach-O headers and returns them as `NSNumber` values, including raw
IDs for unfamiliar architectures.
The AppKit `NSBundle` bridge now exposes `bundleURL`, `executableURL`, and
`resourceURL`, plus `URLForResource:withExtension:` and its subdirectory
variant. `bundleWithURL:` and `initWithURL:` accept file URLs and pass their
path through the existing CFBundle registry. Each returned URL is constructed
as a host `NSURL` file URL from the same CFBundle path lookup used by the
existing path APIs. Non-file URL and security-scope semantics remain
unsupported. The Clang Release `wintosh` target compiles and links with these
methods, and the MSVC Release target also compiles and links. Both builds
exposed and fixed two compile errors: the export-resolver bridge needed its
`abi::` namespace qualifier, and the menu finalizer referenced a key-equivalent
field that exists only on menu items. The resolver call bridge is now
`noexcept`; guest failure or a null implementation returns zero, which both
loader call sites turn into a controlled bootstrap error. No Mach-O guest
fixture exercised the new bundle URL selectors or resolver-failure path.

### Mach-O dependency-scoped symbol binding (2026-11-17)

Import records retain their client image and `n_desc` library ordinal.
Two-level binding resolves against the selected dependency, SELF, or main
executable; undefined-symbol records, bind opcodes, and chained imports are
merged by symbol and lookup ordinal. This also feeds lazy-bind-only symbols and
weak-bind streams into provider resolution. Bind actions, threaded bindings, indirect pointer
slots, lazy binding, and relocation symbol vectors now match within the client
image's binding set. `MH_FORCE_FLAT` and images without `MH_TWOLEVEL` keep the
flat lookup fallback. Dependency dylibs are mapped even when they define no
imported symbols, so their initializers and Objective-C registration still run.
The process bootstrap now applies each graph binding only to its recorded
client image, preserves the dependency ordinal through relocation lookup, and
uses the resolved re-export provider symbol. Imports that resolve back to the
main executable reuse its existing mapping instead of mapping a duplicate.
Objective-C class, category, and selector sections are now registered only
after rebasing and import fixups, but before image initializers execute.
The chained-import parser now handles all three Apple import formats, including
16-bit signed ordinals and 64-bit addends in `DYLD_CHAINED_IMPORT_ADDEND64`.
The symbol table parser retains `N_INDR` target names, and export lookup follows
alias chains and re-exported provider names before creating client bindings.
Flat lookup now searches the main image first, followed by dependencies in
recursive load-command order; within that search, the first strong definition
wins over weak definitions, with the first weak definition retained as fallback.
When a two-level dependency supplies a weak definition, binding now searches the
loaded graph for a strong override or the first weak definition in load order.
Some weak-coalescing and re-export edge cases remain incomplete. The whitespace
check passes; no build or Mach-O runtime fixture was run. Mach-O `N_ABS`
symbols and absolute exports-trie entries now bypass image-slide adjustment.
Re-export traversal now uses the owner's resolved dependency ordinal and can
forward an absent built-in dylib export to the host symbol resolver, preserving
the graph's inherited `LC_RPATH` selection.
Export-trie addresses are now normalized from offsets relative to the image's
`mach_header` into preferred VM addresses. `STUB_AND_RESOLVER` resolver
addresses are retained in the same coordinate system and checked against an
executable segment before invocation. The process bootstrap and dynamic-image
loader call referenced no-argument resolvers after provider imports are bound,
update the corresponding client bindings, replay retained
relocations and bind/lazy/chained/indirect fixups, and run initializers only
after graph binding. Original relocation-site contents are retained per mapping
so repeat relocation passes preserve link-time addends. All graph images now
complete their initial rebase and bind passes before any resolver runs.
Referenced export resolvers are topologically ordered from their images'
resolver-bearing imports; detected cycles fail before any resolver executes.
The weak-binding precedence follows Apple's
legacy rule in
[ImageLoader.cpp](https://github.com/opensource-apple/dyld/blob/master/src/ImageLoader.cpp).
Undefined symbols marked `N_REF_TO_WEAK` now request flat lookup; this remains
separate from `N_WEAK_REF`, which permits an unresolved import to become NULL,
as specified in Apple's [nlist.h](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/nlist.h).

### Mach-O lazy-bind stream termination (2026-11-17)

The legacy `LC_DYLD_INFO` lazy-bind parser now treats each `BIND_OPCODE_DONE`
as the end of one symbol's opcode sequence and continues parsing subsequent
symbols. Each lazy action now retains the start offset of its symbol sequence,
which is the offset supplied by the lazy stub; non-lazy and weak-bind actions
retain their opcode offsets. Non-lazy and weak-bind streams still stop at their
terminal marker. Apple's legacy dyld `eachLazyBind` continues at each lazy
marker, while `doBindFastLazySymbol` starts decoding at the supplied sequence
offset. No guest Mach-O fixture or build was run in this turn.

### NSApplication sendAction routing (2026-11-17)

`sendAction:to:from:` now invokes an explicitly targeted action when the target
implements the selector. With a nil target it searches the key window's
responder chain and delegate, the distinct main window's chain and delegate,
then the application and its delegate. `targetForAction:` and
`targetForAction:to:from:` expose explicit-target validation and key-window
responder-chain lookup. `NSResponder.tryToPerform:with:` searches from the
receiver up its responder chain and then checks a window or application
delegate. Native menu and control actions share the same
`sendAction:` dispatch path. The selector is invoked through the existing guest
Objective-C bridge. Clang and MSVC x64 host builds compile/link; no Mach-O
action-dispatch fixture has exercised this path.

### Native ARM64 Mach-O entry path (2026-11-17)

The execution architecture guard now accepts only ARM64 images on Windows
ARM64 and retains fail-closed cross-architecture behavior. The Darwin-start
stack trampoline is wired for Clang ARM64; the ordinary integer/pointer
`main(argc, argv, envp)` entry uses x0-x2. All 32 host C++ translation units
and the ARM64 trampoline compile and archive as 33 ARM64 COFF members. A fresh
ARM64 CMake configure cannot identify/link the compiler because the environment
is missing `msvcrtd.lib`; no ARM64 runner link or guest execution is verified.
Apple's aggregate,
stack-passed, variadic, and red-zone ABI differences mean general Objective-C
and framework calls still need an ARM64 bridge and guest fixture.

### NSApplication activation lifecycle (2026-11-17)

`WM_ACTIVATEAPP` now drives `NSApplicationWillBecomeActiveNotification`,
`NSApplicationDidBecomeActiveNotification`,
`NSApplicationWillResignActiveNotification`, and
`NSApplicationDidResignActiveNotification`. Matching optional delegate
callbacks receive the notification on the app thread; state transitions are
coalesced across multiple top-level windows, and `isActive` reads the adapter's
transition state. Apple specifies `NSApp` as the notification object and
main-actor delivery. This does not reproduce every AppKit activation, hide,
unhide, screen-change, or workspace event. No Mach-O guest callback fixture
was run.

`hide:` and both unhide routes now deliver the corresponding will/did
notifications and optional delegate callbacks around window visibility
changes. Reentrant hide/unhide requests during those callbacks are coalesced.
`unhide:` requests activation only after restoring windows; the native
activation transition remains a separate event. No guest lifecycle fixture
was run.

NSWindow key/main callbacks now follow tracked state transitions. The state
is updated before callbacks, and losing-window `WM_ACTIVATE` is coalesced with
the next window's activation so one transition cannot emit duplicate resign
notifications. Ordering a window out or destroying it clears its tracked state
and emits resign callbacks once when it was key/main. This still lacks a
guest-window lifecycle fixture.

### Darwin dyld image enumeration (2026-11-17)

During a guest launch, `_dyld_image_count`, `_dyld_get_image_name`,
`_dyld_get_image_header`, and `_dyld_get_image_vmaddr_slide` now enumerate the
mapped guest Mach-O executable, its mapped dependency providers, and Mach-O
images loaded through `dlopen`. They no longer expose Windows PE module
headers as guest Mach-O headers. Header queries return the mapped `__TEXT`
segment where present. `dlopen` dependency providers are included and the
dynamic portion of the list is sorted by path for repeatable queries. The four guest-facing
functions use the x86_64 Darwin SysV calling convention in Clang builds. Native
host calls outside a guest launch retain the old host-module view for the
existing host API surface. `_dyld_register_func_for_add_image` now stores
guest callbacks and immediately reports the current image snapshot;
`_dyld_register_func_for_remove_image` stores removal callbacks, and image-list
replacement plus `dlopen`/`dlclose` notify registered callbacks for added and
removed Mach-O headers. Remove callbacks run before the dynamic image is
released. Callback pointers are discarded when the guest image-list scope ends
so a later guest launch cannot call stale guest code. Callback registration,
image notifications, and dynamic unload transitions are serialized with a
reentrant process lock. Exact callback ordering around all dyld initializers
and guest code that races its own image use with `dlclose` remain open. MSVC
and Clang x64 runner targets compile and link; the changed stdio unit also
compiles for ARM64. No guest callback fixture or tests were run.

`dladdr` now searches the live guest executable and its dependency providers
before the Windows-module fallback. The bootstrap publishes each mapped Mach-O
segment range and a sorted table of defined symbol addresses for the duration
of the guest image scope. An address inside one of those ranges reports the
guest image path and `__TEXT` header as its base, plus the nearest preceding
defined symbol when one exists. Dynamic images already opened with `dlopen`
continue to use their live Mach-O objects. This change has build evidence only;
there is no guest Mach-O fixture for `dladdr`, concurrent `dlclose`, or image
unload races.

The `dlopen` adapter now honors Darwin `RTLD_NOLOAD` for Mach-O images already
registered in its dynamic-image path map: a hit adds a reference, while a miss
returns an error without loading the image. `RTLD_NODELETE` pins an image with a
synthetic reference after the caller closes its final handle, so later
`dlopen`/`dlclose` pairs remain balanced while the image stays mapped. These
flags are not yet applied to Win32 PE fallback modules; `RTLD_LOCAL`,
`RTLD_GLOBAL`, and `RTLD_FIRST` symbol-visibility behavior is also unimplemented.
The local Clang x64 Release `wintosh` target compiles and links; no dedicated
guest fixture was run.

### NSApplication stop and termination replies (2026-11-17)

The AppKit event loop now exposes `stop:` and exits after the current Win32
event. Worker-thread stop requests are relayed to the application thread,
which calls `PostQuitMessage`. `terminate:` consults the delegate's
`applicationShouldTerminate:` and supports cancel, deferred, and immediate
replies; `replyToApplicationShouldTerminate:` completes deferred replies.
Closing the last window now continues the event loop by default and calls
`applicationShouldTerminateAfterLastWindowClosed:` when the delegate implements
it; a true reply enters the termination flow. BOOL object callbacks accept both
`B` and `c` return encodings. Nested modal-loop stopping and full NSEvent
semantics remain open. Accepted termination now dispatches
`NSApplicationWillTerminateNotification` and invokes
`applicationWillTerminate:` once on the app thread before quitting, including
for deferred replies arriving from a worker; plain `stop:` does not.
MSVC and Clang x64 runner targets compile and link; no guest delegate fixture
was run.

The event path translates Win32 left/right mouse down, double click, up, move,
and captured drag messages into AppKit `NSEvent` types. It also translates
key down/up and modifier changes, computes characters through the active
Windows keyboard layout, maps PC scan positions to macOS hardware key codes
(with virtual-key fallback for unmapped keys),
and sends key events through the first-responder chain. `NSApplication.sendEvent:`
routes input to guest overrides; unhandled keys continue to native controls.
Key-down dispatch first asks the key window's `performKeyEquivalent:`; the
default window and view implementations search the content view and nested
subviews. If none handles the event, app-menu key equivalents run before
`keyDown:`. The Win32 procedure no longer fires menu shortcuts before the
guest `sendEvent:` override. `NSButton` exposes `keyEquivalent`,
`keyEquivalentModifierMask`, `performKeyEquivalent:`, and `performClick:`.
Matching enabled, visible buttons invoke native click-state and target/action dispatch; Command is
the default modifier, with physical Windows Ctrl accepted as Command. Classic
keyed NIB button decoding reads `NSKeyEquiv` and `NSKeyEquivModMask`. Storyboard
decoding and complete Cocoa text key bindings remain unsupported.
Events expose type, location, modifiers, timestamp, window, button/click data,
characters, key code, and repeat state. `acceptsMouseMovedEvents` opts into
hover delivery. IME composition, dead-key fidelity, tracking areas, enter/exit,
pressure/tablet events, and full Cocoa hit-testing remain unsupported. The
Objective-C dispatcher now preserves signed and unsigned 16-bit scalar return
values so `NSEvent.keyCode` survives guest-side `objc_msgSend`. Clang and MSVC
Release `wintosh_windows_host` targets compiled successfully; no Mach-O guest
input/key-equivalent fixture was run.

Windows vertical and horizontal wheel messages now also create scroll-wheel
`NSEvent` values for a hit-tested view implementing `scrollWheel:` (or an app
that overrides `sendEvent:`). `deltaX/Y` and `scrollingDeltaX/Y` use the existing
40-point-per-notch adapter step; precision is reported false. If no guest
handler claims the event, the existing native `NSScrollView` scroll path runs.
Trackpad momentum/phase, precise deltas, and Cocoa scroll physics are not
implemented. The Release host library compiled; no guest input fixture was run.

Each successfully retrieved Win32 event now runs inside its own Objective-C
autorelease-pool scope, including Wintosh's internal stop/terminate messages.
The pool drains after that event's callbacks and dispatch return, keeping
temporary Cocoa objects bounded by event lifetime instead of the whole app
session. This approximates AppKit's event-scoped cleanup; it does not add
nested modal-loop or full NSEvent behavior. No guest fixture was run.

### NSFileHandle bridge and runner link check (2026-11-17)

The AppKit host now provides path-based `NSFileHandle` creation, standard
handles, file-handle wrapping for Wintosh Darwin descriptors, bounded and
to-end reads, available data, writes, seeking, truncation, synchronization,
and close semantics. Darwin descriptors are resolved through Wintosh's
descriptor table; wrapped native handles are duplicated so the guest descriptor
remains valid unless `closeOnDealloc` requests Darwin-side close. The MSVC and
Clang x64 `wintosh.exe` runner targets compile and link. The ARM64 host library
is present, but the configured runner link cannot find the MSVC ARM64 runtime
libraries (`msvcrt.lib`, `oldnames.lib`, `msvcprt.lib`), so ARM64 runner linking
is unverified. No guest fixture or tests were run.

### Objective-C NSUserDefaults bridge (2026-11-16)

The CoreFoundation guest resolver now exports `CFPreferencesCopyAppValue`,
`CFPreferencesCopyValue`, `CFPreferencesCopyMultiple`, `CFPreferencesCopyKeyList`,
`CFPreferencesSetAppValue`, `CFPreferencesSetValue`, `CFPreferencesSetMultiple`,
`CFPreferencesAppSynchronize`, `CFPreferencesSynchronize`,
`CFPreferencesGetAppBooleanValue`, `CFPreferencesGetAppIntegerValue`, and
`CFPreferencesAppValueIsForced`, `CFPreferencesAddSuitePreferencesToApp`, and
`CFPreferencesRemoveSuitePreferencesFromApp`. Application values use the
prefix-relative application-domain plist store; forced-key queries check the managed
preferences plist. `CFPreferencesCopyAppValue` checks the application domain
first, then explicitly added suite plist domains in add order. `kCFPreferencesCurrentApplication`
resolves to the main bundle identifier, with `org.wintosh.application` as the
fallback when no bundle identifier is available. Values use the existing XML
property-list parser and serializer. Writes stay in a process-local pending map
until `CFPreferencesAppSynchronize` or `CFPreferencesSynchronize`; reads in that
process see pending values.
Exact-domain `CopyValue`/`SetValue` and their `Synchronize` entry point support
the application domain for `CurrentUser`; `CurrentHost` and `AnyHost` both map
to the same host store. `AnyUser`, cross-user/global domains, suite search for
the exact-domain and multiple-value APIs, and non-application preference
domains remain unimplemented.

The AppKit/Foundation host now exposes `standardUserDefaults`, suite
initialization, object/string/array/dictionary/data/integer/boolean/double reads, scalar and
property-list object writes, removal, synchronization, and `registerDefaults:`
values over
the existing prefix-relative preferences store. Reads prioritize command-line
arguments, managed preferences, the application/suite store, `NSGlobalDomain`,
other volatile domains, then the per-process `NSRegistrationDomain` fallback;
`open_arguments` now contains only parsed argument-domain values so stale
persistent values cannot mask later writes. The store persists strings,
signed 64-bit integers, finite doubles, booleans, and recursively converts
arrays, string-keyed dictionaries, data, and dates through the CoreFoundation
XML-plist serializer/parser. Property-list objects are supported by object
reads/writes and registration defaults. Null and unsupported collection values
are rejected. The Objective-C bridge also implements `volatileDomainForName:`,
`setVolatileDomain:forName:`, `removeVolatileDomainForName:`, and
`volatileDomainNames`; each instance keeps independent volatile domains as
serialized snapshots. `registerDefaults:` now populates the volatile
registration domain instead of attaching fallback values to the persistent
application domain. Array, dictionary, and data typed getters return the
matching property-list object or nil for a missing key or a different value
type. The global domain is read from the prefix-relative `NSGlobalDomain.plist`.
`objectIsForcedForKey:` and `objectIsForcedForKey:inDomain:` report keys loaded
from the administrator-managed preferences file; the domain-specific form is
limited to this defaults object's application domain. `stringArrayForKey:`
returns an immutable array only when every stored element is a string.
`dictionaryRepresentation`
now merges the domain snapshots in search order and preserves the first value
for duplicate keys.
`addSuiteNamed:` inserts readable suite stores between the app/suite and global
domains in call order; `removeSuiteNamed:` drops the corresponding read domain.
After `NSGlobalDomain`, reads consult language domains in the order from its
`AppleLanguages` array; if that key is absent, Windows preferred UI languages
provide the ordered locale fallback. Region-specific domains are checked before
their base language. Language-domain values participate in `objectForKey:`
lookups and are available through `volatileDomainForName:` and
`volatileDomainNames`. Those APIs also expose the argument and registration
domains. Full configurable search-list controls, live language-list refresh,
and change notifications remain open. Builds validate compile/link only; no
guest fixture was run.

### NSApplication lifecycle selectors (2026-11-16)

`NSApplication` now exposes `finishLaunching`, `activateIgnoringOtherApps:`,
`isActive`, `windows`, `keyWindow`, and `mainWindow`. Launch notifications and
delegate callbacks run once, whether the guest calls `finishLaunching` itself
or enters through `run`; activation brings the first visible Wintosh window
forward. `NSWindow` supports key/main queries, make-key/main calls, and
can-become-key/main queries. `delegate`/`setDelegate:` retain the window
delegate; `windowShouldClose:` can veto Cocoa and native close requests, and
resize/key/main transitions post the corresponding `NSWindow...Notification`
and send delegate callbacks with the notification object. Win32 activation,
Cocoa fronting, order-out, and close update the tracked references. `hide:`, `unhide:`,
`unhideWithoutActivation`, and `isHidden` hide currently visible windows and
restore still-live windows hidden by that operation. Window min/max sizes are
stored and enforced through `WM_GETMINMAXINFO`; centering and minimize/restore/
zoom selectors map to Win32 window operations. Closing a window keeps the loop
alive while another AppKit window remains; after the last closes, the delegate's
`applicationShouldTerminateAfterLastWindowClosed:` can request termination,
which then passes through `applicationShouldTerminate:`. Without that optional
delegate method, the event loop continues with no windows, matching Cocoa's
default. Event tracking modes and full activation policy remain open.
Release builds check compile/link only; no guest application was run.

### NSButton checkbox and radio state (2026-11-16)

`NSButton` now exposes `buttonType`, `setButtonType:`, `state`, `setState:`,
`allowsMixedState`, and `setAllowsMixedState:`. Checkbox-like buttons cycle
off/on by default and off/on/mixed when mixed state is enabled; disabling it
while mixed resets the value to off. Switch/three-state and push-like toggle
buttons use native Win32 styles and synchronize state before target/action
dispatch. Radio buttons update sibling radio buttons in the same superview as
an exclusive group. Radio grouping flags across nested containers and
accessibility presentation remain open.

### NSProgressIndicator (2026-11-16)

`NSProgressIndicator` maps to the Win32 common-controls progress bar. It
supports minimum/maximum/current values, increments, determinate and
indeterminate state, bar/spinning style, and start/stop animation. Determinate
values map proportionally into the native 0–10000 range; started indeterminate
and spinning modes use the Windows marquee. Native appearance, timing, and accessibility behavior differ
from AppKit.

### NSSlider (2026-11-16)

`NSSlider` is registered as an `NSControl` subclass and maps to the Win32
common-controls trackbar. It exposes numeric/string values, minimum and maximum,
orientation, continuous mode, first-responder eligibility, and inherited
target/action handling. Native movement updates the Cocoa value and dispatches
actions continuously or on completion according to `isContinuous`. Tick mark
configuration, knob appearance, and accessibility semantics remain open.

### NSStepper (2026-11-16)

`NSStepper` uses the Win32 common-controls up-down control. It exposes
`minValue`, `maxValue`, `increment`, and `doubleValue`; native arrow clicks
clamp or wrap at the Cocoa range according to `valueWraps` (default true),
update the value, and dispatch the inherited target/action only on a change.
With `autorepeat` false, mouse input applies one change on release; with it true,
Windows supplies the press-and-repeat timing, which differs from Apple's exact
0.5-second delay and 10-per-second repeat rate. Keyboard behavior remains open.

### NSPopUpButton (2026-11-16)

`NSPopUpButton` subclasses the host `NSButton` and uses a native noneditable
Win32 ComboBox. It supports inserting and removing titled entries, count and
title queries, selection by index or title, selected title/string value, and
`pullsDown` state. User selection updates the index and dispatches inherited
target/action. Pull-down command presentation, NSMenuItem-backed entries,
images, and popup tracking callbacks are not implemented. `selectedItem`,
`itemAtIndex:`, `itemArray`, `itemTitles`, `itemWithTitle:`, `indexOfItem:`,
and `lastItem` expose stable NSMenuItem objects. Changing an item's title updates
the native list and title queries; other item state does not yet control native
entry behavior.

### NSView object lifetime (2026-11-16)

`NSView` state now registers an Objective-C runtime finalizer. Releasing the
last reference detaches the native control tree, removes view/control and
popup-item mappings, clears first-responder references into the released tree,
and releases retained titles, targets, popup items, and child views.

### Multiline NSTextView (2026-11-16)

`NSTextView` is registered as an `NSView` subclass and uses a native multiline
Win32 edit control with vertical scrolling. `string`/`setString:`, native edit
synchronization, `selectedRange`/`setSelectedRange:`, `isEditable`/`setEditable:`,
first-responder support, and focus traversal are implemented. Selection offsets
use UTF-16 units and synchronize with native edit selections. Text containers
and storage, attributed text, advanced selection/range behavior, layout, undo,
and delegate notifications remain open.

### Scroll and clip views (2026-11-16)

`NSScrollView` and `NSClipView` are registered as `NSView` subclasses.
Scroll views own a clipping content view and document view; clip bounds constrain
scroll positions and report `documentVisibleRect`. Programmatic scroll points
and vertical/horizontal mouse-wheel input move the clip bounds when that axis
is enabled. Native edit-control
windows receive a window region limited to the nearest clip view. Enabled axes
create native Windows scrollbar controls with line/page/thumb/end actions,
document-aware range and position, and clip-area inset. `autohidesScrollers`
hides axes whose document fits. macOS scroller appearance and its full tracking
behavior remain open. `NSView.visibleRect` intersects bounds with ancestor view
clipping; `scrollRectToVisible:` performs minimal axis-aligned scrolling and
reports whether it moved. `NSClipView.documentVisibleRect` converts into the
document view's coordinate space and `documentRect` accounts for documents
smaller than the clip; the scroll view exposes the same visible rectangle and
`reflectScrolledClipView:` refreshes native thumbs. `NSView.enclosingScrollView`
finds the nearest ancestor.
Rotation, sibling occlusion, and printing visibility are not modeled.

`NSView` exposes `boundsOrigin`/`setBoundsOrigin:` and
`boundsSize`/`setBoundsSize:` as component-level views of its bounds rectangle.
Setters preserve the untouched components and route through `setBounds:`,
keeping coordinate conversion, child resizing, scroll-view refresh, and redraw
invalidation consistent. The guest Objective-C dispatcher handles the
CGPoint/CGSize value encodings; no dedicated Mach-O selector fixture exists.

### NSView rectangle conversion guest ABI (2026-11-23)

The registered `convertRect:toView:` and `convertRect:fromView:` methods now
have matching guest dispatch for their combined CGRect return, CGRect
argument, and view-object signature. x86_64 `objc_msgSend_stret`, legacy
`objc_msgSendSuper_stret`, and `objc_msgSendSuper2_stret` route the hidden
return buffer, stack CGRect, and view register to the typed implementation.
Windows ARM64 reads the CGRect and view from the Darwin variadic stack and
returns the four-double rectangle through the HFA result registers. Both local
Release builds compile and link; a guest fixture has not exercised this ABI.

### NSView size conversion (2026-11-23)

`convertSize:toView:` and `convertSize:fromView:` now convert the size vector
through the composed axis scales and flipped-axis signs, omit translations, and
return positive width and height. Nil endpoints use the window coordinate
system. x86_64 dispatch reads the `CGSize` doubles from XMM registers and the
other view from a GP register; Windows ARM64 reads both arguments from its
Darwin variadic stack and returns the two-double HFA. Apple documents these
selectors as same-window conversions with nil mapping to window coordinates.
The local Clang x64 Release runner and Windows ARM64 host archive builds both
succeed; no guest fixture verifies the calls.

`NSView.autoresizingMask` and `setAutoresizingMask:` now store the six flexible
margin/size bits and keyed view decoding restores `NSAutoresizingMask`. Changes
to a parent bounds size distribute the delta proportionally among a child's
flexible leading margin, extent, and trailing margin, including when the window
content view is resized. Subclass overrides of
`resizeSubviewsWithOldSize:` and `resizeWithOldSuperviewSize:` receive the old
`NSSize` through a typed guest ABI dispatch. Auto Layout and a guest NIB runtime
remain unsupported/unverified.

Added a basic Foundation `NSTimer` target/selector subset with unscheduled and
scheduled factories, manual `fire`, invalidation, validity, interval, mutable
fire date, and user info access. Repeating timers advance from scheduled dates
and skip missed intervals. Scheduled timers queue callbacks on the AppKit
application thread and wake its Win32 message loop. Run-loop modes,
invocation-based timers, and delivery without a running AppKit loop remain
unsupported.

### AppKit menus (2026-11-16)

The host registers `NSMenu` and `NSMenuItem` objects with retained menu-item
ownership, indexed insertion/removal and lookup, submenu relationships,
separators, enabled/check state, action/target, tag, and key-equivalent storage.
`NSApplication.mainMenu` rebuilds Win32 menu bars for host-backed windows;
selected menu commands dispatch through an explicit target, the active
window's responder chain, or the application delegate. Key equivalents are
handled from window and control key messages using the active Windows keyboard
layout, including Unicode characters and Cocoa escape, return, tab, delete,
arrow, function, and navigation key codes. Cocoa Command/Control map to
Windows Ctrl, Option to Alt, and Shift to Shift. Menus with `autoenablesItems`
call `validateMenuItem:` and update native enabled state as they open.
`popUpContextMenu:withEvent:forView:` and its `withFont:` form display a native
blocking popup at the current Windows pointer and dispatch the chosen item
through the common target/action route. `NSView.menu`/`setMenu:` retain a
context menu; right-clicks on host controls and hit-tested nested views show
that menu at the Windows context-event coordinates. Keyboard context-menu
invocation falls back to the pointer. Nonblocking tracking, grapheme/multi-
character shortcuts, exact Command-vs-Control modifier distinctions, and all
Cocoa validation timing remain open. The Clang/MSVC builds validate
compile/link only; no guest menu interaction was run.

### NSProcessInfo guest environment and OS version (2026-11-17)

The host registers `NSProcessInfo.processInfo` and implements `arguments`,
`environment`, `processName`, `hostName`, `processIdentifier`,
`processorCount`, `activeProcessorCount`, `physicalMemory`, `systemUptime`,
`userName`, and `fullUserName`. `processorCount` sums each Windows processor
group's maximum count; `activeProcessorCount` reports currently active logical
processors. Uptime uses Windows unbiased interrupt time, which excludes
low-power sleep, with a monotonic tick-count fallback. Account names come from
the guest `USER`/`LOGNAME` variables or guest home path; the optional
`WINTOSH_FULL_USER_NAME` value configures the guest full name. These values do
not query the Windows account. Arguments come
from `NSApplicationMain`'s guest `argc/argv`; the environment dictionary now
copies the launch-scoped guest `environ` vector, including runner `--env`
values, rather than enumerating host variables. `operatingSystemVersion`, its
display string, deprecated OS name/number methods, and
`isOperatingSystemAtLeastVersion:` use the configurable `WINTOSH_MACOS_VERSION`
profile. The 24-byte `NSOperatingSystemVersion` return is handled by the
x86_64 `objc_msgSend_stret` bridge; the comparison dispatcher decodes the
stack-passed aggregate argument. Defaults are macOS 14.0.0 / build 23A344.
Apple describes these process properties and version comparisons in its
[ProcessInfo reference](https://developer.apple.com/documentation/foundation/processinfo),
[processor-count references](https://developer.apple.com/documentation/foundation/processinfo/activeprocessorcount),
and [operating-system version reference](https://developer.apple.com/documentation/foundation/operatingsystemversion).
Clang and MSVC Release builds validate compilation and linking; no guest fixture verifies
environment isolation or the aggregate method calls. Activity/state, thermal,
and power APIs remain absent.

### NSAutoreleasePool lifecycle (2026-11-16)

AppKit/Foundation host registration now includes `NSAutoreleasePool` with
`init`, `new`, and `drain`. Each pool object owns one runtime autorelease-pool
token; releasing or draining the object pops that token and releases queued
objects in LIFO order. The underlying Objective-C runtime pool functions were
already exported to guest code. Clang and MSVC builds validate compilation and
linking only; nested guest pool behavior has not been exercised by a Mach-O
fixture.

### Objective-C NSRange return ABI (2026-11-16)

The x86_64 SysV message-send dispatcher now handles methods returning the
two-word Darwin `NSRange` (`{_NSRange=QQ}`) with zero arguments, an object,
object plus options, or object plus options and a search range. The trampoline
preserves the returned words in `RAX` and `RDX`, covering common string and
character-set range searches. This remains an explicit layout adapter; other
aggregate returns and ABI classes remain unsupported.
Clang and MSVC host builds are the available validation for this change; no
Mach-O fixture was run.

### Virtual system framework providers (2026-11-16)

dyld's dependency graph now recognizes CoreFoundation, CoreGraphics, and
ImageIO as built-in provider names alongside Foundation and AppKit. When no
matching macOS dylib is staged, these dependencies can proceed to Wintosh's
host symbol resolver, which still rejects every unavailable strong import;
the names do not create placeholder APIs. If a real dependency binary is
present in the resolved search path, normal graph loading still takes
precedence. This closes a loader-level dependency-name gap for the framework
families with existing host exports, but does not establish API completeness
or app compatibility.

### Synchronous AppKit notifications (2026-11-16)

The AppKit host now registers `NSNotification` and a singleton
`NSNotificationCenter`. Notifications retain their name, object, and user-info
dictionary. The center supports selector observers with weak observer/object
references, optional name and object filters, observer removal, and synchronous
delivery through `postNotification:` and both name-based posting methods.
`NSApplication` posts will-finish and did-finish notifications and passes each
notification object to the matching delegate callback. Block observers,
operation queues, thread/queue delivery policies, and the full set of AppKit
events remain unsupported. These changes have build evidence only; there is no
Mach-O guest fixture for observer lifetime or callback delivery.

### Windows typed pasteboard bridge (2026-11-22)

`NSPasteboard.generalPasteboard` and `pasteboardWithName:` for the general
pasteboard map to the Win32 clipboard. The bridge exposes string and `NSData`
read/write, type queries/declarations, clearing, change counts, and text, PNG,
TIFF, RTF, and HTML type constants. Text uses `CF_UNICODETEXT`; the other types
use registered Windows clipboard formats. HTML data is wrapped in or extracted
from the Windows CF_HTML fragment envelope. Writes require the same type to
have been declared first, and fail if another process has changed the clipboard
since declaration; successful writes refresh the declared clipboard sequence.
Clipboard writes allocate transferable global memory and bound reads by the
clipboard allocation size. Each write supplies one format eagerly; simultaneous
format ownership, ownership callbacks, and non-general named pasteboards remain
open. The local
Clang x64 Release build is the validation evidence; no guest copy/paste fixture
was run.

### Host/guest architecture boundary (2026-11-16)

CMake now selects the x86_64 SysV and startup assembly only when the 64-bit
target is not identified as ARM64/AArch64. The Darwin-start trampoline in the
Mach-O loader is explicitly x86_64-only. Mach-O parsing/mapping and an ARM64
relocation subset do not provide ARM64 instruction execution: on the current
x64 Windows host ARM64 and i386 guests still require a CPU translator. A
Windows ARM64 runtime path remains unverified and lacks Darwin-start and
Objective-C ABI support. See `docs/porting/MATHEMATICAL-MODEL.md` for the
parse/map/execute matrix.

### Basic NSString drawing (2026-11-16)

`NSString drawAtPoint:` now renders a UTF-8-backed string through the active
CoreGraphics bitmap context. The implementation transforms the baseline point
with the context CTM, draws one line with the stock Windows GUI font and current
NSColor fill RGB (black by default), and restores the context's per-pixel alpha
bytes after GDI updates its color channels. The Windows host now links `gdi32`.
`NSFont` now implements `systemFontOfSize:`, `boldSystemFontOfSize:`,
`fontWithName:size:`, and `set`; selected fonts are retained through the active
view draw and released when that bitmap pass ends. GDI may substitute a missing
font face. `drawAtPoint:withAttributes:` reads font and foreground-color values
using the exported `NSFontAttributeName` and `NSForegroundColorAttributeName`
data symbols. `drawInRect:` and `drawInRect:withAttributes:` now clip within
their rectangle, wrap by words or characters, and align left, center, or right
using the selected font's GDI metrics. Temporary font and color attributes are
restored after drawing. `NSParagraphStyle` and `NSMutableParagraphStyle` expose
default style, `alloc/init`, alignment, line-break mode, and line spacing; the standard
`NSParagraphStyleAttributeName` symbol is exported. `NSAttributedString` also
implements `drawInRect:` using those style attributes. Clipping mode clips
without wrapping; head, middle, and tail truncation use a measured Unicode
ellipsis and preserve UTF-16 surrogate pairs. Justification,
natural-direction/RTL layout, per-run attributes, glyph shaping, full text
measurement, and verified Cocoa baseline parity remain absent. No Mach-O
drawing fixture has verified rendered pixels.
`NSAttributedString` now has immutable string/attributes construction,
`string`, `length`, and `drawAtPoint:`; its retained dictionary feeds the same
font/color renderer. `NSMutableAttributedString` adds mutable construction,
`mutableString`, UTF-16 range replacement, and `setAttributes:range:` for the
entire string. Partial attribute ranges are ignored; per-run styling and full
paragraph layout remain unsupported. Clang and MSVC Release host-library builds
succeed; no Mach-O drawing fixture has verified rendered pixels.

### AppKit rounded `NSBezierPath` rectangles (2026-11-16)

`NSBezierPath` now exposes `bezierPathWithRoundedRect:xRadius:yRadius:` and
`appendBezierPathWithRoundedRect:xRadius:yRadius:`. The Windows backend
standardizes negative rectangle dimensions, clamps finite per-axis radii to
half the rectangle dimensions, and creates cubic Bézier corners. The x86_64
SysV Objective-C dispatcher decodes the stack-passed 32-byte `CGRect` and the
two XMM-passed radius doubles for the matching class and instance methods.
`lineCapStyle` supports butt, round, and square caps on open stroked contours;
the corresponding CoreGraphics state is saved and restored around path
strokes.
Clang and MSVC Release builds of `wintosh_windows_host` succeed. No Mach-O
guest drawing fixture has verified rendered pixels, so this remains source and
build evidence rather than AppKit runtime compatibility evidence.

### NSBezierPath relative cubic segments (2026-11-23)

`relativeCurveToPoint:controlPoint1:controlPoint2:` adds the end and both
control-point offsets to `currentPoint`, then appends a cubic segment. The
existing Objective-C ABI dispatcher already handles the three CGPoint
arguments. No Mach-O guest fixture has exercised this selector yet.

`appendBezierPathWithPoints:count:` now reads up to one million finite guest
points with `ReadProcessMemory`, rejects unreadable or overflowing buffers,
and appends a connected line sequence. The first point starts a new subpath
when the path is empty or its last stored element closes a shape; otherwise it
connects from the current open subpath. Its x86_64 SysV and Windows ARM64
Objective-C dispatch paths decode the point pointer and signed `NSInteger`
count. A Mach-O guest fixture has not verified these selector and pointer
semantics.

### NSBezierPath center arcs (2026-11-23)

`appendBezierPathWithArcWithCenter:radius:startAngle:endAngle:clockwise:`
converts degree angles to a direction-normalized sweep, connects the starting
arc point to the current path (or moves there for an empty path), and emits
cubic segments no larger than 90 degrees, capped at one full circle. The x86_64
guest dispatcher decodes the CGPoint plus four scalar XMM arguments and the
BOOL from a general-purpose register. No Mach-O fixture has verified the
selector ABI or rendered pixels.

### NSBezierPath associated-point mutation (2026-11-23)

`setAssociatedPoints:atIndex:` updates a move, line, close, or cubic element
without changing its kind. Cubic input follows Cocoa order (control point 1,
control point 2, endpoint) and is mapped to the host command's internal order.
Rectangles and ovals expand before an element is edited; close-point values are
used by rendering, bounds, and hit-testing. The selector is registered with
the pointer-plus-`NSInteger` encoding and dispatched through the x86_64 SysV
and Windows ARM64 guest paths. Documentation records the point-cardinality
and permutation model in `porting/MATHEMATICAL-MODEL.md`. The local Clang x64
Release runner and Windows ARM64 host archive build successfully; no guest
fixture verifies mutation or resulting geometry.

### NSColor RGBA accessors (2026-11-16)

The four stored RGBA components are available through `redComponent`,
`greenComponent`, `blueComponent`, and `alphaComponent`, as well as the
BOOL-returning `getRed:green:blue:alpha:` method. The Objective-C dispatcher
passes all four output pointers using the guest SysV ABI. These accessors
preserve the existing RGB-only color-space boundary. Calibrated, device, and
neutral-white constructors use the guest two-double SysV Objective-C path.
`whiteComponent` reads the value recorded for grayscale factories and named
grayscale colors; non-grayscale calls return zero instead of raising Apple's
color-space exception. `getWhite:alpha:` now returns `false` for non-grayscale
colors without writing its output pointers and returns the grayscale/alpha
pair for recorded grayscale colors through the guest ABI. The current Clang
and MSVC Release builds succeed. `colorWithAlphaComponent:` now returns an
immutable color with the requested alpha while preserving stored RGB and
grayscale classification. `blendedColorWithFraction:ofColor:` now creates a
weighted RGBA result using the mixed XMM-double and GP-object SysV argument
locations and returns nil when either color is absent. No guest Mach-O fixture
exercises these color methods yet. Calibrated/device HSB factory methods now
clamp finite hue, saturation, brightness, and alpha inputs to `[0, 1]` and
convert HSB to the adapter's RGB storage. `getHue:saturation:brightness:alpha:`
converts the stored RGB triple back to HSB and writes only non-null guest output
pointers. The individual `hueComponent`, `saturationComponent`, and
`brightnessComponent` queries use the same conversion. Device and calibrated
color-space distinctions remain unmodeled, and these paths also lack a Mach-O
guest fixture. The device CMYK factory and `getCyan:magenta:yellow:black:alpha:`
now provide a clamped RGB-backed conversion; CMYK profiles are not preserved.
The Objective-C dispatcher handles five XMM double arguments and five guest
output pointers for these selectors. There is still no Mach-O guest fixture.
Calibrated/device/sRGB four-component constructors, white constructors, and
alpha replacement now clamp finite components to `[0, 1]`; extended-gamut RGB
values remain unsupported by this RGB-backed model. `numberOfComponents` and
`getComponents:` return the adapter's two-component grayscale or four-component
RGB representation; the component-array pointer is decoded through the guest
SysV ABI.

## Evidence summary

### Mach IPC descriptor gate (2026-10-11)

The native Clang Mach ABI smoke now passes the complex port-descriptor slice
end to end (`MACH_NOTIFY_NO_SENDERS`, single- and multi-descriptor transfer,
dead-name notification, broker capability, and self-deallocate; exit code 0).
The
Windows dispatcher now bounds OOL-type probing by the available descriptor
bytes and keeps locally owned destination ports on the local queue instead of
forcing them through the cross-process broker. Broker routing remains enabled
for non-local destinations only when every transferred descriptor has a
registered broker capability. This closes one ABI family; full Mach rights,
MIG, OOL lifetime, and arbitrary Darwin IPC compatibility remain open.

Mach-O code signatures now also have a structural gate:
`HasValidCodeSignatureSuperBlob()` validates the embedded SuperBlob magic,
length, count, nested blob bounds, and the CodeDirectory magic/minimum
structure when that slot is present. This is format validation only; it does
not validate CodeDirectory hashes, CMS signatures, or authenticity.

| Area | Current state | Evidence / limitation |
|---|---|---|
| Native Windows source | present | `darling-source/src/native/windows` |
| x64 Foundation type encoding | verified | `darling_windows_foundation_smoke.exe` reports `FOUNDATION_TYPE_ENCODING_ABI=PASS` |
| Win32 Foundation type encoding | verified | same smoke gate on Win32 |
| Low-level primitive families | verified in selected batches | tracked in `PRIMITIVE-MATRIX.csv`; not equivalent to full Darwin semantics |
| Full generated `ALL_BUILD` | not currently confirmed | aggregate orchestration can stall without active compiler output; the current configured Clang CTest suite is independently verified at 60/60 |
| WSL runtime backend | unavailable in the current host evidence | probe reported `Wsl/E_ACCESSDENIED` |
| Real Darling server / arbitrary Mach-O app | not proven | no local end-to-end application run |
| Darling component licenses | incomplete review | 150 external components; 147 remain review-required |
| Corresponding source completeness | exception remains | Heimdal filename cannot be materialized on NTFS |

## Current local integration evidence

The complete configured Release smoke suite now builds target-by-target and
passes: **60/60 CTest tests**. The Visual
Studio aggregate build target can
still stall during orchestration, so the reproducible fallback is to build
the generated targets individually; this does not represent a test failure.

The licensing inventory remains conservative: the 150 external-component
rows are provenance records, not relicensing decisions. Only components with
an actual component-local license/notice file or separately recorded evidence
are marked accordingly; text-marker matches in fetched candidates remain
manual review material.

The Win32 build now compiles the host library and runner. It includes i386
exception PC/SP reporting and a MASM Darwin-start trampoline that installs the
initial argc/argv/envp/apple stack before transferring to guest code. No
32-bit Mach-O runtime fixture has yet exercised that transfer. Win32 also maps
Mach `i386_THREAD_STATE` get/set calls to the native 16-register `CONTEXT`
layout; float, debug, and exception state flavors remain unsupported. CI runs
the complete suite serially because filesystem, path and ConPTY fixtures share
process-global Windows state.

The first cross-process Mach IPC slice is now implemented as a version-1
`WIPC` envelope codec. It validates bounded little-endian operation, request,
port-token, disposition-count, and payload fields. The runtime smoke test
reports `MACH_IPC_ENVELOPE=PASS` and `MACH_IPC_ENVELOPE_VALIDATION=PASS`.
This is only the transport-neutral message format; full Mach dispositions,
cancellation, MIG descriptors, and Mach-compatible error semantics remain
unimplemented.

The broker now consumes the same envelope over a Windows named pipe for the
first real cross-process operation: `Allocate` creates a broker-owned port
token and `Deallocate` releases it. The separate-process smoke test passes
`BROKER_MACH_ALLOCATE` and clean broker shutdown. Cross-process message
delivery (`Send`/`Receive`) now has a bounded broker FIFO for allocated tokens,
covered by `BROKER_MACH_SEND_RECEIVE=PASS`. The broker also supports bounded
timed receives and a cross-client blocked-receive wakeup, covered by
`BROKER_MACH_RECEIVE_TIMEOUT=PASS` and
`BROKER_MACH_BLOCKED_RECEIVE_WAKE=PASS`. Rights/dispositions, MIG, and a
Mach-compatible name-space remain open. The lifecycle smoke also proves
`BROKER_MACH_LIFECYCLE=PASS`: a deallocated token is rejected on later use.
An empty nonblocking receive now returns the explicit
`MACH_RECEIVE_WOULD_BLOCK` result (`BROKER_MACH_EMPTY_RECEIVE=PASS`), rather
than being conflated with malformed input. The named-pipe frame limit is
now aligned with the envelope's 4 MiB bounded payload limit; a 2 MiB
cross-process FIFO round trip passes as `BROKER_MACH_LARGE_PAYLOAD=PASS`.
This remains inline transport, not Mach out-of-line memory.
The envelope smoke also rejects oversized payloads and trailing bytes before
dispatch (`MACH_IPC_ENVELOPE_LIMIT=PASS` and
`MACH_IPC_ENVELOPE_TRAILING=PASS`).
The host now also exposes named shared-memory creation/opening and mapping;
the runtime smoke verifies a second mapped view with
`MACH_IPC_SHARED_MEMORY=PASS`. It is the Windows primitive for a future
out-of-line descriptor. The version-1 envelope now carries an explicit
out-of-line token and size trailer, validated on encode/decode; broker-side
mapping allocation and descriptor transfer now work for one bounded
disposition: `BROKER_MACH_OOL=PASS` creates a named mapping on `Send`, and
the receiver opens and verifies it after `Receive`. PID-bearing receives now
also duplicate a native mapping handle into the target process. This remains
a Windows shared-mapping approximation, not full Mach protection transfer or
full OOL lifetime/disposition semantics. The broker now associates OOL
regions with their owning port and releases them on `Deallocate`.
Envelope decoding also rejects partially specified or oversized OOL
descriptors (`MACH_IPC_DESCRIPTOR_VALIDATION=PASS`).
The broker explicitly rejects OOL disposition counts above one with
`BROKER_MACH_DISPOSITION_LIMIT=PASS`; this is an intentional boundary of the
single-mapping OOL transport. Port-right descriptor lists are supported for
multiple entries and are covered by `BROKER_MULTI_DESCRIPTOR_RIGHTS=PASS`.
The runtime now also provides named notification events with signal/reset and
timed wait behavior; `MACH_IPC_NOTIFICATION=PASS` verifies the host primitive.
The broker now routes `NotificationCreate`, `NotificationSignal`,
`NotificationReset`, and `NotificationWait`; the cross-process smoke covers
timeout and signal behavior as `BROKER_MACH_NOTIFICATION=PASS`. Full Mach
notification-port rights and delivery semantics remain open. Explicit
`NotificationDestroy` cleanup and stale-token rejection are covered by
`BROKER_MACH_NOTIFICATION_DESTROY=PASS`; broker reset followed by timeout is
also covered by `BROKER_MACH_NOTIFICATION_RESET=PASS`.
The local Mach C ABI additionally supports one port-death notification
registration per local port, replacement reporting, explicit unregister, and
delivery on forced destruction; its smoke coverage is limited to that adapter
contract, while
full no-senders/dead-name rights and Darwin notification message layouts
remain open.
The local `MachPort` adapter also exposes an explicit waiter interrupt;
`runtime_smoke` proves that a blocked receiver wakes without closing the port.
This is the local cancellation primitive only; broker request cancellation by
request ID remains unimplemented.
Its local FIFO is also bounded at 1024 messages; the smoke fills, rejects, and
drains that boundary. This bounds adapter memory growth but is not yet the
full Darwin queue-right policy.
Malformed notification wait payloads are rejected and covered by
`BROKER_MACH_NOTIFICATION_VALIDATION=PASS`.
The broker also accepts the explicit `Destroy` operation, releasing the
port and rejecting later use (`BROKER_MACH_DESTROY=PASS`).
Each port FIFO is bounded to 1024 queued messages and reports
`MACH_SEND_QUEUE_FULL` instead of allowing unbounded broker memory growth;
the boundary is exercised by the broker smoke as
`BROKER_MACH_QUEUE_LIMIT=PASS`.
The named-pipe transport reserves up to eight server instances and the broker
accept loop dispatches each connection to a joinable worker. The shared broker
state is protected by a mutex at the request boundary, so the existing
port/OOL/notification tables have an explicit synchronization contract for
bounded multi-client transport.

The platform path adapter now provides lexical `AbsolutePath` resolution and
handle-based `CanonicalPath` resolution through Windows final-name lookup,
with an explicit absolute-path fallback when the host denies that query.
Filesystem smoke coverage also exercises stat/lstat, timestamps, symlinks,
hard links, vector I/O, locks, filesystem enumeration, and host-backed Darwin
`rename` including the Windows access-denied fallback. ACLs, xattrs,
resource forks, mount namespaces, and full reparse-tag fidelity remain open.

The CoreFoundation run-loop and signal-registry smoke tests are marked
`RUN_SERIAL` in CTest because they intentionally exercise process-global
boundary state. This prevents concurrent smoke processes from turning a valid
runtime check into cross-test interference.

The current source batch expands the CoreFoundation adapter with strings,
data, arrays, integer/real numbers, dictionaries, sets, dates, filesystem URLs, booleans,
null values, a process-local RunLoop/notification subset, and a minimal XML
property-list serializer with escaping, nested arrays, and Base64 data nodes. `corefoundation_smoke` covers this
subset. It deliberately does not claim plist parsing or binary plists, full
collection ownership/`CFEqual` semantics, true Apple timer/source behavior,
or full Foundation/CoreFoundation compatibility.
The adapter now also parses XML property-list strings, integers, reals,
booleans, nested arrays, and dictionaries with string keys and reserializes the
result. Base64 `<data>` and UTC/offset `<date>` decoding with fractional
seconds are also supported. Binary plists and complete ownership semantics
remain open.
The adapter now additionally has a limited `bplist00` reader for single-byte
ASCII strings, BMP UTF-16 strings, integers, booleans, null, simple arrays,
string-key dictionaries, extended length markers for strings, arrays, and
dictionaries, plus binary data/date, IEEE-754 real-number, and UID markers
preserved as a distinct private `PropertyListUID` CF type with a 64-bit value
accessor separate from `CFNumber`,
including UTF-8 string byte extraction and UTF-16-unit string length for valid UTF-8 (including surrogate-pair accounting) and common-Unicode-whitespace trimming plus mutable append/replace-all/range-replace, mutable data byte access/append/replace/resize/clear, array append/append-array/insert/remove/remove-range/replace, dictionary duplicate-key collapse/set/remove/merge, numeric cross-type equality, set add/remove/union/intersection/subtract operations, date and number comparison plus interval creation/difference, and named/wildcard local notifications with global removal. Unicode collation, normalization, malformed-UTF-8 recovery policy, complex
numeric/time semantics, and complete ownership semantics remain open.

`NSKeyedUnarchiver +unarchiveObjectWithData:` now parses a limited
`NSKeyedArchiver` binary-plist archive and resolves UID references into
Foundation strings, numbers, data, dates, arrays, dictionaries, and null
values. An object cache preserves repeated references, including cycles in
arrays, dictionaries, and custom classes, by caching placeholders first. Custom
classes may run `initWithCoder:` with temporary `decodeObjectForKey:`,
`decodeObjectOfClass:forKey:`, `containsValueForKey:`, `decodeBoolForKey:`,
signed/unsigned 32- and 64-bit integer decoding, and float/double decoding.
NSCoder superclass/conditional decoding, secure-coding validation, decode
delegates, and NIB instantiation remain unsupported. No archive fixture was
run for cyclic graphs.

`NSView -initWithCoder:` now reads `NSFrame`, optional `NSBounds`, `NSSubviews`,
hidden state from `NSvFlags`, and `NSNextKeyView` from the temporary archive
context. `NSCustomView` placeholders resolve `NSClassName`, allocate a loaded
`NSView` subclass with `initWithFrame:` using `NSFrame`/`NSFrameSize`, and restore
selected bounds, subview, hidden, and key-view state. Rectangle values use
NSString-based keyed geometry coding. Other placeholder types, connections,
`awakeFromNib`, automatic `NSMainNibFile` startup, and NIB fixture coverage
remain absent.

The same batch now exposes bulk extraction for array ranges, dictionary
key/value pairs, and set members, plus validated `CFData` byte-range copies.
These are pointer-level operations and do not add CoreFoundation callback,
equality, or ownership semantics.
Container ownership has since been strengthened: arrays, dictionaries, and
sets retain members on creation and release them when the container is
destroyed. Callback allocators, hash callbacks, mutation, and complete Apple
collection semantics remain open.

The string adapter also exposes a read-only direct C-string pointer whose
validity is limited to the lifetime of its CFString object.
It also provides `CFRange`-based literal and ASCII case-insensitive substring
search; this does not claim Unicode collation or the full CFString option set.

The Foundation type-encoding adapter now covers Darwin LP64 `long`, block
encoding `@?`, `void`, unknown/function pointers, decimal bitfields, and the
Darwin `long double` encoding `D` with a fixed 16-byte representation. Its
remaining ABI gap includes exact packed bitfield
layout, compiler-specific aggregate ABI corner cases, and the full Foundation
object model.
The same adapter now also provides value-level `NSRange` construction,
maximum, containment, intersection, and union primitives, covered by the
Foundation smoke gate.
Quoted aggregate field names such as `"x"` and `"y"` are skipped while
parsing, matching common Objective-C runtime encodings.

CoreFoundation now has a first CFBundle path subset: a guest launch records the
main executable path; `CFBundleGetMainBundle` derives its bundle root;
`CFBundleCreate`, bundle/executable/resource-directory URLs, resource URL
lookup, the cached nonlocalized Info.plist dictionary, arbitrary key lookup,
bundle identifier access, and `CFBundleCopyLocalizedString` are exposed through
the Darwin symbol table. The string lookup searches the Windows preferred UI
localizations, the bundle development region, `Base`, and the unlocalized
table. Resource URL lookup now searches the same preferred language folders
before falling back to unlocalized bundle resources, so localized NIB and
other resource lookup shares that ordering. It reads OpenStep `.strings` files
with UTF-8 or UTF-16 text, as well as
XML and supported binary property-list tables. Executable discovery reads
`CFBundleExecutable` from XML or the supported binary-plist subset. Resource
lookup rejects absolute and parent-traversal names. The
`CFBundleGetValueForInfoDictionaryKey` lookup also checks localized string
values in `InfoPlist.strings` before the global dictionary. Local-info-dictionary
access and full plist type coverage remain open. The CFBundle executable
surface now also exposes `CFBundleIsExecutableLoaded`, preflight, load/unload,
and function/data pointer lookup, plus standard URLs for built-in plug-ins,
private/shared frameworks, and shared support. Directory discovery now
creates CFBundle objects from immediate child bundles with an optional
extension filter; it is bounded to 65,536 inspected entries and 4,096 child
directories. A process-wide normalized-path catalog now uniques bundle objects
and supports `CFBundleGetBundleWithIdentifier` and `CFBundleGetAllBundles`.
The catalog intentionally keeps bundle objects alive for the process, so code
unloading relies on the explicit unload API. Non-main bundles use the existing
Mach-O `dlopen`/`dlsym`/`dlclose` path. Bundle lookup checks app bundle
`Contents` paths and
versioned framework `Versions/Current` plus concrete version directories for
the executable, Info.plist, and resources. Preflight currently returns no
`CFError`; its preliminary, side-effect-free Mach-O parse checks structure and
host architecture only, so dependency/link errors can still appear during
load. Resource localization through all framework variants, full Objective-C
`NSBundle` coverage, full plug-in discovery, and main image symbol
enumeration remain incomplete. The new CFBundle loader symbols have no
dedicated guest fixture and this change is not build-verified in this
environment.
The Objective-C bridge now also supports separate lazy metaclass objects for
registered classes, class-method lookup through those metaclasses, and runtime
`class_addProperty` registration
with serialized attribute pairs and verified lookup. The machine-readable matrix records the same coverage for LP64, blocks,
and metaclass mutation. The DYLD resolver now combines caller-provided
`@rpath` roots with embedded `LC_RPATH` entries even when the caller already
provided search paths; `DYLD_COMBINED_RPATH=PASS` covers this behavior. The
dependency graph now carries the ordered runpath context from the main image
through nested dylibs, preserving load-command order when a shared image is
first reached. The graph still chooses the first path context for a shared
image; this extension has build evidence only, not nested guest-fixture or
multi-parent runtime evidence. The
native prefix also creates the Apple-documented `Library` data roots
(`Application Support`, `Caches`, `Frameworks`, `Logs`, `Preferences`, and
`Saved Application State`) plus `System/Library/Frameworks` and `usr/lib`.
`runtime_smoke` verifies the layout and `dyld_smoke` verifies system and local
framework shorthand resolution. This remains a path/layout boundary; it does
not provide Apple framework binaries or their full ABI.
Mach-O category registration is also covered: category instance/class methods,
properties, and protocols are merged into the target class and verified by
`objc_smoke`. Selector-reference teardown and full method-cache/swizzling
semantics remain open.
The Windows bridge now also exposes ABI-safe typed
`darling_objc_msgSend_object2` for `@@:@@` and
`darling_objc_msgSend_int64_2` for `q@:qq`, alongside the existing typed
`darling_objc_msgSend_bool1` and `darling_objc_msgSend_pointer1` entry points
for `B@:B` and `^v@:^v`, alongside the floating-point entry points
`darling_objc_msgSend_double1` and `darling_objc_msgSend_double2`
for the supported `d@:d` and `d@:dd` signatures; `objc_smoke` verifies the
native floating-point call paths. The bridge also exposes
`darling_objc_msgSend_void1_int64` for `v@:q`. This does not make
the variadic `objc_msgSend` ABI-complete for arbitrary signatures. The
CGRect-compatible `darling_objc_msgSend_rect0` typed struct return is also
covered by `objc_smoke`; this remains a fixed-layout adapter, not general
aggregate ABI support.
The corresponding `darling_objc_msgSend_void1_int` bridge for `v@:i` is also
covered by the same gate.
The block bridge `darling_objc_msgSend_block1` for `@@:@?` is likewise covered;
arbitrary block signatures and complete copy/dispose ABI behavior remain open.
The zero-argument object-return bridge `darling_objc_msgSend_object0` for
`@@:` is covered as well.
`method_exchangeImplementations` now swaps compatible instance-method IMPs
and class-method IMPs, and the smoke verifies both swaps and restoration; full Apple method-cache,
incompatible encodings are rejected without mutation. Full Apple method-cache,
concurrency, and arbitrary swizzling semantics remain open.
complex numbers, quoted aggregates, and nested pointers.

CoreGraphics now has a separate partial adapter with the `coregraphics_smoke`
gate. In addition to geometry and color values, it supports 8-bit
premultiplied RGBA/BGRA bitmap contexts, fill/clear/stroke rectangles, bitmap
data/size queries, and basic multi-contour move/line/close/fill/even-odd-fill/
stroke paths, immutable bitmap-context snapshots as `CGImage`, and
nearest-neighbor `CGContextDrawImage` with premultiplied-alpha compositing.
The current transformation matrix supports get/set/concatenate/translate/scale/
rotate, with save/restore of the CTM, fill/stroke colors, and line width. Its
inverse maps rectangle and image samples back to user space; paths are transformed
before raster fill and stroke. Path and rectangle clipping use a device-space
coverage mask; `CGContextClipToMask` scales bitmap snapshot alpha into that mask.
Successive clip regions multiply coverage and participate in saved graphics state.
`CGContextSetAlpha` modulates bitmap fills and image compositing and is preserved
by save/restore; clear operations remain governed by clip coverage.
Blend state now implements normal source-over, multiply/screen/overlay/darken/
lighten/difference/exclusion, and the Porter-Duff clear/copy/source-in/source-out/
source-atop/destination-over/destination-in/destination-out/destination-atop/XOR
operators for fills and bitmap images.
Path construction now flattens cubic and quadratic Bézier curves to 32 segments,
ellipses to 64 segments, and circular arcs by a radius-dependent chord-error
bound capped at 8192 segments. Tangent arcs calculate their tangent points and
center from neighboring line vectors. Antialiasing remains open.
ImageIO now has a minimal `CGImageSource` path backed by CoreFoundation data,
CoreGraphics data providers created from CFData, URLs, filenames, or caller
memory with a release callback, and file
URLs. The source is a distinct CoreFoundation object with its own type
ID and generic `CFRetain`/`CFRelease` lifetime. Windows Imaging Component
validates the source, reports its frame count, decodes a requested frame into
premultiplied BGRA pixels, and reports the source UTI through
`CGImageSourceGetType`; source-level properties report image count, and
indexed properties report pixel width and height. Source and per-frame status
queries distinguish incomplete, complete, and invalid data. Incremental sources
accept replacement snapshots of accumulated `CFData`, matching ImageIO's update
contract. Common WIC containers map to PNG, JPEG, GIF, TIFF, BMP, ICO, and
JPEG XR identifiers.
`CGImageSourceCopyTypeIdentifiers` enumerates registered default WIC decoders
for the mapped containers. `CGImageSourceCreateThumbnailAtIndex` generates a
WIC-scaled thumbnail when the `Always` or `IfAbsent` flag is true and honors
the maximum-pixel-size option. `IfAbsent` first asks the WIC frame for its
embedded thumbnail and falls back to scaling the full frame.
`CreateThumbnailWithTransform` applies EXIF orientations 1–8. Other metadata
remains open, as do animated-image timing, text, antialiasing, events, display
backends, and AppKit.
Data providers also expose size, direct byte-pointer access, bounded
position-based reads, and a copied `CFData` representation. These APIs cover
direct-provider access, while `CGDataProviderCreateSequential` adds callback-
backed stream reads with optional skip/rewind and final `releaseInfo` cleanup.
Sequential reads are serialized per provider; size and direct-pointer queries
report the unknown/unavailable state for that provider kind. The provider
length query returns caller-owned buffers' recorded length as well as
internally copied storage, so external-provider copies and ImageIO source
creation see the supplied bytes. `CGDataProviderCreateDirect` adds sized
positioned-read providers or provider-owned byte pointers, with paired
`CGDataProviderReleaseBytePointer` and `releaseInfo` callbacks.
`CGImageSourceCreateWithDataProvider` snapshots both sequential providers
through EOF and direct providers through bounded positional reads, including
partial callback reads, then validates the encoded bytes. These snapshots are
bounded by WIC's 32-bit in-memory stream size limit; data beyond it fails
source creation.
The write path now includes `CGDataConsumerCreate`, its CFData and URL forms,
consumer retain/release/type-ID access, and
`CGImageDestinationCreateWithDataConsumer`. Callback consumers support partial
writes and release client info when the last reference is freed. The existing
`CGImageDestinationCreateWithData` and `CreateWithURL` entry points use the same
consumer path. Finalization encodes 32-bit premultiplied pixels through WIC for
PNG, JPEG, TIFF, GIF, and BMP. It requires the exact declared image count;
multiple frames are accepted only for GIF/TIFF. Metadata/options, HEIF, and
codec qualification remain open. Public CoreFoundation data/type functions
have Darwin-ABI wrappers with their native allocator and range arguments before
host-symbol binding.
The adapter now includes point/size/rectangle constructors and
standardization, affine transform inversion/application, rectangle operations,
transform equality and integral rectangles, and RGBA/grayscale/white/clear colors.

The Release package was regenerated locally as
`Wintosh-0.1.1-AMD64.zip` and inspected. SHA-256 is
`E8531F0F9B36252DA5E98165E1BB44777DA478DA11AB2CDE101A290545B0AB99`.
It contains
`bin/wintosh.exe`, `bin/wintosh_broker.exe`, the Windows README, and the
bundled license/provenance files.

The current unpushed worktree additionally verifies the product CLI and the
native loader path on x64 Windows:

| Gate | Result | Evidence |
|---|---|---|
| `wintosh.exe` build | pass | CMake Release target `wintosh` |
| Mach-O entry execution | pass | `wintosh_runner_smoke=0` |
| `.app` bundle resolution | pass | native runner smoke resolves `Contents/MacOS/<bundle-name>` |
| Host `_getenv` binding and `envp` | pass | `wintosh_runner_env_smoke=0` |
| Dylib graph, RPATH, bind/rebase/chained paths | pass | `wintosh_dyld_smoke=0` |
| Objective-C registry/runtime | pass | `wintosh_objc_smoke=0`, `wintosh_objc_runtime_smoke=0` |
| Memory/process/thread/sync/socket/time families | pass | selected smoke targets all returned zero |
| Filesystem family | pass with environmental skip | hardlink check reports `ACCESS_DENIED` when Windows rights are unavailable |

These are native adapter and synthetic Mach-O gates. They do not prove that an
unmodified third-party macOS executable or the full Darling server runs.

The Darwin bootstrap now scopes the absolute loaded guest image path around
initialization and entry execution. `_NSGetExecutablePath` returns that Mach-O
path (with Windows separators normalized) to guest code and falls back to the
host module path outside a guest launch. This implementation change has not
been rebuilt in the current environment, so existing host API gate results do
not yet verify it.

## Current-batch provenance audit

The current Mach ABI, broker, and smoke-test files retain their existing
GPL-3.0-compatible headers; the broker uses the explicit GNU GPL v3 wording.
This batch adds no copied external source.  Darling and PureDarwin remain
references recorded in the matrix, while the separate component-license audit
still reports 150 external components with 147 review-required cases.

## What this means

Wintosh is a documented native Windows compatibility experiment with real
verified primitive adapters, not yet a complete Darling port. The current
implementation can demonstrate selected ABI-shaped behaviors and smoke tests;
it cannot yet claim that normal macOS applications, full Foundation, AppKit,
or the Darling userland execute on Windows.

## Next gates

1. Make a clean reproducible x64 and Win32 `ALL_BUILD` run.
2. Preserve smoke output as release evidence.
3. Resolve or exclude the 147 external license-review cases.
4. Resolve the NTFS-incompatible Heimdal source checkout or document an
   alternate corresponding-source bundle.
5. Implement Foundation/CoreFoundation families incrementally.
6. Prove a guest/runtime boundary with a real Darling server or a documented
   WSL/emulated-Linux backend.

## Broker reconnect boundary (2026-10-09)

The broker no longer terminates its Mach namespace when a named-pipe client
disconnects.  It recreates listening pipe instances and accepts subsequent
clients while retaining allocated port tokens, queued messages, out-of-line
shared-memory regions, and notification objects.  Accepted connections run in
joinable workers, so this covers both reconnect and bounded concurrent-client
transport.

Current verification:

| Gate | Result | Evidence |
|---|---|---|
| Broker Release rebuild | pass | `wintosh_broker` and `wintosh_darling_windows_broker_smoke` built in `build-windows-current` |
| Broker smoke | pass | CTest `darling_windows_broker_smoke`, 1/1 |
| Reconnect smoke | pass | broker smoke explicitly destroys the first client, reconnects, verifies `PING/PONG`, then shuts down |
| Full configured Release suite | pass | CTest Release suite, 39/39 |

The broker now also accepts a four-byte millisecond timeout on `Receive` and
waits on a condition variable until a message arrives, the port is destroyed,
or the timeout expires.  An empty payload preserves the nonblocking probe.
The smoke gate covers the timeout-expiry path.
`Receive` also accepts an eight-byte timeout-plus-inline-capacity request. If
the queued message exceeds that capacity, the broker returns a Receive
response containing the required inline size without consuming the message;
the broker smoke covers the too-large response and successful retry. This is
the adapter's bounded `MACH_RCV_TOO_LARGE`-style primitive, not yet complete
Mach message descriptor or trailer semantics.
Receive timeout payloads are validated even when a message is already queued;
malformed timeout requests cannot bypass validation by taking the fast path.
Receive requests also reject nonzero disposition and OOL descriptor fields;
descriptor fields are now directionally validated in the broker envelope.
Send requests likewise reject stale OOL fields unless they carry the supported
single-disposition OOL payload path.
Notification control operations reject unexpected payloads and descriptors;
only `NotificationWait` accepts the documented timeout payload.
Port `Deallocate` and `Destroy` requests apply the same empty-envelope rule;
administrative lifecycle operations cannot smuggle message descriptors.

Broker notification objects are now shared independently of the state-map
lock, and `NotificationWait` releases that lock while waiting.  This removes
the notification wait as a serialization bottleneck for the worker pool.

The broker now accepts each connection on a worker while the main loop creates
the next listening instance.  The broker smoke gate proves two simultaneous
clients: one allocates a port, the second sends through that shared token, and
the first receives the payload.  Shutdown now sets a shared flag, wakes the
accept loop through an internal pipe request, and joins all workers before
returning.  This proves concurrent bounded broker transport, not yet full
session-owned Mach rights.
The same broker smoke also proves a blocked notification wait on one client is
woken by a signal sent from a second client.

The current cross-client test intentionally uses a broker-wide token to prove
transport only.  Tokens are not yet authenticated or session-scoped, so this
must not be described as Darwin send-right ownership; the next capability
batch must add session identity, explicit transfer, stale-session rejection,
and teardown tests without weakening the existing shared-port transport gate.

### Opaque broker capability tokens

The broker no longer allocates port, notification, or out-of-line mapping
tokens from predictable counters. Each token is generated as a nonzero
64-bit opaque value and collision-checked against the corresponding live
namespace before publication. The broker smoke remains green, including
cross-client send/receive, notification wakeup, OOL handoff, reconnect, and
controlled shutdown. This hardens token guessing only; it does not yet bind a
token to an authenticated client session, provide explicit right transfer, or
revoke a disconnected client's rights. Those are the next capability-family
items.

### Versioned session-handshake foundation

The WIPC envelope is now version 2 and carries an optional session token.
Each broker connection worker supports `SessionOpen` and returns a distinct
nonzero opaque session ID; the broker smoke verifies this path. Existing
zero-token envelopes remain accepted as a compatibility mode so the current
primitive families can migrate incrementally. Nonzero session IDs are now
checked against their originating connection and replay on a second
connection is rejected by the broker smoke. Port and notification ownership
are now enforced for nonzero session tokens, while OOL mappings only inherit
ownership metadata through their port. Explicit capability transfer and full
teardown revocation semantics remain the next implementation batch.

Session-owned port allocation is now implemented as the first resource-level
step: a port allocated with a nonzero session token records that owner, and
Send, Receive, Deallocate, and Destroy reject a missing or foreign session
token. The smoke creates and destroys one authenticated port and keeps the
legacy shared-port transport proof separate. Notification owners are now
recorded for session-created objects and OOL mappings inherit their owning
 port's session metadata. The broker smoke now rejects a foreign session's
 notification signal and allows the owner to destroy it. Complete OOL
 handle-transfer semantics remain pending.

Session teardown is now implemented for the tracked resource families: when a
named-pipe connection disappears, the broker removes that session's ports,
queues, notifications, and OOL mappings and wakes broker waiters. Legacy
token-0 resources remain persistent across reconnects by design. A dedicated
stale-session reconnect rejection is now covered by the broker smoke after
the transport observes the disconnect. Full kernel-equivalent OOL transfer
remains future work.

The broker now implements and tests `CapabilityTransfer`: an authenticated
source session transfers a session-owned port to another active session, after
which the target can send and destroy it. The source no longer owns that port.
This covers broker-level ownership transfer only; Mach right dispositions,
OOL mapping ownership transfer is now covered by the broker smoke as well;
true Windows handle passing, Mach right dispositions, and kernel rights
semantics remain separate gaps. `SessionOpen` also accepts an optional
four-byte client PID; the primary broker smoke exercises this form. It is the
identity prerequisite for future `DuplicateHandle` work, not handle passing
itself.
The broker now validates PID-bearing sessions with `OpenProcess` using the
future duplication/query access mask and rejects inaccessible PIDs; the smoke
covers both a valid current PID and an invalid PID. The broker now duplicates
an OOL mapping handle into a PID-registered target process on Receive and
returns the native handle value in the version-2 envelope; the smoke verifies
and closes that handle after a capability transfer. This is the first real
Windows handle-passing primitive, not yet full Mach VM lifetime semantics.

Remaining broker gaps are request cancellation, complete Mach
rights/dispositions, true kernel-equivalent handle passing and OOL lifetime
semantics, MIG descriptors, full Mach error-code parity, and real unmodified
Darling/Mach-O application execution.

The local C Mach ABI now validates the four supported basic right dispositions
(`MOVE_RECEIVE`, `COPY_SEND`, `MOVE_SEND`, and `MAKE_SEND`) and rejects unknown
dispositions or unknown source names, while retaining the task/thread/host
pseudo-names used by the adapter.  `mach_abi_smoke` covers both accepted and
rejected cases.  This is validation and reference accounting only; it is not
yet Darwin's full per-task right namespace or transfer semantics.

The local ABI now keeps separate receive- and send-reference counters. Right
dispositions update the corresponding counter, `mach_port_type` reports the
available right bits, and `mach_port_get_refs`/`mach_port_mod_refs` select the
requested right. The smoke covers the split accounting; per-task namespaces
and full kernel transfer rules remain open.

Port- and port-set-management calls now reject task names other than the
current Windows process, and `mach_abi_smoke` covers the foreign-task failure
path. This is a process-bound adapter guard, not yet Darwin's full per-task
port-name namespace.

For local ports, `MOVE_RECEIVE` and `MOVE_SEND` now consume the corresponding
right on a real local source port while adding it to the destination; pseudo
task/thread/host sources remain borrowed adapter rights. This narrows the
disposition gap, but does not create Darwin's per-task name space.

The local ABI now also exports `mach_port_extract_right`. It implements the
same four supported dispositions for a process-local name, reports the
extracted name and disposition, updates split receive/send references, and
removes a name when its final right is consumed. The Mach smoke gate covers
copy-send, move-send, output disposition, and stale-name rejection. This is
still an adapter-level namespace and not the kernel's complete right-transfer
or descriptor ABI.

`mach_port_mod_refs` now applies the same final-right cleanup as deallocation:
when the selected reference reaches zero, the port is closed, removed from all
local port sets, removed from the local name map, and blocked waiters are
woken. The Mach smoke gate covers stale lookup and send rejection after this
path. Cross-task namespaces and kernel-equivalent right destruction remain
outside the adapter.

When a local move consumes the final right of a source port, the adapter now
closes and removes that source name from all port sets and wakes waiters; the
Mach smoke covers the stale-source result. This is local adapter lifetime
behavior, not yet the kernel's full per-task port-death machinery.

Local port destruction now marks the backing queue closed, wakes blocked
receivers, removes the name from port sets, and rejects later sends or
receives; `mach_abi_smoke` covers a receiver blocked during forced destroy.
The pseudo task/thread/host names retain their borrowed no-op deallocation
behavior.  This closes the local lifetime race; cross-process broker rights
are still separate and incomplete.

The local `mach_msg` bridge now rejects unknown option bits, zero options, and
send frames whose declared `msgh_size` does not match `send_size`.  Negative
cases are covered by `mach_abi_smoke`; this remains a bounded adapter and does
not implement complex descriptors, vouchers, audit trailers, or MIG.

Receive now checks the destination capacity before dequeuing.  A
`MACH_MSG_TOO_LARGE` result therefore preserves the message for a later retry,
which is covered by the Mach ABI smoke test.

The local C ABI now applies the same 1024-message queue bound as the broker and
returns `darling_mach_send_queue_full` instead of allowing unbounded growth;
the Mach ABI smoke gate fills the queue and verifies the overflow path.

Local inline Mach messages are also bounded at 4 MiB before allocation, matching
the versioned broker envelope limit; an oversized send returns the existing
`MACH_MSG_TOO_LARGE` adapter status.

Local Mach receive now materializes moved port rights while holding the
receive queue lock without recursively locking that same port when a message
contains the receive port itself. Descriptor types are validated before
rights are changed; a failed transfer rolls back already materialized rights
and leaves the queued message available. MSVC, Clang x64, and ARM64 source
compilation succeeded; no runtime fixture was run for this edge case.

Port-set receive no longer sleeps in a fixed one-millisecond polling loop:
local sends and port destruction signal a shared condition variable, so set
waiters wake promptly while preserving the requested deadline.
Set-member removal and set destruction signal the same wait path, closing the
remaining local port-set lifecycle wakeup gap.
`mach_abi_smoke` now also runs a real cross-thread set-wakeup check: a blocked
set receiver is released by a later member send before its 500-ms deadline.

Local `mach_port_deallocate` now decrements one reference and removes the port
only when the last reference is released.  The smoke gate verifies the
intermediate reference count; `mach_port_destroy` explicitly collapses the
local rights and uses the close/wakeup path for forced destruction.
The same smoke gate now also verifies that the final deallocation removes the
name and rejects a subsequent send.
`mach_port_get_refs` and `mach_port_mod_refs` now reject unknown right kinds;
the adapter's Receive-/Send-right constants are the only accepted selectors.

Broker OOL receive now releases the broker's owning mapping handle at transfer
time while an already-open receiver mapping remains valid.  This bounds broker
retention after delivery; true Mach VM protection and cross-process descriptor
rights are still not implemented.

The Mach VM bridge now resolves a non-self task name as a Windows process ID
and uses `OpenProcess` plus `VirtualAllocEx`, `VirtualFreeEx`,
`VirtualProtectEx`, `ReadProcessMemory`, and `WriteProcessMemory`. The
self-process path remains covered by `mach_abi_smoke`; remote COW,
inheritance, wired/purgeable memory, and a separate child-process integration
gate remain open.

`mach_vm_region` now reports the containing Windows memory region through
`VirtualQueryEx` and maps its protection to the supported Darwin basic-info
flavor. Named submaps, shared-region metadata, inheritance details, and the
remaining region flavors are not implemented.

`mach_vm_region_recurse` now delegates the same verified basic flavor with a
zero-depth contract and rejects nonzero submap depth rather than pretending to
support Darwin submaps. This makes the supported boundary explicit while
leaving recursive submap traversal open.

`mach_vm_read` now allocates a local Windows buffer, reads from the selected
task through `ReadProcessMemory`, returns the byte count, and can be released
through the existing `mach_vm_deallocate` path. This covers the basic
out-of-line read shape; exact Darwin VM object ownership and remote lifetime
semantics remain open.

The signal ABI now also implements `sigemptyset`, `sigfillset`, `sigaddset`,
`sigdelset`, and `sigismember` over the existing four-word Darwin signal-set
layout. The signal smoke verifies construction, membership, removal, full-set
population, resolver lookup, and invalid-number rejection. This is still a
process-local adapter; native cross-process delivery, complete kernel masks,
and exact Darwin signal-set ABI edge cases remain open.

The signal-set resolver also accepts the common double-underscore spellings
`__sigemptyset`, `__sigfillset`, `__sigaddset`, `__sigdelset`, and
`__sigismember`; the signal smoke checks representative lookup entries.

The thread-mask ABI now resolves `pthread_sigmask` and its common
double-underscore spelling to the existing thread-local Darwin mask adapter.
This covers the import boundary; full pthread cancellation interaction and
native cross-process signal delivery remain open.

Deferred plain signal handlers now temporarily apply the installed `sigaction`
mask and the signal's default self-blocking rule, then restore the prior
thread-local mask and drain newly unblocked pending signals. This aligns the
local delivery path with the existing sigaction-handler path; native kernel
delivery, reentrancy races, and complete Darwin restart semantics remain open.

The signal smoke now observes the configured `sigaction` mask from inside a
`SA_SIGINFO` handler and verifies that the mask is restored after return.

It also verifies deferred delivery: a blocked SIGTERM is queued, then
delivered after unblocking, with the configured handler mask active during the
callback and the handler invoked exactly once.

The deferred path now also honors `SA_RESETHAND`, matching the existing direct
delivery path by resetting the installed plain handler after its first call.
The smoke test reads the post-delivery disposition and verifies that it is
`SIG_DFL`.

The pthread bridge now provides deferred cancellation through
`pthread_cancel`, `pthread_setcancelstate`, `pthread_setcanceltype`, and
`pthread_testcancel`. The host smoke starts a worker, requests cancellation,
joins it, and verifies `PTHREAD_CANCELED`; asynchronous cancellation is
explicitly rejected because forcibly terminating arbitrary Windows threads is
not a safe Darwin-compatible implementation.

Pending cancellation is now delivered immediately when a worker transitions
from `PTHREAD_CANCEL_DISABLE` back to `PTHREAD_CANCEL_ENABLE`; the host worker
smoke exercises that state transition. The direct `pthread_testcancel` path
now drains the registered cleanup stack before TLS destructors as well. The
implementation still does not provide asynchronous cancellation.

The condition-variable waits now poll deferred cancellation in bounded
intervals, so a waiting pthread can observe `pthread_cancel` without requiring
an unrelated signal. The host smoke also verifies mutex reacquisition before
the cleanup callback runs. Exact Darwin cancellation-point ordering,
exception-free ABI behavior, and timed-wait interruption semantics remain
open.

Blocking pthread mutex and read/write-lock acquisition now uses the same
bounded cancellation-point polling, while uncontended acquisition remains a
single fast `try_lock`. Native priority inheritance, robust/shared locks,
cleanup ordering, and exact Darwin cancellation behavior remain open.

The host API smoke now proves the mutex case with a worker blocked behind a
held mutex, cancellation, canceled join, and orderly mutex cleanup.

The pthread mutex-attribute boundary now exposes attribute objects with type
and process-sharing getters/setters. Normal, recursive, and error-checking
mutexes use the Windows adapter; the host smoke verifies recursive relocking,
error-checking self-deadlock/trylock behavior, and foreign unlock rejection.
Process-shared and robust mutex behavior remains open.

Protocol and robustness mutex attributes now have resolver and default-value
coverage. Non-default priority protocols and robust recovery are rejected with
`ENOTSUP`; this exposes the ABI boundary without claiming owner-death recovery
or priority inheritance.

The condition-variable attribute boundary now exposes opaque attributes with
pshared and clock accessors. The host smoke verifies default and monotonic
clock values, monotonic timed-wait expiry, and explicit `ENOTSUP` for
process-shared conditions; exact Darwin interruption behavior remains open.

The read/write-lock attribute boundary now exposes default-only opaque objects
with pshared accessors. The host smoke verifies default reads and explicit
`ENOTSUP` for process-shared rwlocks; cross-process rwlock semantics remain
open.
Immediate `pthread_rwlock_tryrdlock` and `pthread_rwlock_trywrlock` entry
points are also covered; the host smoke verifies reader coexistence, writer
`EBUSY`, and writer acquisition after release.
Absolute-deadline timed read/write locks now poll with cancellation points and
return `ETIMEDOUT` for expired deadlines; the host smoke verifies this path.

The signal smoke now prints all setup return codes and symbol-group results on
stdout. The installed signal actions and handlers are protected by a shared
state mutex while callbacks execute outside that lock; ten consecutive
isolated runs now complete with `DARWIN_SYSCALL_SIGNAL=PASS`.

It also proves cancellation while a worker is inside `pthread_cond_wait`, then
joins the canceled worker and destroys the condition and mutex normally.

`pthread_join` now uses bounded waits with a deferred cancellation check, so a
worker blocked in a join can observe cancellation. Handle ownership and exact
POSIX join cleanup/reacquire behavior after a canceled join remain open.

The Windows adapter now has an explicit LIFO cleanup-record stack with push/pop
operations, and deferred cancellation drains it before TLS destructors. The
opt-in PureDarwin-shaped macros are exercised by the host smoke. Cancellation
from `pthread_cond_wait` and `pthread_cond_timedwait` is detected, but the
cleanup callback ownership assertion is not yet reliable. A native
exception-free transition is still required to reacquire the mutex before
draining cleanup handlers; full cleanup-handler ordering around every POSIX
cancellation point and exact Darwin cancellation behavior remain open.

The VM read bridge now records returned local buffers and makes
`mach_vm_deallocate` release those buffers in the current Windows process,
even when the read targeted a different PID. This closes the previous
remote-OOL deallocation mismatch; complete Darwin VM object lifetime and
cross-task ownership semantics remain open.

Managed Apple-block copy and release now share the block-runtime registry lock;
a copy cannot resurrect a block after its final release removes it from the
registry. External compiler block layouts and full BlocksRuntime callback and
copy-helper semantics remain outside this adapter.

The weak-reference bridge now also implements `objc_moveWeak`: it removes any
previous destination registration, transfers the tracked weak location, clears
the source, and treats identical source and destination locations as a no-op.
The Objective-C smoke gate covers transfer, source clearing, self-move, and
zeroing after object destruction; atomic replacement and destruction races
remain open.

The move path now also creates the destination registration when the source
slot contains an object but was not previously present in the weak table. This
keeps later object destruction able to zero the moved destination rather than
leaving an externally initialized slot stale.

The final `objc_release` transition now runs under the same runtime lock as
Weak-table access, so `objc_loadWeakRetained` cannot retain an object after its
last release has begun clearing the weak locations. This closes the basic
load-versus-final-release race in the process-local adapter; full lock-free ARC
ordering and object resurrection rules remain outside the implementation.

`objc_copyWeak` now performs source read, destination replacement, and weak
table registration under one runtime lock. Identical source and destination
locations are handled as a no-op, avoiding the previous split-load/store race;
lock-free ARC ordering and full concurrent runtime semantics remain open.

`objc_storeStrong` now performs the slot replacement under the runtime lock and
releases the previous value after leaving it. This gives strong-slot updates a
consistent relationship with the adapter's retain/release and weak-table
operations; lock-free ARC ordering and compiler-specific ownership barriers
remain open.

The ARC bridge now exports `objc_storeStrong`: it retains the incoming object
before replacing the slot and releases the previous value afterward. The
Objective-C smoke gate covers set/clear and resolver lookup. Atomic memory
ordering, weak-reference races, and complete ARC ownership conventions remain
outside this minimal process-local adapter.

The weak-reference bridge now also exports `objc_loadWeakRetained`, loading
and retaining the result under the same runtime lock used by final release.
The Objective-C smoke gate covers the retained load and balances the returned
retain. Lock-free ARC ordering, atomic weak replacement, and full ARC weak
semantics remain open.

The Objective-C ABI now exports `class_replaceMethod`. It replaces an existing
instance-method entry under the runtime lock, returns the previous IMP, keeps
the type encoding updated, and returns null when no previous method exists.
The Objective-C smoke gate verifies replacement, dispatch visibility through a
subclass, resolver lookup, and restoration. Full method-cache invalidation and
concurrent runtime mutation semantics remain open.

Method views now retain their owning class and method kind. `method_setImplementation`
therefore mutates the actual instance- or class-method table, returns the old
IMP, updates the view, and keeps the metaclass mirror synchronized for class
methods. The Objective-C smoke gate covers instance-method mutation and
resolver lookup; full method-cache and concurrent mutation semantics remain
open.

`class_getInstanceSize` now reports a conservative class size from the base
object and known Ivar offsets/type widths, including inherited Ivars, and is
available through the resolver. Unknown aggregate encodings fall back to
pointer width; exact ABI packing, dynamically added-Ivar layout, and object
storage growth remain open.

The minimal Ivar ABI now supports `class_addIvar` before class registration
with Darwin-style log2 alignment, extends instance allocation to the computed
size, and exposes pointer-sized `object_getIvar`/`object_setIvar` access.
The Objective-C smoke gate covers duplicate rejection, registration, storage
round-trip, and resolver lookup. Retain/release ownership, non-pointer Ivar
encodings, dynamic post-registration layout, and exact ABI alignment remain
open.

The framework ABI audit now confirms that all declared prefixed C entry points
in the current CoreFoundation (90), Foundation (7), and CoreGraphics (58)
headers have corresponding source definitions. This is declaration coverage
only; it does not establish full Darwin behavior or framework compatibility.
The exact method and remaining behavioral boundary are recorded in
`docs/FRAMEWORK-ABI-COVERAGE.md`.

CoreFoundation XML serialization now maps the adapter's URL objects to valid
escaped string elements instead of emitting an invalid unsupported marker. The
CoreFoundation smoke gate covers the filesystem URL round-trip shape; native
CFURL property-list callback semantics remain outside this minimal bridge.

`CFRunLoopGetCurrent` now returns a thread-local run-loop object rather than a
process-global singleton. The CoreFoundation smoke gate verifies that a worker
thread receives a distinct loop, can be observed while running, and can be
stopped without changing the caller's loop. One-shot timers now retain the
loop's shared queue state instead of capturing the thread-local wrapper, so a
timer cannot dereference that wrapper after its owner thread exits; cancellation,
timer ownership, modes, sources, observers, and native CFRunLoop scheduling
semantics remain open.

`class_respondsToSelector` now checks the metaclass method chain for ordinary
class objects while retaining instance-method behavior for explicit metaclass
handles. The Objective-C smoke gate covers class-object, metaclass, instance,
and missing-selector queries; method-cache invalidation, swizzling races, and
the complete Apple metaclass ABI remain open.

`CFRunLoopRunInMode` now drains queued blocks until the requested deadline or
an explicit stop when `return_after_source` is false. The CoreFoundation smoke
gate verifies two queued sources are both delivered during one run and that
the run returns by timeout afterward; source ordering beyond the adapter queue,
run-loop modes, cancellation, and native observer/scheduling semantics remain
open.

The process wait family now serializes selection and reaping across `waitpid`,
`wait4`, and `waitid` with one recursive wait mutex. This prevents concurrent
callers from selecting and consuming the same registered child; recursion is
required because the normal `waitid` reap path delegates to `wait4`. Kernel
orphan/zombie ownership, stopped/continued states, and full Darwin wait
concurrency semantics remain outside the Windows registry model.

The individual `kill` bridge now preserves the requested signal number in the
child termination registry; previously an individual SIGTERM was incorrectly
remembered as SIGKILL. The process-syscall smoke test now verifies
`waitid(P_PID)` reports `CLD_KILLED` with `si_status=15` for the emulated
SIGTERM path. This remains controlled `TerminateProcess` emulation, not native
Unix signal delivery or arbitrary signal-handler execution in another process.

The `waitid(WNOWAIT)` observation path now reports the actual selected child
PID for `P_ALL` and `P_PGID` selections by reading it from the retained
Windows process handle, instead of leaking the selector value into `si_pid`.
It also consults the registered termination-signal metadata for that observed
PID, so group/all observations preserve the emulated SIGTERM/SIGKILL detail.
Normal Darwin group wait ordering, concurrent kernel ownership, and stopped or
continued-child events remain open.

The normal (reaping) `waitid` path now previews the ready registered handle for
`P_ALL`/`P_PGID` before delegating to the serialized `wait4` path, preserving
the selected child's emulated SIGTERM/SIGKILL reason after the registry entry
is consumed. The preview is only metadata capture; `wait4` remains the single
reaping operation. Exact kernel selection ordering and stopped/continued event
semantics remain open.

`NSGetSizeAndAlignment` now rejects overflowing array-count parsing and
array-size multiplication instead of wrapping a malformed Objective-C type
encoding into a smaller ABI size. The Foundation smoke gate covers a huge
array-count rejection while valid scalar, pointer, array, struct, union, and
qualified encodings remain passing; exact Apple bit-field packing and private
type encodings remain outside this minimal parser.

Struct and union alignment rounding is now checked for `size_t` overflow as
well, so malformed aggregates cannot wrap during field padding or final size
alignment. The Foundation smoke gate covers both array-count and aggregate
overflow rejection; valid aggregate layout behavior remains unchanged.

CoreGraphics vector normalization now rejects non-finite lengths, and affine
inversion treats a non-finite determinant as non-invertible instead of
returning a falsely valid NaN transform. The CoreGraphics smoke gate covers
both fail-closed cases; full floating-point exception and platform-specific
NaN propagation behavior remain outside this geometry adapter.

The dyld smoke matrix now explicitly contrasts strong and weak undefined
symbols: a strong missing provider export must raise the unresolved-import
error, while a weak undefined symbol remains a zero binding. This strengthens
the loader's fail-closed boundary without treating legitimate weak imports as
fatal; real code-signature, shared-cache, and foreign-CPU execution remain
outside the current loader.

The POSIX time resolver now also accepts the Darwin cancellation-safe aliases
`clock_gettime_nocancel`, `clock_getres_nocancel`, `nanosleep_nocancel`,
`gettimeofday_nocancel`, and `usleep_nocancel`, including the common
double-underscore spellings. They intentionally reuse the already verified
Windows time adapters; cancellation-point interruption and exact Darwin restart
semantics remain open.

`clock_nanosleep` and `clock_nanosleep_nocancel` are now mapped for the
supported wall-clock and monotonic-clock IDs, including relative and absolute
deadlines with checked timespec validation. The host API smoke checks both
resolver names; Windows sleep interruption and exact Darwin cancellation
semantics remain outside this adapter.

The host smoke now executes relative and absolute monotonic `clock_nanosleep`
calls, verifies an already expired absolute deadline returns immediately, and
checks invalid clock rejection. This proves the adapter's current deadline
behavior rather than resolver presence alone.

The same resolver family now covers `sleep_nocancel` and the common `__sleep`
spelling in addition to the existing `sleep` entry point. These aliases retain
the current whole-second Windows behavior and do not claim Darwin interruption
or cancellation semantics.

The runtime README records the Mach-O execution boundary: the Windows x86_64
runner enters compatible x86_64 images after mapping, relocations, bindings,
initializers, and protection setup. The Win32 target now builds and includes an
i386 Darwin-start stack transfer; the guest-entry path has not yet been
validated with a real 32-bit Mach-O fixture. ARM64 instructions still require
a CPU translation layer on x86_64 Windows. This is loader execution evidence,
not proof of full unmodified macOS application support.
The dyld smoke now also executes a separate minimal `__mod_init_func` fixture
and reports `DYLD_MOD_INIT=PASS`; complete ObjC runtime initialization and
full dyld parity remain open.

The signal resolver now also covers common Darwin spellings for
`sigaction_nocancel`, `sigprocmask_nocancel`, `sigwait_nocancel`, and
`sigwaitinfo_nocancel`, `sigpending_nocancel`, and `sigqueue_nocancel`, plus
the double-underscore forms. They reuse the existing Windows signal adapter;
native cross-process signal delivery, full masks, interruption, and cancellation
semantics remain open.

A minimal AppKit slice now resolves NSApplicationMain, reads NSPrincipalClass from the main bundle Info.plist and initializes that NSApplication subclass (falling back to NSApplication when the key is absent), registers host-owned NSObject, NSResponder, NSString, NSConstantString, NSMutableString, NSApplication, NSWindow, NSView, NSControl, NSButton, and NSTextField classes, exposes class symbols and NSApp, implements common NSObject class/instance queries, allocation, equality, hash, description, and autorelease messages, dispatches application will-finish/did-finish callbacks with a nil notification placeholder, and implements window lifecycle/title/content-view methods, CGRect frame/setFrame:display: conversion, and contentSize/setContentSize resizing through Win32 with root content-view geometry updates. The Mach-O loader registers x86_64 `__cfstring` literals before image initializers; `UTF8String` and `length` read byte and UTF-16 constant-string records, while retain/release leave literal storage immutable. Dynamically allocated UTF-8 NSString values support UTF-8/string construction, length, UTF8String, equality, concatenation, prefix/suffix queries, UTF-16 indexed substring selection/from/to operations, and immutable copy. NSMutableString adds `+string`, UTF-8/string construction, `appendString:`, `setString:`, and UTF-16 indexed deletion/replacement/insertion; a runtime finalizer releases its native buffer at final object release. Range methods reject boundaries that split surrogate pairs. Other encodings, mutable-string operations, formatting, and localization remain unsupported. The selected CGRect stack argument and following style/backing/defer register arguments for `initWithContentRect:styleMask:backing:defer:` are decoded; dimensions, primary-screen position, and common style bits map to Win32. Windows own a default NSView; NSView supports frame/bounds initialization and mutation, frameOrigin/frameSize getters and setters, window/superview/subviews queries, hidden-state queries/setter, inherited control visibility, and viewDidHide/viewDidUnhide effective-state callbacks, descendant checks, responder links, nextKeyView/setNextKeyView, NSTextField string/placeholder/editable APIs backed by native Win32 edit controls with guest string synchronization and Return target/action, point conversion to/from another same-window view or window coordinates with frame/bounds axis-aligned scales and NSView isFlipped-driven y transforms, and add/remove hierarchy operations. Win32 button placement composes ancestor frame/bounds origins, axis-aligned scales, and flipped-y transforms to map nested views into window coordinates. NSWindow exposes `firstResponder`, `makeFirstResponder:`, `selectNextKeyView:`, `selectPreviousKeyView:`, and `acceptsFirstResponder`; NSButton and NSTextField accept focus and clicking a button updates the first responder. Hiding or detaching a focused subtree chooses the first visible enabled native control in the content view, falling back to the window. Tab follows visible enabled native-control nextKeyView links with a view-tree fallback; Shift+Tab follows a direct predecessor link with a reverse view-tree fallback; finalizing a view clears nextKeyView links from other views that point to it. Custom-view focus remains unsupported; NSTextField formatting, secure entry, field-editor behavior, validation, and delegate notifications are also unsupported. NSApplication, NSWindow, and NSView inherit the host NSResponder class. NSControl provides target/action, enabled state, stringValue, and integer/floating-point/boolean value getters and setters; NSButton maps to a Win32 push button with title and the `+buttonWithTitle:target:action:` convenience constructor. Its factory's Darwin x86_64 object/object/selector argument encoding is explicitly dispatched and returns an autoreleased instance. Button clicks deliver to an explicit target or, when nil, search parent views, window, application, then the application delegate for the first responder implementing the action selector. Backing/defer behavior is not reproduced. The run loop shows a fallback window only if the delegate has not shown an NSWindow. `objc_msgSend` handles guest `*` C-string returns, selector arguments, and selected CGRect/NSRange arguments; the `objc_msgSend_stret` family handles no-argument CGRect returns, including legacy and Super2 superclass search, such as NSView frame and bounds; typed helpers call guest IMPs with SysV for delegate callbacks, `UTF8String`, window initialization, views, and buttons. General aggregate signatures and the `screen:` variant remain unsupported, as do nib/storyboard loading, real NSNotification objects, rotation coordinate conversion, `convertRect:` APIs, full keyboard and input-method translation, and complete AppKit policy. Window painting now creates a bitmap-backed CoreGraphics context, exposes `NSGraphicsContext.currentContext`/`CGContext`, traverses visible views, calls `drawRect:` with view bounds under composed frame/bounds and flipped-y transforms, clips to each view's bounds, and blits the BGRA buffer through Win32. The base NSView draw method is a no-op. NSColor now supports common named RGB colors, calibrated/device/sRGB four-component constructors, and set/setFill/setStroke into the active CGContext, backed by guest-ABI dispatch for four-double class factories. `redComponent`, `greenComponent`, `blueComponent`, `alphaComponent`, and pointer-output, BOOL-returning `getRed:green:blue:alpha:` are available. Calibrated/device/neutral-white factories and grayscale-only whiteComponent are available. For non-grayscale input whiteComponent returns zero instead of raising Apple color-space exception. Color-space conversion and pattern/dynamic colors remain absent. NSBezierPath stores and replays move/line/cubic-curve/close plus rectangle/oval commands, with empty/rect/oval factories, path/primitive append, point clearing, line-width access, fill/even-odd-fill/stroke, and a CGPoint-triple guest SysV dispatcher; curve-extrema bounds, controlPointBounds, and isEmpty queries are available; rounded-rectangle factories and append support plus butt/round/square line-cap styles for open stroked contours are present but have no dedicated guest drawing fixture; elementCount/elementAtIndex:associatedPoints: enumerate move/line/curve/close elements, expanding rectangles and ovals into their component elements; no dedicated guest drawing fixture covers element ABI behavior. windingRule selects nonzero or even-odd filling for fill; evenOddFill remains explicitly even-odd. NSBezierPath flatness defaults to 0.6 and drives CoreGraphics recursive De Casteljau curve subdivision with device-scale-adjusted point-to-segment tolerance, a depth-12 limit, and a 4096-point contour cap; extreme curves can exceed the requested error bound. Class-level default width/cap/join/miter/flatness/winding attributes are settable and copied into new paths; `+[NSBezierPath fillRect:]` and `strokeRect:` create and render paths using those defaults; `clipRect:` intersects the active graphics-context clip with the specified rectangle; `strokeLineFromPoint:toPoint:` renders a temporary move/line path using the class defaults. `-[NSBezierPath addClip]` intersects the active graphics-context clip with the path using its winding rule and restores the prior context flatness. `currentPoint` tracks move/line/curve/close elements, and relative move/line methods add their offsets to that point. `containsPoint:` performs flatness-tessellated cubic hit-testing over implicitly closed subpaths with nonzero winding and no stroke-width contribution; no guest hit-test fixture exists yet. `appendBezierPathWithArcFromPoint:toPoint:radius:` computes tangent geometry and appends cubic arc segments, reducing the radius for short adjacent segments. Miter/round/bevel joins and configurable miter limits now drive Quartz stroking; CoreGraphics exports line-cap, line-join, and miter-limit setters, and graphics-state save/restore preserves the miter limit. NSBezierPath set/get line-dash APIs now retain positive pattern lengths and phase, repeat odd-length patterns, and pass the pattern to CoreGraphics strokes. Rasterized dash seams and antialiasing remain approximate, and there is no dedicated guest drawing fixture. `setNeedsDisplay:` and `setNeedsDisplayInRect:` now invalidate the corresponding window region, and the damaged portion is mapped into each view for `drawRect:`. Fine-grained old/new exposure tracking, text rendering and broader Quartz/AppKit drawing APIs, and pixel-verified Mach-O drawing fixtures remain absent. MSVC and Clang runner builds link; no Mach-O guest fixture demonstrates these paths. See frameworks/README.md and the AppKit row in PRIMITIVE-MATRIX.csv.

### NSImageView sizing and positioning (2026-11-22)

`NSImageView` now implements `imageScaling`/`setImageScaling:` for proportional-down, independent-axis, no-scaling, and proportional-up/down modes. It also implements all nine standard `imageAlignment` values, defaults to proportional-down and centered, invalidates the window when either property changes, and restores both values from keyed archives. Drawing derives the destination rectangle from the decoded image size and view bounds; the existing view clip clips oversized native-size images. Pixel-level Mach-O guest coverage remains absent. The `imageViewWithImage:` factory allocates a view sized to the image and initializes its image; `isEditable`/`setEditable:` expose the property with a non-editable default. Image drag-and-drop replacement remains unimplemented.


`NSSavePanel` now exposes `directoryURL` and `setDirectoryURL:`. Both the
Windows open-file and save-file dialogs start in the configured directory; a
successful file selection updates the panel to the selected file's parent
folder. Directory-only selection updates it to the selected folder. This
builds on native-dialog integration and remains without a guest Mach-O panel
fixture.

The Windows Objective-C host registers NSArray/NSMutableArray,
NSDictionary/NSMutableDictionary, NSSet/NSMutableSet, and NSData/NSMutableData.
Array literals dispatch
`+arrayWithObjects:count:`; the selected array methods include factories,
count, indexed access, `isEqual:` containment, and mutable add/insert/remove/
replace/clear operations. Dictionary literals dispatch
`+dictionaryWithObjects:forKeys:count:`; selected methods include empty
creation, `initWithObjects:forKeys:count:`, count, key lookup, allKeys/allValues,
and mutable set/remove/clear operations. NSArray also supports
`initWithObjects:count:`.
Set literals dispatch `+setWithObjects:count:` and selected set queries and
mutations use `isEqual:` semantics with unique retained elements.
NSData supports byte-pointer/length construction and access; NSMutableData adds
zero-filled creation, append, resize, and bounded range replacement. `subdataWithRange:`
copies the selected bytes, and `getBytes:length:` copies into caller storage.
Mutable byte pointers can be invalidated by mutation.
The runtime decodes the respective one-pointer/count and two-pointer/count
guest SysV signatures. Collections retain their contents and release them from
Objective-C finalizers. Remaining collection APIs, Foundation-compatible exception behavior, and dedicated guest
fixtures remain open. Clang and MSVC Release runner builds link the implemented
collection slice; no runtime guest behavior was executed in this update.

The Objective-C `NSBundle` bridge now wraps CFBundle with a distinct host object
and lifetime. It supports the main bundle, path/identifier construction,
bundle/executable/resource paths, common Info.plist values, resource path
lookup, localized strings, and load state/load/unload through the existing
Mach-O loader. `infoDictionary` recursively converts common plist strings,
numbers, booleans, dates, nulls, data, arrays, and dictionaries; unknown CF type
conversion remains missing. URL-based construction, bundle URLs, and dynamic
class loading are still missing. No Mach-O app fixture has exercised these
selectors yet.

The Objective-C host also registers a narrow `NSNumber` implementation with
signed/unsigned integer, double, and boolean factories; scalar getters,
`stringValue`, and numeric equality. Common plist integer values retain their
64-bit signedness during conversion. Other NSNumber constructors, conversion
rules, formatting, and complete Foundation comparison behavior remain open.
The same bridge now registers a small `NSDate` implementation for reference-time
creation, retrieval, interval addition, and equality, along with the `NSNull`
singleton. Unix-time convenience methods, date formatting, and broader
calendar behavior remain unsupported.

The Darwin 64-bit Mach header layout is corrected to six 32-bit words: the
voucher field at byte 16 and message ID at byte 20. Compile-time size/offset
assertions protect this ABI layout. `mach_msg` now validates selected remote
and reply dispositions, reserves moved rights as in-transit before local queue
insertion with rollback on failure, and swaps destination/reply header fields
plus disposition bits on local receive. `mach_msg_destroy` now releases
copied-out header rights as well as body descriptors. Voucher disposition and
brokered header-right transfers are rejected as unsupported. Preflight can race
external right mutation. Body MOVE-SEND, MOVE-SEND-ONCE, and MOVE-RECEIVE
descriptors now use transit references too. Queue teardown releases transit
header/body rights, queued OOL storage, and selected copied/created body
rights. Local OOL receive now validates the owned allocation and declared size
before dequeue, instead of interpreting the OOL layout as port descriptors.
Broker capability aliasing and stronger concurrency guarantees remain
required; brokered header-right transfers remain unsupported.

The Objective-C runtime's fixed-arity exports use SysV ABI declarations in x64 Clang builds. objc_msgSend/objc_msgSendSuper/objc_msgSendSuper2 use assembly entry shims and typed aggregate AppKit dispatch plus raw forwarding for scalar-only guest IMP signatures, including mixed GP/XMM registers and bounded overflow stack words. The `objc_msgSend_stret` family handles no-argument CGRect returns, including legacy and Super2 superclass search, through the x86_64 hidden result buffer. Aggregate classification and returns, variadic IMPs, blocks, and exception behavior remain incomplete; see abi/README.md and the objc-guest-abi matrix row.

The Windows ARM64 Clang path now maps guest imports of `objc_msgSend`,
`objc_msgSendSuper`, and `objc_msgSendSuper2` to ARM64 assembly entry shims.
These preserve the native Windows C wrappers and pass Apple's variadic stack
area to a typed reader; Super2 begins lookup at `objc_super.super_class`'s
superclass. Their shared assembly return path restores `x0-x1` and `d0-d3`;
the typed dispatcher currently covers scalar float/double, CGPoint/CGSize/
CGRect HFA, and NSRange returns. Direct Clang probes confirm the register
classes match Windows ARM64 for these signatures. The Objective-C dispatcher,
Win32 symbol resolver, and assembly object compile to ARM64 COFF, but there is
still no linked ARM64 runner or Mach-O guest execution evidence. Unsupported
method encodings, broader
aggregate/overflow cases, and red-zone behavior remain open. See the ARM64
dispatch matrix in `docs/porting/MATHEMATICAL-MODEL.md` and `abi/README.md`.

### Classic x86_64 lazy binding at first call (2026-11-23)

For images without chained fixups, bootstrap now preserves lazy bind actions
and lazy indirect-symbol slots instead of applying them at startup. Persistent
per-image sessions use the stub-helper image-cache address and lazy-stream
offset to select and resolve the import on first call. The session registry
also supports guest calls made from another host thread. ARM64, 32-bit and
chained-fixup images remain eager, as does dynamic loading. The x64 executable
and ARM64 host library build successfully. No guest fixture or runtime test
was run, so the actual Apple stub-helper-to-slot path remains unverified.

### Trampoline extended-state preservation (2026-11-23)

The x86_64 binder trampoline now detects XSAVE and OSXSAVE, reads the enabled
XCR0 state, allocates an aligned save area, and uses XSAVE/XRSTOR around the
resolver. Older x64 hosts use FXSAVE/FXRSTOR. The x64 Clang executable builds;
no runtime fixture exercised either state-save branch.

### Dynamic-image lazy binding (2026-11-23)

Classic x86_64 images loaded through `dlopen` now leave lazy bind actions and
lazy pointer slots unresolved and register per-image sessions for the lifetime
of the dynamic image. Registrations are thread-neutral and are removed during
`dlclose` after terminators run. ARM64, 32-bit, and chained-fixup images remain
eager. The x64 build succeeds; no runtime `dlopen` lazy-import fixture has
verified initializer calls, cross-thread first calls, or unload behavior.

### Resolve every action in one lazy-bind sequence (2026-11-23)

`ResolveForLazyInfo` now applies each parsed lazy-bind action associated with
the selected stream offset and returns the final resolved target. This follows
dyld's compressed lazy binder, which walks the sequence through `DONE`. The x64
executable and ARM64 host library compile; no runtime lazy-sequence fixture was
run.

### ARM64 threaded-bind chain bounds (2026-11-23)

Threaded-bind chain traversal is now bounded by the remaining pointer capacity
of its segment, rather than by the number of unique entries in the ordinal
table. This permits chains that reuse imports across more slots than there are
distinct bind targets, while retaining per-node segment bounds and requiring
the chain to terminate within the segment. The x64 executable and ARM64 host
library compile; no guest Mach-O fixture exercised threaded-bind chains.

### Checked dyld bind and rebase offset arithmetic (2026-11-23)

Classic bind, weak/lazy bind, and rebase bytecode parsing now checks every
segment-offset addition and the rebase `skip * count` product for `uint64_t`
overflow before storing actions. The existing per-action VM/file segment
bounds checks still validate the resulting addresses when fixups are applied.
The x64 executable and ARM64 host library compile; no malformed Mach-O fixture
has exercised the new overflow rejection paths.

### Classic 32-bit absolute text rebases (2026-11-23)

The classic rebase applier now supports `REBASE_TYPE_TEXT_ABSOLUTE32`: it reads
and writes a 32-bit target, adds the image slide with explicit range checks,
and temporarily changes protection while restoring the segment's original
protection afterward. `REBASE_TYPE_TEXT_PCREL32` remains rejected because the
classic dyld implementation does not define an apply operation for it. The x64
executable and ARM64 host library compile; no Mach-O fixture exercised this
fixup path.

### Bitness-aware classic rebase streams (2026-11-23)

Classic rebase opcode strides and scaled-address operations now use the
Mach-O image's pointer width. Rebase runs are checked against their segment's
available slots before action allocation, and the per-run limit is raised from
4096 to 1,048,576 entries (matching the bind-stream repeat ceiling). This
removes the previous low ceiling for large images while retaining segment and
resource bounds. Both Windows targets compile; no large or malformed Mach-O
fixture exercised the parser.

### Larger ARM64 threaded-bind ordinal tables (2026-11-23)

The classic threaded-bind ordinal-table limit is raised from 4096 to
1,048,576 entries, matching the guarded bind/rebase repeat ceiling. This avoids
rejecting large images solely because they have many threaded imports while
retaining a bounded allocation. Both Windows targets compile; no guest image
with a large threaded-bind table has been run.

### Chained-fixup multi-start pages (2026-11-23)

Chained-fixup page-start decoding now follows the selected segment-start table
base and supports `START_MULTI` pages by walking the bounded `chain_starts`
list through its `START_LAST` entry. Each chain is constrained to its page and
uses the pointer-format stride; supported page sizes are 4 KiB and 16 KiB.
This also avoids 32-bit overflow while calculating the segment-start location.
Both Windows targets compile; no multi-start Mach-O fixture has exercised the
new path.

### ARM64e chained-fixup field decoding (2026-11-23)

Unauthenticated ARM64e bind addends now sign-extend their 19-bit field by
subtracting `2^19` when the sign bit is set. Authenticated layouts no longer
interpret diversity/key metadata as an addend; authenticated rebase targets
are decoded from their 32-bit runtime-offset field. Authenticated pointer
application itself still requires a PAC backend. Both Windows targets compile;
no encoded ARM64e fixup fixture has exercised these fields.

### Chained offset rebases use the image text base (2026-11-23)

`DYLD_CHAINED_PTR_64_OFFSET` and ARM64e offset formats now add their encoded VM
offset to the runtime `__TEXT` address, rather than whichever segment happened
to be first in the mapped list. This handles ordinary executables whose first
segment is `__PAGEZERO`. Mapping also leaves empty no-access segments reserved
but uncommitted, avoiding an unnecessary commit of the large `__PAGEZERO`
range. Both Windows targets compile; no real signed Mach-O app fixture has
verified the mapping path.

### Weak-bind addends and checked symbol arithmetic (2026-11-23)

Classic regular/lazy/threaded binds and chained imports now preserve the
encoded addend even when a weak symbol is unresolved, applying the relocation
expression `S + A` with `S = 0`. Signed addends use checked address addition or
subtraction so malformed values cannot invoke signed-overflow behavior. Both
Windows targets compile; weak-import addend fixtures have not been run.

### 64-bit chained rebase high-byte semantics (2026-11-23)

`DYLD_CHAINED_PTR_64` rebases now apply the slide to the low 56-bit target and
restore the encoded `high8` byte afterward, as specified by dyld. Offset-based
formats continue to include `high8` in the encoded target before adding the
image base/slide. Both Windows targets compile; no tagged-pointer Mach-O fixture
has exercised the high-byte cases.

### x86_64 implicit relocation addends and GOT slots (2026-11-23)

The x86_64 relocation engine now reads the implicit addend from the original
field contents and retains it across repeat relocation passes. Absolute and
SUBTRACTOR forms use the signed 64-bit field; PC-relative, branch, and TLV
forms use the signed 32-bit field with their existing relocation-specific
PC bias. `GOT_LOAD` and `GOT` PC-relative forms resolve the symbol's indirect
pointer slot, preferring a non-lazy slot when one exists. The runtime expression
is now `S + A` for absolute forms and `S + A - (P + bias)` for PC-relative
forms; GOT forms substitute the slot address for `S`. The Windows x64 loader
build succeeds. There is no dedicated Mach-O fixture for implicit addends or
GOT relocation execution yet.

### x86_64 cross-image branch thunks (2026-11-23)

Each mapped image now reserves a 64 KiB near-image thunk pool. When an external
`X86_64_RELOC_BRANCH` target is outside the signed 32-bit displacement range,
the loader emits a 14-byte `jmp [RIP+0]` trampoline containing the absolute
64-bit target and patches the call to the nearby trampoline. Identical targets
reuse one thunk; pages become executable-read only after the bytes are written.
This addresses cross-image function branches while leaving data-address forms
on their existing checked/deferred path, since code trampolines cannot preserve
data-reference semantics. The Windows x64 build succeeds; no far-branch Mach-O
fixture has verified the generated thunk at runtime.

### Shared near window for dynamic x86_64 images (2026-11-23)

Provider placement now uses the main image's `__TEXT` address, avoiding the
multi-gigabyte `__PAGEZERO` anchor in common executables. Each provider's full
reservation is constrained to ±`0x3f000000` from that anchor. Therefore the
maximum address delta between any two provider reservations is `0x7e000000`,
less than the signed PC-relative maximum `0x7fffffff`; their cross-image data
and branch references fit the 32-bit displacement when Windows finds space in
the window. If the window is unavailable, fallback mapping remains possible
and out-of-range non-branch relocations still fail or defer according to the
existing path. The Windows x64 build succeeds; no multi-dylib distance fixture
has verified runtime placement yet.

### Compressed chained-fixup import names (2026-11-23)

Chained-fixup `symbols_format=1` now inflates its zlib/RFC-1951 string pool
before resolving import-name offsets. The decoder validates the zlib header,
DEFLATE block/Huffman structure, back-reference bounds, and Adler-32 trailer;
the expanded pool is capped at 256 MiB. Format 0 remains zero-copy, and unknown
symbol formats fail explicitly. The Windows x64 build succeeds; compressed
Mach-O import fixtures have not exercised the decoder yet.

### 32-bit userland chained fixups (2026-11-23)

The loader now parses `DYLD_CHAINED_PTR_32` for 32-bit Mach-O images using its
4-byte pointer width, 5-bit next field, 20-bit import ordinal, and 6-bit inline
addend. Rebase targets receive the image slide; tagged non-pointer values are
restored by subtracting dyld's `(64 MiB + max_valid_pointer) / 2` bias. The
fixup writer uses the 32-bit absolute path and rejects width/image mismatches.
The Windows x64 build compiles this parser path, but it cannot execute 32-bit
Mach-O on its 64-bit host. The Win32 host library and runner now build, and the
Win32 path contains a native i386 Darwin-start stack transfer. Runtime execution
of a 32-bit chained-fixup Mach-O has not yet been demonstrated; metadata and
fixup parsing alone are not guest execution evidence.
