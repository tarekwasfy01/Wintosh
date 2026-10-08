/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_socket.h"
#include "darling_windows_errno.h"

#include <limits>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowSocketError(const char* operation)
{
	const int error = WSAGetLastError();
	switch (error) {
	case WSAEINTR:
		DarwinErrno::Set(4);
		break;
	case WSAEWOULDBLOCK:
		DarwinErrno::Set(35);
		break;
	case WSAECONNRESET:
		DarwinErrno::Set(54);
		break;
	case WSAECONNREFUSED:
		DarwinErrno::Set(61);
		break;
	case WSAETIMEDOUT:
		DarwinErrno::Set(60);
		break;
	default:
		DarwinErrno::Set(5);
		break;
	}
	throw std::system_error(error, std::system_category(), operation);
}

sockaddr_in LoopbackAddress(std::uint16_t port)
{
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return address;
}

std::mutex& UnixStreamRegistryMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::map<std::string, std::uint16_t>& UnixStreamRegistry()
{
	static std::map<std::string, std::uint16_t> registry;
	return registry;
}

UnixSocket ListenUnixStreamFallback(const std::string& path)
{
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX fallback socket");
	const auto address = LoopbackAddress(0);
	if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
		listen(socket, 1) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX fallback bind/listen");
	}
	sockaddr_in bound{};
	int bound_length = sizeof(bound);
	if (getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &bound_length) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX fallback address");
	}
	{
		std::lock_guard lock(UnixStreamRegistryMutex());
		if (UnixStreamRegistry().contains(path)) {
			closesocket(socket);
			throw std::runtime_error("AF_UNIX path is already bound");
		}
		UnixStreamRegistry()[path] = ntohs(bound.sin_port);
	}
	return UnixSocket(socket, path, true, true);
}

UnixSocket ConnectUnixStreamFallback(const std::string& path)
{
	std::uint16_t port = 0;
	{
		std::lock_guard lock(UnixStreamRegistryMutex());
		const auto found = UnixStreamRegistry().find(path);
		if (found == UnixStreamRegistry().end())
			throw std::system_error(WSAECONNREFUSED, std::system_category(),
				"AF_UNIX fallback connect");
		port = found->second;
	}
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX fallback socket");
	const auto address = LoopbackAddress(port);
	if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX fallback connect");
	}
	return UnixSocket(socket, {}, true, false);
}

} // namespace

WinsockRuntime::WinsockRuntime()
{
	WSADATA data{};
	const auto result = WSAStartup(MAKEWORD(2, 2), &data);
	if (result != 0) {
		throw std::system_error(result, std::system_category(), "WSAStartup");
	}
}

WinsockRuntime::~WinsockRuntime() noexcept
{
	WSACleanup();
}

TcpSocket TcpSocket::ListenLoopback(std::uint16_t port)
{
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) {
		ThrowSocketError("socket");
	}
	const auto address = LoopbackAddress(port);
	if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
		listen(socket, 1) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("bind/listen");
	}
	return TcpSocket(socket);
}

TcpSocket TcpSocket::ConnectLoopback(std::uint16_t port)
{
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) {
		ThrowSocketError("socket");
	}
	const auto address = LoopbackAddress(port);
	if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("connect");
	}
	return TcpSocket(socket);
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : m_socket(other.m_socket)
{
	other.m_socket = INVALID_SOCKET;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_socket = other.m_socket;
		other.m_socket = INVALID_SOCKET;
	}
	return *this;
}

TcpSocket::~TcpSocket() noexcept
{
	Reset();
}

std::uint16_t TcpSocket::LocalPort() const
{
	sockaddr_in address{};
	int length = sizeof(address);
	if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) {
		ThrowSocketError("getsockname");
	}
	return ntohs(address.sin_port);
}

TcpSocket TcpSocket::Accept() const
{
	const auto accepted = accept(m_socket, nullptr, nullptr);
	if (accepted == INVALID_SOCKET) {
		ThrowSocketError("accept");
	}
	return TcpSocket(accepted);
}

std::size_t TcpSocket::Send(const void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		throw std::overflow_error("socket send size exceeds Winsock range");
	}
	const auto result = send(m_socket, static_cast<const char*>(data),
		static_cast<int>(bytes), 0);
	if (result == SOCKET_ERROR) {
		ThrowSocketError("send");
	}
	return static_cast<std::size_t>(result);
}

