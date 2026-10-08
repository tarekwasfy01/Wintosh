/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <winsock2.h>
#include <afunix.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace darling::windows_host {

class WinsockRuntime final {
public:
	WinsockRuntime();
	WinsockRuntime(const WinsockRuntime&) = delete;
	WinsockRuntime& operator=(const WinsockRuntime&) = delete;
	~WinsockRuntime() noexcept;
};

class TcpSocket final {
public:
	static TcpSocket ListenLoopback(std::uint16_t port = 0);
	static TcpSocket ConnectLoopback(std::uint16_t port);
	TcpSocket(const TcpSocket&) = delete;
	TcpSocket& operator=(const TcpSocket&) = delete;
	TcpSocket(TcpSocket&& other) noexcept;
	TcpSocket& operator=(TcpSocket&& other) noexcept;
	~TcpSocket() noexcept;

	[[nodiscard]] std::uint16_t LocalPort() const;
	[[nodiscard]] TcpSocket Accept() const;
	[[nodiscard]] bool Valid() const noexcept { return m_socket != INVALID_SOCKET; }
	[[nodiscard]] std::size_t Send(const void* data, std::size_t bytes) const;
	[[nodiscard]] std::size_t Receive(void* data, std::size_t bytes) const;
	void Shutdown() noexcept;
	void SendAll(const std::string& data) const;
	[[nodiscard]] std::string ReceiveExact(std::size_t bytes) const;

private:
	 explicit TcpSocket(SOCKET socket) noexcept : m_socket(socket) {}
	void Reset() noexcept;
	SOCKET m_socket = INVALID_SOCKET;
};

class UnixSocket final {
public:
	static UnixSocket Listen(const std::string& path);
	static UnixSocket Connect(const std::string& path);
	static std::pair<UnixSocket, UnixSocket> Pair();
	UnixSocket(const UnixSocket&) = delete;
	UnixSocket& operator=(const UnixSocket&) = delete;
	UnixSocket(UnixSocket&& other) noexcept;
	UnixSocket& operator=(UnixSocket&& other) noexcept;
	~UnixSocket() noexcept;

	[[nodiscard]] UnixSocket Accept() const;
	[[nodiscard]] bool Valid() const noexcept { return m_socket != INVALID_SOCKET; }
	[[nodiscard]] std::size_t Send(const void* data, std::size_t bytes) const;
	[[nodiscard]] std::size_t Receive(void* data, std::size_t bytes) const;
	void SendAll(const std::string& data) const;
	[[nodiscard]] std::string ReceiveExact(std::size_t bytes) const;

public:
	UnixSocket(SOCKET socket, std::string path, bool emulated, bool listener) noexcept :
		m_socket(socket), m_path(std::move(path)), m_emulated(emulated), m_listener(listener) {}

private:
	void Reset() noexcept;
	SOCKET m_socket = INVALID_SOCKET;
	std::string m_path;
	bool m_emulated = false;
	bool m_listener = false;
};

class UnixDatagramSocket final {
public:
	[[nodiscard]] static bool Supported() noexcept;
	static UnixDatagramSocket Bind(const std::string& path);
	UnixDatagramSocket(const UnixDatagramSocket&) = delete;
	UnixDatagramSocket& operator=(const UnixDatagramSocket&) = delete;
	UnixDatagramSocket(UnixDatagramSocket&& other) noexcept;
	UnixDatagramSocket& operator=(UnixDatagramSocket&& other) noexcept;
	~UnixDatagramSocket() noexcept;

	[[nodiscard]] bool Valid() const noexcept { return m_socket != INVALID_SOCKET; }
	[[nodiscard]] bool Emulated() const noexcept { return m_emulated; }
	void SendTo(const std::string& path, const void* data, std::size_t bytes) const;
	void SendTo(const std::string& path, const std::string& data) const;
	[[nodiscard]] std::size_t Receive(void* data, std::size_t bytes) const;
	[[nodiscard]] std::string Receive(std::size_t bytes) const;

private:
	UnixDatagramSocket(SOCKET socket, std::string path, bool emulated) noexcept :
		m_socket(socket), m_path(std::move(path)), m_emulated(emulated) {}
	void Reset() noexcept;
	SOCKET m_socket = INVALID_SOCKET;
	std::string m_path;
	bool m_emulated = false;
};

class UnixSequencedPacketSocket final {
public:
	[[nodiscard]] static bool Supported() noexcept;
	static UnixSequencedPacketSocket Bind(const std::string& path);
	static UnixSequencedPacketSocket Connect(const std::string& path);
	UnixSequencedPacketSocket(const UnixSequencedPacketSocket&) = delete;
	UnixSequencedPacketSocket& operator=(const UnixSequencedPacketSocket&) = delete;
	UnixSequencedPacketSocket(UnixSequencedPacketSocket&& other) noexcept;
	UnixSequencedPacketSocket& operator=(UnixSequencedPacketSocket&& other) noexcept;
	~UnixSequencedPacketSocket() noexcept;

	[[nodiscard]] bool Valid() const noexcept { return m_socket != INVALID_SOCKET; }
	[[nodiscard]] bool Emulated() const noexcept { return m_emulated; }
	[[nodiscard]] UnixSequencedPacketSocket Accept() const;
	void SendPacket(const std::string& packet) const;
	[[nodiscard]] std::string ReceivePacket(std::size_t maximum_bytes) const;

private:
	UnixSequencedPacketSocket(SOCKET socket, std::string path,
		bool listener, bool emulated) noexcept :
		m_socket(socket), m_path(std::move(path)), m_listener(listener),
		m_emulated(emulated) {}
	void Reset() noexcept;
	SOCKET m_socket = INVALID_SOCKET;
	std::string m_path;
	bool m_listener = false;
	bool m_emulated = false;
};

} // namespace darling::windows_host
