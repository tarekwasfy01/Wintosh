/*
 * Stage 1 out-of-process broker proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darwin_windows_process.h"
#include "darling_windows_runtime.h"

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
