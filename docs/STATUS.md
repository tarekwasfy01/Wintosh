# Wintosh current status

Status date: 2026-10-09

## Evidence summary

| Area | Current state | Evidence / limitation |
|---|---|---|
| Native Windows source | present | `darling-source/src/native/windows` |
| x64 Foundation type encoding | verified | `darling_windows_foundation_smoke.exe` reports `FOUNDATION_TYPE_ENCODING_ABI=PASS` |
| Win32 Foundation type encoding | verified | same smoke gate on Win32 |
| Low-level primitive families | verified in selected batches | tracked in `PRIMITIVE-MATRIX.csv`; not equivalent to full Darwin semantics |
| Full generated `ALL_BUILD` | not currently confirmed | previous environment invocation hung without active compiler output and was stopped |
| WSL runtime backend | unavailable in the current host evidence | probe reported `Wsl/E_ACCESSDENIED` |
| Real Darling server / arbitrary Mach-O app | not proven | no local end-to-end application run |
| Darling component licenses | incomplete review | 150 external components; 147 remain review-required |
| Corresponding source completeness | exception remains | Heimdal filename cannot be materialized on NTFS |

## Current local integration evidence

The complete configured Release smoke suite now builds target-by-target and
passes: **38/38 CTest tests**. Three consecutive parallel CTest runs also
passed after correcting the ObjC smoke's autorelease ownership. The Visual
Studio aggregate build target can
still stall during orchestration, so the reproducible fallback is to build
the generated targets individually; this does not represent a test failure.

The first cross-process Mach IPC slice is now implemented as a version-1
`WIPC` envelope codec. It validates bounded little-endian operation, request,
port-token, disposition-count, and payload fields. The runtime smoke test
reports `MACH_IPC_ENVELOPE=PASS` and `MACH_IPC_ENVELOPE_VALIDATION=PASS`.
This is only the transport-neutral message format; broker-backed port
allocation and lookup, cross-process send/receive, dispositions, waiters,
notifications, cancellation, MIG descriptors, out-of-line memory, and
Mach-compatible error semantics remain unimplemented.

The broker now consumes the same envelope over a Windows named pipe for the
first real cross-process operation: `Allocate` creates a broker-owned port
token and `Deallocate` releases it. The separate-process smoke test passes
`BROKER_MACH_ALLOCATE` and clean broker shutdown. Cross-process message
delivery (`Send`/`Receive`) now has a bounded broker FIFO for allocated tokens,
covered by `BROKER_MACH_SEND_RECEIVE=PASS`. Rights/dispositions,
notifications, waiters, MIG, out-of-line memory, and a Mach-compatible
name-space remain open. The lifecycle smoke also proves
`BROKER_MACH_LIFECYCLE=PASS`: a deallocated token is rejected on later use.
An empty nonblocking receive now returns the explicit
`MACH_RECEIVE_WOULD_BLOCK` result (`BROKER_MACH_EMPTY_RECEIVE=PASS`), rather
than being conflated with malformed input. A blocking waiter/timeout and
multi-client transport are still unimplemented. The named-pipe frame limit is
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
The broker explicitly rejects disposition counts above one with
`BROKER_MACH_DISPOSITION_LIMIT=PASS`; multiple rights/dispositions remain
unimplemented rather than being silently collapsed.
The runtime now also provides named notification events with signal/reset and
timed wait behavior; `MACH_IPC_NOTIFICATION=PASS` verifies the host primitive.
The broker now routes `NotificationCreate`, `NotificationSignal`,
`NotificationReset`, and `NotificationWait`; the cross-process smoke covers
timeout and signal behavior as `BROKER_MACH_NOTIFICATION=PASS`. Full Mach
notification-port rights and delivery semantics remain open. Explicit
`NotificationDestroy` cleanup and stale-token rejection are covered by
`BROKER_MACH_NOTIFICATION_DESTROY=PASS`; broker reset followed by timeout is
also covered by `BROKER_MACH_NOTIFICATION_RESET=PASS`.
Malformed notification wait payloads are rejected and covered by
`BROKER_MACH_NOTIFICATION_VALIDATION=PASS`.
The broker also accepts the explicit `Destroy` operation, releasing the
port and rejecting later use (`BROKER_MACH_DESTROY=PASS`).
Each port FIFO is bounded to 1024 queued messages and reports
`MACH_SEND_QUEUE_FULL` instead of allowing unbounded broker memory growth;
the boundary is exercised by the broker smoke as
`BROKER_MACH_QUEUE_LIMIT=PASS`.
The named-pipe transport now reserves up to eight server instances, preparing
the endpoint for concurrent client workers; the current broker loop still
serves one connected client and does not yet claim multi-client semantics.
The shared broker state is now protected by a mutex at the request boundary,
so the existing port/OOL/notification tables have an explicit synchronization
contract for the upcoming worker split.

