/* Stage 1 proof for the minimal Mach C ABI bridge. GPL-3.0-only. */
#include "darling_windows_mach.h"
#include "darling_windows_stdio.h"

#include <iostream>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

int main()
{
	const auto task = darling_windows_mach_task_self();
	const auto thread = darling_windows_mach_thread_self();
	const auto host = darling_windows_mach_host_self();
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
	const bool ok = task != 0 && thread != 0 && host != 0 &&
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
		darling_windows_mach_msg(&message, darling_mach_receive_msg,
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
	std::cout << "MACH_C_ABI_SELF_DEALLOCATE=" << (ok ? "PASS" : "FAIL") << "\n";
	return ok ? 0 : 1;
}
