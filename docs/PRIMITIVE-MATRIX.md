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
payload into a `VirtualAlloc` block, transfers queue ownership to a receiver,
and exposes explicit release through the native ABI. Raw guest mappings,
protection transfer, physical-copy/deallocation-request semantics, and
cross-process transfer remain open. Negative smoke cases explicitly reject
OOL deallocation and physical-copy requests.
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
substring search; Unicode collation and the complete CFString option set remain
outside this boundary.
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