std::size_t TcpSocket::Receive(void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		throw std::overflow_error("socket receive size exceeds Winsock range");
	}
	const auto result = recv(m_socket, static_cast<char*>(data),
		static_cast<int>(bytes), 0);
	if (result == SOCKET_ERROR) {
		ThrowSocketError("recv");
	}
	return static_cast<std::size_t>(result);
}

void TcpSocket::Shutdown() noexcept
{
	if (m_socket != INVALID_SOCKET) {
		shutdown(m_socket, SD_BOTH);
	}
}

void TcpSocket::SendAll(const std::string& data) const
{
	std::size_t offset = 0;
	while (offset < data.size()) {
		const auto result = Send(data.data() + offset, data.size() - offset);
		if (result == 0) {
			ThrowSocketError("send");
		}
		offset += result;
	}
}

std::string TcpSocket::ReceiveExact(std::size_t bytes) const
{
	std::string result(bytes, '\0');
	std::size_t offset = 0;
	while (offset < bytes) {
		const auto received = Receive(result.data() + offset, bytes - offset);
		if (received == 0) {
			ThrowSocketError("recv");
		}
		offset += received;
	}
	return result;
}

void TcpSocket::Reset() noexcept
{
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
}

namespace {

std::mutex& DatagramRegistryMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::map<std::string, sockaddr_in>& DatagramRegistry()
{
	static std::map<std::string, sockaddr_in> registry;
	return registry;
}

bool NativeUnixDatagramUnavailable(int error) noexcept
{
	return error == WSAEAFNOSUPPORT || error == WSAEPROTONOSUPPORT ||
		error == WSAESOCKTNOSUPPORT || error == WSAEOPNOTSUPP;
}

sockaddr_in EmulatedDatagramAddress(const std::string& path)
{
	std::scoped_lock lock(DatagramRegistryMutex());
	const auto endpoint = DatagramRegistry().find(path);
	if (endpoint == DatagramRegistry().end())
		throw std::runtime_error("AF_UNIX datagram path is not bound");
	return endpoint->second;
}

std::mutex& SequencedRegistryMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::map<std::string, sockaddr_in>& SequencedRegistry()
{
	static std::map<std::string, sockaddr_in> registry;
	return registry;
}

sockaddr_in SequencedAddress(const std::string& path)
{
	std::scoped_lock lock(SequencedRegistryMutex());
	const auto endpoint = SequencedRegistry().find(path);
	if (endpoint == SequencedRegistry().end())
		throw std::runtime_error("AF_UNIX sequenced-packet path is not bound");
	return endpoint->second;
}

void SendSocketBytes(SOCKET socket, const char* data, std::size_t bytes)
{
	while (bytes != 0) {
		if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			throw std::overflow_error("socket packet exceeds Winsock range");
		const auto sent = send(socket, data, static_cast<int>(bytes), 0);
		if (sent == SOCKET_ERROR || sent == 0)
			ThrowSocketError("AF_UNIX sequenced-packet send");
		data += sent;
		bytes -= static_cast<std::size_t>(sent);
	}
}

void ReceiveSocketBytes(SOCKET socket, char* data, std::size_t bytes)
{
	while (bytes != 0) {
		if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			throw std::overflow_error("socket packet exceeds Winsock range");
		const auto received = recv(socket, data, static_cast<int>(bytes), 0);
		if (received == SOCKET_ERROR || received == 0)
			ThrowSocketError("AF_UNIX sequenced-packet receive");
		data += received;
		bytes -= static_cast<std::size_t>(received);
	}
}

sockaddr_un UnixAddress(const std::string& path)
{
	if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
		throw std::invalid_argument("AF_UNIX path is empty or too long");
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.data(), path.size());
	return address;
}

} // namespace

UnixSocket UnixSocket::Listen(const std::string& path)
{
	if (path.empty())
		throw std::invalid_argument("AF_UNIX path is empty");
	if (path.size() < sizeof(sockaddr_un::sun_path)) {
		const auto address = UnixAddress(path);
		DeleteFileA(path.c_str());
		const auto socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (socket != INVALID_SOCKET) {
			if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
				listen(socket, 1) == 0)
				return UnixSocket(socket, path, false, true);
			closesocket(socket);
		}
	}
	return ListenUnixStreamFallback(path);
}

