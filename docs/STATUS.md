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
the receiver opens and verifies it after `Receive`. This is a Windows shared
mapping approximation, not yet Mach handle passing, protection transfer, or
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
