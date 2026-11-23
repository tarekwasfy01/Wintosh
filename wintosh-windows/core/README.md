# Core

Bootstrap, runtime initialization and the broker entry point shared by the
Wintosh Windows adapter.

## Broker Mach rights

Each broker port stores the right-count vector
`r = (receive, send, send-once)`. A `MOVE_*` descriptor subtracts one right
from the sender while the message is queued; successful receive adds that
right to the receiver and changes the port's session owner from sender to
receiver. The send-side count update is rolled back if validation or queueing
fails. `COPY_SEND` and `MAKE_SEND*` update aggregate counts, but this broker
still has one session owner per port; independent simultaneous per-session
send rights are not represented yet.