UnixSocket UnixSocket::Connect(const std::string& path)
{
	if (path.empty())
		throw std::invalid_argument("AF_UNIX path is empty");
	if (path.size() < sizeof(sockaddr_un::sun_path)) {
		const auto address = UnixAddress(path);
		const auto socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (socket != INVALID_SOCKET) {
			if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
				return UnixSocket(socket, {}, false, false);
			closesocket(socket);
		}
	}
	return ConnectUnixStreamFallback(path);
}

std::pair<UnixSocket, UnixSocket> UnixSocket::Pair()
{
	const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX socketpair listener");
	const auto address = LoopbackAddress(0);
	if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
		listen(listener, 1) == SOCKET_ERROR) {
		closesocket(listener);
		ThrowSocketError("AF_UNIX socketpair bind/listen");
	}
	sockaddr_in bound{};
	int bound_length = sizeof(bound);
	if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_length) == SOCKET_ERROR) {
		closesocket(listener);
		ThrowSocketError("AF_UNIX socketpair getsockname");
	}
	const auto client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client == INVALID_SOCKET) {
		closesocket(listener);
		ThrowSocketError("AF_UNIX socketpair client");
	}
	if (connect(client, reinterpret_cast<const sockaddr*>(&bound), sizeof(bound)) == SOCKET_ERROR) {
		closesocket(client);
		closesocket(listener);
		ThrowSocketError("AF_UNIX socketpair connect");
	}
	const auto peer = accept(listener, nullptr, nullptr);
	closesocket(listener);
	if (peer == INVALID_SOCKET) {
		closesocket(client);
		ThrowSocketError("AF_UNIX socketpair accept");
	}
	return {UnixSocket(client, {}, false, false), UnixSocket(peer, {}, false, false)};
}

UnixSocket::UnixSocket(UnixSocket&& other) noexcept :
	m_socket(other.m_socket), m_path(std::move(other.m_path)),
	m_emulated(other.m_emulated), m_listener(other.m_listener)
{
	other.m_socket = INVALID_SOCKET;
	other.m_emulated = false;
	other.m_listener = false;
}

UnixSocket& UnixSocket::operator=(UnixSocket&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_socket = other.m_socket;
		m_path = std::move(other.m_path);
		m_emulated = other.m_emulated;
		m_listener = other.m_listener;
		other.m_socket = INVALID_SOCKET;
		other.m_emulated = false;
		other.m_listener = false;
	}
	return *this;
}

UnixSocket::~UnixSocket() noexcept
{
	Reset();
}

UnixSocket UnixSocket::Accept() const
{
	const auto accepted = accept(m_socket, nullptr, nullptr);
	if (accepted == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX accept");
	return UnixSocket(accepted, {}, m_emulated, false);
}

std::size_t UnixSocket::Send(const void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::overflow_error("AF_UNIX send size exceeds Winsock range");
	const auto result = send(m_socket, static_cast<const char*>(data),
		static_cast<int>(bytes), 0);
	if (result == SOCKET_ERROR)
		ThrowSocketError("AF_UNIX send");
	return static_cast<std::size_t>(result);
}

std::size_t UnixSocket::Receive(void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::overflow_error("AF_UNIX receive size exceeds Winsock range");
	const auto result = recv(m_socket, static_cast<char*>(data),
		static_cast<int>(bytes), 0);
	if (result == SOCKET_ERROR)
		ThrowSocketError("AF_UNIX recv");
	return static_cast<std::size_t>(result);
}

void UnixSocket::SendAll(const std::string& data) const
{
	std::size_t offset = 0;
	while (offset < data.size()) {
		const auto sent = Send(data.data() + offset, data.size() - offset);
		if (sent == 0)
			ThrowSocketError("AF_UNIX send");
		offset += sent;
	}
}

std::string UnixSocket::ReceiveExact(std::size_t bytes) const
{
	std::string result(bytes, '\0');
	std::size_t offset = 0;
	while (offset < bytes) {
		const auto received = Receive(result.data() + offset, bytes - offset);
		if (received == 0)
			ThrowSocketError("AF_UNIX recv");
		offset += received;
	}
	return result;
}

void UnixSocket::Reset() noexcept
{
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	if (m_emulated && m_listener && !m_path.empty()) {
		std::lock_guard lock(UnixStreamRegistryMutex());
		UnixStreamRegistry().erase(m_path);
	}
	if (!m_emulated && !m_path.empty()) {
		DeleteFileA(m_path.c_str());
		m_path.clear();
	}
}

UnixDatagramSocket UnixDatagramSocket::Bind(const std::string& path)
{
	const auto address = UnixAddress(path);
	DeleteFileA(path.c_str());
	const auto socket = ::socket(AF_UNIX, SOCK_DGRAM, 0);
	if (socket != INVALID_SOCKET) {
		if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
			return UnixDatagramSocket(socket, path, false);
		const int error = WSAGetLastError();
		closesocket(socket);
		if (!NativeUnixDatagramUnavailable(error))
			ThrowSocketError("AF_UNIX datagram bind");
	}

	const auto fallback = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fallback == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX datagram fallback socket");
	sockaddr_in fallback_address{};
	fallback_address.sin_family = AF_INET;
	fallback_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	fallback_address.sin_port = htons(0);
	if (bind(fallback, reinterpret_cast<const sockaddr*>(&fallback_address),
		sizeof(fallback_address)) == SOCKET_ERROR) {
		closesocket(fallback);
		ThrowSocketError("AF_UNIX datagram fallback bind");
	}
	int address_length = sizeof(fallback_address);
	if (getsockname(fallback, reinterpret_cast<sockaddr*>(&fallback_address),
		&address_length) == SOCKET_ERROR) {
		closesocket(fallback);
		ThrowSocketError("AF_UNIX datagram fallback address");
	}
	{
		std::scoped_lock lock(DatagramRegistryMutex());
		if (DatagramRegistry().contains(path)) {
			closesocket(fallback);
			throw std::runtime_error("AF_UNIX datagram path is already bound");
		}
		DatagramRegistry().emplace(path, fallback_address);
	}
	return UnixDatagramSocket(fallback, path, true);
}

bool UnixDatagramSocket::Supported() noexcept
{
	const auto socket = ::socket(AF_UNIX, SOCK_DGRAM, 0);
	if (socket != INVALID_SOCKET) {
		closesocket(socket);
		return true;
	}
	const auto fallback = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fallback == INVALID_SOCKET)
		return false;
	closesocket(fallback);
	return true;
}

