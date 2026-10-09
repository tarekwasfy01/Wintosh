/* Focused Windows socket-family smoke for the Darling ABI adapter. */
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
	const char payload[] = "socket-smoke";
	char received[sizeof(payload)]{};
	const bool io_ok = darling_windows_write(client, payload, sizeof(payload) - 1) ==
		static_cast<int>(sizeof(payload) - 1) &&
		darling_windows_read(peer, received, sizeof(payload) - 1) ==
		static_cast<int>(sizeof(payload) - 1) &&
		std::memcmp(received, payload, sizeof(payload) - 1) == 0;
	darling_windows_close(peer);
	darling_windows_close(client);
	darling_windows_close(listener);
	if (!io_ok) return 6;
	std::cout << "DARWIN_SOCKET_SMOKE=PASS\n";
	return 0;
}
