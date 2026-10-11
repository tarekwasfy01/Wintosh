/* Wintosh Mach non-destructive port-peek smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <cstdint>
#include <iostream>

int main()
{
	const auto task = darling_windows_mach_task_self();
	darling_mach_port_name_t port = 0;
	if (darling_windows_mach_port_allocate(task, &port) != 0 || port == 0) return 1;
	struct Message final { darling_mach_msg_header header; std::uint32_t value; } outgoing{};
	outgoing.header.msgh_size = sizeof(outgoing);
	outgoing.header.msgh_remote_port = port;
	outgoing.header.msgh_id = 0x706b;
	outgoing.value = 0xfeedface;
	const auto send = darling_windows_mach_msg(&outgoing.header, darling_mach_send_msg,
		sizeof(outgoing), 0, 0, 0, 0);
	std::uint32_t sequence = 0, message_size = 0, trailer_size = sizeof(darling_mach_msg_seqno_trailer);
	darling_mach_msg_id_t message_id = 0;
	darling_mach_msg_seqno_trailer trailer{};
	const auto peek = darling_windows_mach_port_peek(task, port, 0x01000000,
		&sequence, &message_size, &message_id, &trailer, &trailer_size);
	Message received{};
	const auto receive = darling_windows_mach_msg(&received.header,
		darling_mach_receive_msg | darling_mach_receive_timeout, 0, sizeof(received), port, 100, 0);
	if (port != 0) (void)darling_windows_mach_port_destroy(task, port);
	if (send != 0 || peek != 0 || sequence == 0 || message_size != sizeof(outgoing) ||
		message_id != outgoing.header.msgh_id || trailer_size != sizeof(trailer) ||
		trailer.msgh_seqno != sequence || receive != 0 || received.header.msgh_id != message_id ||
		received.value != outgoing.value) {
		std::cerr << "MACH_PEEK_SEND=" << send << " PEEK=" << peek
			<< " RECEIVE=" << receive << "\n";
		return 1;
	}
	std::cout << "MACH_PEEK=PASS SEQ=" << sequence << "\n";
	return 0;
}
