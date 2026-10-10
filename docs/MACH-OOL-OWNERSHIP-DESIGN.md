# Mach OOL ownership contract

This document defines the next native Windows implementation boundary for
Mach out-of-line (OOL) memory. It is a design contract, not a claim that the
full Darwin VM contract is already implemented.

## Current boundary

Wintosh currently accepts one local virtual-copy OOL descriptor, validates its
address with a checked, page-aware `VirtualQuery` readability walk, copies the
payload into a native `VirtualAlloc` block, and
transports the rewritten descriptor through the local Mach queue. Both the
`mach_msg` receive path and direct `mach_port_receive` promote the queue
reference to receiver ownership, which is released through the native ABI
helper. OOL deallocation and physical-copy requests are rejected.

## Required ownership states

Every accepted physical OOL copy must have one table entry containing:

- source address and byte length;
- host allocation handle and mapped address;
- sender reference and queued-message reference counts;
- receiver reference count;
- deallocation policy and copy mode;
- owning local task or broker connection identity.

The entry is created before queue insertion, transferred atomically with the
message, and released only after the receiver and all broker-owned references
are gone. A raw guest pointer must never be passed to `VirtualFree`.

## Native Windows phases

1. Validate the descriptor and checked size arithmetic.
2. Allocate a Wintosh-owned region with `VirtualAlloc` and copy readable source
   bytes into it.
3. Store the allocation in the ownership table and rewrite the delivered
   descriptor to the owned mapping.
4. Retain the table entry while the message is queued and while the receiver
   owns the mapping.
5. Release with `VirtualFree` only after the ownership count reaches zero.
6. For a different process, replace the local mapping with an explicit broker
   transfer; never reuse the sender's pointer value.

## Explicit non-goals

This contract does not yet implement Darwin VM protection, copy-on-write,
inheritance, purgeable memory, wired memory, Mach trailers, MIG, or complete
cross-task name translation. Those remain separate matrix rows.

The first implementation slice is now present as
`MachOolOwnershipTable`: it tracks queue and receiver references under a
mutex and invokes the supplied release callback only after the final reference
is released. The local `mach_msg` path now uses this table for its virtual-copy
mapping; raw guest mappings and physical OOL copying remain outside the
boundary.

## Provenance and license

This is original Wintosh design documentation for a GPL-3.0-compatible adapter.
Darwin/XNU and Darling are specification and behavior references only; no
Apple/XNU source is copied by this document. See `licenses/` and
`docs/SOURCE-EXTRACTION-MATRIX.md` for provenance boundaries.
