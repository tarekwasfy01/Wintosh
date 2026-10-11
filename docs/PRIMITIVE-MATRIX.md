# Darling Windows primitive matrix

This matrix is the control plane for the incremental port. Each family is
mapped through three deliberately separate layers:

`Darling surface -> Linux/WSL reference -> Windows backend`

Linux or WSL is used to observe POSIX/Darwin-adjacent contracts, error cases,
calling conventions and test vectors. It is not treated as proof that Linux
implements macOS semantics. Windows code is implemented only in the native
adapter layer, with the corresponding source and license provenance retained.

## Batch policy

1. Group symbols by primitive family and ABI shape.
2. Record the Linux/WSL analogue and its limitations.
3. Implement the Win32/Windows adapter as one batch.
4. Add x64 and Win32 smoke coverage.
5. Mark a row `verified` only after build and runtime evidence on both targets.
6. Keep Darwin-only or Apple-framework behavior explicitly `missing` instead
   of silently mapping it to an unrelated Windows behavior.

The machine-readable source is [PRIMITIVE-MATRIX.csv](../PRIMITIVE-MATRIX.csv).
The consolidated cross-source extraction is [SOURCE-EXTRACTION-MATRIX.md](SOURCE-EXTRACTION-MATRIX.md).

The Windows host adapter exposes a tested Darwin `rename` primitive through
`DarwinFilesystem::Rename` and the public `darling_windows_rename` ABI. It uses
`MoveFileExW` with replacement semantics and a narrowly scoped copy/delete
fallback for Windows volumes that reject the write-through move. Atomicity is
guaranteed only on the native move path; the fallback remains an explicit
emulation boundary.
The repository-wide navigation map is [DOCUMENTATION-INDEX.md](DOCUMENTATION-INDEX.md),
and the current evidence snapshot is [STATUS.md](STATUS.md).
Current verified low-level families include libc/file descriptors, sockets,
process and time primitives, pthread synchronization/TLS, Mach-O loading, and
the process-local Mach C ABI including basic VM and port-type operations. This
matrix does not claim that ordinary macOS applications run yet.

The focused `socket_smoke` independently verifies five consecutive TCP
listener/client/accept/payload-I/O runs, including keepalive, Darwin
`SO_NOSIGPIPE`, the `SO_REUSEPORT` mapping, descriptor duplication, and the
nonblocking descriptor flag. It also transfers a payload through the Darwin
`darling_msghdr` ABI using `sendmsg`/`recvmsg`. The larger host smoke still
exercises additional exception-data and resolver combinations; the focused test
also verifies IPv6 `inet_pton`/`inet_ntop`. Those remaining combinations stay
separate coverage. The specialized `syscalls_smoke` additionally exercises
`SCM_RIGHTS` descriptor transfer and malformed-control rejection in five
successful runs. The native receive path translates Winsock's `MSG_PARTIAL`
result into Darwin's `MSG_TRUNC` bit instead of exposing a Winsock bit value to
the guest ABI. Socket type modifiers `SOCK_NONBLOCK` and `SOCK_CLOEXEC` are
also removed before the WinSock call; the former sets the native nonblocking
mode and the latter is retained in the descriptor flag model. Full Darwin
message-flag, alignment, truncation, and descriptor lifetime parity remains
open. `MSG_WAITALL` now has a native Windows fallback that explicitly drains
the requested `WSABUF` span when Winsock does not honor the flag contract;
addressed receive variants and full interruption parity remain open.
The same type modifiers are now applied to both ends of the native `socketpair`
adapter and covered by `syscalls_smoke`.

The native `MachMessage` runtime boundary now converts inline and OOL payloads
to and from the broker's `MachIpcEnvelope`; `runtime_smoke` verifies both
directions. `SendMachIpcEnvelope` now performs the corresponding binary
request/response exchange over the native Named Pipe transport and is covered
by `MACH_IPC_RPC=PASS`. This is the operational serialization bridge, not yet
automatic routing of the local C `mach_msg` entry point through the broker
session.

The Mach message ABI also exposes `darling_windows_mach_msg_destroy` for
disposing received port descriptors and locally-owned out-of-line payloads.
Malformed descriptor layouts fail closed. Distributed right accounting,
notification-port delivery, and complete Darwin trailer semantics remain open.
The dedicated `mach_destroy_smoke` test verifies receive-right disposal on both
x64 and Win32.
The companion `mach_destroy_ool_smoke` test verifies OOL receive ownership and
rejects a second release on both targets.
The `mach_too_large_smoke` test verifies that `MACH_MSG_TOO_LARGE` preserves
the queued message for a subsequent receive on both targets.
The native C ABI now also exposes non-destructive `mach_port_peek` for local
ports, returning the queued message size/id and the requested null, sequence,
sender, audit, or context trailer; `mach_peek_smoke` proves that a peek does
not consume the message. The broker now has a versioned `Peek` operation that
returns the same metadata without consuming its queue; the broker smoke now
verifies this end to end on x64 and Win32. Broker-side trailer byte
materialization is implemented and covered for the sequence, sender, audit,
and aligned context layouts on x64 and Win32; the context field is serialized
at its Darwin-compatible 8-byte-aligned offset. Broker PID/TID fields are
Windows host metadata, while complete Darwin credentials and peek error parity
remain open.
The regular broker-backed `mach_msg` receive path still appends trailers in the
client ABI after transport; a focused x64 follow-up exposed an unresolved
interaction with the subsequent port-descriptor receive sequence, so full
receive-trailer plus descriptor/OOL ordering is not yet marked verified.
Broker error replies are now classified before envelope decoding and map the
known receive-would-block, dead-port, cancellation, and send-queue-full cases
to explicit Darwin-facing return values; unknown transport errors remain
fail-closed.
`MACH_RCV_LARGE` and `MACH_RCV_LARGE_IDENTITY` are now accepted at the option
boundary and retain the queue-preserving Too-Large behavior; Darwin's complete
large-message identity payload remains open.
The dynamic host-symbol table now resolves both `mach_msg_destroy` spellings,
so Mach-O imports can reach the destruction primitive through the loader.
It also resolves both `mach_port_peek` spellings, so the new non-destructive
peek primitive is reachable through Mach-O symbol binding.

`OpenMachIpcSession` now performs the broker session handshake, including the
current-process validation payload, and the broker smoke uses this helper for
the authenticated session path. This makes session establishment reusable by
the future C-ABI routing layer. `MachIpcCapabilityTable` now provides the
thread-safe local-name to broker-token/session-token mapping primitive and is
covered by `MACH_IPC_CAPABILITY_TABLE=PASS`; automatic population from Mach
port allocation and automatic use by `mach_msg` are still separate steps.
Broker receive envelopes carrying a duplicated native mapping handle can now
be materialized back into OOL bytes through `MachIpcSharedMemory`; the runtime
smoke covers handle mapping and closure. Descriptor rewriting in the C ABI is
still a separate integration step. OOL payload-size mismatches and missing
payload/handle combinations are rejected before mapping.

The process row also covers the four-word Darwin signal-set constructors and
membership operations, verified by `signals_smoke`; this does not establish
native cross-process signal delivery or complete Darwin signal semantics.

`pthread_sigmask` resolves to the same thread-local mask backend and is
verified as an import boundary by the signal smoke test; cancellation and
cross-process delivery remain separate gaps.

The pthread family now includes a verified deferred-cancellation path using
`pthread_cancel`, `pthread_setcancelstate`, `pthread_setcanceltype`, and
`pthread_testcancel`. Async cancellation is intentionally not claimed.

Thread naming now keeps names by pthread handle as well as for the calling
thread. The host smoke verifies setting a worker name and reading it back from
the parent after join; unknown handles and invalid buffers remain `EINVAL`.
`pthread_threadid_np` likewise resolves a live managed pthread handle to its
native Windows thread ID; invalid or no-longer-managed handles remain
`EINVAL`.
Darling's `pthread_mach_thread_np` is exposed as the value-returning companion
for callers such as CoreAudio that pass the thread into Mach policy APIs; it
returns the same native ID and `0` for an invalid handle.
Handle-scoped names are removed when the thread is joined or detached, so the
adapter does not retain stale pthread identity state.