UnixDatagramSocket::UnixDatagramSocket(UnixDatagramSocket&& other) noexcept :
	m_socket(other.m_socket), m_path(std::move(other.m_path)), m_emulated(other.m_emulated)
{
	other.m_socket = INVALID_SOCKET;
	other.m_emulated = false;
}

UnixDatagramSocket& UnixDatagramSocket::operator=(UnixDatagramSocket&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_socket = other.m_socket;
		m_path = std::move(other.m_path);
		m_emulated = other.m_emulated;
		other.m_socket = INVALID_SOCKET;
		other.m_emulated = false;
	}
	return *this;
}

UnixDatagramSocket::~UnixDatagramSocket() noexcept
{
	Reset();
}

void UnixDatagramSocket::SendTo(const std::string& path,
	const void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::overflow_error("AF_UNIX datagram size exceeds Winsock range");
	const auto address = UnixAddress(path);
	const sockaddr* destination = reinterpret_cast<const sockaddr*>(&address);
	int destination_length = static_cast<int>(sizeof(address));
	sockaddr_in fallback_address{};
	if (m_emulated) {
		fallback_address = EmulatedDatagramAddress(path);
		destination = reinterpret_cast<const sockaddr*>(&fallback_address);
		destination_length = static_cast<int>(sizeof(fallback_address));
	}
	const auto result = sendto(m_socket, static_cast<const char*>(data),
		static_cast<int>(bytes), 0, destination, destination_length);
	if (result == SOCKET_ERROR)
		ThrowSocketError("AF_UNIX datagram sendto");
	if (static_cast<std::size_t>(result) != bytes)
		throw std::runtime_error("AF_UNIX datagram was partially sent");
}

void UnixDatagramSocket::SendTo(const std::string& path,
	const std::string& data) const
{
	SendTo(path, data.data(), data.size());
}

std::size_t UnixDatagramSocket::Receive(void* data, std::size_t bytes) const
{
	if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::overflow_error("AF_UNIX datagram receive size exceeds Winsock range");
	const auto result = recvfrom(m_socket, static_cast<char*>(data),
		static_cast<int>(bytes), 0, nullptr, nullptr);
	if (result == SOCKET_ERROR)
		ThrowSocketError("AF_UNIX datagram recvfrom");
	return static_cast<std::size_t>(result);
}

std::string UnixDatagramSocket::Receive(std::size_t bytes) const
{
	std::string result(bytes, '\0');
	const auto received = Receive(result.data(), result.size());
	result.resize(received);
	return result;
}

