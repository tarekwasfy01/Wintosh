/*
 * Stage 1 out-of-process broker proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darwin_windows_process.h"
#include "darling_windows_runtime.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

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

		const darling::windows_host::MachIpcEnvelope allocate_request{
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
		const darling::windows_host::MachIpcEnvelope empty_receive_request{
			darling::windows_host::MachIpcOperation::Receive, 104, token, 0, {}};
		const auto empty_receive_bytes = darling::windows_host::EncodeMachIpcEnvelope(empty_receive_request);
		client.Write(std::string(empty_receive_bytes.begin(), empty_receive_bytes.end()));
		if (client.Read() != "MACH_RECEIVE_WOULD_BLOCK") {
			std::cerr << "BROKER_MACH_EMPTY_RECEIVE=FAIL\n";
			return 5;
		}
		std::cout << "BROKER_MACH_EMPTY_RECEIVE=PASS\n";
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
		std::cout << "BROKER_MACH_NOTIFICATION=PASS\n";

		client.Write("SHUTDOWN");
		const auto shutdown = client.Read();
		std::cout << "BROKER_SHUTDOWN=" << shutdown << "\n";
		if (shutdown != "BYE") {
			std::cerr << "BROKER_EXIT=FAIL\n";
			return 6;
		}
		client.Write("ACK");
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