The pthread attribute adapter now also exposes the WSL/winpthreads
`inheritsched`, `scope`, and `stackaddr` getters/setters plus the combined
`stack` accessors. The values are
validated and retained; system scope is accepted, alternate scope and a
caller-supplied stack address return `ENOTSUP` because `CreateThread` owns the
Windows stack, and the stored address is never used as an execution stack.
The matching `schedpolicy` and `schedparam` ABI is also present with default
policy/priority storage; non-default scheduling requests return `ENOTSUP` until
a Windows scheduling-policy mapping is implemented.

The CoreAudio-facing Mach thread-policy boundary now resolves
`thread_policy_set`: extended policy maps to normal Windows timeslice priority,
precedence policy maps/clamps to `SetThreadPriority`, and time-constraint
policy deliberately returns `KERN_NOT_SUPPORTED`. The Mach smoke covers both
the working precedence path and the explicit unsupported boundary; this is not
Darwin real-time scheduling equivalence.

The inspection-oriented thread-info flavors are now available as well:
`THREAD_BASIC_INFO` returns Windows thread user/system CPU time in the exact
Darwin `time_value_t` shape, and `THREAD_IDENTIFIER_INFO` returns the native
Windows thread identifier as the stable adapter identity. The implementation
validates the caller-provided natural-word count and rejects unsupported
flavors instead of fabricating Darwin data. `THREAD_EXTENDED_INFO` additionally
returns nanosecond-scaled Windows CPU times and current Windows priority; the
Darwin scheduler telemetry remains defaulted. The process-local thread-name
bridge now reads the existing `pthread_setname_np` registry into the extended
info name field and is covered by the Mach smoke. The
scheduler-specific info flavors remain open. Layouts and flavor/count values
were checked against Apple's published XNU `mach/thread_info.h`; Wintosh does
not copy that header into the repository.

The legacy scheduler-info flavors `THREAD_SCHED_TIMESHARE_INFO`,
`THREAD_SCHED_RR_INFO`, and `THREAD_SCHED_FIFO_INFO` now accept their exact
Darwin-sized result buffers and expose the current Windows thread priority.
Windows has no equivalent Darwin depression state or Mach quantum here, so
those fields remain zero and this is an inspection adapter, not a scheduler
emulation. Their layouts and counts are taken from Apple's `mach/policy.h`.

The matching `thread_policy_get` read path is now resolved for extended and
precedence policies. It reports the current Windows priority mapping and marks
the result as non-default; time-constraint policy remains explicitly
unsupported because Windows has no equivalent Mach deadline contract.

`thread_suspend` and `thread_resume` now map foreign Mach thread names to the
Windows suspend counter, with an adapter-local count that rejects resume
underflow. Self-suspension is rejected deliberately because it would deadlock
the host bridge. `thread_abort` now queues a non-destructive
Windows APC, and `CancelSynchronousIo` for a synchronous Win32-I/O wait; the
smoke test now proves both an alertable `SleepEx` wakeup and cancellation of
a blocked synchronous `ReadFile`. It never
maps to `TerminateThread`. Non-alertable waits and exact Mach abort/
suspend-count semantics remain open.

The x86-64 `thread_get_state` flavor is now implemented with the verified
Darwin 21-word register layout and count 42. Windows `CONTEXT` registers are
copied for the current thread or for a briefly suspended foreign thread;
foreign threads are resumed on every exit path. x86-64 FPU-state reads now
copy the Windows `XSAVE_FORMAT` into the exact 524-byte Darwin float-state
layout. The current-thread FPU write path now maps Darwin's MXCSR control word
through `_mm_setcsr` and is covered by a readback/restore smoke test. Foreign
thread writeback still maps only the MXCSR control word and remains rejected by
the host in the current path; the remaining FPU registers and full XSAVE
writeback are not yet mapped. The AVX read adapter now uses an
`InitializeContext`-allocated XState buffer and is verified against a live
foreign thread when the host exposes AVX, otherwise it fails closed with
`KERN_NOT_SUPPORTED`. The AVX512 read path now uses the same allocated XState
context and exact 2444-byte Darwin layout, failing closed when K-mask, ZMMH,
or ZMM features are unavailable. ARM flavors remain open.

The matching x86-64 `thread_set_state` path now writes integer/control
registers for a foreign suspended Windows thread and always resumes it before
returning. Self-targeted writes are rejected intentionally; no unsafe
`TerminateThread`-style shortcut is used. Full FPU/XSAVE writeback, AVX,
ARM, and full Mach exception-state flavors remain open.

The x86-64 debug-state flavor is also mapped to Windows `CONTEXT` debug
registers `Dr0` through `Dr7`; foreign-thread writes use the same
suspend/resume guard as integer state. FPU and AVX state remain separate and
are not represented by zero-filled substitutes.

Thread exception-port registration now has a process-local adapter for
`thread_set_exception_ports` and `thread_get_exception_ports`. It preserves
mask, handler port, behavior, and state flavor for the current Wintosh
thread-name namespace. This is registration state only: Windows exception
dispatch is not yet routed into Mach messages, but `thread_swap_exception_ports`
now performs validated replacement and returns the previous registrations.
`thread_get_exception_ports_info` now uses the verified two-word XNU
`ipc_info_port_t` layout and returns matching registered handler ports. The
receiver object is zero because the adapter has no kernel Mach task object;
the explicit dispatch bridge now serializes matching exceptions to a Wintosh port.

An explicit `darling_windows_dispatch_mach_exception` bridge now serializes
the current thread's exception type and two codes to the first matching
registered Wintosh port. It is deliberately opt-in rather than a global VEH;
this gives callers a deterministic bridge without intercepting unrelated
Windows faults. The opt-in VEH hook maps access violations, illegal instructions,
and integer divide-by-zero to the minimal Darwin exception types and always
returns `EXCEPTION_CONTINUE_SEARCH`, so it never swallows the host fault.
Behavior-specific Mach replies and real Mach receiver objects remain open.

The opt-in entry point `darling_windows_set_exception_dispatch_enabled` maps
access violations, illegal instructions, and integer divide-by-zero to the
minimal Darwin exception types before queuing the message.

The x86-64 exception-state read flavor is also exposed with the exact
four-word Darwin layout. Windows can provide the current processor number, but
there is no active Mach trap record at an ordinary inspection point, so trap,
error, and fault-address fields remain zero; exception-state writes remain
unsupported.

The WSL/legacy winpthreads `pthread_yield` spelling now aliases the verified
Windows `SwitchToThread`-backed `sched_yield` primitive.

CoreAudio's `pthread_cond_timedwait_relative_np` is now exposed as well. It
converts a validated relative Darwin timespec to the existing absolute,
cancellation-aware condition wait and preserves mutex reacquisition behavior;
the condition attribute now selects `steady_clock` for `CLOCK_MONOTONIC`, with
the host smoke covering an expired monotonic deadline. Exact Darwin
interruption behavior remains open.

The Windows adapter also provides an explicit port-local LIFO cleanup-record
stack through `darling_windows_pthread_cleanup_push` and
`darling_windows_pthread_cleanup_pop`. Deferred cancellation drains the stack
before TLS destructors, including the direct `pthread_testcancel` path; the
host smoke verifies both cancellation entry points. The opt-in PureDarwin-
shaped macro wrapper is source-compatible for matched pairs; full Darwin
cleanup ordering at every cancellation point remains open.

