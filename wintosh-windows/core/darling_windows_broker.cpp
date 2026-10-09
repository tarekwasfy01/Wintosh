/*
 * Stage 1 Darling Windows host broker.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_runtime.h"

#include <iostream>
#include <deque>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::vector<std::uint8_t> AsBytes(const std::string& value)
{
	return {value.begin(), value.end()};
}

std::string AsString(const std::vector<std::uint8_t>& value)
{
	return {value.begin(), value.end()};
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	try {
		constexpr std::size_t max_port_queue_depth = 1024;
		if (argc != 3) {
			std::wcerr << L"usage: darling_windows_broker <prefix> <pipe-name>\n";
			return 2;
		}

		auto server = darling::windows_host::NamedPipeRpcServer::Create(argv[2]);
		// Publish the IPC endpoint before filesystem bootstrap so the parent can
		// synchronize with a live broker even when prefix creation is slow.
		const auto prefix = darling::windows_host::Prefix::Create(argv[1]);
		server.WaitForClient();
		std::uint64_t next_port_token = 1;
		std::unordered_set<std::uint64_t> allocated_ports;
		std::unordered_map<std::uint64_t, std::deque<darling::windows_host::MachIpcEnvelope>> port_queues;
		std::unordered_map<std::uint64_t, darling::windows_host::MachIpcSharedMemory> out_of_line_regions;
		std::unordered_map<std::uint64_t, std::uint64_t> out_of_line_owners;
		std::uint64_t next_out_of_line_token = 1;
		std::unordered_map<std::uint64_t, darling::windows_host::MachIpcNotification> notifications;
		std::uint64_t next_notification_token = 1;

		for (;;) {
			const auto request = server.Read();
			if (request == "PING") {
				server.Write("PONG");
				continue;
			}
			if (request == "SHUTDOWN") {
				server.Write("BYE");
				if (server.Read() != "ACK") {
					return 3;
				}
				return 0;
			}
			try {
				const auto envelope = darling::windows_host::DecodeMachIpcEnvelope(
					AsBytes(request));
				if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationCreate) {
					const auto token = next_notification_token++;
					const auto name = L"Local\\wintosh-mach-notification-" + std::to_wstring(token);
					notifications.emplace(token, darling::windows_host::MachIpcNotification::Create(name));
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationSignal ||
					envelope.operation == darling::windows_host::MachIpcOperation::NotificationReset ||
					envelope.operation == darling::windows_host::MachIpcOperation::NotificationWait) {
					auto notification = notifications.find(envelope.port_token);
					if (notification == notifications.end())
						throw std::invalid_argument("unknown Mach IPC notification token");
					std::vector<std::uint8_t> result;
					if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationSignal)
						notification->second.Signal();
					else if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationReset)
						notification->second.Reset();
					else {
						DWORD timeout = INFINITE;
						if (envelope.payload.size() == 4)
							timeout = static_cast<DWORD>(envelope.payload[0]) |
								(static_cast<DWORD>(envelope.payload[1]) << 8) |
								(static_cast<DWORD>(envelope.payload[2]) << 16) |
								(static_cast<DWORD>(envelope.payload[3]) << 24);
						else if (!envelope.payload.empty())
							throw std::invalid_argument("invalid Mach IPC notification timeout");
						result.push_back(notification->second.Wait(timeout) ? 1 : 0);
					}
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, envelope.port_token, 0, std::move(result)};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Allocate) {
					const auto token = next_port_token++;
					allocated_ports.insert(token);
					port_queues.emplace(token, std::deque<darling::windows_host::MachIpcEnvelope>{});
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Allocate,
						envelope.request_id, token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Deallocate ||
					envelope.operation == darling::windows_host::MachIpcOperation::Destroy) {
					if (allocated_ports.erase(envelope.port_token) == 0)
						throw std::invalid_argument("unknown Mach IPC port token");
					port_queues.erase(envelope.port_token);
					for (auto it = out_of_line_owners.begin(); it != out_of_line_owners.end();) {
						if (it->second == envelope.port_token) {
							out_of_line_regions.erase(it->first);
							it = out_of_line_owners.erase(it);
						} else {
							++it;
						}
					}
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation,
						envelope.request_id, envelope.port_token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Send) {
					if (!allocated_ports.contains(envelope.port_token))
						throw std::invalid_argument("unknown Mach IPC port token");
					if (port_queues.at(envelope.port_token).size() >= max_port_queue_depth)
						throw std::runtime_error("Mach IPC send queue is full");
					auto queued = envelope;
					if (queued.disposition_count > 0) {
						if (queued.disposition_count != 1 || queued.payload.empty())
							throw std::invalid_argument("unsupported Mach IPC disposition");
						const auto token = next_out_of_line_token++;
						const auto name = L"Local\\wintosh-mach-ool-" + std::to_wstring(token);
						auto region = darling::windows_host::MachIpcSharedMemory::Create(
							name, queued.payload.size());
						std::memcpy(region.Data(), queued.payload.data(), queued.payload.size());
						out_of_line_regions.emplace(token, std::move(region));
						out_of_line_owners.emplace(token, envelope.port_token);
						queued.payload.clear();
						queued.disposition_count = 0;
						queued.out_of_line_token = token;
						queued.out_of_line_size = static_cast<std::uint32_t>(envelope.payload.size());
					}
					port_queues.at(envelope.port_token).push_back(std::move(queued));
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Send,
						envelope.request_id, envelope.port_token, 0, {},
						queued.out_of_line_token, queued.out_of_line_size};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Receive) {
					if (!allocated_ports.contains(envelope.port_token))
						throw std::invalid_argument("unknown Mach IPC port token");
					auto& queue = port_queues.at(envelope.port_token);
					if (queue.empty())
						throw std::runtime_error("Mach IPC receive would block");
					auto message = std::move(queue.front());
					queue.pop_front();
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Receive,
						envelope.request_id, envelope.port_token, message.disposition_count,
						std::move(message.payload), message.out_of_line_token,
						message.out_of_line_size};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				server.Write("UNSUPPORTED_MACH_IPC_OPERATION");
			} catch (const std::runtime_error& error) {
				if (std::string(error.what()) == "Mach IPC receive would block") {
					server.Write("MACH_RECEIVE_WOULD_BLOCK");
				} else if (std::string(error.what()) == "Mach IPC send queue is full") {
					server.Write("MACH_SEND_QUEUE_FULL");
				} else {
					server.Write("INVALID_MACH_IPC_REQUEST");
				}
			} catch (const std::exception&) {
				server.Write("INVALID_MACH_IPC_REQUEST");
			}
		}
	} catch (const std::exception& error) {
		std::cerr << "darling_windows_broker: " << error.what() << "\n";
		return 1;
	}
}
