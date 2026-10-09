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
		std::unordered_map<std::uint64_t, std::deque<std::vector<std::uint8_t>>> port_queues;

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
				if (envelope.operation == darling::windows_host::MachIpcOperation::Allocate) {
					const auto token = next_port_token++;
					allocated_ports.insert(token);
					port_queues.emplace(token, std::deque<std::vector<std::uint8_t>>{});
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Allocate,
						envelope.request_id, token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Deallocate) {
					if (allocated_ports.erase(envelope.port_token) == 0)
						throw std::invalid_argument("unknown Mach IPC port token");
					port_queues.erase(envelope.port_token);
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Deallocate,
						envelope.request_id, envelope.port_token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Send) {
					if (!allocated_ports.contains(envelope.port_token))
						throw std::invalid_argument("unknown Mach IPC port token");
					port_queues.at(envelope.port_token).push_back(envelope.payload);
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Send,
						envelope.request_id, envelope.port_token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Receive) {
					if (!allocated_ports.contains(envelope.port_token))
						throw std::invalid_argument("unknown Mach IPC port token");
					auto& queue = port_queues.at(envelope.port_token);
					if (queue.empty())
						throw std::runtime_error("Mach IPC receive would block");
					auto payload = std::move(queue.front());
					queue.pop_front();
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Receive,
						envelope.request_id, envelope.port_token, 0, std::move(payload)};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				server.Write("UNSUPPORTED_MACH_IPC_OPERATION");
			} catch (const std::exception&) {
				server.Write("INVALID_MACH_IPC_REQUEST");
			}
		}
	} catch (const std::exception& error) {
		std::cerr << "darling_windows_broker: " << error.what() << "\n";
		return 1;
	}
}