Condition-variable waits poll the deferred request in bounded intervals, so
the cancellation path also covers the main blocking pthread primitive.
Condition waits currently detect deferred cancellation and the host smoke
verifies that the cleanup callback observes the mutex after reacquisition.
Exact Darwin cancellation-point ordering, exception-free ABI behavior, and
timed-wait interruption semantics remain separate partial gaps.
The mutex-attribute subfamily now has `init`, `destroy`, `gettype`, `settype`,
`getpshared`, and `setpshared` adapters plus resolver coverage. Normal and
recursive mutexes are backed by distinct Windows synchronization objects and
the host smoke verifies recursive relocking. Error-checking and process-shared
execution modes return `ENOTSUP` explicitly; they are not silently treated as
ordinary mutexes. Error-checking self-deadlock, busy `trylock`, and foreign
unlock rejection are covered by the host smoke.
Protocol and robustness attribute getters/setters are also resolved and retain
their default values; enabling priority protocols or robust recovery returns
`ENOTSUP` until those execution semantics are implemented.
The signal smoke exposes setup diagnostics separately, and the action/handler
state is synchronized while callbacks remain reentrant outside the lock. Ten
consecutive isolated runs complete with `DARWIN_SYSCALL_SIGNAL=PASS`.
Condition-variable attributes now expose `init`, `destroy`, pshared, and clock
accessors. Default and monotonic clock values are verified; process-shared
conditions return `ENOTSUP` explicitly.
Read/write-lock attributes now expose default-only init/destroy/pshared accessors
with resolver coverage; process-shared rwlocks return `ENOTSUP` explicitly.
The immediate `tryrdlock`/`trywrlock` primitives are now resolved and tested:
shared readers can coexist, writers report `EBUSY` while readers hold the lock,
and acquire successfully after release.

The focused `pthread_sync_smoke` independently runs five successful cycles for
mutex, spinlock, reader/writer-lock, and reusable two-thread barrier behavior.
It also verifies a normal two-thread condition-variable signal/wait lifecycle.
This separates stable synchronization primitives from the larger host smoke;
condition cancellation and full cleanup ordering remain a separate partial
family.
Absolute-deadline `timedrdlock` and `timedwrlock` entry points now poll with
cancellation points and return `ETIMEDOUT` for expired deadlines; the host
smoke verifies the expired-reader case.

The pthread spin-lock family now resolves `pthread_spin_init`,
`pthread_spin_destroy`, `pthread_spin_lock`, `pthread_spin_trylock`, and
`pthread_spin_unlock`. The Windows adapter uses an acquire/release
`atomic_flag`, yields with `YieldProcessor` while contended, and checks deferred
cancellation while spinning. Process-shared spinlocks remain `ENOTSUP`; the
host smoke verifies initialization, exclusive acquisition, busy `trylock`,
unlock, destruction, and resolver coverage.

The mutex family also resolves `pthread_mutex_timedlock`. It polls the existing
mutex backend against an absolute Darwin timespec and returns `ETIMEDOUT` for an
expired deadline; the host smoke verifies the resolver and expired-lock path.
Wall-clock adjustment equivalence and cancellation while blocked remain open.

Mutex priority-ceiling attributes now resolve through
`pthread_mutexattr_getprioceiling` and `pthread_mutexattr_setprioceiling`, and
mutex instances expose the matching
`pthread_mutex_getprioceiling`/`pthread_mutex_setprioceiling` ABI. The default
ceiling is readable through both paths; changing an instance ceiling currently
returns `ENOTSUP` until Windows priority-inheritance semantics are implemented.
The host smoke covers resolver, default-value, and unsupported-set behavior.

The pthread barrier family now resolves `pthread_barrier_init`,
`pthread_barrier_wait`, and `pthread_barrier_destroy` through an opaque
Windows adapter backed by a mutex/condition generation barrier. The host smoke
synchronizes two real threads through two successive barrier phases; the
serial-thread return value is now returned by the last arriving participant as
`-1`. Default
barrier attributes are verified and process-shared barriers explicitly return
`ENOTSUP`; invalid lifecycle inputs are covered, while cancellation while
blocked remains open.

Mach port reference mutation now rejects signed-delta underflow and unsigned
overflow, including the `INT32_MIN` edge case, while preserving the existing
process-local queue behavior.
Port release also removes stale membership from every process-local port set;
cross-process Mach IPC and the full kernel notification/disposition model
remain outside this adapter. A minimal local port-death notification request
is now available, supports replacement/unregister, and is smoke-tested; it uses the legacy header's reserved word
for the notification id until the full Darwin message header is introduced.
The local port-set receiver now uses condition-variable wakeups for sends,
member removal, and set/port destruction, with a cross-thread smoke proof.
The local `MachPort` also supports an explicit waiter interrupt, covered by
`runtime_smoke`; this does not yet cancel a broker request across clients.
The same local queue rejects sends beyond 1024 pending messages and is
drain-tested; exact Darwin queue-limit/error semantics remain open.
The local C ABI now tracks receive and send references separately and exposes
the corresponding type bits; this is still an adapter-level namespace rather
than Darwin's per-task kernel right table.
Send-once references are now reported by `mach_port_type` through the distinct
`MACH_PORT_TYPE_SEND_ONCE` bit instead of being folded into ordinary send
rights; `mach_port_get_refs` now reports ordinary-send and send-once counts
independently, and the Mach smoke covers a receive-plus-send-once port. This remains a
process-local adapter rather than a complete per-task Darwin right table.
`mach_port_mod_refs` now applies positive and negative deltas to the send-once
counter independently, including underflow rejection.
`mach_port_deallocate` also releases a send-once reference before falling back
to a receive right, with the behavior covered by the Mach smoke.
When that release removes the final send or send-once reference, the existing
no-senders notification path is now invoked after the registry lock is
released, avoiding a native Windows lock cycle.
Receive-reference mutations no longer emit a no-senders notification because
they do not change the sender set.
Normal send-reference decrements likewise no longer consume send-once
references; each right class now has isolated `mod_refs` accounting.
`mach_port_deallocate` also suppresses No-Senders for receive-only release;
the regression covers both receive mutation paths.
Forced `mach_port_destroy` now records whether sender rights existed and emits
the matching No-Senders notification after destruction; the native Mach smoke
covers a Send-once right destroyed through this path.
All public port calls currently bind to the current Windows process and share
one process-local registry. Foreign task names are rejected consistently;
true task-owned namespaces and cross-task name translation remain an
architectural gap, not an unimplemented single symbol.
The named-pipe broker separately covers bounded inline/OOL transport,
reconnect persistence, joinable connection workers, and a two-client
send/receive plus notification-wakeup proof. `Receive` also has a bounded
inline-capacity response that preserves an oversized queued message for retry.
Request cancellation, complete rights/disposition semantics, and descriptor
ownership remain open. The native ABI now carries layout-checked Darwin 64-bit
body, port-descriptor, OOL-descriptor, and basic trailer structures; this is a
layout foundation only and does not yet transfer descriptor rights or memory.
The first functional slice now accepts complex inline messages containing one or
more valid port descriptors with `MACH_MSG_TYPE_COPY_SEND` and transports the
descriptor bytes through the local native queue. Move dispositions, OOL
descriptors, and trailers remain explicitly unsupported. `MOVE_SEND` and
`MOVE_SEND_ONCE` are now accepted for inline port descriptors and consume the
corresponding local sender reference after successful queueing; the Mach smoke
covers `MOVE_SEND`.
The same path now consumes one `MACH_MSG_TYPE_MOVE_SEND_ONCE` reference and is
covered by a second native receive/send-once descriptor case.
`MACH_MSG_TYPE_COPY_RECEIVE` now increments the referenced local receive-right
count after queueing and is covered by the descriptor smoke; move-receive and
`MACH_MSG_TYPE_MOVE_RECEIVE` now consumes the source receive right after
queueing and is covered by the descriptor smoke; cross-task name translation
remains open. `MACH_MSG_TYPE_MAKE_SEND` and `MAKE_SEND_ONCE` now add the
corresponding local sender reference while retaining the receive right. A
single local `COPY` OOL descriptor is now identified using its native ABI
`type` offset, validated, and transported without deallocation or
cross-process copying. The first native
ownership slice is now available as `MachOolOwnershipTable`: it tracks queue
and receiver references under a mutex and invokes a release callback only
after the final reference is dropped. The local `mach_msg` path now copies the
payload into a `VirtualAlloc` block after a checked readable-range walk,
transfers queue ownership to either receive API,
and exposes explicit release through the native ABI. Raw guest mappings,
protection transfer and cross-process transfer remain open. `copy=1` is
implemented as an eager independent host copy, not as XNU physical-page
semantics. Deallocation is accepted only for an already-owned local mapping;
raw unmanaged deallocation requests remain rejected.
When a `mach_msg` receive buffer has room beyond the payload, the native path
now appends the Darwin `trailer_none` layout and updates `msgh_size`; exact
payload-sized buffers retain the previous behavior. Credential and audit
trailers remain open.
The local ABI distinguishes reference-decrementing `mach_port_deallocate`
from forced `mach_port_destroy`; the smoke gate also verifies that forced
destruction closes a blocked receiver and rejects subsequent use. This is a
tested adapter rule, not proof of Darwin's per-task right tables or
interprocess right transfer. `get_refs` and
`mod_refs` now reject unknown right selectors while accepting the adapter's
Receive-/Send-right values.

