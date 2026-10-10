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
using darling_exception_mask_t = std::uint32_t;
using darling_exception_behavior_t = std::uint32_t;
using darling_exception_flavor_t = std::uint32_t;
using darling_mach_msg_id_t = std::int32_t;

struct darling_mach_msg_header final {
	std::uint32_t msgh_bits;
	std::uint32_t msgh_size;
	darling_mach_port_name_t msgh_remote_port;
	darling_mach_port_name_t msgh_local_port;
	union {
		std::uint32_t msgh_reserved;
		darling_mach_msg_id_t msgh_id;
	};
};

// Darwin 64-bit mach_msg descriptor/trailer layouts. These are ABI data
// structures only; descriptor ownership transfer is implemented separately.
struct darling_mach_msg_body final {
	std::uint32_t msgh_descriptor_count;
};

struct darling_mach_msg_port_descriptor final {
	darling_mach_port_name_t name;
	std::uint32_t pad1;
	std::uint8_t disposition;
	std::uint8_t type;
	std::uint16_t pad2;
};

struct darling_mach_msg_ool_descriptor final {
	std::uint64_t address;
	std::uint32_t size;
	std::uint8_t deallocate;
	std::uint8_t copy;
	std::uint16_t pad1;
	std::uint32_t type;
};

struct darling_mach_msg_trailer final {
	std::uint32_t type;
	std::uint32_t size;
};

constexpr std::uint32_t darling_mach_msg_descriptor_port = 0;
constexpr std::uint32_t darling_mach_msg_descriptor_ool = 1;
constexpr std::uint32_t darling_mach_msg_trailer_none = 0;

constexpr std::uint32_t darling_mach_send_msg = 0x00000001;
constexpr std::uint32_t darling_mach_msg_complex = 0x80000000;
constexpr std::uint32_t darling_mach_receive_msg = 0x00000002;
constexpr std::uint32_t darling_mach_send_timeout = 0x00000010;
constexpr std::uint32_t darling_mach_receive_timeout = 0x00000100;
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
constexpr std::uint32_t darling_mach_port_type_send_once = 4;
// Darwin mach_msg disposition values used by the minimal host ABI.
constexpr std::uint32_t darling_mach_move_receive = 16;
constexpr std::uint32_t darling_mach_move_send_once = 17;
constexpr std::uint32_t darling_mach_copy_send = 19;
constexpr std::uint32_t darling_mach_move_send = 20;
constexpr std::uint32_t darling_mach_make_send = 21;
constexpr std::uint32_t darling_mach_make_send_once = 23;
constexpr std::uint32_t darling_mach_copy_receive = 22;
constexpr std::uint32_t darling_mach_dispose_receive = 24;
constexpr std::uint32_t darling_mach_dispose_send = 25;
constexpr std::uint32_t darling_mach_dispose_send_once = 26;
constexpr darling_kern_return_t darling_mach_send_queue_full = 0x10004002;

