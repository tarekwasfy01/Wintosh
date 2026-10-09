/*
 * Darwin Mach C-ABI entry points backed by the Windows host layer.
 * GPL-3.0-only; see the bundled license and source manifests.
 */
#pragma once

#include <cstdint>
#include <cstddef>

using darling_mach_port_name_t = std::uint32_t;
using darling_kern_return_t = std::int32_t;
using darling_mach_vm_address_t = std::uint64_t;
using darling_mach_vm_size_t = std::uint64_t;

struct darling_mach_msg_header final {
	std::uint32_t msgh_bits;
	std::uint32_t msgh_size;
	darling_mach_port_name_t msgh_remote_port;
	darling_mach_port_name_t msgh_local_port;
	std::uint32_t msgh_reserved;
};

constexpr std::uint32_t darling_mach_send_msg = 0x00000001;
constexpr std::uint32_t darling_mach_receive_msg = 0x00000002;
constexpr std::uint32_t darling_vm_prot_read = 1;
constexpr std::uint32_t darling_vm_prot_write = 2;
constexpr std::uint32_t darling_vm_prot_execute = 4;
constexpr std::uint32_t darling_vm_region_basic_info = 9;
constexpr std::uint32_t darling_vm_region_basic_info_count = 9;
struct darling_mach_vm_region_basic_info final {
	std::int32_t protection;
	std::int32_t max_protection;
	std::int32_t inheritance;
	std::uint32_t shared;
	std::uint32_t reserved;
	std::int32_t behavior;
	std::uint16_t user_wired_count;
	std::uint16_t reserved2;
};
constexpr std::uint32_t darling_mach_port_type_none = 0;
constexpr std::uint32_t darling_mach_port_type_receive = 1;
constexpr std::uint32_t darling_mach_port_type_send = 2;
// Darwin mach_msg disposition values used by the minimal host ABI.
constexpr std::uint32_t darling_mach_move_receive = 16;
constexpr std::uint32_t darling_mach_copy_send = 19;
constexpr std::uint32_t darling_mach_move_send = 20;
constexpr std::uint32_t darling_mach_make_send = 21;
constexpr darling_kern_return_t darling_mach_send_queue_full = 0x10004002;

extern "C" darling_mach_port_name_t darling_windows_mach_task_self();
extern "C" darling_mach_port_name_t darling_windows_mach_thread_self();
extern "C" darling_mach_port_name_t darling_windows_mach_host_self();
extern "C" darling_kern_return_t darling_windows_host_page_size(
	darling_mach_port_name_t host, std::uint32_t* size);
extern "C" darling_kern_return_t darling_windows_mach_vm_allocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t size, std::uint32_t flags);
extern "C" darling_kern_return_t darling_windows_mach_vm_deallocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size);
extern "C" darling_kern_return_t darling_windows_mach_vm_protect(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, bool set_maximum, std::uint32_t protection);
extern "C" darling_kern_return_t darling_windows_mach_vm_read_overwrite(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination,
	darling_mach_vm_size_t* out_size);
extern "C" darling_kern_return_t darling_windows_mach_vm_read(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, darling_mach_vm_address_t* data,
	darling_mach_vm_size_t* out_size);
extern "C" darling_kern_return_t darling_windows_mach_vm_write(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	const void* data, darling_mach_vm_size_t size);
extern "C" darling_kern_return_t darling_windows_mach_vm_copy(
	darling_mach_port_name_t task, darling_mach_vm_address_t source,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination);
extern "C" darling_kern_return_t darling_windows_mach_vm_region(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t* size, std::uint32_t flavor, void* info,
	std::uint32_t* info_count);
extern "C" darling_kern_return_t darling_windows_mach_vm_region_recurse(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t* size, std::uint32_t* depth, void* info,
	std::uint32_t* info_count);
extern "C" darling_kern_return_t darling_windows_mach_port_deallocate(
	darling_mach_port_name_t task, darling_mach_port_name_t name);
extern "C" darling_kern_return_t darling_windows_mach_port_destroy(
	darling_mach_port_name_t task, darling_mach_port_name_t name);
extern "C" darling_kern_return_t darling_windows_mach_port_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* name);
extern "C" darling_kern_return_t darling_windows_mach_port_insert_right(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	darling_mach_port_name_t right, std::uint32_t disposition);
extern "C" darling_kern_return_t darling_windows_mach_port_extract_right(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t disposition, darling_mach_port_name_t* right,
	std::uint32_t* right_disposition);
extern "C" darling_kern_return_t darling_windows_mach_port_mod_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::int32_t delta);
extern "C" darling_kern_return_t darling_windows_mach_port_get_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::uint32_t* refs);
extern "C" darling_kern_return_t darling_windows_mach_port_type(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t* type);
extern "C" darling_kern_return_t darling_windows_mach_port_set_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* set);
extern "C" darling_kern_return_t darling_windows_mach_port_move_member(
	darling_mach_port_name_t task, darling_mach_port_name_t member,
	darling_mach_port_name_t set);
extern "C" darling_kern_return_t darling_windows_mach_port_remove_member(
	darling_mach_port_name_t task, darling_mach_port_name_t member,
	darling_mach_port_name_t set);
extern "C" darling_kern_return_t darling_windows_mach_port_set_destroy(
	darling_mach_port_name_t task, darling_mach_port_name_t set);
extern "C" darling_kern_return_t darling_windows_mach_port_set_receive(
	darling_mach_port_name_t set, void* data, std::uint32_t capacity,
	std::uint32_t* size, std::uint32_t timeout_ms);
extern "C" darling_kern_return_t darling_windows_mach_port_send(
	darling_mach_port_name_t name, const void* data, std::uint32_t size);
extern "C" darling_kern_return_t darling_windows_mach_port_receive(
	darling_mach_port_name_t name, void* data, std::uint32_t capacity,
	std::uint32_t* size, std::uint32_t timeout_ms);
extern "C" darling_kern_return_t darling_windows_mach_msg(
	darling_mach_msg_header* message, std::uint32_t option,
	std::uint32_t send_size, std::uint32_t receive_size,
	darling_mach_port_name_t receive_name, std::uint32_t timeout_ms,
	darling_mach_port_name_t notify);