The framework layer now also has a deliberately small CoreFoundation ABI
adapter for retain/release, UTF-8 strings with limited mutable operations, byte data, arrays, integer
integer/real numbers, dictionaries, sets, dates, filesystem URLs, booleans, null values,
stable nonzero type identifiers through `CFGetTypeID`, and deterministic scalar `CFHash`
values for equal strings/data/numbers/dates/booleans,
RunLoop callbacks/timers, date and number comparison, date interval creation/difference, and process-local notifications with named and wildcard observers, including global observer removal. It also has a
minimal XML property-list serializer with XML escaping, nested arrays,
Base64 data nodes, and ISO-8601 date nodes. Its
`corefoundation_smoke` gate proves only that adapter subset; there is no plist
parser or binary-plist implementation, collection ownership is intentionally
minimal, collection hashing now follows the implemented equality semantics,
and timers/sources
are not Apple-compatible implementations. This is not a claim of full
CoreFoundation or Foundation compatibility.
The XML boundary now also parses scalar strings, integers, reals, booleans,
nested arrays, and dictionaries with string keys and can reserialize the
result. Base64 `<data>` and UTC/offset `<date>` decoding with fractional
seconds are also supported. A limited binary `bplist00` reader now handles
single-byte ASCII strings, BMP UTF-16 strings, integers, booleans, null, simple
arrays, string-key dictionaries, extended length markers for strings, arrays,
and dictionaries, plus binary data, date, IEEE-754 real-number, and UID
primitives, including UTF-8 string byte extraction and ASCII-whitespace trimming plus mutable append/replace-all/range-replace, mutable data byte access/append/replace/resize/clear, array append/append-array/insert/remove/remove-range/replace, dictionary duplicate-key collapse/set/remove/merge, and set add/remove/union/intersection/subtract operations.
Surrogate pairs,
complex numeric/time semantics, and full ownership semantics remain open.

Immutable strings additionally expose a read-only direct C-string pointer and
`CFStringGetLength` now reports UTF-16 code units for valid UTF-8 input,
including surrogate-pair length for non-BMP code points; trimming recognizes
the common Unicode whitespace set while malformed UTF-8 remains byte-local;
callers must not retain that pointer beyond the lifetime of the CFString.
The adapter also provides `CFRange`-based literal and ASCII case-insensitive
substring search, including the additive `CFStringFindWithOptions` bounded
search, `CFStringCompareWithOptions`, and options-based prefix/suffix entry
points; Unicode
collation and the complete CFString option set remain outside this boundary.
CFNumber integer and real values can also be read through either numeric
accessor with explicit C++ conversion semantics, and `CFEqual` treats equal
integer/real numeric values as equal; exact Apple overflow and
numeric-type flags remain outside the adapter.

Arrays, dictionaries, and sets now retain object members on creation and
release owned members when the container reaches zero references. This is
Dictionary creation and merge collapse `CFEqual`-equivalent duplicate keys with
last-value-wins behavior. This is still a minimal ownership model without callback allocators, hash callbacks,
mutation APIs, or complete Apple collection semantics. Set creation now
deduplicates members using the adapter's recursive `CFEqual` semantics rather
than pointer identity.
Prefix and suffix checks are also available; they compare the adapter's UTF-8
byte strings and are not Unicode collation.
Ordinal string comparison is available on the same bytewise basis; locale and
Unicode normalization are intentionally not implemented.

`CFEqual` is partially implemented for the scalar immutable types above and
recursively compares the current array, set, and dictionary containers.
Hash callbacks, mutation, callbacks, and full Apple toll-free-bridging
behavior are not included.
Array lookup can use that same partial equality implementation; range and
callback-based search variants remain open.
Dictionary key-presence and set-value lookup are also available through the
same partial equality boundary.
Array first-index and count-of-value operations use that same boundary;
last-index lookup is also available; mutation and range-callback variants
remain open.
The public exports in this adapter are covered by `corefoundation_smoke`,
including the explicit RunLoop wake-up path; this is export coverage, not a
claim of Apple-compatible scheduling semantics.

The Foundation type-encoding adapter now also accepts Darwin `void`, unknown/
function-pointer, block (`@?`), complex (`j` prefix), `long double` (`D`), and decimal bitfield encodings. The
Darwin `D` mapping is conservatively fixed at 16-byte size/alignment rather
than inheriting MSVC's host representation. Bitfields use a conservative
4-byte Windows storage word; exact compiler-specific packed layout remains
open.

`Process::ThreadIds()` now provides a deduplicated child-thread inventory. It
always retains the already verified primary thread and supplements it with
Toolhelp entries when the host exposes them, so a restricted snapshot API does
not erase known thread state. Per-thread context APIs are available for
follow-up capture, but the smoke only proves the primary context round trip;
secondary-thread context translation remains unverified.

`Process::MemoryRegions()` provides a `VirtualQueryEx`-based committed-region
inventory and fails closed when the host denies `PROCESS_QUERY_INFORMATION` or
`PROCESS_VM_READ`. The current managed smoke host denies that cross-process
query, so the API is compile- and non-crash-checked but the region contents are
not yet runtime-verified here; this is not COW support.

`Process::ReadMemory` and `WriteMemory` now form the explicit VM access
boundary used by a future snapshot copier. Both reject null/zero/invalid
requests and reopen the child with the required Windows VM rights; the current
smoke verifies invalid-address rejection only because the managed host denies
valid cross-process VM access.
`ProcessMemorySnapshot` now combines one bounded region's metadata and bytes:
it rejects zero-sized or oversized regions and publishes the candidate only
after the complete read succeeds. Valid cross-process capture is still not
runtime-verified on this managed host, so this is not yet a full address-space
snapshot, COW snapshot, or restore mechanism.
`Process::CaptureMemorySnapshot` extends this to a caller-provided region list,
with checked cumulative byte limits and all-or-nothing publication of the
result vector. It is the bounded primitive for a future Mach task-memory
snapshot, not a claim that arbitrary Windows process memory is readable.
`Process::CaptureTaskSnapshot` combines the retained thread inventory with
the bounded memory snapshot and publishes both only after both phases succeed.
It deliberately fails when the host exposes no queryable regions; therefore a
successful Darwin task snapshot still requires a host with permitted
cross-process VM inspection.

