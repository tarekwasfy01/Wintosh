# Cross-process Mach IPC design

This document defines the next Windows implementation boundary for Mach IPC.
It is a design contract, not a claim that the feature is implemented yet.
The current `darling_windows_mach.cpp` adapter remains process-local; the
existing named-pipe broker currently exposes only bootstrap RPC.

## Transport boundary

The Windows broker is the transport owner. A Mach domain is identified by a
broker instance and an authenticated client session. The transport uses the
existing framed named-pipe RPC layer, but Mach messages use a separate binary
payload type and a versioned envelope:

| field | width | meaning |
|---|---:|---|
| magic | 4 | `WIPC` |
| version | 2 | protocol version, initially `1` |
| operation | 2 | allocate/send/receive/deallocate/destroy |
| request id | 8 | client correlation value |
| port token | 8 | broker-issued opaque port name |
| disposition count | 4 | number of right descriptors |
| payload size | 4 | byte length of the message body |

All multi-byte fields are little-endian and all lengths are bounded before
allocation. The broker must reject truncated frames, unknown versions,
unknown operations, duplicate request IDs, and payloads above the configured
message limit.

## Port and right model

Port names are opaque per broker session; a Windows process must not treat a
token as a kernel handle or reuse it after deallocation. The broker stores
receive queues and reference counts, while clients hold capability tokens.
Send-right transfer is represented by a broker-issued capability descriptor,
never by a raw Windows `HANDLE`. A descriptor is consumed exactly once on a
successful transfer and is invalidated on session teardown.

## Receive and failure semantics

Receive supports bounded timeout, cancellation by request ID, and an explicit
`MACH_RCV_TOO_LARGE`-style response containing the required size without
consuming the message. Queue shutdown returns a stable port-dead result.
Broker disconnect invalidates session-owned send and receive rights and
removes all session memberships from port sets.

## Security and provenance

The named pipe must use a per-session name and an explicit ACL; the current
development broker's broad local ACL is not sufficient for a production Mach
transport. Payloads are copied and bounds-checked at the broker boundary.
No Darling, Apple, or XNU implementation source is copied by this design.
The behavior target is Darling's GPL/component boundary and the Mach contract
observed from the retained Darling/PureDarwin references; new Wintosh adapter
code remains subject to the repository's documented GPL boundary.

## Current gaps

The following are still unimplemented: broker-backed port allocation and
lookup, cross-process send/receive, rights dispositions, port-set waiters,
notifications, cancellation, MIG descriptors, out-of-line memory, and full
Mach error-code parity. Until those gates are implemented and tested, the
project must continue to describe Mach IPC as process-local only.