void UnixDatagramSocket::Reset() noexcept
{
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	if (m_emulated && !m_path.empty()) {
		std::scoped_lock lock(DatagramRegistryMutex());
		DatagramRegistry().erase(m_path);
	}
	if (!m_path.empty()) {
		DeleteFileA(m_path.c_str());
		m_path.clear();
	}
	m_emulated = false;
}

bool UnixSequencedPacketSocket::Supported() noexcept
{
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET)
		return false;
	closesocket(socket);
	return true;
}

UnixSequencedPacketSocket UnixSequencedPacketSocket::Bind(const std::string& path)
{
	if (path.empty())
		throw std::invalid_argument("AF_UNIX sequenced-packet path is empty");
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX sequenced-packet socket");
	sockaddr_in address = LoopbackAddress(0);
	if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
		SOCKET_ERROR || listen(socket, 1) == SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX sequenced-packet bind/listen");
	}
	int address_length = sizeof(address);
	if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &address_length) ==
		SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX sequenced-packet address");
	}
	{
		std::scoped_lock lock(SequencedRegistryMutex());
		if (SequencedRegistry().contains(path)) {
			closesocket(socket);
			throw std::runtime_error("AF_UNIX sequenced-packet path is already bound");
		}
		SequencedRegistry().emplace(path, address);
	}
	return UnixSequencedPacketSocket(socket, path, true, true);
}

UnixSequencedPacketSocket UnixSequencedPacketSocket::Connect(const std::string& path)
{
	const auto address = SequencedAddress(path);
	const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX sequenced-packet socket");
	if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
		SOCKET_ERROR) {
		closesocket(socket);
		ThrowSocketError("AF_UNIX sequenced-packet connect");
	}
	return UnixSequencedPacketSocket(socket, {}, false, true);
}

UnixSequencedPacketSocket UnixSequencedPacketSocket::Accept() const
{
	const auto socket = accept(m_socket, nullptr, nullptr);
	if (socket == INVALID_SOCKET)
		ThrowSocketError("AF_UNIX sequenced-packet accept");
	return UnixSequencedPacketSocket(socket, {}, false, true);
}

UnixSequencedPacketSocket::UnixSequencedPacketSocket(
	UnixSequencedPacketSocket&& other) noexcept :
	m_socket(other.m_socket), m_path(std::move(other.m_path)),
	m_listener(other.m_listener), m_emulated(other.m_emulated)
{
	other.m_socket = INVALID_SOCKET;
	other.m_listener = false;
	other.m_emulated = false;
}

UnixSequencedPacketSocket& UnixSequencedPacketSocket::operator=(
	UnixSequencedPacketSocket&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_socket = other.m_socket;
		m_path = std::move(other.m_path);
		m_listener = other.m_listener;
		m_emulated = other.m_emulated;
		other.m_socket = INVALID_SOCKET;
		other.m_listener = false;
		other.m_emulated = false;
	}
	return *this;
}

UnixSequencedPacketSocket::~UnixSequencedPacketSocket() noexcept
{
	Reset();
}

void UnixSequencedPacketSocket::SendPacket(const std::string& packet) const
{
	if (packet.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::overflow_error("AF_UNIX sequenced-packet is too large");
	const auto length = htonl(static_cast<std::uint32_t>(packet.size()));
	SendSocketBytes(m_socket, reinterpret_cast<const char*>(&length), sizeof(length));
	SendSocketBytes(m_socket, packet.data(), packet.size());
}

std::string UnixSequencedPacketSocket::ReceivePacket(std::size_t maximum_bytes) const
{
	std::uint32_t network_length = 0;
	ReceiveSocketBytes(m_socket, reinterpret_cast<char*>(&network_length),
		sizeof(network_length));
	const auto length = ntohl(network_length);
	if (length > maximum_bytes)
		throw std::length_error("AF_UNIX sequenced-packet exceeds receive limit");
	std::string packet(length, '\0');
	ReceiveSocketBytes(m_socket, packet.data(), packet.size());
	return packet;
}

void UnixSequencedPacketSocket::Reset() noexcept
{
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	if (m_listener && !m_path.empty()) {
		std::scoped_lock lock(SequencedRegistryMutex());
		SequencedRegistry().erase(m_path);
	}
	m_path.clear();
	m_listener = false;
	m_emulated = false;
}

} // namespace darling::windows_host