The Mach ABI now exposes `task_for_pid` through the Windows host symbol table.
It validates the host token and PID, probes `PROCESS_QUERY_LIMITED_INFORMATION`,
and represents an accessible Windows PID as a managed opaque Darwin task name.
The name is resolved internally for task-info, thread enumeration, suspend,
resume, termination, and VM operations; `mach_task_self` remains compatible
with the current-process name. A transitional fallback still accepts legacy
numeric task names as PIDs. This establishes a stable local task-name layer,
but not Darwin entitlement/security semantics, transferable Mach rights, or a
complete per-task port namespace.
`task_info(TASK_BASIC_INFO)` is also mapped for accessible Windows processes:
Windows process times and working-set/pagefile counters populate the Darwin
time and size fields. The adapter reports a neutral policy and zero suspend
count because those values are not equivalent to Windows process metadata;
full Darwin task accounting remains open.
`task_threads` now enumerates a task's Windows thread IDs through Toolhelp and
returns them in a VM-owned array; the existing `mach_vm_deallocate` path frees
that array. This proves the enumeration/allocation contract, while individual
thread-port rights and Darwin port namespaces remain incomplete.
`task_suspend` and `task_resume` map foreign tasks to `NtSuspendProcess` and
`NtResumeProcess`, with a guarded per-task suspend count and underflow
rejection. Self-suspension is rejected deliberately to avoid deadlocking the
hosting Darwin process; exact Darwin self-task semantics remain open.
`task_terminate` maps a foreign task to `TerminateProcess`, waits for its
terminal state, and removes its adapter suspend-count entry. Self-termination
is rejected intentionally; signal- and exception-based Darwin termination
semantics are not yet represented.
Successful termination now also removes stale task-level exception registrations,
so a later lookup cannot observe an owner that no longer exists. Thread-level
registrations and global port-object cleanup still need a complete lifecycle
model.
The termination path now snapshots the child's thread IDs before exit and
removes their thread-level exception registrations as well, preventing stale
per-thread owners. Global port objects and unrelated namespaces still require
the future task-port lifecycle model.
The foreign-task VM path now requests `PROCESS_QUERY_INFORMATION` for
`VirtualQueryEx` instead of the limited query right, and the x64 smoke proves
`task_for_pid -> mach_vm_region` on a real child. Memory reads/writes and
remote allocation still require their separate VM rights and remain
independently gated.
The same x64 child test also validates `mach_vm_region_recurse` at depth zero,
with consistent base address, nonzero size, and returned info count. Nested
Darwin submap semantics are still not modeled.
The x64 smoke now walks foreign committed regions and proves `mach_vm_read`
for one readable page, including returned-size validation and read-buffer
deallocation. This is a bounded read proof, not yet a full task-memory dump or
write/allocate transaction.
The x64 smoke now proves a complete bounded foreign-task transaction:
`mach_vm_allocate -> mach_vm_write -> mach_vm_read -> mach_vm_deallocate`,
including byte-for-byte validation. It does not yet provide COW semantics,
protection transitions, or a rollback/restore transaction.
The same foreign allocation path now also verifies `mach_vm_protect` for a
read-only transition followed by restoration to read/write. This confirms the
basic protection mapping, but not Darwin maximum-protection inheritance or
copy-on-write behavior.
The protection check also requires a write to fail while the remote page is
read-only, then restores read/write protection before continuing; this proves
enforcement, not merely successful API return values.
Unknown protection bits are now rejected before reaching `VirtualProtectEx`,
so unsupported Darwin flags cannot silently become a Windows protection mode.
`set_maximum` is now tracked per task/address pair: after a maximum is set,
later protection requests cannot add rights beyond it. The smoke verifies a
read-only maximum rejects a later read/write expansion; this is a bounded
maximum-rights model, not full Darwin inheritance or COW.
Maximum-right entries are now removed when the corresponding VM allocation is
deallocated and when a task terminates, preventing protection metadata from
leaking across allocation or process lifetimes.
The x64 smoke also verifies `mach_vm_copy` between two remote allocations by
comparing the copied bytes after a remote read. This is an explicit copy
operation; it must not be confused with Darwin VM copy-on-write sharing or
snapshot rollback.
The copy test also mutates the source after copying and confirms the destination
still contains the original bytes, proving independent remote allocations
rather than an alias. This remains explicit copying, not Darwin COW metadata.
The x64 smoke additionally validates `mach_vm_read_overwrite` into a local
buffer and checks the exact transferred size and bytes, covering both Darwin
read return shapes for the same remote allocation.
The x64 Mach smoke now consumes every returned `task_threads` entry through
`thread_get_state` before deallocation, proving the enumeration-to-thread-state
chain for the current task. This still does not create durable Mach thread-port
objects.
Foreign x86 `thread_set_state` for `x86_FLOAT_STATE64` now writes back the
complete 512-byte XSAVE floating-point payload after Darwin's two-word prefix,
covering x87/MMX/SSE control and register state rather than only MXCSR. The
same XState bridge now writes AVX and AVX-512 payloads when the Windows host
reports the required feature sets; unsupported host features return
`KERN_NOT_SUPPORTED`. ARM state and exception-state writes remain open.
The portable ABI header now defines the Darwin `ARM_THREAD_STATE64` flavour,
count, and 68-word layout for ARM64 callers. Native `CONTEXT` mapping is not
claimed yet: the current verified targets are x64 and Win32, and an ARM64
Windows build/runtime gate is still required before this row can be marked
implemented.
`x86_EXCEPTION_STATE64` now has an adapter-local get/set roundtrip, including
trap number, error, and fault address, with cleanup on foreign task termination.
It preserves metadata for translated callers; it does not rewrite a live
Windows trap frame or create a kernel exception context.
Managed foreign task names now participate in the local port lifecycle: both
`mach_port_deallocate` and `mach_port_destroy` release an opaque name returned
by `task_for_pid`, and later task operations reject that released name. This
prevents stale task handles from remaining usable after explicit namespace
cleanup; cross-process Mach right transfer and kernel-backed port rights remain
open.
The same managed task names now report a send right through `mach_port_type`
and `mach_port_get_refs`, and can be used as a source right for the local
`mach_port_insert_right` validation path. This is still an adapter-level right
description, not a Windows kernel handle or a transferable Darwin right.
The smoke test also covers copying a managed task send right into an allocated
local receive port through `mach_port_insert_right`; the local port then
reports the expected send reference before cleanup.
Local `MACH_MSG_TYPE_COPY_SEND` descriptors now increment the transferred
send-right reference after successful queueing; the Mach smoke verifies that a
source with one send reference reports two until the namespace is destroyed.
Managed task ports also support `mach_port_extract_right` for `COPY_SEND` and
`MOVE_SEND`; the former preserves the mapping, while the latter consumes it.
The smoke test verifies both postconditions independently. Other right types are rejected because a task port is not a receive or
send-once object in this adapter.
Task-port send references are now counted: copy extraction increments the
count, while deallocation/modification consumes one reference at a time;
`mod_refs(-1)` is allowed to remove the final reference.
The public disposition constants now use the Darwin values for
`MOVE_SEND(17)`, `MOVE_SEND_ONCE(18)`, `MAKE_SEND(20)`, and
`MAKE_SEND_ONCE(21)` instead of the earlier adapter-local substitutions; this
keeps compiled callers ABI-compatible with the documented Mach names.
Thread lifecycle cleanup now also removes adapter suspend counters for every
thread discovered during task termination, and `thread_info(BASIC)` reports
the tracked suspend count instead of always returning zero. The scheduler and
run-state fields remain an approximation of Windows thread state; explicit
adapter suspension is now reported as Darwin `TH_STATE_STOPPED` where the
selected flavor contains a run-state field. Scheduler `depressed` fields are
not used as a substitute because they represent a different Darwin concept.
Stored time-constraint policy records for those terminated threads are now
removed in the same cleanup path, preventing a later Windows thread-ID reuse
from inheriting stale Darwin policy data.
`task_info(TASK_BASIC_INFO)` now reports the adapter's task-level suspend
counter while a foreign process is suspended, rather than returning a constant
zero; native Windows memory and time fields remain the authoritative portions
of the structure.
Task-level `task_set_exception_ports` and `task_get_exception_ports` now reuse
the existing owner-keyed exception registry, so task and thread registrations
have identical mask/filter behavior. Delivery is still only wired through the
existing minimal exception dispatcher; full kernel exception routing is not
implemented.
`task_swap_exception_ports` now provides the corresponding atomic replace-and-
return operation at task scope by reusing the established exception registry;
the smoke verifies that the old handler is returned and the new handler is
visible afterward. Exception delivery itself remains a minimal adapter.
The same boundary now exposes `AllocateMemory`, `ProtectMemory`, and
`FreeMemory`, completing the basic remote-VM operation shape needed by a future
restore path. The smoke covers invalid-operation rejection; successful remote
allocation and writes remain unverified under the current host policy.
Quoted aggregate field names are skipped while parsing, matching common
Objective-C runtime encodings.
Quoted object class names (`@"NSString"`) and pointers to nested encodings
(`^{...}`) are consumed recursively as well.

The same Foundation adapter also exposes basic `NSRange` construction, maximum,
containment, intersection, and union operations.

