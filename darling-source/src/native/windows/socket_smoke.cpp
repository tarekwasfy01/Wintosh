/*
 * Stage 1 Winsock BSD-socket boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_socket.h"

#include <iostream>
#include <filesystem>
#include <thread>

int main()
{
	try {
		darling::windows_host::WinsockRuntime winsock;
		auto listener = darling::windows_host::TcpSocket::ListenLoopback();
		const auto port = listener.LocalPort();
		std::string request;
		std::thread server([&] {
			auto connection = listener.Accept();
			request = connection.ReceiveExact(4);
			connection.SendAll(request == "PING" ? "PONG" : "FAIL");
		});

		auto client = darling::windows_host::TcpSocket::ConnectLoopback(port);
		client.SendAll("PING");
		const auto response = client.ReceiveExact(4);
		server.join();

		std::cout << "WINDOWS_SOCKET_REQUEST=" << request << "\n";
		std::cout << "WINDOWS_SOCKET_RESPONSE=" << response << "\n";
		// Windows AF_UNIX exposes a 108-byte sun_path; use a short relative
		// fixture name because managed TEMP roots may already consume most of it.
		wchar_t profile_buffer[32768]{};
		const DWORD profile_length = GetEnvironmentVariableW(L"USERPROFILE",
			profile_buffer, static_cast<DWORD>(std::size(profile_buffer)));
		if (profile_length == 0 || profile_length >= std::size(profile_buffer))
			throw std::runtime_error("USERPROFILE unavailable");
		const auto unix_path = (std::filesystem::path(
			std::wstring(profile_buffer, profile_length)) /
			(L"darling-unix-socket-" + std::to_wstring(GetCurrentProcessId()))).string();
		darling::windows_host::UnixSocket unix_listener =
			darling::windows_host::UnixSocket::Listen(unix_path);
		std::string unix_request;
		std::thread unix_server([&] {
			auto connection = unix_listener.Accept();
			unix_request = connection.ReceiveExact(4);
			connection.SendAll(unix_request == "PING" ? "PONG" : "FAIL");
		});
		auto unix_client = darling::windows_host::UnixSocket::Connect(unix_path);
		unix_client.SendAll("PING");
		const auto unix_response = unix_client.ReceiveExact(4);
		unix_server.join();
		std::cout << "WINDOWS_UNIX_SOCKET_REQUEST=" << unix_request << "\n";
		std::cout << "WINDOWS_UNIX_SOCKET_RESPONSE=" << unix_response << "\n";
		auto socket_pair = darling::windows_host::UnixSocket::Pair();
		socket_pair.first.SendAll("PAIR");
		const auto pair_request = socket_pair.second.ReceiveExact(4);
		socket_pair.second.SendAll("BACK");
		const auto pair_response = socket_pair.first.ReceiveExact(4);
		std::cout << "WINDOWS_UNIX_SOCKETPAIR_REQUEST=" << pair_request << "\n";
		std::cout << "WINDOWS_UNIX_SOCKETPAIR_RESPONSE=" << pair_response << "\n";
		const auto datagram_supported =
			darling::windows_host::UnixDatagramSocket::Supported();
		if (!datagram_supported)
			throw std::runtime_error("Darwin datagram transport unavailable");
		const auto datagram_server_path = unix_path + "-dgram-server";
		const auto datagram_client_path = unix_path + "-dgram-client";
		auto datagram_server = darling::windows_host::UnixDatagramSocket::Bind(
			datagram_server_path);
		auto datagram_client = darling::windows_host::UnixDatagramSocket::Bind(
			datagram_client_path);
		datagram_client.SendTo(datagram_server_path, "PING");
		const auto datagram_request = datagram_server.Receive(32);
		datagram_server.SendTo(datagram_client_path, "PONG");
		const auto datagram_response = datagram_client.Receive(32);
		std::cout << "WINDOWS_UNIX_DGRAM_REQUEST=" << datagram_request << "\n";
		std::cout << "WINDOWS_UNIX_DGRAM_RESPONSE=" << datagram_response << "\n";
		std::cout << "WINDOWS_UNIX_DGRAM="
			<< (datagram_server.Emulated() ? "EMULATED" : "NATIVE") << "\n";
		const auto sequenced_path = unix_path + "-seqpacket";
		auto sequenced_listener =
			darling::windows_host::UnixSequencedPacketSocket::Bind(sequenced_path);
		std::string sequenced_request;
		std::thread sequenced_server([&] {
			auto connection = sequenced_listener.Accept();
			sequenced_request = connection.ReceivePacket(32);
			connection.SendPacket(sequenced_request == "PING" ? "PONG" : "FAIL");
		});
		auto sequenced_client =
			darling::windows_host::UnixSequencedPacketSocket::Connect(sequenced_path);
		sequenced_client.SendPacket("PING");
		const auto sequenced_response = sequenced_client.ReceivePacket(32);
		sequenced_server.join();
		std::cout << "WINDOWS_UNIX_SEQPACKET_REQUEST=" << sequenced_request << "\n";
		std::cout << "WINDOWS_UNIX_SEQPACKET_RESPONSE=" << sequenced_response << "\n";
		std::cout << "WINDOWS_UNIX_SEQPACKET="
			<< (sequenced_listener.Emulated() ? "EMULATED" : "NATIVE") << "\n";
		return request == "PING" && response == "PONG" &&
			unix_request == "PING" && unix_response == "PONG" &&
			pair_request == "PAIR" && pair_response == "BACK" &&
			datagram_request == "PING" && datagram_response == "PONG" &&
			sequenced_request == "PING" && sequenced_response == "PONG" ? 0 : 2;
	} catch (const std::exception& error) {
		std::cerr << "SOCKET_SMOKE_ERROR=" << error.what() << "\n";
		return 3;
	}
}