The platform path adapter now provides lexical `AbsolutePath` resolution and
handle-based `CanonicalPath` resolution through Windows final-name lookup,
with an explicit absolute-path fallback when the host denies that query.
Filesystem smoke coverage also exercises stat/lstat, timestamps, symlinks,
hard links, vector I/O, locks, and filesystem enumeration. ACLs, xattrs,
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
dictionaries, plus binary data/date, IEEE-754 real-number, and UID primitives,
including UTF-8 string byte extraction and UTF-16-unit string length for valid UTF-8 (including surrogate-pair accounting) and common-Unicode-whitespace trimming plus mutable append/replace-all/range-replace, mutable data byte access/append/replace/resize/clear, array append/append-array/insert/remove/remove-range/replace, dictionary duplicate-key collapse/set/remove/merge, numeric cross-type equality, set add/remove/union/intersection/subtract operations, date and number comparison plus interval creation/difference, and named/wildcard local notifications with global removal. Unicode collation, normalization, malformed-UTF-8 recovery policy, complex
numeric/time semantics, and complete ownership semantics remain open.

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
The Objective-C bridge now also supports separate lazy metaclass objects for
registered classes, class-method lookup through those metaclasses, and runtime
`class_addProperty` registration
with serialized attribute pairs and verified lookup. The machine-readable matrix records the same coverage for LP64, blocks,
and metaclass mutation. The DYLD resolver now combines caller-provided
`@rpath` roots with embedded `LC_RPATH` entries even when the caller already
provided search paths; `DYLD_COMBINED_RPATH=PASS` covers this behavior.
complex numbers, quoted aggregates, and nested pointers.

CoreGraphics now has a separate partial geometry/color adapter with the
`coregraphics_smoke` gate. It does not provide drawing contexts, paths, image
decoding, text rendering, events, or AppKit.
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
| Host `_getenv` binding and `envp` | pass | `wintosh_runner_env_smoke=0` |
| Dylib graph, RPATH, bind/rebase/chained paths | pass | `wintosh_dyld_smoke=0` |
| Objective-C registry/runtime | pass | `wintosh_objc_smoke=0`, `wintosh_objc_runtime_smoke=0` |
| Memory/process/thread/sync/socket/time families | pass | selected smoke targets all returned zero |
| Filesystem family | pass with environmental skip | hardlink check reports `ACCESS_DENIED` when Windows rights are unavailable |

These are native adapter and synthetic Mach-O gates. They do not prove that an
unmodified third-party macOS executable or the full Darling server runs.

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

The broker no longer terminates its Mach namespace when the current named-pipe
client disconnects.  It recreates the listening pipe instance and accepts a
subsequent client while retaining allocated port tokens, queued messages,
out-of-line shared-memory regions, and notification objects.  This closes the
previous single-connection lifetime hole; it is deliberately a sequential
reconnect path, not yet a concurrent multi-client dispatcher.

Current verification:

| Gate | Result | Evidence |
|---|---|---|
| Broker Release rebuild | pass | `wintosh_broker` and `wintosh_darling_windows_broker_smoke` built in `build-windows-current` |
| Broker smoke | pass | CTest `darling_windows_broker_smoke`, 1/1 |
| Reconnect smoke | pass | broker smoke explicitly destroys the first client, reconnects, verifies `PING/PONG`, then shuts down |
| Full configured Release suite | pass | CTest Release suite, 38/38 |

The broker now also accepts a four-byte millisecond timeout on `Receive` and
waits on a condition variable until a message arrives, the port is destroyed,
or the timeout expires.  An empty payload preserves the nonblocking probe.
The smoke gate covers the timeout-expiry path.
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
the notification wait as a serialization bottleneck for the planned worker
pool; the broker still uses a single connection loop, so concurrent client
execution is not yet proven.

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

Remaining broker gaps are concurrent client workers, a cross-client wakeup
test for a blocked receiver, complete Mach rights/dispositions, true kernel
handle passing, and real unmodified Darling/Mach-O application execution.

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
receives.  The pseudo task/thread/host names retain their borrowed no-op
deallocation behavior.  This closes the local lifetime race; cross-process
broker rights are still separate and incomplete.

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

Port-set receive no longer sleeps in a fixed one-millisecond polling loop:
local sends and port destruction signal a shared condition variable, so set
waiters wake promptly while preserving the requested deadline.
Set-member removal and set destruction signal the same wait path, closing the
remaining local port-set lifecycle wakeup gap.
`mach_abi_smoke` now also runs a real cross-thread set-wakeup check: a blocked
set receiver is released by a later member send before its 500-ms deadline.

Local `mach_port_deallocate` now decrements one reference and removes the port
only when the last reference is released.  The smoke gate verifies the
intermediate reference count; `mach_port_destroy` still shares the adapter's
current deallocation path and needs a separate forced-destroy implementation.
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

The runtime README now records the actual Mach-O execution boundary: the
Windows x86_64 runner can enter compatible x86_64 images after mapping,
relocations, bindings, initializers, and protection setup, while i386 and
ARM64 images remain rejected without a CPU translation layer. This is loader
execution evidence, not proof of full unmodified macOS application support.