The Objective-C matrix row includes the current typed Windows dispatch surface:
object, zero-argument and argument-bearing integer/Boolean/pointer/double,
including explicit 32-bit `int` and 64-bit integer entry points,
including unsigned 64-bit (`Q`) zero- and one-argument entry points,
and unsigned 32-bit (`I`) zero- and one-argument entry points,
plus single-precision float (`f`) zero-, one-, and two-argument entry points,
fixed CGRect, block, and void dispatch
including void methods with a typed pointer argument (`v@:^v`)
and object-returning methods with a typed pointer argument (`@@:^v`)
plus the fixed CGRect return and one-CGRect-argument (`{CGRect=...}`) bridges
entry points, plus the tested `method_exchangeImplementations` path. These
are fixed signatures backed by the custom runtime; they do not claim a
general variadic Apple ABI or complete method-cache/swizzling semantics.

CoreGraphics geometry now has an independent partial adapter for affine
transforms, point/rectangle application, inversion, containment, intersection,
transform equality, integral rectangles,
and union, plus edge division with oversized amounts clamped to the actual
edge length, translation/scale/rotation convenience constructors, null/infinite
rectangle predicates, rectangle containment, immutable RGBA color values,
component extraction, direct red/green/blue/white getters, component-count
queries, and equality. This is
geometry/color ABI coverage only; drawing contexts, paths, images, text,
events, and Quartz display backends remain open.

The collection ABI also exposes bulk extraction primitives for array ranges,
dictionary key/value pairs, and set values. `CFData` additionally supports
validated byte-range copies. These operations are pointer-level
adapters and intentionally do not retain returned members or apply Apple's
full callback/equality machinery.

## Status vocabulary

The `status` column describes implementation maturity: `verified` means the
listed test gate passed on both x64 and Win32; `partial` means only part of the
surface or a local adapter is covered; `missing` means no Windows backend is
implemented; `not implemented` and `reference-only` mean the source was
analysed but no runnable port is claimed. The `implementation` column is a
separate description of the technical form, such as `adapter`,
`documentation`, or `none`.

## Current matrix counts

The current CSV contains 18 porting families: 10 are marked `verified`, 7 are
`partial`, and 1 is `not implemented`.
These are family-level counts, not a percentage of Darling and not a claim
that the verified rows compose into a working macOS runtime. The legal
`licensing` row remains `partial` while the external-component audit is open.

The verified `time` family includes the Darwin cancellation-safe resolver
aliases `clock_gettime_nocancel`, `clock_getres_nocancel`, `nanosleep_nocancel`,
`gettimeofday_nocancel`, and `usleep_nocancel`, including common
double-underscore spellings. The aliases intentionally share the existing
Windows implementations; they do not claim Darwin thread-cancellation or
interruption semantics.

It also includes `clock_nanosleep` and its cancellation-safe spelling for
relative and absolute waits on the supported wall-clock and monotonic-clock
IDs. Deadline handling is adapted to Windows clocks and is not full Darwin
interruption or cancellation behavior.

The same row covers the `sleep_nocancel` and `__sleep` resolver spellings;
these use the existing whole-second sleep adapter.

## Mach message options

The Mach message adapter accepts Darwin send/receive timeout option bits.
Receive timeouts apply only with `MACH_RCV_TIMEOUT`; with `MACH_SEND_TIMEOUT`
a full local queue waits for space until the same deadline. Without the
respective timeout bit the adapter keeps its immediate-send or indefinite-
receive behavior. Unsupported option bits still fail closed.

## Source boundaries

Linux/WSL references are compatibility evidence and test material, not a new
license grant. PureDarwin, Darling and external components remain catalogued in
`licenses/`; Apple/XNU/libSystem and framework code is not copied into the
Windows adapter without component-level review.
The C Mach ABI now exposes explicit broker-capability binding, lookup, and
unbind entry points backed by the shared capability table; `mach_abi_smoke`
reports `MACH_C_ABI_BROKER_CAPABILITY=PASS`. This is an integration seam for
port allocation and descriptor rewriting, not yet automatic broker allocation
or automatic routing of `mach_msg`.

The optional native broker context is now active: `darling_windows_mach_broker_enable`
opens a Named Pipe session, and `mach_port_allocate` sends a broker `Allocate`
request and records the returned capability. The broker smoke verifies this
complete path with `BROKER_C_ABI_ALLOCATE=PASS`. Local allocation remains the
fallback when the broker context is disabled.

The corresponding destruction path now sends broker `Destroy`, validates the
response, unbinds the local capability, and then tears down the local port.
The same broker smoke covers allocation and destruction before disabling the
session; send/receive message routing is still a separate family.

For a non-complex message, `mach_msg` now resolves the destination capability
and sends the complete inline message through the broker `Send` operation,
with response validation. `darling_windows_broker_smoke` covers this path
inside `BROKER_C_ABI_ALLOCATE=PASS`. Complex messages with OOL or port
descriptors remain on the local path until descriptor translation is added.

The matching simple `Receive` path is now broker-backed as well: the C ABI
sends timeout and receive-capacity metadata, copies the returned inline bytes
into the caller buffer, and appends the supported basic trailer. The broker
smoke verifies Send -> Receive -> Destroy; OOL and descriptor responses still
require dedicated translation.

The C-ABI receive path now also accepts a broker response carrying a duplicated
native mapping handle, copies it into locally owned memory, registers the OOL
ownership, and writes a Mach OOL descriptor. This covers the materialization
primitive; a full C-ABI OOL Send -> Receive test is still pending because the
current envelope format does not preserve mixed inline-plus-OOL message data.

That OOL-only path is now exercised end-to-end: `mach_msg` sends the copied OOL
bytes as a broker disposition, the broker returns a duplicated mapping handle,
and the C-ABI receive reconstructs and releases the local OOL allocation.
`darling_windows_broker_smoke` reports `BROKER_C_ABI_OOL=PASS`. Mixed inline+OOL
messages and port descriptors remain unsupported in the broker bridge.

Complex messages containing only port descriptors now use the broker transport
as a full inline message. The descriptor names intentionally remain local
names, so this is valid only for a shared local namespace; true cross-process
capability-token rewriting is still pending. OOL-only messages continue to use
the dedicated mapping-disposition path above.

The envelope protocol now carries an explicit version-3 list of port-descriptor
broker tokens, with encode/decode coverage in `MACH_IPC_DESCRIPTOR_TOKENS=PASS`.
The C ABI populates that list from local capabilities and rewrites returned
tokens through the session mapping; this is locally built and tested but not
yet released to GitHub in the current working tree.

The end-to-end broker smoke now exercises a complex port-descriptor message and
reports `BROKER_C_ABI_PORT_DESCRIPTOR=PASS`: the descriptor is sent with its
broker token and restored to the local name on receive. The current test uses
one session and one local mapping; multi-session capability transfer and
disposition-specific right ownership remain open.

The version-4 envelope adds a separate `inline_payload` alongside the OOL
payload. The C ABI now preserves the Mach header/body/descriptor while sending
OOL bytes and patches the returned mapping address on receive. The broker smoke
continues to report `BROKER_C_ABI_OOL=PASS`; mixed inline+OOL data is now
transported, while exact Darwin right semantics and multi-session rewriting
remain open.

The C ABI now exposes broker capability transfer and updates the local mapping
to the target session after a validated `CapabilityTransfer` response. The
table-level session update is covered by `MACH_IPC_CAPABILITY_REBIND=PASS`.
The broker smoke now also opens a second active session, transfers the port to
it, transfers ownership back, restores the local mapping, and destroys the
port. It reports `BROKER_C_ABI_TRANSFER=PASS`; true independent-process right
semantics and disposition-specific reference accounting remain open.

Broker Send validation now checks every port-descriptor token for allocation
and session ownership before queueing the message. This is intentionally kept
separate from OOL disposition handling; foreign descriptor tokens are rejected
instead of being treated as raw local names.
The negative path is covered by `BROKER_DESCRIPTOR_TOKEN_VALIDATION=PASS`.

