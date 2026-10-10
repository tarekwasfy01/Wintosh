/*
 * Stage 1 out-of-process broker proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darwin_windows_process.h"
#include "darling_windows_mach.h"
#include "darling_windows_runtime.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {

std::wstring CurrentExecutableDirectory()
{
	wchar_t buffer[32768]{};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length == 0 || length >= std::size(buffer)) {
		throw std::runtime_error("GetModuleFileNameW failed");
	}
	return std::filesystem::path(std::wstring(buffer, length)).parent_path().wstring();
}

} // namespace

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			std::cerr << "TEMP_PATH=FAIL\n";
			return 2;
		}

		const auto root = std::filesystem::path(temp_path) /
			(L"darling-broker-stage1-" + std::to_wstring(GetCurrentProcessId()));
		const auto pipe_name = L"darling-broker-stage1-" +
			std::to_wstring(GetCurrentProcessId());
		const auto broker = std::filesystem::path(CurrentExecutableDirectory()) /
			L"wintosh_broker.exe";
		const std::wstring command_line = L"\"" + broker.wstring() + L"\" \"" +
			root.wstring() + L"\" \"" + pipe_name + L"\"";
		std::cout << "BROKER_PIPE=darling-broker-stage1-" << GetCurrentProcessId() << "\n";

		auto child = darling::windows_host::Process::Launch(broker.wstring(), command_line);
		if (!child.Valid()) {
			std::cerr << "BROKER_START=FAIL\n";
			return 3;
		}
		// Let the child finish CRT/Prefix bootstrap before entering the bounded
		// named-pipe retry loop; this avoids matrix-order startup jitter.
		Sleep(1000);

		{
		darling::windows_host::NamedPipeRpcClient client = [&] {
			try {
				return darling::windows_host::NamedPipeRpcClient::Connect(
					pipe_name);
			} catch (...) {
				std::cerr << "BROKER_WAIT_RESULT=" << child.Wait(0)
					<< " BROKER_EXIT_CODE=" << child.ExitCode() << "\n";
				child.Terminate(9);
				(void)child.Wait(1000);
				throw;
			}
		}();
		client.Write("PING");
		const auto ping = client.Read();
		std::cout << "BROKER_RPC=" << ping << "\n";
		if (ping != "PONG") {
			return 4;
		}
		const auto session_token = darling::windows_host::OpenMachIpcSession(client);
		std::cout << "BROKER_SESSION_OPEN=" << session_token << "\n";
		if (session_token == 0) {
			return 5;
		}
		darling_mach_port_name_t broker_local_port = 0;
		const auto broker_enable = darling_windows_mach_broker_enable(pipe_name.c_str());
		const auto broker_allocate = darling_windows_mach_port_allocate(
			darling_windows_mach_task_self(), &broker_local_port);
		std::uint64_t broker_capability = 0;
		std::uint64_t broker_capability_session = 0;
		const auto broker_lookup = darling_windows_mach_port_lookup_broker(
			broker_local_port, &broker_capability, &broker_capability_session);
		const auto broker_disable = darling_windows_mach_broker_disable();
		std::cout << "BROKER_C_ABI_ALLOCATE=" <<
			(broker_enable == 0 && broker_allocate == 0 && broker_lookup == 0 &&
			 broker_capability != 0 && broker_capability_session != 0 ? "PASS" : "FAIL") << "\n";
		if (broker_local_port != 0)
			darling_windows_mach_port_destroy(darling_windows_mach_task_self(), broker_local_port);
		if (broker_enable != 0 || broker_allocate != 0 || broker_lookup != 0 ||
			broker_disable != 0 || broker_capability == 0 || broker_capability_session == 0)
			return 6;
		const auto process_id = static_cast<std::uint32_t>(GetCurrentProcessId());
		const std::vector<std::uint8_t> process_id_payload{
			static_cast<std::uint8_t>(process_id),
			static_cast<std::uint8_t>(process_id >> 8),
			static_cast<std::uint8_t>(process_id >> 16),
			static_cast<std::uint8_t>(process_id >> 24)};
		darling::windows_host::MachIpcEnvelope session_response;
		session_response.operation = darling::windows_host::MachIpcOperation::SessionOpen;
		session_response.request_id = 90;
		session_response.session_token = session_token;
		{
			auto invalid_pid_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope invalid_pid_open{
				darling::windows_host::MachIpcOperation::SessionOpen, 901, 0, 0,
				{0xff, 0xff, 0xff, 0xff}};
			const auto invalid_pid_bytes = darling::windows_host::EncodeMachIpcEnvelope(invalid_pid_open);
			invalid_pid_client.Write(std::string(invalid_pid_bytes.begin(), invalid_pid_bytes.end()));
			if (invalid_pid_client.Read() != "INVALID_MACH_IPC_REQUEST") return 19;
		}
		std::cout << "BROKER_SESSION_PID_VALIDATION=PASS\n";
		darling::windows_host::MachIpcEnvelope owned_allocate{
			darling::windows_host::MachIpcOperation::Allocate, 91, 0, 0, {}};
		owned_allocate.session_token = session_response.session_token;
		const auto owned_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(owned_allocate);
		client.Write(std::string(owned_allocate_bytes.begin(), owned_allocate_bytes.end()));
		const auto owned_allocate_wire = client.Read();
		const auto owned_allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(owned_allocate_wire.begin(), owned_allocate_wire.end()));
		if (owned_allocate_response.session_token != 0 || owned_allocate_response.port_token == 0)
			return 7;
		darling::windows_host::MachIpcEnvelope owned_destroy{
			darling::windows_host::MachIpcOperation::Destroy, 92,
			owned_allocate_response.port_token, 0, {}};
		owned_destroy.session_token = session_response.session_token;
		const auto owned_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(owned_destroy);
		client.Write(std::string(owned_destroy_bytes.begin(), owned_destroy_bytes.end()));
		if (client.Read().empty()) return 8;
		std::cout << "BROKER_SESSION_PORT_OWNERSHIP=PASS\n";
		darling::windows_host::MachIpcEnvelope owned_notification_create{
			darling::windows_host::MachIpcOperation::NotificationCreate, 93, 0, 0, {}};
		owned_notification_create.session_token = session_response.session_token;
		const auto owned_notification_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			owned_notification_create);
		client.Write(std::string(owned_notification_bytes.begin(), owned_notification_bytes.end()));
		const auto owned_notification_wire = client.Read();
		const auto owned_notification_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(owned_notification_wire.begin(), owned_notification_wire.end()));
		if (owned_notification_response.port_token == 0) return 9;
		{
			auto notification_probe_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope probe_session_open{
				darling::windows_host::MachIpcOperation::SessionOpen, 940, 0, 0, {}};
			const auto probe_session_bytes = darling::windows_host::EncodeMachIpcEnvelope(probe_session_open);
			notification_probe_client.Write(std::string(probe_session_bytes.begin(), probe_session_bytes.end()));
			(void)notification_probe_client.Read();
			darling::windows_host::MachIpcEnvelope foreign_notification_signal{
				darling::windows_host::MachIpcOperation::NotificationSignal, 94,
				owned_notification_response.port_token, 0, {}};
			foreign_notification_signal.session_token = session_response.session_token;
			const auto foreign_notification_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				foreign_notification_signal);
			notification_probe_client.Write(std::string(foreign_notification_bytes.begin(), foreign_notification_bytes.end()));
			if (notification_probe_client.Read() != "INVALID_MACH_IPC_REQUEST") return 10;
		}
		darling::windows_host::MachIpcEnvelope owned_notification_destroy{
			darling::windows_host::MachIpcOperation::NotificationDestroy, 95,
			owned_notification_response.port_token, 0, {}};
		owned_notification_destroy.session_token = session_response.session_token;
		const auto owned_notification_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			owned_notification_destroy);
		client.Write(std::string(owned_notification_destroy_bytes.begin(), owned_notification_destroy_bytes.end()));
		if (client.Read().empty()) return 11;
		std::cout << "BROKER_SESSION_NOTIFICATION_OWNERSHIP=PASS\n";
		darling::windows_host::MachIpcEnvelope transfer_allocate{
			darling::windows_host::MachIpcOperation::Allocate, 96, 0, 0, {}};
		transfer_allocate.session_token = session_response.session_token;
		const auto transfer_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(transfer_allocate);
		client.Write(std::string(transfer_allocate_bytes.begin(), transfer_allocate_bytes.end()));
		const auto transfer_allocate_wire = client.Read();
		const auto transfer_allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(transfer_allocate_wire.begin(), transfer_allocate_wire.end()));
		darling::windows_host::MachIpcEnvelope transfer_ool_send{
			darling::windows_host::MachIpcOperation::Send, 961,
			transfer_allocate_response.port_token, 1, {'O', 'O', 'L'}};
		transfer_ool_send.session_token = session_response.session_token;
		const auto transfer_ool_bytes = darling::windows_host::EncodeMachIpcEnvelope(transfer_ool_send);
		client.Write(std::string(transfer_ool_bytes.begin(), transfer_ool_bytes.end()));
		const auto transfer_ool_wire = client.Read();
		const auto transfer_ool_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(transfer_ool_wire.begin(), transfer_ool_wire.end()));
		if (transfer_ool_response.out_of_line_token == 0) return 17;
		{
			auto transfer_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope transfer_session_open{
				darling::windows_host::MachIpcOperation::SessionOpen, 970, 0, 0, process_id_payload};
			const auto transfer_session_bytes = darling::windows_host::EncodeMachIpcEnvelope(transfer_session_open);
			transfer_client.Write(std::string(transfer_session_bytes.begin(), transfer_session_bytes.end()));
			const auto transfer_session_wire = transfer_client.Read();
			const auto transfer_session_response = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(transfer_session_wire.begin(), transfer_session_wire.end()));
			darling::windows_host::MachIpcEnvelope unauthorized_receive{
				darling::windows_host::MachIpcOperation::Receive, 9701,
				transfer_allocate_response.port_token, 0, {}};
			unauthorized_receive.session_token = transfer_session_response.session_token;
			const auto unauthorized_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				unauthorized_receive);
			transfer_client.Write(std::string(unauthorized_receive_bytes.begin(),
				unauthorized_receive_bytes.end()));
			if (transfer_client.Read() != "INVALID_MACH_IPC_REQUEST") return 19;
			std::cout << "BROKER_MACH_OOL_OWNERSHIP_RETAINED=PASS\n";
			std::vector<std::uint8_t> target_session_payload(8);
			for (unsigned shift = 0; shift < 64; shift += 8)
				target_session_payload[shift / 8] = static_cast<std::uint8_t>(
					transfer_session_response.session_token >> shift);
			darling::windows_host::MachIpcEnvelope transfer_request{
				darling::windows_host::MachIpcOperation::CapabilityTransfer, 97,
				transfer_allocate_response.port_token, 0, target_session_payload};
			transfer_request.session_token = session_response.session_token;
			const auto transfer_request_bytes = darling::windows_host::EncodeMachIpcEnvelope(transfer_request);
			client.Write(std::string(transfer_request_bytes.begin(), transfer_request_bytes.end()));
			const auto transfer_response = darling::windows_host::DecodeMachIpcEnvelope(
				[&] { const auto value = client.Read(); return std::vector<std::uint8_t>(value.begin(), value.end()); }());
			if (transfer_response.operation != darling::windows_host::MachIpcOperation::CapabilityTransfer)
				return 12;
			darling::windows_host::MachIpcEnvelope transferred_receive{
				darling::windows_host::MachIpcOperation::Receive, 971,
				transfer_allocate_response.port_token, 0, {}};
			transferred_receive.session_token = transfer_session_response.session_token;
			const auto transferred_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(transferred_receive);
			transfer_client.Write(std::string(transferred_receive_bytes.begin(), transferred_receive_bytes.end()));
			const auto transferred_receive_wire = transfer_client.Read();
			const auto transferred_receive_response = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(transferred_receive_wire.begin(), transferred_receive_wire.end()));
			if (transferred_receive_response.out_of_line_token == 0 ||
				transferred_receive_response.out_of_line_handle == 0) return 18;
			CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(
				transferred_receive_response.out_of_line_handle)));
			darling::windows_host::MachIpcEnvelope transferred_send{
				darling::windows_host::MachIpcOperation::Send, 98,
				transfer_allocate_response.port_token, 0, {'T'}};
			transferred_send.session_token = transfer_session_response.session_token;
			const auto transferred_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(transferred_send);
			transfer_client.Write(std::string(transferred_send_bytes.begin(), transferred_send_bytes.end()));
			if (transfer_client.Read().empty()) return 13;
			darling::windows_host::MachIpcEnvelope transferred_destroy{
				darling::windows_host::MachIpcOperation::Destroy, 99,
				transfer_allocate_response.port_token, 0, {}};
			transferred_destroy.session_token = transfer_session_response.session_token;
			const auto transferred_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(transferred_destroy);
			transfer_client.Write(std::string(transferred_destroy_bytes.begin(), transferred_destroy_bytes.end()));
			if (transfer_client.Read().empty()) return 14;
		}
		std::cout << "BROKER_CAPABILITY_TRANSFER=PASS\n";
		std::uint64_t stale_session_token = 0;
		{
			auto stale_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope stale_open{
				darling::windows_host::MachIpcOperation::SessionOpen, 980, 0, 0, {}};
			const auto stale_open_bytes = darling::windows_host::EncodeMachIpcEnvelope(stale_open);
			stale_client.Write(std::string(stale_open_bytes.begin(), stale_open_bytes.end()));
			const auto stale_open_wire = stale_client.Read();
			stale_session_token = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(stale_open_wire.begin(), stale_open_wire.end())).session_token;
		}
		Sleep(150);
		darling::windows_host::MachIpcEnvelope stale_probe_allocate{
			darling::windows_host::MachIpcOperation::Allocate, 981, 0, 0, {}};
		stale_probe_allocate.session_token = session_response.session_token;
		const auto stale_probe_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(stale_probe_allocate);
		client.Write(std::string(stale_probe_allocate_bytes.begin(), stale_probe_allocate_bytes.end()));
		const auto stale_probe_wire = client.Read();
		const auto stale_probe_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(stale_probe_wire.begin(), stale_probe_wire.end()));
		std::vector<std::uint8_t> stale_target_payload(8);
		for (unsigned shift = 0; shift < 64; shift += 8)
			stale_target_payload[shift / 8] = static_cast<std::uint8_t>(stale_session_token >> shift);
		darling::windows_host::MachIpcEnvelope stale_transfer{
			darling::windows_host::MachIpcOperation::CapabilityTransfer, 982,
			stale_probe_response.port_token, 0, stale_target_payload};
		stale_transfer.session_token = session_response.session_token;
		const auto stale_transfer_bytes = darling::windows_host::EncodeMachIpcEnvelope(stale_transfer);
		client.Write(std::string(stale_transfer_bytes.begin(), stale_transfer_bytes.end()));
		const auto stale_transfer_result = client.Read();
		std::cout << "BROKER_STALE_TRANSFER_RESULT=" << stale_transfer_result << "\n";
		if (stale_transfer_result != "INVALID_MACH_IPC_REQUEST") return 15;
		darling::windows_host::MachIpcEnvelope stale_probe_destroy{
			darling::windows_host::MachIpcOperation::Destroy, 983,
			stale_probe_response.port_token, 0, {}};
		stale_probe_destroy.session_token = session_response.session_token;
		const auto stale_probe_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(stale_probe_destroy);
		client.Write(std::string(stale_probe_destroy_bytes.begin(), stale_probe_destroy_bytes.end()));
		if (client.Read().empty()) return 16;
		std::cout << "BROKER_STALE_SESSION_REJECTION=PASS\n";

		darling::windows_host::MachIpcEnvelope allocate_request{
			darling::windows_host::MachIpcOperation::Allocate, 100, 0, 0, {}};
		const auto allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(allocate_request);
		client.Write(std::string(allocate_bytes.begin(), allocate_bytes.end()));
		const auto allocate_wire = client.Read();
		const auto allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(allocate_wire.begin(), allocate_wire.end()));
		std::cout << "BROKER_MACH_ALLOCATE=" << allocate_response.port_token << "\n";
		if (allocate_response.operation != darling::windows_host::MachIpcOperation::Allocate ||
			allocate_response.request_id != 100 || allocate_response.port_token == 0) {
			std::cerr << "BROKER_MACH_ALLOCATE=FAIL\n";
			return 5;
		}
		const auto token = allocate_response.port_token;
		{
			auto second_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			second_client.Write("PING");
			if (second_client.Read() != "PONG") {
				std::cerr << "BROKER_CONCURRENT_CONNECT=FAIL\n";
				return 5;
			}
			darling::windows_host::MachIpcEnvelope foreign_session_probe{
				darling::windows_host::MachIpcOperation::Allocate, 1190, 0, 0, {}};
			foreign_session_probe.session_token = session_response.session_token;
			const auto foreign_session_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				foreign_session_probe);
			second_client.Write(std::string(foreign_session_bytes.begin(), foreign_session_bytes.end()));
			if (second_client.Read() != "INVALID_MACH_IPC_REQUEST") {
				std::cerr << "BROKER_SESSION_OWNERSHIP=FAIL\n";
				return 6;
			}
			std::cout << "BROKER_SESSION_OWNERSHIP=PASS\n";
			const darling::windows_host::MachIpcEnvelope cross_client_send{
				darling::windows_host::MachIpcOperation::Send, 120, token, 0,
				{'C', 'R', 'O', 'S', 'S'}};
			const auto cross_client_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				cross_client_send);
			second_client.Write(std::string(cross_client_bytes.begin(), cross_client_bytes.end()));
			const auto cross_client_wire = second_client.Read();
			const auto cross_client_response = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(cross_client_wire.begin(), cross_client_wire.end()));
			if (cross_client_response.operation != darling::windows_host::MachIpcOperation::Send ||
				cross_client_response.port_token != token) {
				std::cerr << "BROKER_CONCURRENT_SEND=FAIL\n";
				return 5;
			}
			const darling::windows_host::MachIpcEnvelope cross_client_receive{
				darling::windows_host::MachIpcOperation::Receive, 121, token, 0, {}};
			const auto cross_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				cross_client_receive);
			client.Write(std::string(cross_receive_bytes.begin(), cross_receive_bytes.end()));
			const auto cross_receive_wire = client.Read();
			const auto cross_receive_response = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(cross_receive_wire.begin(), cross_receive_wire.end()));
			if (cross_receive_response.payload != cross_client_send.payload) {
				std::cerr << "BROKER_CONCURRENT_RECEIVE=FAIL\n";
				return 5;
			}
			std::cout << "BROKER_CONCURRENT_CLIENTS=PASS\n";
		}
		const darling::windows_host::MachIpcEnvelope empty_receive_request{
			darling::windows_host::MachIpcOperation::Receive, 104, token, 0, {}};
		const auto empty_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(empty_receive_request);
		client.Write(std::string(empty_receive_bytes.begin(), empty_receive_bytes.end()));
		if (client.Read() != "MACH_RECEIVE_WOULD_BLOCK") {
			std::cerr << "BROKER_MACH_EMPTY_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_EMPTY_RECEIVE=PASS\n";
		const darling::windows_host::MachIpcEnvelope timed_receive_request{
			darling::windows_host::MachIpcOperation::Receive, 115, token, 0,
			{25, 0, 0, 0}};
		const auto timed_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(timed_receive_request);
		client.Write(std::string(timed_receive_bytes.begin(), timed_receive_bytes.end()));
		if (client.Read() != "MACH_RECEIVE_WOULD_BLOCK") {
			std::cerr << "BROKER_MACH_RECEIVE_TIMEOUT=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_RECEIVE_TIMEOUT=PASS\n";
		std::vector<std::uint8_t> blocked_receive_payload;
		std::string blocked_cancel_result;
		std::thread blocked_receiver([&] {
			auto blocked_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope blocked_session_open{
				darling::windows_host::MachIpcOperation::SessionOpen, 1178, 0, 0, {}};
			const auto blocked_session_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				blocked_session_open);
			blocked_client.Write(std::string(blocked_session_bytes.begin(), blocked_session_bytes.end()));
			const auto blocked_session_wire = blocked_client.Read();
			const auto blocked_session = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(blocked_session_wire.begin(), blocked_session_wire.end()));
			const darling::windows_host::MachIpcEnvelope blocked_request{
				darling::windows_host::MachIpcOperation::Receive, 118, token, 0,
				{232, 3, 0, 0}};
			auto authenticated_blocked_request = blocked_request;
			authenticated_blocked_request.session_token = blocked_session.session_token;
			const auto blocked_bytes = darling::windows_host::EncodeMachIpcEnvelope(
				authenticated_blocked_request);
			blocked_client.Write(std::string(blocked_bytes.begin(), blocked_bytes.end()));
			const auto blocked_wire = blocked_client.Read();
			blocked_cancel_result.assign(blocked_wire.begin(), blocked_wire.end());
		});
		Sleep(250);
		const darling::windows_host::MachIpcEnvelope cross_client_cancel{
			darling::windows_host::MachIpcOperation::Cancel, 118, 0, 0, {}};
		auto authenticated_cancel = cross_client_cancel;
		authenticated_cancel.session_token = session_response.session_token;
		const auto cancel_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			authenticated_cancel);
		client.Write(std::string(cancel_bytes.begin(), cancel_bytes.end()));
		const auto cancel_result = client.Read();
		if (cancel_result.empty()) {
			blocked_receiver.join();
			std::cerr << "BROKER_MACH_CROSS_CLIENT_CANCEL=FAIL\n";
			return 5;
		}
		blocked_receiver.join();
		if (blocked_cancel_result != "MACH_REQUEST_CANCELLED") {
			std::cerr << "BROKER_MACH_CROSS_CLIENT_CANCEL=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_CROSS_CLIENT_CANCEL=PASS\n";
		const darling::windows_host::MachIpcEnvelope send_request{
			darling::windows_host::MachIpcOperation::Send, 101, token, 0,
			{'B', 'R', 'O', 'K', 'E', 'R'}};
		const auto send_bytes = darling::windows_host::EncodeMachIpcEnvelope(send_request);
		client.Write(std::string(send_bytes.begin(), send_bytes.end()));
		const auto send_wire = client.Read();
		const auto send_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(send_wire.begin(), send_wire.end()));
		if (send_response.operation != darling::windows_host::MachIpcOperation::Send ||
			send_response.request_id != 101 || send_response.port_token != token) {
			std::cerr << "BROKER_MACH_SEND=FAIL\n";
			return 5;
		}

		const darling::windows_host::MachIpcEnvelope receive_request{
			darling::windows_host::MachIpcOperation::Receive, 102, token, 0, {}};
		const auto receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(receive_request);
		client.Write(std::string(receive_bytes.begin(), receive_bytes.end()));
		const auto receive_wire = client.Read();
		const auto receive_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(receive_wire.begin(), receive_wire.end()));
		if (receive_response.operation != darling::windows_host::MachIpcOperation::Receive ||
			receive_response.request_id != 102 || receive_response.port_token != token ||
			receive_response.payload != send_request.payload) {
			std::cerr << "BROKER_MACH_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_SEND_RECEIVE=PASS\n";
		const darling::windows_host::MachIpcEnvelope immediate_receive{
			darling::windows_host::MachIpcOperation::Receive, 122, token, 0, {}};
		const auto immediate_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			immediate_receive);
		client.Write(std::string(immediate_receive_bytes.begin(), immediate_receive_bytes.end()));
		if (client.Read() != "MACH_RECEIVE_WOULD_BLOCK") {
			std::cerr << "BROKER_MACH_IMMEDIATE_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_IMMEDIATE_RECEIVE=PASS\n";
		const darling::windows_host::MachIpcEnvelope reused_request_id{
			darling::windows_host::MachIpcOperation::Receive, 122, token, 0, {1, 0, 0, 0}};
		const auto reused_request_id_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			reused_request_id);
		client.Write(std::string(reused_request_id_bytes.begin(), reused_request_id_bytes.end()));
		if (client.Read() != "MACH_RECEIVE_WOULD_BLOCK") {
			std::cerr << "BROKER_MACH_REQUEST_ID_REUSE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_REQUEST_ID_REUSE=PASS\n";
		const darling::windows_host::MachIpcEnvelope invalid_ready_receive{
			darling::windows_host::MachIpcOperation::Receive, 116, token, 0, {1}};
		const auto invalid_ready_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_ready_receive);
		client.Write(std::string(invalid_ready_receive_bytes.begin(), invalid_ready_receive_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_RECEIVE_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_RECEIVE_VALIDATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope invalid_receive_descriptor{
			darling::windows_host::MachIpcOperation::Receive, 117, token, 1, {}, 1, 1};
		const auto invalid_receive_descriptor_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_receive_descriptor);
		client.Write(std::string(invalid_receive_descriptor_bytes.begin(), invalid_receive_descriptor_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_RECEIVE_DESCRIPTOR_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_RECEIVE_DESCRIPTOR_VALIDATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope invalid_send_descriptor{
			darling::windows_host::MachIpcOperation::Send, 118, token, 0, {'X'}, 1, 1};
		const auto invalid_send_descriptor_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_send_descriptor);
		client.Write(std::string(invalid_send_descriptor_bytes.begin(), invalid_send_descriptor_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_SEND_DESCRIPTOR_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_SEND_DESCRIPTOR_VALIDATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope too_large_send{
			darling::windows_host::MachIpcOperation::Send, 119, token, 0,
			{'T', 'O', 'O', 'L', 'A', 'R', 'G', 'E'}};
		const auto too_large_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			too_large_send);
		client.Write(std::string(too_large_send_bytes.begin(), too_large_send_bytes.end()));
		(void)client.Read();
		const darling::windows_host::MachIpcEnvelope too_large_receive{
			darling::windows_host::MachIpcOperation::Receive, 120, token, 0,
			{0, 0, 0, 0, 1, 0, 0, 0}};
		const auto too_large_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			too_large_receive);
		client.Write(std::string(too_large_receive_bytes.begin(), too_large_receive_bytes.end()));
		const auto too_large_wire = client.Read();
		const auto too_large_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(too_large_wire.begin(), too_large_wire.end()));
		if (too_large_response.operation != darling::windows_host::MachIpcOperation::Receive ||
			too_large_response.request_id != 120 || too_large_response.payload !=
				std::vector<std::uint8_t>({8, 0, 0, 0})) {
			std::cerr << "BROKER_MACH_RECEIVE_TOO_LARGE=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope too_large_retry{
			darling::windows_host::MachIpcOperation::Receive, 121, token, 0, {}};
		const auto too_large_retry_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			too_large_retry);
		client.Write(std::string(too_large_retry_bytes.begin(), too_large_retry_bytes.end()));
		const auto too_large_retry_wire = client.Read();
		const auto too_large_retry_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(too_large_retry_wire.begin(), too_large_retry_wire.end()));
		if (too_large_retry_response.payload != too_large_send.payload) {
			std::cerr << "BROKER_MACH_RECEIVE_TOO_LARGE_RETRY=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_RECEIVE_TOO_LARGE=PASS\n";
		const darling::windows_host::MachIpcEnvelope large_send_request{
			darling::windows_host::MachIpcOperation::Send, 105, token, 0,
			std::vector<std::uint8_t>(2 * 1024 * 1024, 0x5a)};
		const auto large_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(large_send_request);
		client.Write(std::string(large_send_bytes.begin(), large_send_bytes.end()));
		const auto large_send_wire = client.Read();
		const auto large_send_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(large_send_wire.begin(), large_send_wire.end()));
		if (large_send_response.operation != darling::windows_host::MachIpcOperation::Send ||
			large_send_response.request_id != 105 || large_send_response.port_token != token) {
			std::cerr << "BROKER_MACH_LARGE_SEND=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope large_receive_request{
			darling::windows_host::MachIpcOperation::Receive, 106, token, 0, {}};
		const auto large_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(large_receive_request);
		client.Write(std::string(large_receive_bytes.begin(), large_receive_bytes.end()));
		const auto large_receive_wire = client.Read();
		const auto large_receive_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(large_receive_wire.begin(), large_receive_wire.end()));
		if (large_receive_response.payload != large_send_request.payload) {
			std::cerr << "BROKER_MACH_LARGE_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_LARGE_PAYLOAD=PASS\n";
		const darling::windows_host::MachIpcEnvelope ool_send_request{
			darling::windows_host::MachIpcOperation::Send, 107, token, 1,
			{'O', 'O', 'L', '-', 'D', 'A', 'T', 'A'}};
		const auto ool_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(ool_send_request);
		client.Write(std::string(ool_send_bytes.begin(), ool_send_bytes.end()));
		const auto ool_send_wire = client.Read();
		const auto ool_send_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(ool_send_wire.begin(), ool_send_wire.end()));
		if (ool_send_response.out_of_line_token == 0 ||
			ool_send_response.out_of_line_size != ool_send_request.payload.size()) {
			std::cerr << "BROKER_MACH_OOL_SEND=FAIL\n";
			return 5;
		}
		const auto ool_name = L"Local\\wintosh-mach-ool-" +
			std::to_wstring(ool_send_response.out_of_line_token);
		auto ool_region = darling::windows_host::MachIpcSharedMemory::Open(
			ool_name, ool_send_response.out_of_line_size);
		const auto* ool_data = static_cast<const std::uint8_t*>(ool_region.Data());
		if (!std::equal(ool_data, ool_data + ool_region.Size(), ool_send_request.payload.begin())) {
			std::cerr << "BROKER_MACH_OOL_CONTENT=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope ool_receive_request{
			darling::windows_host::MachIpcOperation::Receive, 108, token, 0, {}};
		const auto ool_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(ool_receive_request);
		client.Write(std::string(ool_receive_bytes.begin(), ool_receive_bytes.end()));
		const auto ool_receive_wire = client.Read();
		const auto ool_receive_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(ool_receive_wire.begin(), ool_receive_wire.end()));
		if (ool_receive_response.out_of_line_token != ool_send_response.out_of_line_token ||
			ool_receive_response.out_of_line_size != ool_send_response.out_of_line_size) {
			std::cerr << "BROKER_MACH_OOL_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_OOL=PASS\n";
		const darling::windows_host::MachIpcEnvelope unsupported_dispositions{
			darling::windows_host::MachIpcOperation::Send, 113, token, 2, {'X'}};
		const auto unsupported_bytes = darling::windows_host::EncodeMachIpcEnvelope(unsupported_dispositions);
		client.Write(std::string(unsupported_bytes.begin(), unsupported_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_DISPOSITION_LIMIT=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_DISPOSITION_LIMIT=PASS\n";

		const darling::windows_host::MachIpcEnvelope deallocate_request{
			darling::windows_host::MachIpcOperation::Deallocate, 103, token, 0, {}};
		const auto deallocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(deallocate_request);
		client.Write(std::string(deallocate_bytes.begin(), deallocate_bytes.end()));
		const auto deallocate_wire = client.Read();
		const auto deallocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(deallocate_wire.begin(), deallocate_wire.end()));
		if (deallocate_response.operation != darling::windows_host::MachIpcOperation::Deallocate ||
			deallocate_response.request_id != 103 || deallocate_response.port_token != token) {
			std::cerr << "BROKER_MACH_DEALLOCATE=FAIL\n";
			return 5;
		}
		const auto invalid_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(send_request);
		client.Write(std::string(invalid_send_bytes.begin(), invalid_send_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_STALE_TOKEN=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_LIFECYCLE=PASS\n";
		const darling::windows_host::MachIpcEnvelope invalid_lifecycle_request{
			darling::windows_host::MachIpcOperation::Destroy, 119, token, 0,
			{'X'}};
		const auto invalid_lifecycle_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_lifecycle_request);
		client.Write(std::string(invalid_lifecycle_bytes.begin(), invalid_lifecycle_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_LIFECYCLE_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_LIFECYCLE_VALIDATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope death_allocate_request{
			darling::windows_host::MachIpcOperation::Allocate, 124, 0, 0, {}};
		const auto death_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			death_allocate_request);
		client.Write(std::string(death_allocate_bytes.begin(), death_allocate_bytes.end()));
		const auto death_allocate_wire = client.Read();
		const auto death_allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(death_allocate_wire.begin(), death_allocate_wire.end()));
		const auto death_token = death_allocate_response.port_token;
		std::string death_receive_result;
		std::thread death_receiver([&] {
			auto death_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope death_receive{
				darling::windows_host::MachIpcOperation::Receive, 125, death_token, 0,
				{232, 3, 0, 0}};
			const auto bytes = darling::windows_host::EncodeMachIpcEnvelope(death_receive);
			death_client.Write(std::string(bytes.begin(), bytes.end()));
			death_receive_result = death_client.Read();
		});
		Sleep(250);
		const darling::windows_host::MachIpcEnvelope death_destroy{
			darling::windows_host::MachIpcOperation::Destroy, 126, death_token, 0, {}};
		const auto death_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(death_destroy);
		client.Write(std::string(death_destroy_bytes.begin(), death_destroy_bytes.end()));
		const auto death_destroy_result = client.Read();
		if (death_destroy_result.empty()) {
			death_receiver.join();
			throw std::runtime_error("port-death destroy failed");
		}
		death_receiver.join();
		if (death_receive_result != "MACH_PORT_DEAD") {
			std::cerr << "BROKER_MACH_PORT_DEATH=FAIL RESULT=" << death_receive_result << "\n";
			return 5;
		}
		std::cout << "BROKER_MACH_PORT_DEATH=PASS\n";
		const darling::windows_host::MachIpcEnvelope second_allocate_request{
			darling::windows_host::MachIpcOperation::Allocate, 109, 0, 0, {}};
		const auto second_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(second_allocate_request);
		client.Write(std::string(second_allocate_bytes.begin(), second_allocate_bytes.end()));
		const auto second_allocate_wire = client.Read();
		const auto second_allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(second_allocate_wire.begin(), second_allocate_wire.end()));
		const auto destroyed_token = second_allocate_response.port_token;
		const darling::windows_host::MachIpcEnvelope destroy_request{
			darling::windows_host::MachIpcOperation::Destroy, 110, destroyed_token, 0, {}};
		const auto destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(destroy_request);
		client.Write(std::string(destroy_bytes.begin(), destroy_bytes.end()));
		const auto destroy_wire = client.Read();
		const auto destroy_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(destroy_wire.begin(), destroy_wire.end()));
		if (destroy_response.operation != darling::windows_host::MachIpcOperation::Destroy ||
			destroy_response.port_token != destroyed_token) {
			std::cerr << "BROKER_MACH_DESTROY=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope destroyed_send{
			darling::windows_host::MachIpcOperation::Send, 111, destroyed_token, 0, {'X'}};
		const auto destroyed_send_bytes = darling::windows_host::EncodeMachIpcEnvelope(destroyed_send);
		client.Write(std::string(destroyed_send_bytes.begin(), destroyed_send_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_DESTROY_STALE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_DESTROY=PASS\n";
		const darling::windows_host::MachIpcEnvelope queue_allocate_request{
			darling::windows_host::MachIpcOperation::Allocate, 112, 0, 0, {}};
		const auto queue_allocate_bytes = darling::windows_host::EncodeMachIpcEnvelope(queue_allocate_request);
		client.Write(std::string(queue_allocate_bytes.begin(), queue_allocate_bytes.end()));
		const auto queue_allocate_wire = client.Read();
		const auto queue_allocate_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(queue_allocate_wire.begin(), queue_allocate_wire.end()));
		const auto queue_token = queue_allocate_response.port_token;
		for (std::uint64_t request_id = 0; request_id != 1024; ++request_id) {
			const darling::windows_host::MachIpcEnvelope queued_send{
				darling::windows_host::MachIpcOperation::Send, 1000 + request_id,
				queue_token, 0, {'Q'}};
			const auto queued_bytes = darling::windows_host::EncodeMachIpcEnvelope(queued_send);
			client.Write(std::string(queued_bytes.begin(), queued_bytes.end()));
			(void)client.Read();
		}
		const darling::windows_host::MachIpcEnvelope overflow_send{
			darling::windows_host::MachIpcOperation::Send, 2024, queue_token, 0, {'Q'}};
		const auto overflow_bytes = darling::windows_host::EncodeMachIpcEnvelope(overflow_send);
		client.Write(std::string(overflow_bytes.begin(), overflow_bytes.end()));
		if (client.Read() != "MACH_SEND_QUEUE_FULL") {
			std::cerr << "BROKER_MACH_QUEUE_LIMIT=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope queue_destroy{
			darling::windows_host::MachIpcOperation::Destroy, 2025, queue_token, 0, {}};
		const auto queue_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(queue_destroy);
		client.Write(std::string(queue_destroy_bytes.begin(), queue_destroy_bytes.end()));
		(void)client.Read();
		std::cout << "BROKER_MACH_QUEUE_LIMIT=PASS\n";
		const darling::windows_host::MachIpcEnvelope notification_create{
			darling::windows_host::MachIpcOperation::NotificationCreate, 2030, 0, 0, {}};
		const auto notification_create_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_create);
		client.Write(std::string(notification_create_bytes.begin(), notification_create_bytes.end()));
		const auto notification_create_wire = client.Read();
		const auto notification_create_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(notification_create_wire.begin(), notification_create_wire.end()));
		const auto notification_token = notification_create_response.port_token;
		const darling::windows_host::MachIpcEnvelope notification_wait{
			darling::windows_host::MachIpcOperation::NotificationWait, 2031, notification_token, 0,
			{1, 0, 0, 0}};
		const auto notification_wait_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_wait);
		client.Write(std::string(notification_wait_bytes.begin(), notification_wait_bytes.end()));
		const auto notification_wait_wire = client.Read();
		const auto notification_wait_response = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(notification_wait_wire.begin(), notification_wait_wire.end()));
		if (notification_wait_response.payload != std::vector<std::uint8_t>{0}) {
			std::cerr << "BROKER_MACH_NOTIFICATION_TIMEOUT=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope notification_signal{
			darling::windows_host::MachIpcOperation::NotificationSignal, 2032, notification_token, 0, {}};
		const auto notification_signal_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_signal);
		client.Write(std::string(notification_signal_bytes.begin(), notification_signal_bytes.end()));
		(void)client.Read();
		const auto notification_wait_signal_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			darling::windows_host::MachIpcEnvelope{
				darling::windows_host::MachIpcOperation::NotificationWait, 2033,
				notification_token, 0, {}});
		client.Write(std::string(notification_wait_signal_bytes.begin(), notification_wait_signal_bytes.end()));
		const auto notification_wait_signal_wire = client.Read();
		const auto notification_wait_signal = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(notification_wait_signal_wire.begin(), notification_wait_signal_wire.end()));
		if (notification_wait_signal.payload != std::vector<std::uint8_t>{1}) {
			std::cerr << "BROKER_MACH_NOTIFICATION_SIGNAL=FAIL\n";
			return 5;
		}
		const darling::windows_host::MachIpcEnvelope notification_reset{
			darling::windows_host::MachIpcOperation::NotificationReset, 2035, notification_token, 0, {}};
		const auto notification_reset_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_reset);
		client.Write(std::string(notification_reset_bytes.begin(), notification_reset_bytes.end()));
		(void)client.Read();
		const auto notification_wait_reset_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			darling::windows_host::MachIpcEnvelope{
				darling::windows_host::MachIpcOperation::NotificationWait, 2036,
				notification_token, 0, {1, 0, 0, 0}});
		client.Write(std::string(notification_wait_reset_bytes.begin(), notification_wait_reset_bytes.end()));
		const auto notification_wait_reset_wire = client.Read();
		const auto notification_wait_reset = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(notification_wait_reset_wire.begin(), notification_wait_reset_wire.end()));
		if (notification_wait_reset.payload != std::vector<std::uint8_t>{0}) {
			std::cerr << "BROKER_MACH_NOTIFICATION_RESET=FAIL\n";
			return 5;
		}
		std::vector<std::uint8_t> cross_client_wait_payload;
		std::thread waiting_client([&] {
			auto wait_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
			const darling::windows_host::MachIpcEnvelope wait_request{
				darling::windows_host::MachIpcOperation::NotificationWait, 2040,
				notification_token, 0, {}};
			const auto wait_bytes = darling::windows_host::EncodeMachIpcEnvelope(wait_request);
			wait_client.Write(std::string(wait_bytes.begin(), wait_bytes.end()));
			const auto wait_wire = wait_client.Read();
			const auto wait_response = darling::windows_host::DecodeMachIpcEnvelope(
				std::vector<std::uint8_t>(wait_wire.begin(), wait_wire.end()));
			cross_client_wait_payload = wait_response.payload;
		});
		Sleep(25);
		auto signaling_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
		const auto signal_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_signal);
		signaling_client.Write(std::string(signal_bytes.begin(), signal_bytes.end()));
		(void)signaling_client.Read();
		waiting_client.join();
		if (cross_client_wait_payload != std::vector<std::uint8_t>{1}) {
			std::cerr << "BROKER_MACH_CROSS_CLIENT_WAKE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_CROSS_CLIENT_WAKE=PASS\n";
		const auto reset_after_cross_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_reset);
		client.Write(std::string(reset_after_cross_bytes.begin(), reset_after_cross_bytes.end()));
		(void)client.Read();
		const darling::windows_host::MachIpcEnvelope invalid_notification_wait{
			darling::windows_host::MachIpcOperation::NotificationWait, 2037,
			notification_token, 0, {1}};
		const auto invalid_notification_wait_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_notification_wait);
		client.Write(std::string(invalid_notification_wait_bytes.begin(), invalid_notification_wait_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_NOTIFICATION_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_NOTIFICATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope invalid_notification_signal{
			darling::windows_host::MachIpcOperation::NotificationSignal, 2038,
			notification_token, 0, {'X'}};
		const auto invalid_notification_signal_bytes = darling::windows_host::EncodeMachIpcEnvelope(
			invalid_notification_signal);
		client.Write(std::string(invalid_notification_signal_bytes.begin(), invalid_notification_signal_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_NOTIFICATION_VALIDATION=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_NOTIFICATION_VALIDATION=PASS\n";
		const darling::windows_host::MachIpcEnvelope notification_destroy{
			darling::windows_host::MachIpcOperation::NotificationDestroy, 2034, notification_token, 0, {}};
		const auto notification_destroy_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_destroy);
		client.Write(std::string(notification_destroy_bytes.begin(), notification_destroy_bytes.end()));
		(void)client.Read();
		const auto stale_notification_bytes = darling::windows_host::EncodeMachIpcEnvelope(notification_signal);
		client.Write(std::string(stale_notification_bytes.begin(), stale_notification_bytes.end()));
		if (client.Read() != "INVALID_MACH_IPC_REQUEST") {
			std::cerr << "BROKER_MACH_NOTIFICATION_DESTROY=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_NOTIFICATION_DESTROY=PASS\n";

		}

		// The first client is intentionally destroyed before reconnecting.  The
		// broker must retain its namespace and accept this second connection.
		auto reconnect_client = darling::windows_host::NamedPipeRpcClient::Connect(pipe_name);
		reconnect_client.Write("PING");
		if (reconnect_client.Read() != "PONG") {
			std::cerr << "BROKER_RECONNECT=FAIL\n";
			return 6;
		}
		std::cout << "BROKER_RECONNECT=PASS\n";
		reconnect_client.Write("SHUTDOWN");
		const auto shutdown = reconnect_client.Read();
		std::cout << "BROKER_SHUTDOWN=" << shutdown << "\n";
		if (shutdown != "BYE") {
			std::cerr << "BROKER_EXIT=FAIL\n";
			return 6;
		}
		reconnect_client.Write("ACK");
		if (child.Wait(10000) != WAIT_OBJECT_0 || child.ExitCode() != 0) {
			std::cerr << "BROKER_EXIT=FAIL\n";
			return 6;
		}

		std::error_code cleanup_error;
		std::filesystem::remove_all(root, cleanup_error);
		std::cout << "BROKER_EXIT=0\n";
		std::cout << "BROKER_CLEANUP=" << (cleanup_error ? "FAIL" : "PASS") << "\n";
		return cleanup_error ? 7 : 0;
	} catch (const std::exception& error) {
		std::cerr << "BROKER_SMOKE_ERROR=" << error.what() << "\n";
		return 8;
	}
}