extern "C" darling_mach_port_name_t darling_windows_mach_task_self();
extern "C" darling_mach_port_name_t darling_windows_mach_thread_self();
extern "C" darling_mach_port_name_t darling_windows_mach_host_self();
constexpr std::uint32_t darling_thread_extended_policy = 1;
constexpr std::uint32_t darling_thread_time_constraint_policy = 2;
constexpr std::uint32_t darling_thread_precedence_policy = 3;
constexpr darling_kern_return_t darling_kern_not_supported = 0x2c;
constexpr std::uint32_t darling_thread_basic_info_flavor = 3;
constexpr std::uint32_t darling_thread_identifier_info_flavor = 4;
constexpr std::uint32_t darling_thread_extended_info_flavor = 5;
constexpr std::uint32_t darling_thread_sched_timeshare_info_flavor = 10;
constexpr std::uint32_t darling_thread_sched_rr_info_flavor = 11;
constexpr std::uint32_t darling_thread_sched_fifo_info_flavor = 12;
constexpr std::uint32_t darling_thread_basic_info_count = 8;
constexpr std::uint32_t darling_thread_identifier_info_count = 6;
constexpr std::uint32_t darling_thread_extended_info_count = 28;
constexpr std::uint32_t darling_thread_sched_timeshare_info_count = 5;
constexpr std::uint32_t darling_thread_sched_rr_info_count = 5;
constexpr std::uint32_t darling_thread_sched_fifo_info_count = 4;
constexpr std::uint32_t darling_thread_time_constraint_policy_count = 4;
constexpr std::uint32_t darling_x86_thread_state64_flavor = 4;
constexpr std::uint32_t darling_x86_float_state64_flavor = 5;
constexpr std::uint32_t darling_x86_exception_state64_flavor = 6;
constexpr std::uint32_t darling_x86_avx_state64_flavor = 17;
constexpr std::uint32_t darling_x86_avx512_state64_flavor = 20;
constexpr std::uint32_t darling_x86_debug_state64_flavor = 11;
constexpr std::uint32_t darling_x86_thread_state64_count = 42;
constexpr std::uint32_t darling_x86_float_state64_count = 131;
constexpr std::uint32_t darling_x86_avx_state64_count = 211;
constexpr std::uint32_t darling_x86_avx512_state64_count = 611;
constexpr std::uint32_t darling_x86_exception_state64_count = 4;
constexpr std::uint32_t darling_x86_debug_state64_count = 16;
struct darling_time_value final { std::int32_t seconds = 0; std::int32_t microseconds = 0; };
struct darling_thread_basic_info final {
	darling_time_value user_time;
	darling_time_value system_time;
	std::int32_t cpu_usage = 0;
	std::int32_t policy = 0;
	std::int32_t run_state = 1;
	std::int32_t flags = 0;
	std::int32_t suspend_count = 0;
	std::int32_t sleep_time = 0;
};
struct darling_thread_identifier_info final {
	std::uint64_t thread_id = 0;
	std::uint64_t thread_handle = 0;
	std::uint64_t dispatch_qaddr = 0;
};
struct darling_thread_extended_info final {
	std::uint64_t user_time = 0;
	std::uint64_t system_time = 0;
	std::int32_t cpu_usage = 0;
	std::int32_t policy = 0;
	std::int32_t run_state = 1;
	std::int32_t flags = 0;
	std::int32_t sleep_time = 0;
	std::int32_t current_priority = 0;
	std::int32_t priority = 0;
	std::int32_t max_priority = 0;
	char name[64]{};
};
struct darling_thread_sched_timeshare_info final {
	std::int32_t max_priority = 0;
	std::int32_t base_priority = 0;
	std::int32_t current_priority = 0;
	std::int32_t depressed = 0;
	std::int32_t depress_priority = 0;
};
struct darling_thread_sched_rr_info final {
	std::int32_t max_priority = 0;
	std::int32_t base_priority = 0;
	std::int32_t quantum = 0;
	std::int32_t depressed = 0;
	std::int32_t depress_priority = 0;
};
struct darling_thread_sched_fifo_info final {
	std::int32_t max_priority = 0;
	std::int32_t base_priority = 0;
	std::int32_t depressed = 0;
	std::int32_t depress_priority = 0;
};
struct darling_x86_thread_state64 final {
	std::uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;
	std::uint64_t rdi = 0, rsi = 0, rbp = 0, rsp = 0;
	std::uint64_t r8 = 0, r9 = 0, r10 = 0, r11 = 0;
	std::uint64_t r12 = 0, r13 = 0, r14 = 0, r15 = 0;
	std::uint64_t rip = 0, rflags = 0, cs = 0, fs = 0, gs = 0;
};
struct darling_x86_exception_state64 final {
	std::uint16_t trapno = 0;
	std::uint16_t cpu = 0;
	std::uint32_t error = 0;
	std::uint64_t faultvaddr = 0;
};
struct darling_x86_float_state64 final {
	std::uint32_t words[darling_x86_float_state64_count]{};
};
struct darling_x86_avx_state64 final {
	std::uint32_t words[darling_x86_avx_state64_count]{};
};
struct darling_x86_avx512_state64 final {
	std::uint32_t words[darling_x86_avx512_state64_count]{};
};
struct darling_x86_debug_state64 final {
	std::uint64_t dr0 = 0, dr1 = 0, dr2 = 0, dr3 = 0;
	std::uint64_t dr4 = 0, dr5 = 0, dr6 = 0, dr7 = 0;
};
struct darling_exception_handler_info final {
	std::uint32_t port_object = 0;
	std::uint32_t receiver_object = 0;
};
struct darling_mach_exception_message final {
	darling_mach_msg_header header;
	std::uint32_t exception_type = 0;
	std::uint64_t code0 = 0;
	std::uint64_t code1 = 0;
	darling_mach_port_name_t thread = 0;
	darling_exception_behavior_t behavior = 0;
	darling_exception_flavor_t flavor = 0;
};
struct darling_thread_extended_policy_info final { std::int32_t timeshare = 1; };
struct darling_thread_precedence_policy_info final { std::int32_t importance = 0; };
struct darling_thread_time_constraint_policy_info final {
	std::uint32_t period = 0;
	std::uint32_t computation = 0;
	std::uint32_t constraint = 0;
	std::int32_t preemptible = 1;
};
extern "C" darling_kern_return_t darling_windows_thread_policy_set(
	darling_mach_port_name_t thread, std::uint32_t flavor, const void* policy,
	std::uint32_t count);