Envelope version 5 now carries one disposition byte per port-descriptor token;
the broker preserves and validates the metadata, and the C-ABI restores it on
receive. This is transport-level right metadata, not yet full Darwin reference
count behavior for every move/copy/make disposition.
Unknown disposition values are rejected and covered by
`BROKER_DISPOSITION_VALIDATION=PASS`.
Queue-capacity validation now occurs before rights mutation, preventing a
full-queue failure from consuming a move right without enqueueing its message.
The OOL allocation/mapping block now snapshots and restores affected rights on
its immediate exception paths, so mapping creation failure does not leave a
partially consumed disposition.
Queue insertion is also transactional: a late queue allocation failure removes
the newly created OOL mapping and restores the saved rights snapshot.
The complete native Windows regression matrix was rerun after this change:
`40/40` CTest tests passed in Release configuration.
The broker now maintains receive/send/send-once counters per capability for
the supported move/copy/make cases and applies the first-level consume/create
rules before queueing. This is covered by the updated make-send descriptor
smoke. Move receive/send/send-once rights are restored to the receiving
capability when the queued message is successfully dequeued. Complete Darwin
right propagation through every receiver and failure/rollback edge remains
open.

After these changes the complete native Windows CTest matrix passes 40/40 in
Release configuration. This validates regression coverage across the current
host/runtime/ABI families, but it does not expand the claimed compatibility
boundary to full Darwin semantics.

The current uncommitted batch also passes `Check-LicensePreflight.ps1`:
150 provenance rows, 22 license-inventory rows, all required bundled license
files present, and zero port source files without a header notice. The
preflight still marks source-provenance review items explicitly; PASS means
the inventory and required notices are present, not that upstream licenses are
silently relicensed.

`CancelMachIpcRequest` now wraps the native authenticated Cancel envelope;
the broker smoke uses it to interrupt a Receive from another client and
reports `BROKER_MACH_CROSS_CLIENT_CANCEL=PASS`. Full C-ABI request tracking
and signal/interrupt propagation into every blocking primitive remain open.

The new `MigRoutineRegistry` is the first native MIG/RPC dispatch seam. It
supports routine registration, duplicate rejection, request-size bounds,
unknown-routine rejection, dispatch, and unregister. It is intentionally
transport-independent so generated or hand-written Darling-compatible stubs
can be connected to Mach IPC later; it is not generated MIG support yet.
The envelope now also carries a validated `mig_routine_id` field (protocol
version 6), making the routine identity transportable across the broker.
The broker accepts the authenticated `MigDispatch` operation as a transport
boundary and returns the request payload with the same routine ID. This is
deliberately an echo seam until generated subsystem handlers are connected;
it is not yet a Darwin MIG server implementation.
Because the current workspace contains no upstream `.defs`/`.mig` files, the
repository now includes `tools/Scan-MigDefinitions.ps1`. It records relative
path, SHA-256, detected subsystem/routine names, and an explicit upstream
license-review marker without copying source. A real inventory remains
unavailable until the corresponding licensed Darling tree is present.
`tools/Generate-MigRegistry.ps1` converts that inventory into an explicit C++
registration fragment. Generated entries are clearly empty stubs with stable
local IDs; they are integration scaffolding, not claimed Darwin behavior.
Usage and provenance boundaries for both scripts are documented in
`tools/README.md`.
The registry also enforces a per-routine maximum reply size, so generated or
hand-written handlers cannot emit an unbounded response through the broker.

The Win32 Release configuration also compiles successfully and passes all 46
CTest entries. Its dedicated 32-bit Mach-O indirect-import fixture reports
`MACHO_INDIRECT_IMPORT_BIND_32=PASS`; only executable-code indirect calls and
the complete 32-bit dyld startup ABI remain architecture-specific gaps. The
native i386 executable-memory primitive itself reports
`INDIRECT_CALL_ABI_32=PASS`.

The dedicated `macho_entry32_smoke` also reports `MACHO_ENTRY_32=PASS` on
Win32, covering an i386 `LC_UNIXTHREAD` entry and argument/environment vector
call. Dependency loading, libSystem startup, and framework initialization are
still outside this fixture.

The `dyld_graph32_smoke` fixture also reports `DYLD_GRAPH_32=PASS`, proving
that the 32-bit dependency graph resolves an `@loader_path` provider. Symbol
binding through that provider and dependent-entry execution remain open.

The dedicated `dyld_bind32_smoke` fixture now reports `DYLD_BIND_32=PASS` for
an undefined `_probe` import resolved from a 32-bit provider Dylib. Executing
the dependent image through that resolved provider address remains separate.
The same fixture now reports `DYLD_PROVIDER_ENTRY_32=PASS`, proving that the
resolved provider address reaches executable mapped i386 code. The dependent
main-image slot and entry call remain separate gates.

The same fixture now also reports `DYLD_DEPENDENT_SLOT_CALL_32=PASS`, proving
an i386 dependent call through the patched provider slot. This is the
bind-to-call ABI proof; full main-image Mach-O section integration remains a
separate gate.
The same fixture now also reports `DYLD_LOADER_SLOT_PATCH_32=PASS`: the
32-bit main image contains a real `__la_symbol_ptr` section, is opened through
`OpenDynamicImage`, and its mapped indirect slot matches the resolved provider
address. Executing that loader-prepared main image through its Mach-O entry,
plus Darwin startup, remains open.
The same fixture now reports `DYLD_LOADER_MAIN_ENTRY_32=PASS`: after the
loader resolves dependencies and patches the main-image slot, the i386
`LC_UNIXTHREAD` entry executes successfully through
`DynamicImageExecuteEntry`. libSystem initialization and full Darwin process
startup remain open. The fixture validates both `argc` and the first `envp`
string byte.
The production `DarwinBootstrap::Run` path now applies `LC_DYSYMTAB`
indirect-import bindings for both providers and the main image, so the CLI
bootstrap uses the same indirect-slot operation as the loader fixture.
The resolver also expands the conventional `@rpath/Foundation` shorthand to
`Foundation.framework/Foundation`; `DYLD_FRAMEWORK_SHORTHAND=PASS` is covered
by the dyld smoke fixture. The same expansion is now applied consistently to
`@loader_path` and `@executable_path` references.
Absolute `/System/Library/Frameworks/<Name>` references receive the same
bundle-executable fallback under the configured Wintosh prefix.
The dyld fixture reports `DYLD_ABSOLUTE_FRAMEWORK=PASS` for this direct
absolute-install-name form.
The CLI now imports semicolon-separated `DYLD_LIBRARY_PATH` entries into the
resolver search paths in addition to explicit `--rpath` values.
Colon-separated Darwin lists are accepted as well, while a leading Windows
drive-letter colon is preserved.
`DYLD_FALLBACK_LIBRARY_PATH` is also accepted after the primary library path.
Framework-specific `DYLD_FRAMEWORK_PATH` and
`DYLD_FALLBACK_FRAMEWORK_PATH` entries are accepted through the same resolver
path list.
The dyld smoke now launches the real `wintosh.exe` with only the temporary
provider directory in `DYLD_LIBRARY_PATH`; `DYLD_CLI_LIBRARY_PATH=PASS`
confirms the CLI-to-resolver-to-entry path.

The Foundation range family now also covers `NSOffsetRange` and
`NSInsetRange`, including negative offsets, saturation, and inset validation.

The current local Release verification snapshot is `46/46` CTest tests
passing. The added `wintosh_indirect_call_smoke` reports
`INDIRECT_CALL_ABI=PASS` for a native Windows x64 executable-memory indirect
call. The dedicated `wintosh_indirect_import_smoke` now also reports
`MACHO_INDIRECT_IMPORT_BIND=PASS` for an x86-64 `LC_DYSYMTAB` table mapped to
`__la_symbol_ptr` and patched in mapped memory. Full indirect-import execution
through the slot and all pointer-format and architecture variants remain open.
`Check-LicensePreflight.ps1` reports PASS with
zero port source files missing a header notice, while provenance rows marked
`review-required` remain open for license review.

The process family additionally verifies native `posix_spawnp` PATH lookup,
environment propagation, spawn file actions, and `waitpid`/`wait4` on x64 and
Win32. This is spawn-based compatibility and does not claim `fork` or
copy-on-write equivalence. `fork` remains intentionally unimplemented rather
than being silently misrepresented by `CreateProcess`; a future implementation
needs an explicit suspended-process/address-space strategy and a documented
Darwin-compatible parent/child memory contract.

The CoreFoundation collection family now also exposes mutable constructors
for arrays, dictionaries, and sets. Their capacity argument is validated but
is not a hard allocation limit; the existing mutation, ownership, and
collection-operation tests pass on both x64 and Win32.

