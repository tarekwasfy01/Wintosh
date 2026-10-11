/* Wintosh Mach receive-too-large smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

int main()
{
	const auto task = darling_windows_mach_task_self();
	darling_mach_port_name_t port = 0;
	if (darling_windows_mach_port_allocate(task, &port) != 0 || port == 0)
		return 1;
	struct Message final {
		darling_mach_msg_header header;
		std::uint32_t payload;
	} outgoing{};
	outgoing.header.msgh_size = sizeof(outgoing);
	outgoing.header.msgh_remote_port = port;
	outgoing.header.msgh_id = 0x4242;
	outgoing.payload = 0x12345678;
	const auto send_result = darling_windows_mach_msg(
		&outgoing.header, darling_mach_send_msg, sizeof(outgoing), 0, 0, 0, 0);
	std::array<std::uint8_t, sizeof(darling_mach_msg_header)> small{};
	const auto too_large = darling_windows_mach_msg(
		reinterpret_cast<darling_mach_msg_header*>(small.data()),
		darling_mach_receive_msg | darling_mach_receive_timeout | darling_mach_receive_large, 0,
		static_cast<std::uint32_t>(small.size()), port, 100, 0);
	Message received{};
	const auto retry = darling_windows_mach_msg(
		&received.header, darling_mach_receive_msg | darling_mach_receive_timeout, 0,
		sizeof(received), port, 100, 0);
	if (port != 0) (void)darling_windows_mach_port_destroy(task, port);
	if (send_result != 0 || too_large != 0x10004003 || retry != 0 ||
		received.header.msgh_id != outgoing.header.msgh_id ||
		received.payload != outgoing.payload) {
		std::cerr << "MACH_RCV_SEND=" << send_result
			<< " TOO_LARGE=" << too_large
			<< " RETRY=" << retry << "\n";
		return 1;
	}
	std::cout << "MACH_RCV_TOO_LARGE_PRESERVED=PASS\n";
	return 0;
}
