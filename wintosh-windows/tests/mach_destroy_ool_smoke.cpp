/* Wintosh Mach OOL destruction smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

struct OolMessage final {
	darling_mach_msg_header header;
	darling_mach_msg_body body;
	darling_mach_msg_ool_descriptor descriptor;
};

int main()
{
	const auto task = darling_windows_mach_task_self();
	darling_mach_port_name_t port = 0;
	if (darling_windows_mach_port_allocate(task, &port) != 0 || port == 0)
		return 1;

	std::uint32_t source_value = 0xdecafbad;
	OolMessage outgoing{};
	outgoing.header.msgh_bits = darling_mach_msg_complex;
	outgoing.header.msgh_size = sizeof(outgoing);
	outgoing.header.msgh_remote_port = port;
	outgoing.body.msgh_descriptor_count = 1;
	outgoing.descriptor.address = reinterpret_cast<std::uint64_t>(&source_value);
	outgoing.descriptor.size = sizeof(source_value);
	outgoing.descriptor.copy = 0;
	outgoing.descriptor.type = darling_mach_msg_descriptor_ool;

	std::array<std::uint8_t, sizeof(OolMessage) + sizeof(darling_mach_msg_trailer)> received{};
	const auto send_result = darling_windows_mach_msg(
		&outgoing.header, darling_mach_send_msg, sizeof(outgoing), 0, 0, 0, 0);
	const auto receive_result = send_result == 0 ? darling_windows_mach_msg(
		reinterpret_cast<darling_mach_msg_header*>(received.data()),
		darling_mach_receive_msg | darling_mach_receive_timeout, 0,
		static_cast<std::uint32_t>(received.size()), port, 100, 0) : 4;
	const auto* incoming = reinterpret_cast<const OolMessage*>(received.data());
	const auto* payload = incoming->descriptor.address == 0 ? nullptr :
		reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(incoming->descriptor.address));
	const bool payload_ok = receive_result == 0 && payload != nullptr &&
		*payload == source_value;
	const auto destroy_result = payload_ok ? darling_windows_mach_msg_destroy(
		const_cast<darling_mach_msg_header*>(reinterpret_cast<const darling_mach_msg_header*>(received.data()))) : 4;
	const auto second_release = payload_ok ? darling_windows_mach_ool_release(
		const_cast<std::uint32_t*>(payload)) : 0;
	if (port != 0) (void)darling_windows_mach_port_destroy(task, port);
	if (send_result != 0 || receive_result != 0 || destroy_result != 0 || second_release == 0) {
		std::cerr << "MACH_MSG_DESTROY_OOL_SEND=" << send_result
			<< " RECEIVE=" << receive_result
			<< " DESTROY=" << destroy_result
			<< " SECOND_RELEASE=" << second_release << "\n";
		return 1;
	}
	std::cout << "MACH_MSG_DESTROY_OOL=PASS\n";
	return 0;
}
