/* Stage 1 proof for the minimal Mach C ABI bridge. GPL-3.0-only. */
#include "darling_windows_mach.h"
#include "darling_windows_stdio.h"

#include <iostream>
#include <cstring>

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
	darling_mach_port_name_t port_set = 0;
	std::uint32_t refs = 0;
	std::uint32_t port_type = darling_mach_port_type_none;
	darling_mach_msg_header message{};
	char payload[] = "mach-ipc";
	char received[32]{};
	std::uint32_t received_size = 0;
	char timeout_buffer[8]{};
	std::uint32_t timeout_size = 0;
	const bool ok = task != 0 && thread != 0 && host != 0 &&
		darling_windows_host_symbol("mach_task_self") != 0 &&
		darling_windows_host_symbol("mach_thread_self") != 0 &&
		darling_windows_host_symbol("mach_host_self") != 0 &&
		darling_windows_host_symbol("host_page_size") != 0 &&
		darling_windows_host_symbol("mach_vm_allocate") != 0 &&
		darling_windows_host_symbol("mach_vm_deallocate") != 0 &&
		darling_windows_host_symbol("mach_vm_protect") != 0 &&
		darling_windows_host_symbol("mach_vm_read_overwrite") != 0 &&
		darling_windows_host_symbol("mach_vm_write") != 0 &&
		darling_windows_host_symbol("mach_vm_copy") != 0 &&
		darling_windows_host_page_size(host, &page_size) == 0 && page_size >= 4096 &&
		darling_windows_mach_vm_allocate(task, &vm_address, page_size, 0) == 0 &&
		vm_address != 0 &&
		darling_windows_mach_vm_allocate(task, &vm_copy_address, page_size, 0) == 0 &&
		darling_windows_mach_vm_write(task, vm_address, &vm_value, sizeof(vm_value)) == 0 &&
		darling_windows_mach_vm_read_overwrite(task, vm_address, sizeof(vm_value),
		vm_copy_address, &vm_read_size) == 0 && vm_read_size == sizeof(vm_value) &&
		std::memcpy(&vm_copy, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vm_copy_address)),
		sizeof(vm_copy)) != nullptr && vm_copy == vm_value &&
		darling_windows_mach_vm_copy(task, vm_address, sizeof(vm_value), vm_copy_address) == 0 &&
		std::memcpy(&vm_copy, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vm_copy_address)),
		sizeof(vm_copy)) != nullptr && vm_copy == vm_value &&
		darling_windows_mach_vm_protect(task, vm_address, page_size, false,
		 darling_vm_prot_read) == 0 &&
		darling_windows_mach_vm_deallocate(task, vm_copy_address, page_size) == 0 &&
		darling_windows_mach_vm_deallocate(task, vm_address, page_size) == 0 &&
		darling_windows_host_symbol("mach_port_allocate") != 0 &&
		darling_windows_host_symbol("mach_port_get_refs") != 0 &&
		darling_windows_host_symbol("mach_port_type") != 0 &&
		darling_windows_host_symbol("mach_port_set_allocate") != 0 &&
		darling_windows_host_symbol("mach_port_move_member") != 0 &&
		darling_windows_host_symbol("mach_port_remove_member") != 0 &&
		darling_windows_host_symbol("mach_port_set_destroy") != 0 &&
		darling_windows_host_symbol("mach_port_set_receive") != 0 &&
		darling_windows_host_symbol("mach_msg") != 0 &&
		darling_windows_host_symbol("mach_port_destroy") != 0 &&
		darling_windows_mach_port_allocate(task, &allocated) == 0 && allocated != 0 &&
		darling_windows_mach_port_insert_right(task, allocated, thread, 1) == 0 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 2 &&
		darling_windows_mach_port_mod_refs(task, allocated, 1, 1) == 0 &&
		darling_windows_mach_port_get_refs(task, allocated, 1, &refs) == 0 && refs == 3 &&
		darling_windows_mach_port_type(task, allocated, &port_type) == 0 &&
		port_type == darling_mach_port_type_receive &&
		darling_windows_mach_port_set_allocate(task, &port_set) == 0 && port_set != 0 &&
		darling_windows_mach_port_move_member(task, allocated, port_set) == 0 &&
		darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_receive(allocated, received, sizeof(received),
		&received_size, 100) == 0 && received_size == sizeof(payload) &&
		std::memcmp(received, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_receive(allocated, timeout_buffer,
		sizeof(timeout_buffer), &timeout_size, 1) == 268 &&
		(darling_windows_mach_port_send(allocated, payload, sizeof(payload)) == 0) &&
		darling_windows_mach_port_set_receive(port_set, received, sizeof(received),
		&received_size, 100) == 0 && received_size == sizeof(payload) &&
		std::memcmp(received, payload, sizeof(payload)) == 0 &&
		darling_windows_mach_port_remove_member(task, allocated, port_set) == 0 &&
		darling_windows_mach_port_remove_member(task, allocated, port_set) == 3 &&
		(message.msgh_remote_port = allocated,
		message.msgh_size = sizeof(message),
		darling_windows_mach_msg(&message, darling_mach_send_msg,
			sizeof(message), 0, 0, 0, 0) == 0) &&
		darling_windows_mach_msg(&message, darling_mach_receive_msg,
		sizeof(message), sizeof(message), allocated, 100, 0) == 0 &&
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
