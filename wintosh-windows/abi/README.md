# ABI primitives

Darwin-shaped local ABI adapters for Mach ports/VM, Objective-C metadata and
thread-local storage. These are compatibility primitives, not a Darwin kernel.

The libdispatch subset provides main/global queues, serial queues created by
`dispatch_queue_create`, async/sync block submission, and function/context
variants, plus `dispatch_after` through one monotonic deadline scheduler. The
AppKit message loop drains the main queue on its owning thread;
global work uses a bounded concurrent worker pool. Created queues are serial
unless passed `DISPATCH_QUEUE_CONCURRENT`, which creates a per-queue pool of up
to eight workers.
Barrier async/sync operations are exclusive against neighboring work on these
private concurrent queues and act as ordinary work on serial and global queues.
QoS and target-queue attributes are not distinguished. `dispatch_once` and
`dispatch_once_f` serialize one-time
initialization by predicate address. Semaphores support counted signal/wait,
nonblocking `DISPATCH_TIME_NOW`, steady-clock deadlines from `dispatch_time`,
wall-clock absolute deadlines from `dispatch_walltime` translated into the
monotonic scheduler at conversion time, and `DISPATCH_TIME_FOREVER`. Wall-clock
adjustments after conversion do not retarget an already-created deadline.
Groups support enter/leave, async and notify
block/function variants, and steady-clock timed waits. Generic retain/release
accepts queue, group, and timer-source handles. Queue-specific contexts support
set/get, current-queue lookup, and destructor cleanup. Timer sources support
monotonic one-shot/repeating deadlines, event and cancellation handlers,
suspension/resume, and cancellation. Timer leeway is ignored. User-data
ADD/OR/REPLACE sources coalesce merged values and deliver them through the
source queue. READ/WRITE descriptor sources use the existing Darwin poll
adapter with a 20 ms polling interval. Read data reports available byte counts
where Windows exposes them and a readiness marker for EOF or unknown counts;
write sources report readiness. Process sources accept Windows PIDs and the
`DISPATCH_PROC_EXIT` mask, opening a synchronize-only process handle and
reporting the exit event. They do not translate Unix process signals or report
fork/exec events. Signal sources subscribe to Wintosh-internal Darwin signal
raises and merge the number of raises for their signal; they do not yet observe
native console signals or signals directed to child processes. Vnode sources
duplicate disk-file handles and compare file metadata every 40 ms, reporting
delete, write, extend, attribute, link, rename, and handle-query failure events.
This polling adapter can coalesce or miss short-lived intermediate states and
does not implement `DISPATCH_VNODE_FUNLOCK`. Barriers, target-queue inheritance,
and exact Mach-clock deadline conversion remain unimplemented.
Mach receive sources require a live receive right and a zero mask. They poll
the local Mach queue every 10 ms, publish the current pending-message count,
and leave message removal to the guest's `mach_msg` call. Destroying the port
ends the source through its cancellation path. Mach send-right dead-name
sources accept the `DISPATCH_MACH_SEND_DEAD` mask, retain a send reference for
their lifetime, register a local `MACH_NOTIFY_DEAD_NAME` notification, and
deliver the event once when the receive right disappears or the port is
destroyed. The notification receiver uses bounded waits so cancellation can
stop it promptly. Cross-process broker-side port-death observation is not yet
supported.
Apple documents signal sources as asynchronous signal-arrival events and notes
that their pending data includes the number of signals received since the last
handler invocation ([Dispatch Sources](https://developer.apple.com/library/archive/documentation/General/Conceptual/ConcurrencyProgrammingGuide/GCDWorkQueues/GCDWorkQueues.html)).
The vnode event mask values follow Apple's
[`dispatch_source_vnode_flags_t`](https://developer.apple.com/documentation/dispatch/dispatch_source_vnode_flags_t).

Mach clock exports use QPC ticks for `mach_absolute_time` and map
`mach_timebase_info` to the corresponding nanosecond conversion ratio.
`mach_continuous_time` converts `GetTickCount64` uptime into the same QPC tick
domain so it continues across system sleep; its millisecond host resolution is
coarser than `mach_absolute_time`.

TLV descriptors registered together share one template key and one storage
block per thread. Offsets select variables within that block, including aliases
to the same variable. The block is sized before its first pointer is returned;
resolution rejects out-of-range offsets instead of reallocating live storage.
Registration rejects offsets at or above the 1 MiB adapter limit. Callers must
supply the full template size for variable widths and zero-fill storage.

`wintosh_tlv_smoke` covers shared offsets, aliases, thread isolation, stable
pointers, invalid offsets, teardown, and thread destructors. This host-side
gate does not verify Mach-O calls through a Darwin TLV assembly thunk.
The behavioral reference is Apple's
[dyld TLV implementation](https://github.com/apple-oss-distributions/dyld/blob/dyld-852.2/src/threadLocalVariables.c);
the Windows implementation remains project-owned code.

The Objective-C dispatcher now distinguishes registered `Class` receivers
from ordinary `id` instances before reading an instance `isa`, and handles
no-argument class methods with object, void, integer, boolean, and pointer
returns. This is the dispatch foundation needed for class methods such as
`+alloc` and singleton accessors. The AppKit bootstrap uses this path for
host-owned `NSObject`, `NSString`, `NSConstantString`, `NSMutableString`,
`NSApplication`, `NSWindow`, `NSView`, and `NSButton` classes; these are not a
full Foundation/AppKit implementation. On
x64 Clang/Windows, fixed-arity runtime
exports are declared with the Darwin SysV calling convention. The compiler
cannot use `va_start` inside a SysV function, so `objc_msgSend`,
`objc_msgSendSuper`, and `objc_msgSendSuper2` enter through x64 assembly shims.
Both super messengers read the Darwin `objc_super` pair from the first guest
argument: the legacy form starts at its `super_class` field, while Super2 starts
at that class's superclass. The shims capture
the guest GP, XMM, and stack argument areas. This follows Apple's definitions
of [`objc_super` and objc_msgSendSuper](https://github.com/opensource-apple/objc4/blob/master/runtime/message.h)
and the [Super2 search-class rule](https://github.com/opensource-apple/objc4/blob/master/runtime/objc-abi.h).
A typed dispatcher supports
no-argument methods, selected one/two-object and integer methods, and selected
Float/Double methods. It invokes Objective-C methods registered from Mach-O
with the SysV ABI, including `*` C-string returns such as `-UTF8String` and
selected stack-passed CGRect arguments for frame-based AppKit methods.
The `setFrame:display:` form reads the CGRect from the guest stack area and its
following BOOL from the next GP argument register.
The resolver also exposes `objc_msgSend_stret`, `objc_msgSendSuper_stret`, and
`objc_msgSendSuper2_stret` for no-argument CGRect return methods such as
`NSView -frame` and `-bounds`. These paths copy the 32-byte result through the
Darwin x86_64 hidden return pointer and apply the matching super search rule;
they also dispatch the selected `NSView -convertRect:toView:` and
`-convertRect:fromView:` signatures (CGRect return, CGRect argument, view
object), including legacy and Super2 lookup. This remains a selected adapter;
other aggregate layouts and stret signatures are unsupported. The AppKit
bridge also has explicit stret cases for the six-double
`NSAffineTransformStruct` return and `NSAffineTransform -transformRect:`; the
latter reads its stack-passed CGRect argument from the guest stack area. The
typed dispatcher handles `-setTransformStruct:` by copying its six-double
structure argument from the stack. These signatures are selected adapters,
not general aggregate classification. Apple's
runtime marks this primitive unavailable on arm64 and directs callers to use
it for structure returns according to the target ABI
([objc4 `message.h`](https://github.com/opensource-apple/objc4/blob/master/runtime/message.h)).

On Clang Windows ARM64, guest `_objc_msgSend` imports now resolve to
`darling_windows_abi_objc_msgSend_arm64`. The leaf assembly entry passes the
Apple stack-argument area to `DarlingArm64GuestVarargs`; the typed message
dispatcher reads only argument slots selected by the registered method
encoding. The ordinary `objc_msgSend` C entry remains available to Windows
host callers and keeps their native `va_list` behavior. The Objective-C C++
translation unit, symbol resolver, and ARM64 assembly entry compile as ARM64
COFF objects. This is not yet a linked ARM64 runtime or guest execution proof.
The `objc_msgSendSuper` and `objc_msgSendSuper2` ARM64 entries now unpack the
guest `objc_super` pair and use the same stack reader for the supported
scalar/object signatures. Super2 begins lookup at `super_class->superclass`,
while the legacy form begins at `super_class`. Their source, symbol resolver,
and assembly entries compile to ARM64 COFF as well. Aggregate/overflow cases,
unsupported method encodings, red-zone behavior, and real Mach-O dispatch still
need guest fixtures. The shared ARM64 assembly return path reloads `x0-x1` and
`d0-d3` from a typed result frame. The current decoder handles scalar
`float`/`double` returns, CGPoint/CGSize/CGRect HFA returns, and NSRange's
integer-pair return; direct Clang assembly probes show the expected register
classes match between Apple and Windows ARM64 for those types. These probes
verify compiler ABI classification, not guest-to-host execution.

The dispatcher also decodes Darwin x86_64 `NSRange` as two integer-register
words for selected NSString substring and NSMutableString edit methods, including
the range-plus-object argument form. Common BOOL-returning `Class`, `SEL`, and
opaque-pointer arguments now cover NSObject's kind, selector, and protocol
queries. The typed BOOL-returning object-argument bridge accepts both Clang's
`B` and Objective-C's signed-char `c` return encodings, covering AppKit delegate
methods compiled against either BOOL spelling.
It decodes two-Double `CGPoint`/`CGSize` arguments from XMM registers and
returns their components in XMM0/XMM1 for `NSView -frameOrigin`, `-frameSize`,
`-convertPoint:toView:`, and `-convertPoint:fromView:`. The view adapter
composes axis-aligned frame-to-bounds scales and origins for views attached to
the same window, including `NSView -isFlipped` overrides when composing each
view-to-superview y transform. Rotation and arbitrary aggregate signatures
remain unsupported.
Typed host helpers also preserve guest ABI when invoking object-returning,
void-object, and pointer-returning guest methods. For registered guest IMPs,
the dispatcher now has a raw forwarding path for scalar-only signatures. It
restores the captured GP/XMM arguments, copies bounded overflow words to a new
SysV stack frame, and forwards the call without retyping its scalar arguments.
This extends multi-argument and stack-passed scalar methods beyond the explicit
adapter list. Here `N_GP` counts general-purpose arguments including
`self`/`_cmd`, and `N_XMM` counts floating-point arguments. The copied stack extent is
`B = 8 * (max(0, N_GP - 6) + max(0, N_XMM - 8))` bytes, bounded to 512 bytes.
Aggregate classification and returns, long double, variadic
guest IMPs, blocks, and exception behavior remain outside this path. Unsupported
encodings continue to return zero/nil, so this is not a general Cocoa ABI
compatibility guarantee.

The x86_64 message-send trampoline also returns the common two-word Darwin
`NSRange` aggregate (`{_NSRange=QQ}`) in `RAX` and `RDX`, for zero-argument
methods and methods with one object argument, an options word, and an optional
`NSRange` argument. This covers common string and character-set search queries
without claiming general aggregate classification; other aggregate layouts
and return classes still need explicit ABI handling.

The dynamic loader registers x86_64 Mach-O `__cfstring` records with the
Objective-C runtime before image initializers run. Registered literals are
treated as immortal by `objc_retain`/`objc_release`, so their read-only flags
are not mistaken for the runtime's mutable retain counter. The AppKit-side
`NSConstantString` adapter exposes `UTF8String` and `length` for byte and
UTF-16 literals. This is a selected NSString method subset; it does not bridge
the adapter's CoreFoundation objects to the Objective-C object model.

Host adapters may register an object finalizer for native side storage. The
Objective-C runtime removes that registration and invokes it exactly when the
retain count reaches zero, before freeing the object allocation. The current
`NSMutableString` adapter uses this hook to release its separately stored UTF-8
buffer.
