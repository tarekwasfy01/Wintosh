/* Wintosh Mach message destruction smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <cstdint>
#include <iostream>

int main()
{
	const auto task = darling_windows_mach_task_self();
	darling_mach_port_name_t port = 0;
	if (darling_windows_mach_port_allocate(task, &port) != 0 || port == 0)
		return 1;

	struct Message final {
		darling_mach_msg_header header;
		darling_mach_msg_body body;
		darling_mach_msg_port_descriptor descriptor;
	} message{};
	message.header.msgh_bits = darling_mach_msg_complex;
	message.header.msgh_size = sizeof(message);
	message.body.msgh_descriptor_count = 1;
	message.descriptor.name = port;
	message.descriptor.disposition = darling_mach_copy_receive;
	message.descriptor.type = darling_mach_msg_descriptor_port;

	const auto destroy_result = darling_windows_mach_msg_destroy(&message.header);
	std::uint32_t receive_refs = 0;
	const auto refs_result = darling_windows_mach_port_get_refs(
		task, port, darling_mach_port_type_receive, &receive_refs);
	const bool passed = destroy_result == 0 && refs_result != 0;
	if (port != 0) (void)darling_windows_mach_port_destroy(task, port);
	if (!passed) {
		std::cerr << "MACH_MSG_DESTROY_RESULT=" << destroy_result
			<< " REFS_RESULT=" << refs_result
			<< " RECEIVE_REFS=" << receive_refs << "\n";
		return 1;
	}
	std::cout << "MACH_MSG_DESTROY_PORT_RIGHT=PASS\n";
	return 0;
}