extern "C" darling_kern_return_t darling_windows_thread_policy_get(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* policy,
	std::uint32_t* count, bool* get_default);
extern "C" darling_kern_return_t darling_windows_thread_suspend(darling_mach_port_name_t thread);
extern "C" darling_kern_return_t darling_windows_thread_resume(darling_mach_port_name_t thread);
extern "C" darling_kern_return_t darling_windows_thread_abort(darling_mach_port_name_t thread);
extern "C" darling_kern_return_t darling_windows_thread_get_state(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* state,
	std::uint32_t* count);
extern "C" darling_kern_return_t darling_windows_thread_set_state(
	darling_mach_port_name_t thread, std::uint32_t flavor, const void* state,
	std::uint32_t count);
extern "C" darling_kern_return_t darling_windows_thread_set_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_mach_port_name_t port, darling_exception_behavior_t behavior,
	darling_exception_flavor_t flavor);
extern "C" darling_kern_return_t darling_windows_thread_get_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_exception_mask_t* masks, std::uint32_t* masks_count,
	darling_mach_port_name_t* handlers, darling_exception_behavior_t* behaviors,
	darling_exception_flavor_t* flavors);
extern "C" darling_kern_return_t darling_windows_thread_swap_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_mach_port_name_t new_port, darling_exception_behavior_t new_behavior,
	darling_exception_flavor_t new_flavor, darling_exception_mask_t* masks,
	std::uint32_t* masks_count, darling_mach_port_name_t* handlers,
	darling_exception_behavior_t* behaviors, darling_exception_flavor_t* flavors);
extern "C" darling_kern_return_t darling_windows_thread_get_exception_ports_info(
	darling_mach_port_name_t port, darling_exception_mask_t mask,
	darling_exception_mask_t* masks, std::uint32_t* masks_count,
	darling_exception_handler_info* handlers_info,
	darling_exception_behavior_t* behaviors, darling_exception_flavor_t* flavors);
extern "C" darling_kern_return_t darling_windows_dispatch_mach_exception(
	std::uint32_t exception_type, std::uint64_t code0, std::uint64_t code1);
extern "C" darling_kern_return_t darling_windows_set_exception_dispatch_enabled(bool enabled);
extern "C" darling_kern_return_t darling_windows_thread_info(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* info,
	std::uint32_t* count);
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
extern "C" darling_kern_return_t darling_windows_mach_port_request_notification(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	darling_mach_msg_id_t msgid, darling_mach_port_name_t notify,
	darling_mach_port_name_t* previous);
extern "C" darling_kern_return_t darling_windows_mach_port_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* name);
extern "C" darling_kern_return_t darling_windows_mach_broker_enable(
	const wchar_t* pipe_name);
extern "C" darling_kern_return_t darling_windows_mach_broker_disable();
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
extern "C" darling_kern_return_t darling_windows_mach_port_bind_broker(
	darling_mach_port_name_t local_name, std::uint64_t broker_token,
	std::uint64_t session_token);
extern "C" darling_kern_return_t darling_windows_mach_port_lookup_broker(
	darling_mach_port_name_t local_name, std::uint64_t* broker_token,
	std::uint64_t* session_token);
extern "C" darling_kern_return_t darling_windows_mach_port_unbind_broker(
	darling_mach_port_name_t local_name);
extern "C" darling_kern_return_t darling_windows_mach_ool_release(void* address);
