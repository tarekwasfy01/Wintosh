# Cross-process Mach IPC design

This document defines the next Windows implementation boundary for Mach IPC.
It is a design contract and separates implemented Stage-1 behavior from the
remaining Darwin semantics. The current `darling_windows_mach.cpp` adapter
remains process-local; the named-pipe broker now implements bounded envelopes,
queues, notifications, OOL shared mappings, reconnect persistence, and timeout
receive behavior.

## Transport boundary

The Windows broker is the transport owner. A Mach domain is identified by a
broker instance and an authenticated client session. The transport uses the
existing framed named-pipe RPC layer, but Mach messages use a separate binary
payload type and a versioned envelope:

| field | width | meaning |
|---|---:|---|
| magic | 4 | `WIPC` |
| version | 2 | protocol version with optional session token |
| operation | 2 | allocate/send/receive/deallocate/destroy |
| request id | 8 | client correlation value |
| port token | 8 | broker-issued opaque port name |
| disposition count | 4 | number of right descriptors |
| payload size | 4 | byte length of the message body |

Version 2 appends a session token to the envelope. `SessionOpen` is handled
per named-pipe worker and returns a nonzero opaque session token. Existing
zero-token requests remain a deliberate migration mode; they are not yet
authenticated and do not confer Darwin-style rights ownership.
Nonzero tokens are checked against the connection that opened the session;
the broker rejects a token replayed on another connection. The handshake may
also carry a four-byte client PID, which is recorded for future process-handle
duplication; the empty-payload legacy form remains supported. Port ownership
and explicit right transfer are still separate steps.
PID-bearing opens are accepted only when the broker can open the process with
the planned duplication/query rights. OOL Receive now uses that PID to
duplicate the mapping handle into the target process and returns its native
value in the version-2 envelope; the local ABI also implements
`mach_port_extract_right` for the supported move/copy/make dispositions.
Security policy, complete client-side handle adoption, and full Mach VM
lifetime semantics remain incomplete.

The adjacent Mach VM adapter also exposes a `mach_vm_region` basic-info path
backed by `VirtualQueryEx`; it reports the containing Windows region and a
conservative Darwin protection mask for the supported basic flavor. Named
submaps, shared regions, inheritance metadata, and other Darwin flavors are
not represented.
`CapabilityTransfer` now moves ownership of a session-owned port to another
active session identified by an eight-byte target-session payload; the source
loses access and the target can use and destroy the port. This is a bounded
broker capability approximation, not full Mach right/disposition transfer.
Ports allocated with a nonzero session token are now session-owned: lifecycle,
send, and receive operations must present the same token. Zero-token ports
remain legacy shared objects during migration; notification ownership is
recorded and OOL regions inherit their owning port's session metadata, but
dedicated enforcement and true Windows handle transfer are still pending.
On connection loss, tracked session-owned ports, notifications, and OOL
regions are reclaimed while the legacy token-0 namespace persists. This is
broker teardown, not yet full Mach port-death or kernel VM semantics. The
broker smoke also waits for transport teardown and rejects a later transfer
to the stale session.

All multi-byte fields are little-endian and all lengths are bounded before
allocation. The broker must reject truncated frames, unknown versions,
unknown operations, duplicate request IDs, and payloads above the configured
message limit.

## Port and right model

Port names are opaque within the broker; a Windows process must not treat a
token as a kernel handle or reuse it after deallocation. The broker stores
receive queues and capability-token state, while clients hold opaque tokens.
Queues are bounded at 1024 messages and one OOL descriptor is supported as a
Windows shared-mapping approximation.
The legacy token-0 transport still deliberately uses broker-wide tokens for
the cross-client compatibility proof. Nonzero session-owned tokens now have
ownership checks, explicit `CapabilityTransfer`, stale-session rejection, and
teardown cleanup; this remains a bounded approximation rather than complete
Darwin rights ownership.
Tokens are now generated from the Windows C++ random-device source rather than
an incrementing counter, so the trivial next-token guessing primitive is gone.
Legacy token-0 mode has no authentication; nonzero session mode adds scoped
ownership but is not equivalent to kernel-managed Mach rights.
Send-right transfer is represented by a broker-issued capability descriptor,
never by a raw Windows `HANDLE`. A descriptor is consumed exactly once on a
successful transfer and is invalidated on session teardown.

## Receive and failure semantics

Receive supports bounded timeout and an explicit `MACH_RCV_TOO_LARGE`-style
response containing the required inline size without consuming the message.
Queue shutdown returns a stable port-dead result. Session-authenticated request
cancellation is now available through the `Cancel` operation: a blocked
`Receive` registers its nonzero request ID, a matching session can wake it, and
the worker returns `MACH_REQUEST_CANCELLED`. Request cancellation by
request ID is still unimplemented.
Broker reconnect preserves the shared namespace, and the current broker uses a
joinable worker per accepted connection. The smoke gate proves a two-client
send/receive through one shared port token. Workers preserve the shared state
mutex and serialize writes through their own connection; session ownership is
still not equivalent to Darwin rights.

## Security and provenance

The named pipe must use a per-session name and an explicit ACL; the current
development broker's broad local ACL is not sufficient for a production Mach
transport. Payloads are copied and bounds-checked at the broker boundary.
No Darling, Apple, or XNU implementation source is copied by this design.
The behavior target is Darling's GPL/component boundary and the Mach contract
observed from the retained Darling/PureDarwin references; new Wintosh adapter
code remains subject to the repository's documented GPL boundary.

## Current gaps

The following are still unimplemented or approximated: per-task
rights/disposition transfer, full Windows handle-lifetime equivalence, OOL
protection/lifetime semantics, request cancellation, MIG descriptors, and
full Mach error-code parity. The current broker gates prove bounded transport
primitives, not an unmodified Darling Mach runtime or arbitrary macOS
application execution.

## Worker-pool implementation contract

The worker pool, controlled shutdown/join, two-client notification wakeup, and
two-client blocked-receive wakeup test are now in place with the existing
shared state. Remaining worker hardening is cancellation and complete rights
semantics. The accept loop owns listening instances; `BrokerState` owns ports,
queues, OOL mappings, notifications, counters, and the state mutex.
