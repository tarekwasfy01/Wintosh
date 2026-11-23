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

The broker envelope protocol is version 8. `remote_disposition` carries the
destination right, while `local_port_token` and `local_disposition` carry the
reply capability independently of body descriptors. Send validates the
session's right, accounts move/copy/make reply rights while queued, and
materializes the reply port in the receiver's local capability table. This is
a selected Mach header-right subset; vouchers, complete receive-right
accounting, and complete port-death/notification propagation remain open.
