/*
 * Stage 1 Darling Windows host broker.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_runtime.h"

#include <atomic>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
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

		// Publish the IPC endpoint before filesystem bootstrap so the parent can
		// synchronize with a live broker even when prefix creation is slow.
		const auto prefix = darling::windows_host::Prefix::Create(argv[1]);
		// Tokens are capabilities, not public object indexes.  Do not expose a
		// predictable sequence over the named-pipe boundary.  The broker still
		// needs a future session/owner table; this hardening only removes the
		// trivial "guess the next token" primitive.
		std::random_device random_device;
		auto next_opaque_token = [&]() {
			std::uint64_t token = 0;
			while (token == 0) {
				token = (static_cast<std::uint64_t>(random_device()) << 32) |
					static_cast<std::uint64_t>(random_device());
			}
			return token;
		};
		std::unordered_set<std::uint64_t> allocated_ports;
		std::unordered_map<std::uint64_t, std::uint64_t> port_session_owners;
		std::unordered_map<std::uint64_t, std::deque<darling::windows_host::MachIpcEnvelope>> port_queues;
		std::unordered_map<std::uint64_t, darling::windows_host::MachIpcSharedMemory> out_of_line_regions;
		std::unordered_map<std::uint64_t, std::uint64_t> out_of_line_owners;
		std::unordered_map<std::uint64_t, std::uint64_t> out_of_line_session_owners;
		std::unordered_map<std::uint64_t, std::shared_ptr<darling::windows_host::MachIpcNotification>> notifications;
		std::unordered_map<std::uint64_t, std::uint64_t> notification_session_owners;
		std::mutex broker_state_mutex;
		std::condition_variable broker_state_condition;
		std::atomic_bool shutdown_requested = false;
		std::vector<std::thread> workers;
		std::unordered_set<std::uint64_t> active_sessions;
		std::unordered_map<std::uint64_t, DWORD> session_process_ids;

		// Keep the broker state alive across client reconnects.  A disconnected
		// client must not destroy the emulated Mach namespace.
		for (;;) {
			auto server = darling::windows_host::NamedPipeRpcServer::Create(argv[2]);
			server.WaitForClient();
			workers.emplace_back([&, server = std::move(server)]() mutable {
			const auto session_token = next_opaque_token();
			{
				std::lock_guard session_lock(broker_state_mutex);
				active_sessions.insert(session_token);
			}
			for (;;) {
				std::string request;
				try {
					request = server.Read();
				} catch (const std::exception&) {
					// Reclaim only resources explicitly created in this session.  The
					// legacy token-0 namespace intentionally survives reconnects.
					std::unique_lock cleanup_lock(broker_state_mutex);
					for (auto it = notification_session_owners.begin();
						it != notification_session_owners.end();) {
						if (it->second == session_token) {
							notifications.erase(it->first);
							it = notification_session_owners.erase(it);
						} else {
							++it;
						}
					}
					for (auto it = port_session_owners.begin();
						it != port_session_owners.end();) {
						if (it->second == session_token) {
							allocated_ports.erase(it->first);
							port_queues.erase(it->first);
							it = port_session_owners.erase(it);
						} else {
							++it;
						}
					}
					for (auto it = out_of_line_session_owners.begin();
						it != out_of_line_session_owners.end();) {
						if (it->second == session_token) {
							out_of_line_regions.erase(it->first);
							out_of_line_owners.erase(it->first);
							it = out_of_line_session_owners.erase(it);
						} else {
							++it;
						}
					}
					broker_state_condition.notify_all();
						active_sessions.erase(session_token);
					session_process_ids.erase(session_token);
					break;
				}
			if (request == "SHUTDOWN_WAKE") {
				return;
			}
			if (request == "PING") {
				server.Write("PONG");
				continue;
			}
			if (request == "SHUTDOWN") {
				server.Write("BYE");
				if (server.Read() != "ACK") {
					shutdown_requested.store(true);
					return;
				}
				shutdown_requested.store(true);
				try {
					auto wake_client = darling::windows_host::NamedPipeRpcClient::Connect(argv[2]);
					wake_client.Write("SHUTDOWN_WAKE");
				} catch (...) {
				}
				return;
			}
			try {
				std::unique_lock state_lock(broker_state_mutex);
				const auto envelope = darling::windows_host::DecodeMachIpcEnvelope(
					AsBytes(request));
				if (envelope.operation == darling::windows_host::MachIpcOperation::SessionOpen) {
					if (envelope.session_token != 0 || (envelope.payload.size() != 0 && envelope.payload.size() != 4) ||
						envelope.port_token != 0 || envelope.disposition_count != 0 ||
						envelope.out_of_line_token != 0 || envelope.out_of_line_size != 0)
						throw std::invalid_argument("invalid Mach IPC session open");
					if (envelope.payload.size() == 4) {
						const DWORD process_id = static_cast<DWORD>(envelope.payload[0]) |
							(static_cast<DWORD>(envelope.payload[1]) << 8) |
							(static_cast<DWORD>(envelope.payload[2]) << 16) |
							(static_cast<DWORD>(envelope.payload[3]) << 24);
						if (process_id == 0) throw std::invalid_argument("invalid Mach IPC session process");
						const HANDLE process = OpenProcess(
							PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
						if (process == nullptr)
							throw std::invalid_argument("Mach IPC session process is not accessible");
						CloseHandle(process);
						session_process_ids.emplace(session_token, process_id);
					}
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, 0, 0, {}, 0, 0,
						session_token};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				// Nonzero session tokens opt into connection authentication.  Zero
				// remains the temporary compatibility mode for pre-session callers.
				if (envelope.session_token != 0 && envelope.session_token != session_token)
					throw std::invalid_argument("Mach IPC session token does not belong to this connection");
				if (envelope.operation == darling::windows_host::MachIpcOperation::CapabilityTransfer) {
					if (envelope.session_token == 0 || envelope.payload.size() != 8 ||
						envelope.disposition_count != 0 || envelope.out_of_line_token != 0 ||
						envelope.out_of_line_size != 0)
						throw std::invalid_argument("invalid Mach IPC capability transfer");
					std::uint64_t target_session = 0;
					for (unsigned shift = 0; shift < 64; shift += 8)
						target_session |= static_cast<std::uint64_t>(envelope.payload[shift / 8]) << shift;
					if (target_session == 0 || !active_sessions.contains(target_session))
						throw std::invalid_argument("unknown target Mach IPC session");
					auto owner = port_session_owners.find(envelope.port_token);
					if (owner == port_session_owners.end() || owner->second != session_token)
						throw std::invalid_argument("Mach IPC capability is not owned by this session");
					owner->second = target_session;
					for (const auto& [ool_token, owning_port] : out_of_line_owners) {
						if (owning_port == envelope.port_token) {
							auto ool_owner = out_of_line_session_owners.find(ool_token);
							if (ool_owner != out_of_line_session_owners.end())
								ool_owner->second = target_session;
						}
					}
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, envelope.port_token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationCreate) {
					if (!envelope.payload.empty() || envelope.disposition_count != 0 ||
						envelope.out_of_line_token != 0 || envelope.out_of_line_size != 0)
						throw std::invalid_argument("invalid Mach IPC notification create");
					std::uint64_t token = next_opaque_token();
					while (notifications.contains(token)) token = next_opaque_token();
					const auto name = L"Local\\wintosh-mach-notification-" + std::to_wstring(token);
					notifications.emplace(token, std::make_shared<darling::windows_host::MachIpcNotification>(
						darling::windows_host::MachIpcNotification::Create(name)));
					if (envelope.session_token != 0)
						notification_session_owners.emplace(token, envelope.session_token);
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationSignal ||
					envelope.operation == darling::windows_host::MachIpcOperation::NotificationReset ||
					envelope.operation == darling::windows_host::MachIpcOperation::NotificationWait ||
					envelope.operation == darling::windows_host::MachIpcOperation::NotificationDestroy) {
					if (envelope.operation != darling::windows_host::MachIpcOperation::NotificationWait &&
						(!envelope.payload.empty() || envelope.disposition_count != 0 ||
							envelope.out_of_line_token != 0 || envelope.out_of_line_size != 0))
						throw std::invalid_argument("invalid Mach IPC notification descriptors");
					auto notification = notifications.find(envelope.port_token);
					if (notification == notifications.end())
						throw std::invalid_argument("unknown Mach IPC notification token");
					auto notification_owner = notification_session_owners.find(envelope.port_token);
					if (notification_owner != notification_session_owners.end() &&
						notification_owner->second != envelope.session_token)
						throw std::invalid_argument("Mach IPC notification belongs to another session");
					std::vector<std::uint8_t> result;
					if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationDestroy) {
						notifications.erase(notification);
						notification_session_owners.erase(envelope.port_token);
					} else if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationSignal)
						notification->second->Signal();
					else if (envelope.operation == darling::windows_host::MachIpcOperation::NotificationReset)
						notification->second->Reset();
					else {
						DWORD timeout = INFINITE;
						if (envelope.payload.size() == 4)
							timeout = static_cast<DWORD>(envelope.payload[0]) |
								(static_cast<DWORD>(envelope.payload[1]) << 8) |
								(static_cast<DWORD>(envelope.payload[2]) << 16) |
								(static_cast<DWORD>(envelope.payload[3]) << 24);
						else if (!envelope.payload.empty())
							throw std::invalid_argument("invalid Mach IPC notification timeout");
						const auto wait_handle = notification->second;
						state_lock.unlock();
						const auto wait_result = wait_handle->Wait(timeout);
						state_lock.lock();
						result.push_back(wait_result ? 1 : 0);
					}
					darling::windows_host::MachIpcEnvelope response{
						envelope.operation, envelope.request_id, envelope.port_token, 0, std::move(result)};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Allocate) {
					std::uint64_t token = next_opaque_token();
					while (allocated_ports.contains(token)) token = next_opaque_token();
					allocated_ports.insert(token);
					port_queues.emplace(token, std::deque<darling::windows_host::MachIpcEnvelope>{});
					if (envelope.session_token != 0)
						port_session_owners.emplace(token, envelope.session_token);
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Allocate,
						envelope.request_id, token, 0, {}};
					server.Write(AsString(darling::windows_host::EncodeMachIpcEnvelope(response)));
					continue;
				}
				if (envelope.operation == darling::windows_host::MachIpcOperation::Deallocate ||
					envelope.operation == darling::windows_host::MachIpcOperation::Destroy) {
					if (!envelope.payload.empty() || envelope.disposition_count != 0 ||
						envelope.out_of_line_token != 0 || envelope.out_of_line_size != 0)
						throw std::invalid_argument("invalid Mach IPC port lifecycle request");
					auto owner = port_session_owners.find(envelope.port_token);
					if (owner != port_session_owners.end() && owner->second != envelope.session_token)
						throw std::invalid_argument("Mach IPC port belongs to another session");
					if (allocated_ports.erase(envelope.port_token) == 0)
						throw std::invalid_argument("unknown Mach IPC port token");
					port_session_owners.erase(envelope.port_token);
					port_queues.erase(envelope.port_token);
					broker_state_condition.notify_all();
					for (auto it = out_of_line_owners.begin(); it != out_of_line_owners.end();) {
						if (it->second == envelope.port_token) {
							out_of_line_regions.erase(it->first);
							out_of_line_session_owners.erase(it->first);
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
					auto owner = port_session_owners.find(envelope.port_token);
					if (owner != port_session_owners.end() && owner->second != envelope.session_token)
						throw std::invalid_argument("Mach IPC port belongs to another session");
					if (envelope.disposition_count == 0 &&
						(envelope.out_of_line_token != 0 || envelope.out_of_line_size != 0))
						throw std::invalid_argument("invalid Mach IPC send descriptors");
					if (port_queues.at(envelope.port_token).size() >= max_port_queue_depth)
						throw std::runtime_error("Mach IPC send queue is full");
					auto queued = envelope;
					if (queued.disposition_count > 0) {
						if (queued.disposition_count != 1 || queued.payload.empty())
							throw std::invalid_argument("unsupported Mach IPC disposition");
						std::uint64_t token = next_opaque_token();
						while (out_of_line_regions.contains(token)) token = next_opaque_token();
						const auto name = L"Local\\wintosh-mach-ool-" + std::to_wstring(token);
						auto region = darling::windows_host::MachIpcSharedMemory::Create(
							name, queued.payload.size());
						std::memcpy(region.Data(), queued.payload.data(), queued.payload.size());
						out_of_line_regions.emplace(token, std::move(region));
						out_of_line_owners.emplace(token, envelope.port_token);
						auto port_owner = port_session_owners.find(envelope.port_token);
						if (port_owner != port_session_owners.end())
							out_of_line_session_owners.emplace(token, port_owner->second);
						queued.payload.clear();
						queued.disposition_count = 0;
						queued.out_of_line_token = token;
						queued.out_of_line_size = static_cast<std::uint32_t>(envelope.payload.size());
					}
					port_queues.at(envelope.port_token).push_back(std::move(queued));
					broker_state_condition.notify_all();
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
					auto owner = port_session_owners.find(envelope.port_token);
					if (owner != port_session_owners.end() && owner->second != envelope.session_token)
						throw std::invalid_argument("Mach IPC port belongs to another session");
					if (envelope.disposition_count != 0 || envelope.out_of_line_token != 0 ||
						envelope.out_of_line_size != 0)
						throw std::invalid_argument("invalid Mach IPC receive descriptors");
					if (!envelope.payload.empty() && envelope.payload.size() != 4)
						throw std::invalid_argument("invalid Mach IPC receive timeout");
					auto& queue = port_queues.at(envelope.port_token);
					if (queue.empty() && !envelope.payload.empty()) {
						const auto timeout = static_cast<std::uint32_t>(envelope.payload[0]) |
							(static_cast<std::uint32_t>(envelope.payload[1]) << 8) |
							(static_cast<std::uint32_t>(envelope.payload[2]) << 16) |
							(static_cast<std::uint32_t>(envelope.payload[3]) << 24);
						const auto ready = broker_state_condition.wait_for(
							state_lock, std::chrono::milliseconds(timeout), [&] {
								return !allocated_ports.contains(envelope.port_token) ||
									!port_queues.at(envelope.port_token).empty();
							});
						if (!ready || !allocated_ports.contains(envelope.port_token) ||
							port_queues.at(envelope.port_token).empty())
							throw std::runtime_error("Mach IPC receive would block");
					}
					if (queue.empty())
						throw std::runtime_error("Mach IPC receive would block");
					auto message = std::move(queue.front());
					queue.pop_front();
					// Receiving an OOL descriptor transfers the broker's owning
					// mapping handle to the receiver.  Existing receiver views stay
					// valid; the broker no longer retains the region until port death.
					std::uint64_t duplicated_handle = 0;
					if (message.out_of_line_token != 0) {
						auto ool_owner = out_of_line_session_owners.find(message.out_of_line_token);
						if (ool_owner != out_of_line_session_owners.end() &&
							ool_owner->second != envelope.session_token)
							throw std::invalid_argument("Mach IPC OOL mapping belongs to another session");
						auto process_owner = session_process_ids.find(envelope.session_token);
						auto region = out_of_line_regions.find(message.out_of_line_token);
						if (process_owner != session_process_ids.end() && region != out_of_line_regions.end()) {
							const HANDLE target_process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, process_owner->second);
							if (target_process == nullptr)
								throw std::invalid_argument("Mach IPC target process is unavailable");
							HANDLE target_handle = nullptr;
							const BOOL duplicated = DuplicateHandle(
								GetCurrentProcess(), region->second.NativeHandle(), target_process,
								&target_handle, 0, FALSE, DUPLICATE_SAME_ACCESS);
							CloseHandle(target_process);
							if (!duplicated || target_handle == nullptr)
								throw std::invalid_argument("Mach IPC OOL handle duplication failed");
							duplicated_handle = static_cast<std::uint64_t>(
								reinterpret_cast<std::uintptr_t>(target_handle));
						}
						out_of_line_regions.erase(message.out_of_line_token);
						out_of_line_session_owners.erase(message.out_of_line_token);
						out_of_line_owners.erase(message.out_of_line_token);
					}
					darling::windows_host::MachIpcEnvelope response{
						darling::windows_host::MachIpcOperation::Receive,
						envelope.request_id, envelope.port_token, message.disposition_count,
						std::move(message.payload), message.out_of_line_token,
						message.out_of_line_size};
					response.out_of_line_handle = duplicated_handle;
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
			});
			if (shutdown_requested.load()) break;
		}
		for (auto& worker : workers) {
			if (worker.joinable()) worker.join();
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "darling_windows_broker: " << error.what() << "\n";
		return 1;
	}
}
