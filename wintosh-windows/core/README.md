# Core

Bootstrap, runtime initialization and the broker entry point shared by the
Wintosh Windows adapter.

## Broker Mach rights

For port `p` and session `s`, the broker models the send-right row
`R[p,s] = (send, send-once)`; each port's receive right has one session owner.
`MOVE_SEND*` subtracts from the sender when queued and adds to the receiver
after successful receive. `COPY_SEND` and `MAKE_SEND*` preserve the sender's
row and assign the new right to the receiver when delivered. The aggregate
`PortRights` totals are updated at send time; a move temporarily leaves the
aggregate while in flight, whereas a copy or make is counted while queued.
Failed validation or queueing restores the aggregate and sender row. Full
receive-right reference counting and complete port-death/notification
propagation remain open Mach-kernel behaviors.

The broker envelope protocol is version 7. Its `remote_disposition` field
keeps the `msgh_remote_port` right separate from the message's port
descriptors, so the broker can validate and consume a move-send or move-send-once
right at send time.
The broker path accepts remote dispositions 17–21; local loopback currently
does not support header `MAKE_SEND` or `MAKE_SEND_ONCE`.
