/*
 * Focused Windows socket-family smoke for the Darling ABI adapter.
 * GPL-3.0-only; see the bundled license and source manifests.
 */
#include "darling_windows_stdio.h"

#include <windows.h>
#include <winsock2.h>
#include <cstring>
#include <iostream>

int main()
{
	if (darling_windows_host_symbol("socket") == 0 ||
		darling_windows_host_symbol("bind") == 0 ||
		darling_windows_host_symbol("listen") == 0 ||
		darling_windows_host_symbol("connect") == 0 ||
		darling_windows_host_symbol("accept") == 0)
		return 1;
	int listener = darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener < 0) return 2;
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(0);
	int address_length = sizeof(address);
	if (darling_windows_bind(listener, &address, sizeof(address)) != 0 ||
		darling_windows_getsockname(listener, &address, &address_length) != 0 ||
		darling_windows_listen(listener, 1) != 0) {
		darling_windows_close(listener);
		return 3;
	}
	int client = darling_windows_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client < 0 || darling_windows_connect(client, &address, sizeof(address)) != 0) {
		if (client >= 0) darling_windows_close(client);
		darling_windows_close(listener);
		return 4;
	}
	int peer = darling_windows_accept(listener, nullptr, nullptr);
	if (peer < 0) {
		darling_windows_close(client);
		darling_windows_close(listener);
		return 5;
	}
	int keepalive = 1;
	int keepalive_readback = 0;
	int keepalive_length = sizeof(keepalive_readback);
	constexpr int darwin_so_nosigpipe = 0x1022;
	constexpr int darwin_so_reuseport = 0x0200;
	int nosigpipe = 1;
	int reuseport = 1;
	int option_readback = 0;
	int option_length = sizeof(option_readback);
	const int duplicate = darling_windows_fcntl(client, 0, 40);
	const bool options_ok = darling_windows_setsockopt(client, SOL_SOCKET, SO_KEEPALIVE,
		&keepalive, sizeof(keepalive)) == 0 &&
		darling_windows_getsockopt(client, SOL_SOCKET, SO_KEEPALIVE,
		&keepalive_readback, &keepalive_length) == 0 && keepalive_readback != 0 &&
		darling_windows_setsockopt(client, SOL_SOCKET, darwin_so_nosigpipe,
		&nosigpipe, sizeof(nosigpipe)) == 0 &&
		darling_windows_getsockopt(client, SOL_SOCKET, darwin_so_nosigpipe,
		&option_readback, &option_length) == 0 && option_readback == 1 &&
		darling_windows_setsockopt(client, SOL_SOCKET, darwin_so_reuseport,
		&reuseport, sizeof(reuseport)) == 0 &&
		darling_windows_getsockopt(client, SOL_SOCKET, darwin_so_reuseport,
		&option_readback, &option_length) == 0 && option_readback == 1;
	const bool duplicate_ok = duplicate >= 40 &&
		darling_windows_fcntl(duplicate, 4, 0x0004) == 0 &&
		(darling_windows_fcntl(duplicate, 3) & 0x0004) != 0;
	const char payload[] = "socket-smoke";
	char received[sizeof(payload)]{};
	const bool io_ok = darling_windows_write(client, payload, sizeof(payload) - 1) ==
		static_cast<int>(sizeof(payload) - 1) &&
		darling_windows_read(peer, received, sizeof(payload) - 1) ==
		static_cast<int>(sizeof(payload) - 1) &&
		std::memcmp(received, payload, sizeof(payload) - 1) == 0;
	const char message_payload[] = "msg-smoke";
	char message_received[sizeof(message_payload)]{};
	darling_iovec send_iov{const_cast<char*>(message_payload), sizeof(message_payload) - 1};
	darling_iovec recv_iov{message_received, sizeof(message_payload) - 1};
	darling_msghdr send_message{};
	send_message.msg_iov = &send_iov;
	send_message.msg_iovlen = 1;
	darling_msghdr recv_message{};
	recv_message.msg_iov = &recv_iov;
	recv_message.msg_iovlen = 1;
	const bool message_ok = darling_windows_sendmsg(client, &send_message, 0) ==
		static_cast<int>(sizeof(message_payload) - 1) &&
		darling_windows_recvmsg(peer, &recv_message, 0) ==
		static_cast<int>(sizeof(message_payload) - 1) &&
		std::memcmp(message_received, message_payload, sizeof(message_payload) - 1) == 0;
	unsigned char ipv6_address[16]{};
	char ipv6_text[46]{};
	const bool ipv6_ok = darling_windows_inet_pton(AF_INET6, "2001:db8::17",
		ipv6_address) == 1 &&
		darling_windows_inet_ntop(AF_INET6, ipv6_address, ipv6_text,
		sizeof(ipv6_text)) != nullptr && std::strcmp(ipv6_text, "2001:db8::17") == 0;
	darling_windows_close(peer);
	if (duplicate >= 0) darling_windows_close(duplicate);
	darling_windows_close(client);
	darling_windows_close(listener);
	if (!io_ok || !message_ok || !ipv6_ok || !options_ok || !duplicate_ok) return 6;
	std::cout << "DARWIN_SOCKET_SMOKE=PASS\n";
	return 0;
}
