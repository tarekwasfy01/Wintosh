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
The repository-wide navigation map is [DOCUMENTATION-INDEX.md](DOCUMENTATION-INDEX.md),
and the current evidence snapshot is [STATUS.md](STATUS.md).
Current verified low-level families include libc/file descriptors, sockets,
process and time primitives, pthread synchronization/TLS, Mach-O loading, and
the process-local Mach C ABI including basic VM and port-type operations. This
matrix does not claim that ordinary macOS applications run yet.

The process row also covers the four-word Darwin signal-set constructors and
membership operations, verified by `signals_smoke`; this does not establish
native cross-process signal delivery or complete Darwin signal semantics.

`pthread_sigmask` resolves to the same thread-local mask backend and is
verified as an import boundary by the signal smoke test; cancellation and
cross-process delivery remain separate gaps.

The pthread family now includes a verified deferred-cancellation path using
`pthread_cancel`, `pthread_setcancelstate`, `pthread_setcanceltype`, and
`pthread_testcancel`. Async cancellation is intentionally not claimed.

The Windows adapter also provides an explicit port-local LIFO cleanup-record
stack through `darling_windows_pthread_cleanup_push` and
`darling_windows_pthread_cleanup_pop`. Deferred cancellation drains the stack
before TLS destructors, and the host smoke verifies one callback. The opt-in
PureDarwin-shaped macro wrapper is source-compatible for matched pairs; full
Darwin cleanup ordering at every cancellation point remains open.

Condition-variable waits poll the deferred request in bounded intervals, so
the cancellation path also covers the main blocking pthread primitive.
Condition waits currently detect deferred cancellation, but the cleanup
callback ownership assertion is not yet passing reliably. The implementation
still needs a native exception-free transition that reacquires the mutex before
draining cleanup handlers; this sub-path is therefore partial and not claimed
as verified.
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
accessors. Default clock/pshared values are verified; process-shared
conditions and non-default clock execution return `ENOTSUP` explicitly.
Read/write-lock attributes now expose default-only init/destroy/pshared accessors
with resolver coverage; process-shared rwlocks return `ENOTSUP` explicitly.
The immediate `tryrdlock`/`trywrlock` primitives are now resolved and tested:
shared readers can coexist, writers report `EBUSY` while readers hold the lock,
and acquire successfully after release.
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

The pthread barrier family now resolves `pthread_barrier_init`,
`pthread_barrier_wait`, and `pthread_barrier_destroy` through an opaque
Windows adapter backed by C++20 `std::barrier`. The host smoke synchronizes two
real threads through two successive barrier phases; the serial-thread return
value and barrier attributes remain partial.
Mach port reference mutation now rejects signed-delta underflow and unsigned
overflow, including the `INT32_MIN` edge case, while preserving the existing
process-local queue behavior.
Port release also removes stale membership from every process-local port set;
cross-process Mach IPC and the full kernel notification/disposition model
remain outside this adapter.
The local port-set receiver now uses condition-variable wakeups for sends,
member removal, and set/port destruction, with a cross-thread smoke proof.
The local C ABI now tracks receive and send references separately and exposes
the corresponding type bits; this is still an adapter-level namespace rather
than Darwin's per-task kernel right table.
The named-pipe broker separately covers bounded inline/OOL transport and
reconnect persistence; it is still sequential per client rather than a
concurrent Mach dispatcher.
The local ABI distinguishes reference-decrementing `mach_port_deallocate`
from forced `mach_port_destroy`; this is a tested adapter rule, not proof of
Darwin's per-task right tables or interprocess right transfer. `get_refs` and
`mod_refs` now reject unknown right selectors while accepting the adapter's
Receive-/Send-right values.

The framework layer now also has a deliberately small CoreFoundation ABI
adapter for retain/release, UTF-8 strings with limited mutable operations, byte data, arrays, integer
integer/real numbers, dictionaries, sets, dates, filesystem URLs, booleans, null values,
RunLoop callbacks/timers, date and number comparison, date interval creation/difference, and process-local notifications with named and wildcard observers, including global observer removal. It also has a
minimal XML property-list serializer with XML escaping, nested arrays,
Base64 data nodes, and ISO-8601 date nodes. Its
`corefoundation_smoke` gate proves only that adapter subset; there is no plist
parser or binary-plist implementation, collection ownership is intentionally
minimal, dictionary lookup is not full `CFEqual` semantics, and timers/sources
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

## Source boundaries

Linux/WSL references are compatibility evidence and test material, not a new
license grant. PureDarwin, Darling and external components remain catalogued in
`licenses/`; Apple/XNU/libSystem and framework code is not copied into the
Windows adapter without component-level review.
