/* Wintosh Mach sequence-number trailer smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <array>
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
	outgoing.header.msgh_id = 0x5151;
	outgoing.value = 0xabcddcba;
	const auto send = darling_windows_mach_msg(&outgoing.header, darling_mach_send_msg,
		sizeof(outgoing), 0, 0, 0, 0);
	std::array<std::uint8_t, sizeof(Message) + sizeof(darling_mach_msg_seqno_trailer)> received{};
	const auto receive = darling_windows_mach_msg(
		reinterpret_cast<darling_mach_msg_header*>(received.data()),
		darling_mach_receive_msg | darling_mach_receive_timeout | darling_mach_receive_large_identity,
		0, static_cast<std::uint32_t>(received.size()), port, 100, 0);
	const auto* header = reinterpret_cast<const darling_mach_msg_header*>(received.data());
	const auto* trailer = reinterpret_cast<const darling_mach_msg_seqno_trailer*>(
		received.data() + sizeof(Message));
	if (port != 0) (void)darling_windows_mach_port_destroy(task, port);
	if (send != 0 || receive != 0 || header->msgh_id != outgoing.header.msgh_id ||
		header->msgh_size != sizeof(Message) + sizeof(darling_mach_msg_seqno_trailer) ||
		trailer->trailer.size != sizeof(darling_mach_msg_seqno_trailer) ||
		trailer->msgh_seqno == 0) {
		std::cerr << "MACH_SEQNO_SEND=" << send << " RECEIVE=" << receive << "\n";
		return 1;
	}
	std::cout << "MACH_SEQNO_TRAILER=PASS SEQ=" << trailer->msgh_seqno << "\n";
	return 0;
}
