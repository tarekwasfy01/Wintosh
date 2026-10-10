/*
 * Stage 1 runtime proof for the Darling Windows host boundary.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_runtime.h"

#include <chrono>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <system_error>
#include <utility>

int wmain()
{
	darling::windows_host::MachIpcEnvelope envelope{
		darling::windows_host::MachIpcOperation::Send, 42, 7, 1, {'M', 'A', 'C', 'H'}};
	const auto encoded = darling::windows_host::EncodeMachIpcEnvelope(envelope);
	const auto decoded = darling::windows_host::DecodeMachIpcEnvelope(encoded);
	if (decoded.operation != envelope.operation || decoded.request_id != 42 ||
		decoded.port_token != 7 || decoded.disposition_count != 1 ||
		decoded.payload != envelope.payload) {
		std::cerr << "MACH_IPC_ENVELOPE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_ENVELOPE=PASS\n";
	darling::windows_host::MachMessage inline_message{{'I', 'N'}, {}};
	const auto inline_ipc = darling::windows_host::EncodeMachMessageForIpc(inline_message, 50, 8, 11);
	const auto inline_roundtrip = darling::windows_host::DecodeMachMessageFromIpc(inline_ipc);
	darling::windows_host::MachMessage ool_message{{}, {'O', 'O', 'L'}};
	const auto ool_ipc = darling::windows_host::EncodeMachMessageForIpc(ool_message, 51, 8, 11);
	const auto ool_roundtrip = darling::windows_host::DecodeMachMessageFromIpc(ool_ipc);
	if (inline_ipc.disposition_count != 0 || inline_roundtrip.inline_data != inline_message.inline_data ||
		ool_ipc.disposition_count != 1 || ool_roundtrip.out_of_line_data != ool_message.out_of_line_data) {
		std::cerr << "MACH_IPC_MESSAGE_BRIDGE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_MESSAGE_BRIDGE=PASS\n";
	const HANDLE bridge_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
		PAGE_READWRITE, 0, 3, nullptr);
	void* bridge_view = bridge_mapping == nullptr ? nullptr : MapViewOfFile(
		bridge_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 3);
	HANDLE bridge_duplicate = nullptr;
	if (bridge_view == nullptr || !DuplicateHandle(GetCurrentProcess(), bridge_mapping,
		GetCurrentProcess(), &bridge_duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		if (bridge_view != nullptr) UnmapViewOfFile(bridge_view);
		if (bridge_mapping != nullptr) CloseHandle(bridge_mapping);
		std::cerr << "MACH_IPC_NATIVE_HANDLE=FAIL\n";
		return 1;
	}
	std::memcpy(bridge_view, "OOL", 3);
	UnmapViewOfFile(bridge_view);
	CloseHandle(bridge_mapping);
	darling::windows_host::MachIpcEnvelope native_handle_envelope{
		darling::windows_host::MachIpcOperation::Receive, 52, 8, 1, {}, 77, 3, 11,
		static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(bridge_duplicate))};
	const auto native_handle_message = darling::windows_host::DecodeMachMessageFromIpc(
		native_handle_envelope);
	if (native_handle_message.out_of_line_data != std::vector<std::uint8_t>{'O', 'O', 'L'}) {
		std::cerr << "MACH_IPC_NATIVE_HANDLE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_NATIVE_HANDLE=PASS\n";
	try {
		darling::windows_host::MachIpcEnvelope inconsistent_ool{
			darling::windows_host::MachIpcOperation::Receive, 53, 8, 1, {'X'}, 78, 2, 11};
		(void)darling::windows_host::DecodeMachMessageFromIpc(inconsistent_ool);
		std::cerr << "MACH_IPC_OOL_VALIDATION=FAIL\n";
		return 1;
	} catch (const std::invalid_argument&) {
		std::cout << "MACH_IPC_OOL_VALIDATION=PASS\n";
	}
	darling::windows_host::MachIpcEnvelope cancel{
		darling::windows_host::MachIpcOperation::Cancel, 44, 0, 0, {}, 0, 0, 99};
	const auto cancel_decoded = darling::windows_host::DecodeMachIpcEnvelope(
		darling::windows_host::EncodeMachIpcEnvelope(cancel));
	if (cancel_decoded.operation != darling::windows_host::MachIpcOperation::Cancel ||
		cancel_decoded.request_id != 44 || cancel_decoded.session_token != 99) {
		std::cerr << "MACH_IPC_CANCEL_ENVELOPE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_CANCEL_ENVELOPE=PASS\n";
	try {
		(void)darling::windows_host::DecodeMachIpcEnvelope({'B', 'A', 'D'});
		std::cerr << "MACH_IPC_ENVELOPE_VALIDATION=FAIL\n";
		return 1;
	} catch (const std::invalid_argument&) {
		std::cout << "MACH_IPC_ENVELOPE_VALIDATION=PASS\n";
	}
	try {
		darling::windows_host::MachIpcEnvelope oversized{
			darling::windows_host::MachIpcOperation::Send, 43, 7, 0,
			std::vector<std::uint8_t>(4 * 1024 * 1024 + 1, 0)};
		(void)darling::windows_host::EncodeMachIpcEnvelope(oversized);
		std::cerr << "MACH_IPC_ENVELOPE_LIMIT=FAIL\n";
		return 1;
	} catch (const std::length_error&) {
		std::cout << "MACH_IPC_ENVELOPE_LIMIT=PASS\n";
	}
	try {
		auto trailing = encoded;
		trailing.push_back(0);
		(void)darling::windows_host::DecodeMachIpcEnvelope(trailing);
		std::cerr << "MACH_IPC_ENVELOPE_TRAILING=FAIL\n";
		return 1;
	} catch (const std::invalid_argument&) {
		std::cout << "MACH_IPC_ENVELOPE_TRAILING=PASS\n";
	}
	try {
		auto invalid_descriptor = encoded;
		invalid_descriptor.resize(invalid_descriptor.size() - 4);
		invalid_descriptor.insert(invalid_descriptor.end(), {1, 0, 0, 0, 0, 0, 0, 0});
		(void)darling::windows_host::DecodeMachIpcEnvelope(invalid_descriptor);
		std::cerr << "MACH_IPC_DESCRIPTOR_VALIDATION=FAIL\n";
		return 1;
	} catch (const std::invalid_argument&) {
		std::cout << "MACH_IPC_DESCRIPTOR_VALIDATION=PASS\n";
	}
	const auto mapping_name = L"Local\\wintosh-mach-ool-" +
		std::to_wstring(GetCurrentProcessId());
	auto mapping = darling::windows_host::MachIpcSharedMemory::Create(mapping_name, 4096);
	std::memset(mapping.Data(), 0, mapping.Size());
	static_cast<std::uint8_t*>(mapping.Data())[0] = 0x7a;
	auto mapped_peer = darling::windows_host::MachIpcSharedMemory::Open(mapping_name, 4096);
	if (mapped_peer.Size() != 4096 || static_cast<std::uint8_t*>(mapped_peer.Data())[0] != 0x7a) {
		std::cerr << "MACH_IPC_SHARED_MEMORY=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_SHARED_MEMORY=PASS\n";
	const auto notification_name = L"Local\\wintosh-mach-notification-" +
		std::to_wstring(GetCurrentProcessId());
	auto notification = darling::windows_host::MachIpcNotification::Create(notification_name);
	auto notification_peer = darling::windows_host::MachIpcNotification::Open(notification_name);
	if (notification_peer.Wait(1)) {
		std::cerr << "MACH_IPC_NOTIFICATION_TIMEOUT=FAIL\n";
		return 1;
	}
	notification.Signal();
	if (!notification_peer.Wait(1000)) {
		std::cerr << "MACH_IPC_NOTIFICATION_SIGNAL=FAIL\n";
		return 1;
	}
	notification.Reset();
	if (notification_peer.Wait(1)) {
		std::cerr << "MACH_IPC_NOTIFICATION_RESET=FAIL\n";
		return 1;
	}
	std::cout << "MACH_IPC_NOTIFICATION=PASS\n";
	darling::windows_host::MachPort port;
	if (port.Receive(1).has_value()) {
		std::cerr << "MACH_PORT_TIMEOUT=FAIL\n";
		return 1;
	}
	std::thread sender([&] {
		Sleep(20);
		const char payload[] = "DARLING-MACH";
		const char attached[] = "OUT-OF-LINE";
		(void)port.Send({
			std::vector<std::uint8_t>(payload, payload + sizeof(payload) - 1),
			std::vector<std::uint8_t>(attached, attached + sizeof(attached) - 1)});
	});
	const auto message = port.Receive(2000);
	sender.join();
	if (!message || std::string(message->inline_data.begin(), message->inline_data.end()) != "DARLING-MACH" ||
		std::string(message->out_of_line_data.begin(), message->out_of_line_data.end()) != "OUT-OF-LINE") {
		std::cerr << "MACH_PORT_MESSAGE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_PORT_MESSAGE=PASS\n";
	std::cout << "MACH_PORT_OUT_OF_LINE=PASS\n";
	darling::windows_host::MachPort bounded_port;
	for (std::size_t index = 0; index != 1024; ++index) {
		if (!bounded_port.Send({{static_cast<std::uint8_t>(index & 0xff)}, {}})) {
			std::cerr << "MACH_PORT_QUEUE_LIMIT=FAIL\n";
			return 1;
		}
	}
	if (bounded_port.Send({{0xff}, {}})) {
		std::cerr << "MACH_PORT_QUEUE_LIMIT=FAIL\n";
		return 1;
	}
	for (std::size_t index = 0; index != 1024; ++index) {
		const auto queued_message = bounded_port.Receive(1);
		if (!queued_message || queued_message->inline_data.size() != 1 ||
			queued_message->inline_data.front() != static_cast<std::uint8_t>(index & 0xff)) {
			std::cerr << "MACH_PORT_QUEUE_DRAIN=FAIL\n";
			return 1;
		}
	}
	std::cout << "MACH_PORT_QUEUE_LIMIT=PASS\n";
	port.Close();
	if (!port.IsClosed() || port.Send({})) {
		std::cerr << "MACH_PORT_CLOSE=FAIL\n";
		return 1;
	}
	if (port.Receive(10).has_value()) {
		std::cerr << "MACH_PORT_CLOSE_RECEIVE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_PORT_CLOSE=PASS\n";
	darling::windows_host::MachPort waiting_port;
	bool woke_after_close = false;
	std::thread receiver([&] {
		woke_after_close = !waiting_port.Receive(2000).has_value();
	});
	Sleep(20);
	waiting_port.Close();
	receiver.join();
	if (!woke_after_close) {
		std::cerr << "MACH_PORT_CLOSE_WAKE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_PORT_CLOSE_WAKE=PASS\n";
	darling::windows_host::MachPort interrupted_port;
	bool interrupted = false;
	bool second_interrupted = false;
	std::thread interrupted_receiver([&] {
		interrupted = !interrupted_port.Receive(2000).has_value();
	});
	std::thread second_interrupted_receiver([&] {
		second_interrupted = !interrupted_port.Receive(2000).has_value();
	});
	Sleep(20);
	interrupted_port.InterruptWaiters();
	interrupted_receiver.join();
	second_interrupted_receiver.join();
	if (!interrupted || !second_interrupted || interrupted_port.IsClosed()) {
		std::cerr << "MACH_PORT_INTERRUPT=FAIL\n";
		return 1;
	}
	std::cout << "MACH_PORT_INTERRUPT=PASS\n";
	auto task = darling::windows_host::TaskPort::Current();
	if (task.ProcessId() != GetCurrentProcessId() || !task.IsAlive()) {
		std::cerr << "MACH_TASK_PORT=FAIL\n";
		return 1;
	}
	if (task.Wait(1)) {
		std::cerr << "MACH_TASK_WAIT_TIMEOUT=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TASK_WAIT_TIMEOUT=PASS\n";
	DWORD task_exit_code = 0;
	if (!task.ExitCode(task_exit_code) || task_exit_code != STILL_ACTIVE) {
		std::cerr << "MACH_TASK_EXIT_CODE=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TASK_EXIT_CODE=PASS\n";
	auto opened_task = darling::windows_host::TaskPort::Open(GetCurrentProcessId());
	auto moved_task = std::move(opened_task);
	if (moved_task.ProcessId() != GetCurrentProcessId() || !moved_task.IsAlive()) {
		std::cerr << "MACH_TASK_PORT_OPEN=FAIL\n";
		return 1;
	}
	bool invalid_task_rejected = false;
	try {
		auto invalid_task = darling::windows_host::TaskPort::Open(0xffffffffU);
		(void)invalid_task;
	} catch (const std::system_error&) {
		invalid_task_rejected = true;
	}
	if (!invalid_task_rejected) {
		std::cerr << "MACH_TASK_PORT_INVALID=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TASK_PORT_INVALID=PASS\n";
	std::cout << "MACH_TASK_PORT=PASS\n";
	std::cout << "MACH_TASK_PORT_OPEN=PASS\n";
	std::uint32_t invalid_memory = 0;
	if (task.Read(nullptr, &invalid_memory, sizeof(invalid_memory)) ||
		task.Write(nullptr, &invalid_memory, sizeof(invalid_memory))) {
		std::cerr << "MACH_TASK_VM_INVALID=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TASK_VM_INVALID=PASS\n";
	auto* task_memory = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, sizeof(std::uint32_t),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	const std::uint32_t replacement = 0x87654321U;
	std::uint32_t observed = 0;
	if (task_memory == nullptr) {
		std::cerr << "MACH_TASK_VM=FAIL\n";
		return 1;
	}
	*task_memory = 0x12345678U;
	if (!task.Read(task_memory, &observed, sizeof(observed)) || observed != 0x12345678U ||
		!task.Write(task_memory, &replacement, sizeof(replacement)) ||
		*task_memory != replacement) {
		VirtualFree(task_memory, 0, MEM_RELEASE);
		std::cerr << "MACH_TASK_VM=FAIL\n";
		return 1;
	}
	if (!task.Protect(task_memory, sizeof(*task_memory),
		darling::windows_host::TaskPort::MemoryProtection::ReadOnly) ||
		task.Write(task_memory, &replacement, sizeof(replacement)) ||
		!task.Protect(task_memory, sizeof(*task_memory),
			darling::windows_host::TaskPort::MemoryProtection::ReadWrite)) {
		VirtualFree(task_memory, 0, MEM_RELEASE);
		std::cerr << "MACH_TASK_VM_PROTECT=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TASK_VM=PASS\n";
	std::cout << "MACH_TASK_VM_PROTECT=PASS\n";
	MEMORY_BASIC_INFORMATION region{};
	if (!task.Query(task_memory, region) || region.State != MEM_COMMIT ||
		reinterpret_cast<std::uintptr_t>(task_memory) < reinterpret_cast<std::uintptr_t>(region.BaseAddress) ||
		reinterpret_cast<std::uintptr_t>(task_memory) - reinterpret_cast<std::uintptr_t>(region.BaseAddress) >= region.RegionSize) {
		std::cerr << "MACH_TASK_VM_QUERY=FAIL\n";
		VirtualFree(task_memory, 0, MEM_RELEASE);
		return 1;
	}
	std::cout << "MACH_TASK_VM_QUERY=PASS\n";
	VirtualFree(task_memory, 0, MEM_RELEASE);
	auto thread_port = darling::windows_host::ThreadPort::Current();
	if (thread_port.ThreadId() != GetCurrentThreadId() ||
		thread_port.ProcessId() != GetCurrentProcessId() || !thread_port.IsAlive()) {
		std::cerr << "MACH_THREAD_PORT=FAIL\n";
		return 1;
	}
	auto opened_thread = darling::windows_host::ThreadPort::Open(GetCurrentThreadId());
	auto moved_thread = std::move(opened_thread);
	if (moved_thread.ThreadId() != GetCurrentThreadId() || !moved_thread.IsAlive()) {
		std::cerr << "MACH_THREAD_PORT_OPEN=FAIL\n";
		return 1;
	}
	DWORD thread_exit_code = 0;
	if (!moved_thread.ExitCode(thread_exit_code) || thread_exit_code != STILL_ACTIVE) {
		std::cerr << "MACH_THREAD_EXIT_CODE=FAIL\n";
		return 1;
	}
	if (moved_thread.Wait(1)) {
		std::cerr << "MACH_THREAD_WAIT_TIMEOUT=FAIL\n";
		return 1;
	}
	std::cout << "MACH_THREAD_PORT=PASS\n";
	std::cout << "MACH_THREAD_TASK_LINK=PASS\n";
	std::cout << "MACH_THREAD_PORT_OPEN=PASS\n";
	std::cout << "MACH_THREAD_EXIT_CODE=PASS\n";
	std::cout << "MACH_THREAD_WAIT_TIMEOUT=PASS\n";
	std::atomic<bool> worker_run{true};
	std::thread worker([&] {
		while (worker_run.load(std::memory_order_acquire))
			std::this_thread::yield();
	});
	const auto worker_id = GetThreadId(static_cast<HANDLE>(worker.native_handle()));
	auto worker_port = darling::windows_host::ThreadPort::Open(worker_id);
	if (worker_id == 0 || !worker_port.Suspend() || !worker_port.Resume()) {
		worker_run.store(false, std::memory_order_release);
		worker.join();
		std::cerr << "MACH_THREAD_SUSPEND_RESUME=FAIL\n";
		return 1;
	}
	worker_run.store(false, std::memory_order_release);
	worker.join();
	if (!worker_port.Wait(1000)) {
		std::cerr << "MACH_THREAD_WAIT=FAIL\n";
		return 1;
	}
	std::cout << "MACH_THREAD_SUSPEND_RESUME=PASS\n";
	std::cout << "MACH_THREAD_WAIT=PASS\n";
	const int thread_priority = thread_port.Priority();
	if (thread_priority == THREAD_PRIORITY_ERROR_RETURN ||
		!thread_port.SetPriority(thread_priority) || thread_port.Priority() != thread_priority) {
		std::cerr << "MACH_THREAD_PRIORITY=FAIL\n";
		return 1;
	}
	std::cout << "MACH_THREAD_PRIORITY=PASS\n";
	wchar_t temp_path[MAX_PATH]{};
	const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
	if (temp_length == 0 || temp_length >= MAX_PATH) {
		std::cerr << "TEMP_PATH=FAIL\n";
		return 2;
	}

	const auto root = std::filesystem::path(temp_path) /
		(L"darling-stage1-" + std::to_wstring(GetCurrentProcessId()));
	const auto prefix = darling::windows_host::Prefix::Create(root.wstring());
	std::cout << "PREFIX_CREATE=PASS\n";

	auto pipe = darling::windows_host::NamedPipeRpcServer::Create(
		L"darling-stage1-" + std::to_wstring(GetCurrentProcessId()));
	std::string received;
	std::thread server([&] {
		pipe.WaitForClient();
		received = pipe.Read();
		pipe.Write(received == "PING" ? "PONG" : "BAD_REQUEST");
	});
	Sleep(100);

	auto client = darling::windows_host::NamedPipeRpcClient::Connect(pipe.Name());
	client.Write("PING");
	const auto response = client.Read();
	server.join();

	std::cout << "RPC_REQUEST=" << received << "\n";
	std::cout << "RPC_RESPONSE=" << response << "\n";
	if (received != "PING" || response != "PONG") {
		return 3;
	}

	auto envelope_pipe = darling::windows_host::NamedPipeRpcServer::Create(
		L"darling-envelope-" + std::to_wstring(GetCurrentProcessId()));
	std::thread envelope_server([&] {
		envelope_pipe.WaitForClient();
		const auto bytes = envelope_pipe.Read();
		const auto request = darling::windows_host::DecodeMachIpcEnvelope(
			std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
		const auto response_bytes = darling::windows_host::EncodeMachIpcEnvelope(request);
		envelope_pipe.Write(std::string(response_bytes.begin(), response_bytes.end()));
	});
	Sleep(100);
	auto envelope_client = darling::windows_host::NamedPipeRpcClient::Connect(envelope_pipe.Name());
	darling::windows_host::MachIpcEnvelope envelope_request;
	envelope_request.operation = darling::windows_host::MachIpcOperation::Send;
	envelope_request.request_id = 0x1234;
	envelope_request.port_token = 0x55;
	envelope_request.payload = {0x4d, 0x41, 0x43, 0x48};
	const auto envelope_response = darling::windows_host::SendMachIpcEnvelope(
		envelope_client, envelope_request);
	envelope_server.join();
	if (envelope_response.operation != envelope_request.operation ||
		envelope_response.request_id != envelope_request.request_id ||
		envelope_response.payload != envelope_request.payload) {
		std::cerr << "MACH_IPC_RPC=FAIL\n";
		return 5;
	}
	std::cout << "MACH_IPC_RPC=PASS\n";

	std::error_code cleanup_error;
	std::filesystem::remove_all(prefix.Root(), cleanup_error);
	std::cout << "PREFIX_CLEANUP=" << (cleanup_error ? "FAIL" : "PASS") << "\n";
	return cleanup_error ? 4 : 0;
}
