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

For a reply capability `p` sent from session `s` to destination owner `d`, the
broker keeps the sender and in-flight counts distinct:

| Disposition | Send transition | Receive transition |
|---|---|---|
| `MOVE_SEND` (17) | `S[p,s] -= 1; T[p] += 1` | `T[p] -= 1; S[p,d] += 1` |
| `MOVE_SEND_ONCE` (18) | `O[p,s] -= 1; T_o[p] += 1` | `T_o[p] -= 1; O[p,d] += 1` |
| `COPY_SEND` (19) | `G_send[p] += 1` | `S[p,d] += 1` |
| `MAKE_SEND_ONCE` (21) | require receive right; `G_once[p] += 1` | `O[p,d] += 1` |

Here `S` and `O` are per-session send and send-once rights, `T` and `T_o`
are queued transit rights, and `G` is the broker aggregate. The wire fields
`local_port_token` and `local_disposition` carry this capability separately
from descriptors. The disposition values follow Apple's
[XNU Mach message definitions](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/message.h).

The broker envelope protocol is version 8. `remote_disposition` carries the
destination right, while `local_port_token` and `local_disposition` carry the
reply capability independently of body descriptors. Send validates the
session's right, accounts move/copy/make reply rights while queued, and
materializes the reply port in the receiver's local capability table. This is
a selected Mach header-right subset; vouchers, complete receive-right
accounting, and complete port-death/notification propagation remain open. If
the reply receive port is destroyed before delivery, the broker preserves the
request but delivers it without a reply capability; XNU dead-name notification
and exact destroyed-right semantics are not modeled yet.
