# Wintosh current status

Status date: 2026-10-10

## Evidence summary

| Area | Current state | Evidence / limitation |
|---|---|---|
| Native Windows source | present | `darling-source/src/native/windows` |
| x64 Foundation type encoding | verified | `darling_windows_foundation_smoke.exe` reports `FOUNDATION_TYPE_ENCODING_ABI=PASS` |
| Win32 Foundation type encoding | verified | same smoke gate on Win32 |
| Low-level primitive families | verified in selected batches | tracked in `PRIMITIVE-MATRIX.csv`; not equivalent to full Darwin semantics |
| Full generated `ALL_BUILD` | not currently confirmed | aggregate orchestration can stall without active compiler output; every configured Release target is built and exercised target-by-target |
| WSL runtime backend | unavailable in the current host evidence | probe reported `Wsl/E_ACCESSDENIED` |
| Real Darling server / arbitrary Mach-O app | not proven | no local end-to-end application run |
| Darling component licenses | incomplete review | 150 external components; 147 remain review-required |
| Corresponding source completeness | exception remains | Heimdal filename cannot be materialized on NTFS |

## Current local integration evidence

The complete configured Release smoke suite now builds target-by-target and
passes: **39/39 CTest tests**. The Visual
Studio aggregate build target can
still stall during orchestration, so the reproducible fallback is to build
the generated targets individually; this does not represent a test failure.

The Win32 build now fails closed for x64-only Windows `CONTEXT` register
flavors and the Win32 Mach ABI smoke reports that boundary explicitly; the
remaining 32-bit metadata and host primitive tests still pass. CI runs the
complete suite serially because filesystem, path and ConPTY fixtures share
process-global Windows state.

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
The local Mach C ABI additionally supports one port-death notification
registration per local port, replacement reporting, explicit unregister, and
delivery on forced destruction; its smoke coverage is limited to that adapter
contract, while
full no-senders/dead-name rights and Darwin notification message layouts
remain open.
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

The runtime README now records the actual Mach-O execution boundary: the
Windows x86_64 runner can enter compatible x86_64 images after mapping,
relocations, bindings, initializers, and protection setup, while i386 and
ARM64 images remain rejected without a CPU translation layer. This is loader
execution evidence, not proof of full unmodified macOS application support.
The dyld smoke now also executes a separate minimal `__mod_init_func` fixture
and reports `DYLD_MOD_INIT=PASS`; complete ObjC runtime initialization and
full dyld parity remain open.

The signal resolver now also covers common Darwin spellings for
`sigaction_nocancel`, `sigprocmask_nocancel`, `sigwait_nocancel`, and
`sigwaitinfo_nocancel`, `sigpending_nocancel`, and `sigqueue_nocancel`, plus
the double-underscore forms. They reuse the existing Windows signal adapter;
native cross-process signal delivery, full masks, interruption, and cancellation
semantics remain open.
