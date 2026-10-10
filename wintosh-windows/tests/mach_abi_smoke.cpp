/* Stage 1 proof for the minimal Mach C ABI bridge. GPL-3.0-only. */
#include "darling_windows_mach.h"
#include "darling_windows_stdio.h"

#include <iostream>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

int main()
{
#if !defined(_WIN64)
	std::cout << "MACH_ABI_X64_THREAD_STATE=UNSUPPORTED_WIN32\n";
	return 0;
#else
	const auto task = darling_windows_mach_task_self();
	const auto thread = darling_windows_mach_thread_self();
	darling_thread_precedence_policy_info precedence{};
	darling_thread_time_constraint_policy_info time_constraint{};
	if (darling_windows_thread_policy_set(thread, darling_thread_precedence_policy,
		&precedence, 1) != 0 || darling_windows_thread_policy_set(thread,
		darling_thread_time_constraint_policy, &time_constraint, 4) != darling_kern_not_supported)
		return 1;
	darling_thread_precedence_policy_info precedence_read{};
	std::uint32_t precedence_count = 1;
	bool precedence_default = true;
	if (darling_windows_thread_policy_get(thread, darling_thread_precedence_policy,
		&precedence_read, &precedence_count, &precedence_default) != 0 ||
		precedence_count != 1 || precedence_default)
		return 1;
	if (darling_windows_thread_suspend(thread) == 0 || darling_windows_thread_resume(thread) == 0)
		return 1;
	if (darling_windows_thread_abort(thread) == 0)
		return 1;
	darling_x86_thread_state64 state{};
	std::uint32_t state_count = darling_x86_thread_state64_count;
	if (darling_windows_thread_get_state(thread, darling_x86_thread_state64_flavor,
		&state, &state_count) != 0 || state_count != darling_x86_thread_state64_count ||
		state.rip == 0 || state.rsp == 0)
		return 1;
	if (darling_windows_thread_set_state(thread, darling_x86_thread_state64_flavor,
		&state, darling_x86_thread_state64_count) == 0)
		return 1;
	darling_x86_float_state64 float_state{};
	std::uint32_t float_count = darling_x86_float_state64_count;
	if (darling_windows_thread_get_state(thread, darling_x86_float_state64_flavor,
		&float_state, &float_count) != 0 || float_count != darling_x86_float_state64_count)
		return 1;
	std::uint32_t original_mxcsr = 0;
	std::memcpy(&original_mxcsr, reinterpret_cast<const std::uint8_t*>(&float_state) + 32,
		sizeof(original_mxcsr));
	const auto changed_mxcsr = original_mxcsr ^ 0x8000u;
	std::memcpy(reinterpret_cast<std::uint8_t*>(&float_state) + 32, &changed_mxcsr,
		sizeof(changed_mxcsr));
	if (darling_windows_thread_set_state(thread, darling_x86_float_state64_flavor,
		&float_state, darling_x86_float_state64_count) != 0)
		return 1;
	darling_x86_float_state64 changed_float_state{};
	float_count = darling_x86_float_state64_count;
	std::uint32_t changed_readback = 0;
	if (darling_windows_thread_get_state(thread, darling_x86_float_state64_flavor,
		&changed_float_state, &float_count) != 0 ||
		std::memcpy(&changed_readback, reinterpret_cast<const std::uint8_t*>(&changed_float_state) + 32,
			sizeof(changed_readback)) == nullptr || changed_readback != changed_mxcsr)
		return 1;
	std::memcpy(reinterpret_cast<std::uint8_t*>(&float_state) + 32, &original_mxcsr,
		sizeof(original_mxcsr));
	if (darling_windows_thread_set_state(thread, darling_x86_float_state64_flavor,
		&float_state, darling_x86_float_state64_count) != 0)
		return 1;
	std::atomic<bool> worker_ready = false;
	std::atomic<bool> worker_stop = false;
	std::atomic<bool> worker_apc_woken = false;
	std::thread worker([&] {
		worker_ready.store(true, std::memory_order_release);
		while (!worker_stop.load(std::memory_order_acquire)) {
			if (SleepEx(1000, TRUE) == WAIT_IO_COMPLETION)
				worker_apc_woken.store(true, std::memory_order_release);
		}
	});
	while (!worker_ready.load(std::memory_order_acquire)) std::this_thread::yield();
	const auto worker_thread = static_cast<darling_mach_port_name_t>(GetThreadId(worker.native_handle()));
	if (darling_windows_thread_resume(worker_thread) == 0 ||
		darling_windows_thread_suspend(worker_thread) != 0 ||
		darling_windows_thread_resume(worker_thread) != 0 ||
		darling_windows_thread_resume(worker_thread) == 0) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	darling_x86_float_state64 worker_float_state{};
	std::uint32_t worker_float_count = darling_x86_float_state64_count;
	const auto worker_get = worker_thread == 0 ? 4 : darling_windows_thread_get_state(worker_thread,
		darling_x86_float_state64_flavor, &worker_float_state, &worker_float_count);
	if (worker_get != 0 || worker_float_count != darling_x86_float_state64_count) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	darling_x86_avx_state64 worker_avx_state{};
	std::uint32_t worker_avx_count = darling_x86_avx_state64_count;
	const auto worker_avx = darling_windows_thread_get_state(worker_thread,
		darling_x86_avx_state64_flavor, &worker_avx_state, &worker_avx_count);
	if (worker_avx != 0 && worker_avx != darling_kern_not_supported) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	if (worker_avx == 0 && worker_avx_count != darling_x86_avx_state64_count) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	darling_x86_avx512_state64 worker_avx512_state{};
	std::uint32_t worker_avx512_count = darling_x86_avx512_state64_count;
	const auto worker_avx512 = darling_windows_thread_get_state(worker_thread,
		darling_x86_avx512_state64_flavor, &worker_avx512_state, &worker_avx512_count);
	if (worker_avx512 != 0 && worker_avx512 != darling_kern_not_supported) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	if (worker_avx512 == 0 && worker_avx512_count != darling_x86_avx512_state64_count) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	if (darling_windows_thread_abort(worker_thread) != 0) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	for (int attempt = 0; attempt != 100 && !worker_apc_woken.load(std::memory_order_acquire); ++attempt)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if (!worker_apc_woken.load(std::memory_order_acquire)) {
		worker_stop.store(true, std::memory_order_release);
		worker.join();
		return 1;
	}
	worker_stop.store(true, std::memory_order_release);
	worker.join();
	HANDLE pipe_read = nullptr;
	HANDLE pipe_write = nullptr;
	SECURITY_ATTRIBUTES pipe_attributes{};
	pipe_attributes.nLength = sizeof(pipe_attributes);
	pipe_attributes.bInheritHandle = FALSE;
	if (!CreatePipe(&pipe_read, &pipe_write, &pipe_attributes, 0)) return 1;
	std::atomic<bool> io_started = false;
	std::atomic<bool> io_finished = false;
	std::atomic<DWORD> io_error = ERROR_SUCCESS;
	std::thread io_worker([&] {
		io_started.store(true, std::memory_order_release);
		char byte = 0;
		DWORD read = 0;
		const BOOL result = ReadFile(pipe_read, &byte, 1, &read, nullptr);
		io_error.store(result ? ERROR_SUCCESS : GetLastError(), std::memory_order_release);
		io_finished.store(true, std::memory_order_release);
	});
	while (!io_started.load(std::memory_order_acquire)) std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	const auto io_thread = static_cast<darling_mach_port_name_t>(GetThreadId(io_worker.native_handle()));
	const bool io_abort_requested = darling_windows_thread_abort(io_thread) == 0;
	for (int attempt = 0; attempt != 100 && !io_finished.load(std::memory_order_acquire); ++attempt)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	const bool io_abort_ok = io_abort_requested && io_finished.load(std::memory_order_acquire) &&
		io_error.load(std::memory_order_acquire) == ERROR_OPERATION_ABORTED;
	if (!io_abort_ok) {
		CancelSynchronousIo(io_worker.native_handle());
		CloseHandle(pipe_write);
		io_worker.join();
		CloseHandle(pipe_read);
		return 1;
	}
	CloseHandle(pipe_write);
	io_worker.join();
	CloseHandle(pipe_read);
	darling_x86_exception_state64 exception_state{};
	std::uint32_t exception_count = darling_x86_exception_state64_count;
	if (darling_windows_thread_get_state(thread, darling_x86_exception_state64_flavor,
		&exception_state, &exception_count) != 0 ||
		exception_count != darling_x86_exception_state64_count)
		return 1;
	darling_x86_debug_state64 debug_state{};
	std::uint32_t debug_count = darling_x86_debug_state64_count;
	if (darling_windows_thread_get_state(thread, darling_x86_debug_state64_flavor,
		&debug_state, &debug_count) != 0 || debug_count != darling_x86_debug_state64_count ||
		darling_windows_thread_set_state(thread, darling_x86_debug_state64_flavor,
		&debug_state, darling_x86_debug_state64_count) == 0)
		return 1;
	darling_thread_basic_info basic{};
	std::uint32_t basic_count = darling_thread_basic_info_count;
	if (darling_windows_thread_info(thread, darling_thread_basic_info_flavor, &basic,
		&basic_count) != 0 || basic_count != darling_thread_basic_info_count ||
		basic.user_time.seconds < 0 || basic.system_time.seconds < 0)
		return 1;
	darling_thread_identifier_info identifier{};
	std::uint32_t identifier_count = darling_thread_identifier_info_count;
	if (darling_windows_thread_info(thread, darling_thread_identifier_info_flavor,
		&identifier, &identifier_count) != 0 || identifier.thread_id == 0 ||
		identifier_count != darling_thread_identifier_info_count)
		return 1;
	darling_thread_extended_info extended{};
	std::uint32_t extended_count = darling_thread_extended_info_count;
	if (darling_windows_pthread_setname_np("mach-info") != 0)
		return 1;
	if (darling_windows_thread_info(thread, darling_thread_extended_info_flavor,
		&extended, &extended_count) != 0 || extended_count != darling_thread_extended_info_count ||
		extended.max_priority != THREAD_PRIORITY_HIGHEST ||
		std::strcmp(extended.name, "mach-info") != 0)
		return 1;
	darling_thread_sched_timeshare_info timeshare{};
	std::uint32_t timeshare_count = darling_thread_sched_timeshare_info_count;
	if (darling_windows_thread_info(thread, darling_thread_sched_timeshare_info_flavor,
		&timeshare, &timeshare_count) != 0 || timeshare_count != darling_thread_sched_timeshare_info_count ||
		timeshare.depressed != 0)
		return 1;
	darling_thread_sched_rr_info rr{};
	std::uint32_t rr_count = darling_thread_sched_rr_info_count;
	if (darling_windows_thread_info(thread, darling_thread_sched_rr_info_flavor, &rr,
		&rr_count) != 0 || rr_count != darling_thread_sched_rr_info_count)
		return 1;
	darling_thread_sched_fifo_info fifo{};
	std::uint32_t fifo_count = darling_thread_sched_fifo_info_count;
	if (darling_windows_thread_info(thread, darling_thread_sched_fifo_info_flavor, &fifo,
		&fifo_count) != 0 || fifo_count != darling_thread_sched_fifo_info_count)
		return 1;
	const auto host = darling_windows_mach_host_self();
	darling_exception_mask_t exception_mask = 1, returned_mask = 0;
	darling_mach_port_name_t returned_handler = 0;
	darling_exception_behavior_t returned_behavior = 0;
	darling_exception_flavor_t returned_flavor = 0;
	std::uint32_t exception_ports_count = 1;
	if (darling_windows_thread_set_exception_ports(thread, exception_mask, host, 2, 4) != 0 ||
		darling_windows_thread_get_exception_ports(thread, exception_mask, &returned_mask,
		&exception_ports_count, &returned_handler, &returned_behavior, &returned_flavor) != 0 ||
		exception_ports_count != 1 || returned_mask != exception_mask || returned_handler != host ||
		returned_behavior != 2 || returned_flavor != 4)
		return 1;
	std::uint32_t swapped_count = 1;
	returned_mask = 0; returned_handler = 0; returned_behavior = 0; returned_flavor = 0;
	if (darling_windows_thread_swap_exception_ports(thread, exception_mask, 0x1234, 3, 5,
		&returned_mask, &swapped_count, &returned_handler, &returned_behavior, &returned_flavor) != 0 ||
		swapped_count != 1 || returned_handler != host || returned_behavior != 2 || returned_flavor != 4)
		return 1;
	darling_exception_handler_info handler_info{};
	std::uint32_t handler_info_count = 1;
	if (darling_windows_thread_get_exception_ports_info(0x1234, exception_mask,
		&returned_mask, &handler_info_count, &handler_info, &returned_behavior,
		&returned_flavor) != 0 || handler_info_count != 1 ||
		handler_info.port_object != 0x1234 || handler_info.receiver_object != 0)
		return 1;
	if (darling_windows_set_exception_dispatch_enabled(true) != 0 ||
		darling_windows_set_exception_dispatch_enabled(true) != 0 ||
		darling_windows_set_exception_dispatch_enabled(false) != 0)
		return 1;
	darling_mach_port_name_t dispatch_port = 0;
	const auto dispatch_alloc = darling_windows_mach_port_allocate(task, &dispatch_port);
	const auto dispatch_mask = static_cast<darling_exception_mask_t>(1u << 1);
	const auto dispatch_register = dispatch_alloc == 0 ? darling_windows_thread_set_exception_ports(thread, dispatch_mask, dispatch_port, 1, 6) : 4;
	const auto dispatch_send = dispatch_register == 0 ? darling_windows_dispatch_mach_exception(1, 0xfeed, 0xbeef) : 4;
	if (dispatch_alloc != 0 || dispatch_register != 0 || dispatch_send != 0)
		return 1;
	darling_mach_exception_message dispatch_message{};
	std::uint32_t dispatch_size = 0;
	if (darling_windows_mach_port_receive(dispatch_port, &dispatch_message,
		sizeof(dispatch_message), &dispatch_size, 100) != 0 ||
		dispatch_size != sizeof(dispatch_message) || dispatch_message.exception_type != 1 ||
		dispatch_message.code0 != 0xfeed || dispatch_message.code1 != 0xbeef ||
		dispatch_message.thread != thread)
		return 1;
	darling_windows_mach_port_destroy(task, dispatch_port);
	if (darling_windows_dispatch_mach_exception(1, 0xfeed, 0xbeef) == 0)
		return 1;
	std::uint32_t page_size = 0;
	darling_mach_vm_address_t vm_address = 0;
	darling_mach_vm_address_t vm_copy_address = 0;
	std::uint32_t vm_value = 0x12345678;
	std::uint32_t vm_copy = 0;
	darling_mach_vm_size_t vm_read_size = 0;
	darling_mach_port_name_t allocated = 0;
	darling_mach_port_name_t stale_member = 0;
	darling_mach_port_name_t port_set = 0;
	std::uint32_t refs = 0;
	std::uint32_t port_type = darling_mach_port_type_none;
	darling_mach_msg_header message{};
	char payload[] = "mach-ipc";
	char received[32]{};
	char too_small[1]{};
	std::uint32_t received_size = 0;
	char timeout_buffer[8]{};
	std::uint32_t timeout_size = 0;
	const auto local_queue_limit_ok = [&] {
		darling_mach_port_name_t queue_port = 0;
		if (darling_windows_mach_port_allocate(task, &queue_port) != 0)
			return false;
		char queue_payload = 'q';
		if (darling_windows_mach_port_send(queue_port, &queue_payload,
			4 * 1024 * 1024 + 1) != 0x10004003) {
			darling_windows_mach_port_destroy(task, queue_port);
			return false;
		}
		for (std::size_t i = 0; i != 1024; ++i) {
			if (darling_windows_mach_port_send(queue_port, &queue_payload, 1) != 0) {
				darling_windows_mach_port_destroy(task, queue_port);
				return false;
			}
		}
		const auto overflow = darling_windows_mach_port_send(queue_port, &queue_payload, 1);
		const auto destroyed = darling_windows_mach_port_destroy(task, queue_port);
		return overflow == darling_mach_send_queue_full && destroyed == 0;
	}();
	const auto set_wakeup_ok = [&] {
		darling_mach_port_name_t member = 0;
		darling_mach_port_name_t set = 0;
		if (darling_windows_mach_port_allocate(task, &member) != 0 ||
			darling_windows_mach_port_set_allocate(task, &set) != 0 ||
			darling_windows_mach_port_move_member(task, member, set) != 0)
			return false;
		char sent = 'w';
		char received_wakeup = 0;
		std::uint32_t received_wakeup_size = 0;
		darling_kern_return_t receive_result = 4;
		std::thread receiver([&] {
			receive_result = darling_windows_mach_port_set_receive(
				set, &received_wakeup, 1, &received_wakeup_size, 500);
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		const auto send_result = darling_windows_mach_port_send(member, &sent, 1);
		receiver.join();
		const auto member_destroyed = darling_windows_mach_port_destroy(task, member);
		const auto set_destroyed = darling_windows_mach_port_set_destroy(task, set);
		return send_result == 0 && receive_result == 0 && received_wakeup == sent &&
			received_wakeup_size == 1 && member_destroyed == 0 && set_destroyed == 0;
	}();
	const auto last_deallocate_ok = [&] {
		darling_mach_port_name_t port = 0;
		if (darling_windows_mach_port_allocate(task, &port) != 0 ||
			darling_windows_mach_port_insert_right(task, port, thread,
				darling_mach_make_send) != 0)
			return false;
		char value = 'd';
		const auto first = darling_windows_mach_port_deallocate(task, port);
		const auto second = darling_windows_mach_port_deallocate(task, port);
		const auto stale_send = darling_windows_mach_port_send(port, &value, 1);
		return first == 0 && second == 0 && stale_send == 3;
	}();
	const auto local_move_right_ok = [&] {
		darling_mach_port_name_t source = 0;
		darling_mach_port_name_t destination = 0;
		std::uint32_t source_send_refs = 0;
		std::uint32_t destination_send_refs = 0;
		if (darling_windows_mach_port_allocate(task, &source) != 0 ||
			darling_windows_mach_port_allocate(task, &destination) != 0 ||
			darling_windows_mach_port_insert_right(task, source, thread,
				darling_mach_make_send) != 0 ||
			darling_windows_mach_port_insert_right(task, destination, source,
				darling_mach_move_send) != 0)
			return false;
		const auto source_result = darling_windows_mach_port_get_refs(
			task, source, darling_mach_port_type_send, &source_send_refs);
		const auto destination_result = darling_windows_mach_port_get_refs(
			task, destination, darling_mach_port_type_send, &destination_send_refs);
		const auto cleanup_source = darling_windows_mach_port_destroy(task, source);
		const auto cleanup_destination = darling_windows_mach_port_destroy(task, destination);
		const bool send_move_ok = source_result == 0 && destination_result == 0 &&
			source_send_refs == 0 && destination_send_refs == 1 &&
			cleanup_source == 0 && cleanup_destination == 0;
		darling_mach_port_name_t receive_source = 0;
		darling_mach_port_name_t receive_destination = 0;
		if (darling_windows_mach_port_allocate(task, &receive_source) != 0 ||
			darling_windows_mach_port_allocate(task, &receive_destination) != 0 ||
			darling_windows_mach_port_insert_right(task, receive_destination, receive_source,
				darling_mach_move_receive) != 0)
			return false;
		const auto stale_source = darling_windows_mach_port_get_refs(
			task, receive_source, darling_mach_port_type_receive, &source_send_refs);
		const auto receive_destination_refs = darling_windows_mach_port_get_refs(
			task, receive_destination, darling_mach_port_type_receive, &destination_send_refs);
		const auto cleanup_receive_destination = darling_windows_mach_port_destroy(task, receive_destination);
		return send_move_ok &&
			stale_source == 3 && receive_destination_refs == 0 && destination_send_refs == 2 &&
			cleanup_receive_destination == 0;
	}();
	const auto foreign_task_rejected = [&] {
		darling_mach_port_name_t ignored = 0;
		return darling_windows_mach_port_allocate(task + 1, &ignored) == 4 &&
			darling_windows_mach_port_deallocate(task + 1, allocated) == 4 &&
			darling_windows_mach_port_set_allocate(task + 1, &ignored) == 4;
	}();
	const auto mod_refs_lifetime_ok = [&] {
		darling_mach_port_name_t port = 0;
		if (darling_windows_mach_port_allocate(task, &port) != 0)
			return false;
		std::uint32_t refs = 0;
		if (darling_windows_mach_port_mod_refs(task, port,
			darling_mach_port_type_receive, -1) != 0)
			return false;
		const auto stale = darling_windows_mach_port_get_refs(task, port,
			darling_mach_port_type_receive, &refs);
		const auto send_after = darling_windows_mach_port_send(port, "x", 1);
		return stale == 3 && send_after == 3;
	}();
	const auto extract_right_ok = [&] {
		darling_mach_port_name_t source = 0;
		if (darling_windows_mach_port_allocate(task, &source) != 0 ||
			darling_windows_mach_port_insert_right(task, source, thread,
				darling_mach_make_send) != 0)
			return false;
		darling_mach_port_name_t extracted = 0;
		std::uint32_t extracted_disposition = 0;
		std::uint32_t refs = 0;
		const auto copy_result = darling_windows_mach_port_extract_right(
			task, source, darling_mach_copy_send, &extracted, &extracted_disposition);
		const auto copy_disposition = extracted_disposition;
		const auto copy_refs_result = darling_windows_mach_port_get_refs(
			task, source, darling_mach_port_type_send, &refs);
		const auto copy_ref_count = refs;
		const auto move_result = darling_windows_mach_port_extract_right(
			task, source, darling_mach_move_send, &extracted, &extracted_disposition);
		const auto move_disposition = extracted_disposition;
		const auto move_refs_result = darling_windows_mach_port_get_refs(
			task, source, darling_mach_port_type_send, &refs);
		const auto move_ref_count = refs;
		const auto final_move_result = darling_windows_mach_port_extract_right(
			task, source, darling_mach_move_send, &extracted, &extracted_disposition);
		const auto stale_result = darling_windows_mach_port_type(task, source, &refs);
		const auto cleanup = darling_windows_mach_port_destroy(task, source);
		const auto stale_extract = darling_windows_mach_port_extract_right(task, source,
			darling_mach_make_send, &extracted, &extracted_disposition);
		const bool result = copy_result == 0 && extracted == source &&
			copy_disposition == darling_mach_copy_send && move_disposition == darling_mach_move_send &&
			copy_refs_result == 0 &&
			copy_ref_count == 2 && move_result == 0 && move_refs_result == 0 &&
			move_ref_count == 1 && final_move_result == 0 && stale_result == 0 &&
			(refs & darling_mach_port_type_receive) != 0 && cleanup == 0 &&
			stale_extract == 3;
		return result;
	}();
	bool ok = task != 0 && thread != 0 && host != 0 &&
		local_queue_limit_ok &&
		set_wakeup_ok &&
		last_deallocate_ok &&
		local_move_right_ok &&
		foreign_task_rejected &&
		mod_refs_lifetime_ok &&
		extract_right_ok &&
		darling_windows_host_symbol("mach_task_self") != 0 &&
		darling_windows_host_symbol("mach_thread_self") != 0 &&
		darling_windows_host_symbol("mach_host_self") != 0 &&
		darling_windows_host_symbol("host_page_size") != 0 &&
		darling_windows_host_symbol("mach_vm_allocate") != 0 &&
		darling_windows_host_symbol("mach_vm_deallocate") != 0 &&
		darling_windows_host_symbol("mach_vm_protect") != 0 &&
		darling_windows_host_symbol("mach_vm_read_overwrite") != 0 &&
		darling_windows_host_symbol("mach_vm_read") != 0 &&
		darling_windows_host_symbol("mach_vm_write") != 0 &&
		darling_windows_host_symbol("mach_vm_copy") != 0 &&
		darling_windows_host_symbol("mach_vm_region") != 0 &&
		darling_windows_host_symbol("mach_vm_region_recurse") != 0 &&
		darling_windows_host_page_size(host, &page_size) == 0 && page_size >= 4096 &&
		darling_windows_mach_vm_allocate(task, &vm_address, page_size, 0) == 0 &&
		vm_address != 0 &&
		darling_windows_mach_vm_allocate(task, &vm_copy_address, page_size, 0) == 0 &&
		darling_windows_mach_vm_write(task, vm_address, &vm_value, sizeof(vm_value)) == 0 &&
		([&] {
			darling_mach_vm_address_t read_data = 0;
			darling_mach_vm_size_t read_size = 0;
			const auto result = darling_windows_mach_vm_read(task, vm_address,
				sizeof(vm_value), &read_data, &read_size);
			const auto value = read_data == 0 ? 0u : *reinterpret_cast<std::uint32_t*>(
				static_cast<std::uintptr_t>(read_data));
			const auto cleanup = read_data == 0 ? 4 : darling_windows_mach_vm_deallocate(
				task, read_data, read_size);
			return result == 0 && read_data != 0 && read_size == sizeof(vm_value) &&
				value == vm_value && cleanup == 0;
		})() &&
		darling_windows_mach_vm_read_overwrite(task, vm_address, sizeof(vm_value),
		vm_copy_address, &vm_read_size) == 0 && vm_read_size == sizeof(vm_value) &&
		std::memcpy(&vm_copy, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vm_copy_address)),
		sizeof(vm_copy)) != nullptr && vm_copy == vm_value &&
		darling_windows_mach_vm_copy(task, vm_address, sizeof(vm_value), vm_copy_address) == 0 &&
		std::memcpy(&vm_copy, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vm_copy_address)),
		sizeof(vm_copy)) != nullptr && vm_copy == vm_value &&
		([&] {
			darling_mach_vm_address_t region_address = vm_address;
			darling_mach_vm_size_t region_size = 0;
			darling_mach_vm_region_basic_info region_info{};
			std::uint32_t region_count = darling_vm_region_basic_info_count;
			std::uint32_t depth = 0;
			return darling_windows_mach_vm_region(task, &region_address, &region_size,
				darling_vm_region_basic_info, &region_info, &region_count) == 0 &&
				region_address <= vm_address && region_size != 0 &&
				(region_info.protection & darling_vm_prot_read) != 0 &&
				darling_windows_mach_vm_region_recurse(task, &region_address, &region_size,
					&depth, &region_info, &region_count) == 0 && depth == 0;
		})() &&
		darling_windows_mach_vm_protect(task, vm_address, page_size, false,
		 darling_vm_prot_read) == 0 &&
		darling_windows_mach_vm_deallocate(task, vm_copy_address, page_size) == 0 &&
		darling_windows_mach_vm_deallocate(task, vm_address, page_size) == 0 &&
		darling_windows_host_symbol("mach_port_allocate") != 0 &&
		darling_windows_host_symbol("mach_port_get_refs") != 0 &&
		darling_windows_host_symbol("mach_port_extract_right") != 0 &&
		darling_windows_host_symbol("mach_port_type") != 0 &&
		darling_windows_host_symbol("mach_port_set_allocate") != 0 &&
		darling_windows_host_symbol("mach_port_move_member") != 0 &&
		darling_windows_host_symbol("mach_port_remove_member") != 0 &&
		darling_windows_host_symbol("mach_port_set_destroy") != 0 &&
		darling_windows_host_symbol("mach_port_set_receive") != 0 &&
		darling_windows_host_symbol("mach_port_request_notification") != 0 &&
		darling_windows_host_symbol("_mach_port_request_notification") != 0 &&
		darling_windows_host_symbol("mach_msg") != 0 &&
		darling_windows_host_symbol("mach_port_destroy") != 0 &&
		 darling_windows_mach_port_allocate(task, &allocated) == 0 && allocated != 0 &&
		darling_windows_mach_port_insert_right(task, allocated, thread,
			darling_mach_make_send) == 0 &&
		darling_windows_mach_port_insert_right(task, allocated, thread,
			darling_mach_copy_send) == 0 &&
		darling_windows_mach_port_insert_right(task, allocated, thread, 0x7fff) == 4 &&
		darling_windows_mach_port_insert_right(task, allocated, 0xffffffffu,
			darling_mach_copy_send) == 3 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 1 &&
		darling_windows_mach_port_get_refs(task, allocated, 2, &refs) == 0 && refs == 2 &&
		darling_windows_mach_port_get_refs(task, allocated, 0x7fff, &refs) == 4 &&
		darling_windows_mach_port_mod_refs(task, allocated, 1, 1) == 0 &&
		darling_windows_mach_port_mod_refs(task, allocated, 0x7fff, 1) == 4 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 2 &&
		darling_windows_mach_port_mod_refs(task, allocated, 1,
		(std::numeric_limits<std::int32_t>::min)()) == 4 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 2 &&
		darling_windows_mach_port_deallocate(task, allocated) == 0 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 2 &&
		darling_windows_mach_port_type(task, allocated, &port_type) == 0 &&
		port_type == (darling_mach_port_type_receive | darling_mach_port_type_send) &&
		darling_windows_mach_port_set_allocate(task, &port_set) == 0 && port_set != 0 &&
		darling_windows_mach_port_move_member(task, allocated, port_set) == 0 &&
		darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_receive(allocated, received, sizeof(received),
		&received_size, 100) == 0 && received_size == sizeof(payload) &&
		std::memcmp(received, payload, sizeof(payload)) == 0 &&
		(darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 0) &&
		darling_windows_mach_port_receive(allocated, too_small, sizeof(too_small),
		&timeout_size, 100) == 0x10004003 &&
		darling_windows_mach_port_receive(allocated, received, sizeof(received),
		&received_size, 100) == 0 && received_size == sizeof(payload) &&
		std::memcmp(received, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_receive(allocated, timeout_buffer,
		sizeof(timeout_buffer), &timeout_size, 1) == 268 &&
		(darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 0) &&
		darling_windows_mach_port_set_receive(port_set, received, sizeof(received),
		&received_size, 100) == 0 && received_size == sizeof(payload) &&
		std::memcmp(received, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_allocate(task, &stale_member) == 0 &&
		darling_windows_mach_port_move_member(task, stale_member, port_set) == 0 &&
		darling_windows_mach_port_destroy(task, stale_member) == 0 &&
		darling_windows_mach_port_set_receive(port_set, received, sizeof(received),
		&received_size, 1) == 268 &&
		darling_windows_mach_port_remove_member(task, allocated, port_set) == 0 &&
		darling_windows_mach_port_remove_member(task, allocated, port_set) == 3 &&
		(message.msgh_remote_port = allocated,
		message.msgh_size = sizeof(message),
		darling_windows_mach_msg(&message, darling_mach_send_msg,
			sizeof(message), 0, 0, 0, 0) == 0) &&
		darling_windows_mach_msg(&message, darling_mach_receive_msg | darling_mach_receive_timeout,
		sizeof(message), sizeof(message), allocated, 100, 0) == 0 &&
		darling_windows_mach_msg(&message, 0x8000, 0, 0, 0, 0, 0) == 4 &&
		(message.msgh_remote_port = allocated,
		message.msgh_size = sizeof(message) - 1,
		darling_windows_mach_msg(&message, darling_mach_send_msg,
			sizeof(message), 0, 0, 0, 0) == 4) &&
		darling_windows_mach_port_deallocate(task, thread) == 0 &&
		darling_windows_mach_port_destroy(task, allocated) == 0 &&
		darling_windows_mach_port_set_destroy(task, port_set) == 0 &&
		darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 3 &&
		darling_windows_mach_port_type(task, allocated, &port_type) == 3 &&
		port_type == darling_mach_port_type_none &&
		darling_windows_mach_port_deallocate(0, thread) == 4;
	if (ok) {
		darling_mach_port_name_t blocked_port = 0;
		std::atomic<bool> receiver_started = false;
		std::atomic<darling_kern_return_t> receiver_result = 0;
		if (darling_windows_mach_port_allocate(task, &blocked_port) != 0) {
			ok = false;
		} else {
			std::thread blocked_receiver([&] {
				receiver_started.store(true, std::memory_order_release);
				char byte = 0;
				std::uint32_t blocked_size = 0;
				receiver_result.store(darling_windows_mach_port_receive(blocked_port,
					&byte, sizeof(byte), &blocked_size, 5000), std::memory_order_release);
			});
			while (!receiver_started.load(std::memory_order_acquire)) std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			const auto destroyed = darling_windows_mach_port_destroy(task, blocked_port);
			blocked_receiver.join();
			ok = destroyed == 0 && receiver_result.load(std::memory_order_acquire) == 3;
		}
	}
	if (ok) {
		darling_mach_port_name_t notification_target = 0;
		darling_mach_port_name_t notification_port = 0;
		darling_mach_port_name_t replacement_notification_port = 0;
		darling_mach_port_name_t previous_notification = 0xffffffffu;
		ok = darling_windows_mach_port_allocate(task, &notification_target) == 0 &&
			darling_windows_mach_port_allocate(task, &notification_port) == 0 &&
			darling_windows_mach_port_allocate(task, &replacement_notification_port) == 0 &&
			darling_windows_mach_port_request_notification(task + 1, notification_target,
			0x4e544659, notification_port, &previous_notification) == 4 &&
			darling_windows_mach_port_request_notification(task, notification_target,
			0x4e544659, 0xffffffffu, &previous_notification) == 4 &&
			darling_windows_mach_port_request_notification(task, notification_target,
			0x4e544659, notification_port, &previous_notification) == 0 &&
			previous_notification == 0 &&
			darling_windows_mach_port_request_notification(task, notification_target,
			0x4e544659, replacement_notification_port, &previous_notification) == 0 &&
			previous_notification == notification_port &&
			darling_windows_mach_port_request_notification(task, notification_target,
			0, 0, &previous_notification) == 0 &&
			previous_notification == replacement_notification_port &&
			darling_windows_mach_port_request_notification(task, notification_target,
			0x4e544659, replacement_notification_port, &previous_notification) == 0 &&
			previous_notification == 0 &&
			darling_windows_mach_port_destroy(task, notification_target) == 0;
		if (ok) {
			darling_mach_msg_header notification_message{};
			std::uint32_t notification_size = 0;
			ok = darling_windows_mach_port_receive(replacement_notification_port, &notification_message,
				sizeof(notification_message), &notification_size, 100) == 0 &&
				notification_size == sizeof(notification_message) &&
				notification_message.msgh_id == 0x4e544659 &&
				darling_windows_mach_port_receive(notification_port, &notification_message,
					sizeof(notification_message), &notification_size, 1) == 268;
		}
		if (!ok) return 1;
		darling_mach_port_name_t ns_target = 0, ns_notify = 0, ns_previous = 0xffffffffu;
		const auto ns_alloc_target = darling_windows_mach_port_allocate(task, &ns_target);
		const auto ns_alloc_notify = darling_windows_mach_port_allocate(task, &ns_notify);
		const auto ns_insert = darling_windows_mach_port_insert_right(task, ns_target, ns_target,
			darling_mach_make_send);
		const auto ns_request = darling_windows_mach_port_request_notification(task, ns_target,
			0x4a, ns_notify, &ns_previous);
		const auto ns_mod = darling_windows_mach_port_mod_refs(task, ns_target,
			darling_mach_port_type_send, -1);
		darling_mach_msg_header ns_message{};
		std::uint32_t ns_size = 0;
		const auto ns_receive = darling_windows_mach_port_receive(ns_notify, &ns_message,
			sizeof(ns_message), &ns_size, 100);
		const bool no_senders_ok = ns_alloc_target == 0 && ns_alloc_notify == 0 &&
			ns_insert == 0 && ns_request == 0 && ns_previous == 0 && ns_mod == 0 &&
			ns_receive == 0 && ns_size == sizeof(ns_message) &&
			 ns_message.msgh_id == 0x4a;
		if (!no_senders_ok) return 1;
		std::cout << "MACH_NOTIFY_NO_SENDERS=PASS\n";
		if (ns_target != 0) darling_windows_mach_port_destroy(task, ns_target);
		if (ns_notify != 0) darling_windows_mach_port_destroy(task, ns_notify);
		darling_mach_port_name_t dead_target = 0, dead_notify = 0, dead_previous = 0xffffffffu;
		const auto dead_alloc_target = darling_windows_mach_port_allocate(task, &dead_target);
		const auto dead_alloc_notify = darling_windows_mach_port_allocate(task, &dead_notify);
		const auto dead_request = darling_windows_mach_port_request_notification(task, dead_target,
			0x48, dead_notify, &dead_previous);
		const auto dead_destroy = darling_windows_mach_port_destroy(task, dead_target);
		darling_mach_msg_header dead_message{};
		std::uint32_t dead_size = 0;
		const auto dead_receive = darling_windows_mach_port_receive(dead_notify, &dead_message,
			sizeof(dead_message), &dead_size, 100);
		const bool dead_name_ok = dead_alloc_target == 0 && dead_alloc_notify == 0 &&
			dead_request == 0 && dead_previous == 0 && dead_destroy == 0 &&
			dead_receive == 0 && dead_size == sizeof(dead_message) &&
			dead_message.msgh_id == 0x48;
		if (!dead_name_ok) return 1;
		std::cout << "MACH_NOTIFY_DEAD_NAME=PASS\n";
		if (dead_notify != 0) darling_windows_mach_port_destroy(task, dead_notify);
		if (notification_port != 0) darling_windows_mach_port_destroy(task, notification_port);
		if (replacement_notification_port != 0)
			darling_windows_mach_port_destroy(task, replacement_notification_port);
	}
	std::cout << "MACH_C_ABI_SELF_DEALLOCATE=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
#endif
}