Mutable-copy entry points for the same three container families are also
covered; each returned copy can be mutated independently while retaining the
existing ownership behavior.

The run-loop family also covers `CFRunLoopGetMain` on both target
architectures. Full CoreFoundation mode/source and cross-thread scheduling
semantics remain outside this minimal bridge.

The CoreFoundation date family also covers
`CFDateGetTimeIntervalSinceReferenceDate`; the bridge keeps one explicit
reference-date representation and does not claim calendar/time-zone support.

`CFDateCreateWithTimeIntervalSinceNow` is also covered for relative future
dates; clock precision, adjustment, and calendar/time-zone behavior remain
outside this bridge.

The URL family also covers `CFURLCreateWithString` for non-empty absolute
string values and simple relative paths against a base URL; escaping,
including leading `../` parent segments, are covered; escaping, normalization,
and full RFC authority parsing remain open.
Simple `/./` segments and a trailing `/.` are also covered.
Bracketed IPv6 host and port extraction is covered as well; zone identifiers,
authority escaping, and complete RFC URL normalization remain open.
`CFURLCopyNetLocation` is covered for the same simple IPv4/IPv6 authority
forms.
The additive Wintosh bridge `darling_windows_CFURLCopyPathDecoded` covers
percent-decoding of URL paths, while malformed escapes remain literal and
complete RFC 3986 normalization remains open.
The corresponding `darling_windows_CFURLCopyQueryStringDecoded` and
`darling_windows_CFURLCopyFragmentDecoded` bridges cover the same bounded
decoding behavior for query and fragment values.
Simple URL user-info extraction through `CFURLCopyUserName` and
`CFURLCopyPassword` is covered; valid hexadecimal percent escapes are also
covered, while malformed/Unicode escapes and full credential parsing remain
open.

The libc string/allocation family now also covers `reallocarray`,
`reallocarrayf`, `strsep`, and bounded `strnstr`. The memory family includes
`memrchr`, while the BSD bit-scan family includes `ffs`, `fls`, `ffsll`, and
`flsll`; constant-time byte comparison includes `timingsafe_bcmp`, and the
signal-text family includes the common Darwin `strsignal` descriptions.
The error-introspection family also includes a broad common `strerrorname_np`
mapping and bounded `strerrordesc_np` descriptions for stable errno values.
The libc pattern family now includes `fnmatch` with the common wildcard,
character-class, pathname, period, and escape flags; locale-specific collation
and extended bracket expressions remain open.
The ASCII `FNM_CASEFOLD` extension is now supported, including case-folded
literal and bracket-class comparisons; locale-specific collation remains open.
`FNM_LEADING_DIR` is also supported for slash-separated path prefixes; Windows
backslash path separators are supported when `FNM_NOESCAPE` is set. UNC- and
device-path-specific leading-directory semantics remain open.
ASCII POSIX bracket classes `digit`, `alpha`, `alnum`, `lower`, `upper`,
`xdigit`, `blank`, `space`, `cntrl`, `graph`, `print`, and `punct` are now
recognized in `[[:class:]]` expressions.
Unclosed bracket expressions are treated as literal `[` patterns, matching the
Darwin/POSIX fallback behavior; locale-specific parsing remains open.
The path-string family also includes `basename` and `dirname` for common
slash-separated, backslash-separated, and drive-letter cases; UNC/device-path
edge semantics remain open.
The resolver family additionally covers the legacy BSD IPv4 helpers
`inet_addr`, `inet_aton`, and `inet_network`; full historical shorthand-address
parsing remains outside this bridge.
The companion `inet_makeaddr`, `inet_lnaof`, and `inet_netof` helpers cover the
classic class-A-, class-B-, and class-C-style address decomposition used by
legacy callers; modern CIDR/netmask semantics remain open.
Legacy text conversion is also available through `inet_ntoa` and
`inet_ntoa_r`; broader historical formatting quirks remain open.
The legacy `gethostbyname` entry point is also bridged; resolver-global
thread-safety and the remaining historical hostent variants remain open.
`gethostbyname2` now covers AF_INET and AF_INET6 through a thread-local
`hostent` view; the returned structure is valid only until the next call on
the same thread.
The Darwin-style `gethostbyname_r` copy path is covered for caller-owned IPv4
storage and insufficient-buffer `ERANGE`; reentrant IPv6 and multi-address
alias copying remain open.
`gethostbyaddr_r` now has the same caller-owned IPv4 copy boundary; successful
PTR resolution remains environment-dependent and IPv6/multi-alias copying is
still open.
The reverse `gethostbyaddr` ABI and invalid-input path are covered for IPv4;
successful PTR resolution remains environment-dependent, while IPv6-specific
and fully reentrant hostent variants remain open.
The service database pair `getservbyname`/`getservbyport` is also bridged;
availability of local service-database entries remains environment-dependent.
The matching protocol database pair `getprotobyname`/`getprotobynumber` is
also bridged; local protocol-database availability remains environment-dependent.
`flsll`; `timingsafe_bcmp` provides the constant-time byte comparison subset.
The array helpers reject multiplication overflow before calling the Windows
heap, while `strsep` preserves empty fields. Host API smoke coverage verifies
normalized symbol names and runtime behavior on both x64 and Win32.

### Suspended child bootstrap primitive

The Windows process layer now exposes `Process::LaunchSuspended` and
`Process::Resume`. The primitive creates the child with `CREATE_SUSPENDED`,
keeps the primary-thread handle alive, and makes resumption explicit. This is
the reusable hand-off point for a future Darwin child bootstrap and is covered
by `wintosh_process_syscalls_smoke` on x64 and Win32.

This does not implement `fork`, copy-on-write, register/address-space cloning,
or a second Mach namespace. Until those pieces exist, the primitive remains a
controlled spawn operation and must not be advertised as fork compatibility.

`Process::Suspend` and `Process::Resume` also control an already-running
Windows child through the native process suspend APIs. The process smoke covers
launch-suspended resume and running-process suspend/resume on x64 and Win32.
This is a snapshot preparation primitive only; it does not clone or provide
copy-on-write semantics for the suspended address space.

The process wrapper also preserves the primary thread ID across `Resume()`.
This gives a future register-context snapshot a stable thread target without
keeping a stale Windows thread handle alive.

`Process::GetPrimaryThreadContext` and `SetPrimaryThreadContext` now provide a
controlled Windows `CONTEXT` read/write boundary for a suspended child. The
process smoke performs a same-context round trip on x64 and Win32; mapping that
context to Darwin `thread_state_t` and cloning all secondary threads remain
open.

The process layer now exposes an explicit `DarwinX86ThreadState64` bridge for
x86_64 hosts. It maps the general registers, instruction pointer, flags, and
segment metadata while retaining host-validated selectors on write; Win32
rejects this 64-bit state explicitly. ARM64 state and secondary-thread
translation remain separate gaps.

The process layer now defines a Darwin ARM64 thread-state bridge as well. It
maps the 29 general registers, frame/link registers, stack, program counter,
and status register when compiled for Windows ARM64; x64 and Win32 reject the
operation explicitly. ARM64 runtime evidence is not available in the current
host matrix.

The broker also exposes an atomic `CapabilityTransferBatch` operation. The
client serializes the target session and complete broker-token list; the broker
validates every current owner before changing any owner and moves associated
out-of-line regions together. A failed validation therefore cannot leave a
partially transferred port set. Notification transfer and process-address-space
inheritance remain separate open families.

The broker now has a session-authenticated `NotificationTransfer` operation as
well. It changes notification ownership only after validating the source and
target sessions; the legacy unauthenticated notification path remains a
compatibility mode and is not treated as child inheritance.

`OpenAndTransferMachIpcChildSession` now combines child-session opening on a
child broker connection with the atomic capability batch transfer from the
parent connection. It is the reusable bridge for a suspended child bootstrap;
it does not itself create the process, clone memory, or roll back an already
opened session when a future resource family fails.

The broker smoke now exercises this bridge with a real `CREATE_SUSPENDED`
Windows child: a fresh parent-owned port is transferred to the child session,
the child is resumed, and the port is destroyed through the child connection.
This proves session/process hand-off only; it is not proof of address-space
inheritance or Darwin `fork` semantics.

