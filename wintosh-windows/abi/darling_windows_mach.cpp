/*
 * Darwin Mach C-ABI entry points backed by the Windows host layer.
 * GPL-3.0-only; see the bundled license and source manifests.
 */
#include "darling_windows_mach.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <memory>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>

namespace {
std::atomic<darling_mach_port_name_t> next_port{0x100};
struct PortQueue final {
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<std::vector<std::uint8_t>> messages;
	std::uint32_t refs = 1;
};
std::mutex ports_mutex;
std::unordered_map<darling_mach_port_name_t, std::shared_ptr<PortQueue>> ports;
std::unordered_map<darling_mach_port_name_t, std::unordered_set<darling_mach_port_name_t>> port_sets;

std::shared_ptr<PortQueue> FindPort(darling_mach_port_name_t name)
{
	std::lock_guard lock(ports_mutex);
	const auto found = ports.find(name);
	return found == ports.end() ? nullptr : found->second;
}
}

extern "C" darling_mach_port_name_t darling_windows_mach_task_self()
{
	return static_cast<darling_mach_port_name_t>(GetCurrentProcessId());
}

extern "C" darling_mach_port_name_t darling_windows_mach_thread_self()
{
	return static_cast<darling_mach_port_name_t>(GetCurrentThreadId());
}

extern "C" darling_mach_port_name_t darling_windows_mach_host_self()
{
	return 1;
}

extern "C" darling_kern_return_t darling_windows_host_page_size(
	darling_mach_port_name_t host, std::uint32_t* size)
{
	if (host == 0 || size == nullptr) return 4;
	SYSTEM_INFO information{};
	GetSystemInfo(&information);
	*size = information.dwPageSize;
	return *size == 0 ? 4 : 0;
}

namespace {
DWORD VmProtection(std::uint32_t protection)
{
	const bool read = (protection & darling_vm_prot_read) != 0;
	const bool write = (protection & darling_vm_prot_write) != 0;
	const bool execute = (protection & darling_vm_prot_execute) != 0;
	if (execute) return write ? (read ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READWRITE) :
		(read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	return write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS);
}
}

extern "C" darling_kern_return_t darling_windows_mach_vm_allocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t size, std::uint32_t flags)
{
	(void)flags;
	if (task == 0 || address == nullptr || size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	void* allocated = VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(*address)),
		static_cast<SIZE_T>(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (allocated == nullptr) return 3;
	*address = static_cast<darling_mach_vm_address_t>(reinterpret_cast<std::uintptr_t>(allocated));
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_deallocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size)
{
	(void)size;
	if (task == 0 || address == 0 || VirtualFree(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
		0, MEM_RELEASE) == FALSE) return 4;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_protect(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, bool set_maximum, std::uint32_t protection)
{
	(void)set_maximum;
	if (task == 0 || address == 0 || size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	DWORD old_protection = 0;
	return VirtualProtect(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
		static_cast<SIZE_T>(size), VmProtection(protection), &old_protection) ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_read_overwrite(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination,
	darling_mach_vm_size_t* out_size)
{
	if (task == 0 || address == 0 || destination == 0 || out_size == nullptr ||
		size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	SIZE_T copied = 0;
	if (!ReadProcessMemory(GetCurrentProcess(),
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(destination)),
		static_cast<SIZE_T>(size), &copied)) return 4;
	*out_size = static_cast<darling_mach_vm_size_t>(copied);
	return copied == size ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_write(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	const void* data, darling_mach_vm_size_t size)
{
	if (task == 0 || address == 0 || data == nullptr || size == 0 ||
		size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	SIZE_T copied = 0;
	if (!WriteProcessMemory(GetCurrentProcess(),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), data,
		static_cast<SIZE_T>(size), &copied)) return 4;
	return copied == size ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_copy(
	darling_mach_port_name_t task, darling_mach_vm_address_t source,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination)
{
	if (task == 0 || source == 0 || destination == 0 || size == 0 ||
		size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
	SIZE_T copied = 0;
	if (!ReadProcessMemory(GetCurrentProcess(),
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(source)),
		buffer.data(), static_cast<SIZE_T>(size), &copied) || copied != size)
		return 4;
	if (!WriteProcessMemory(GetCurrentProcess(),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(destination)),
		buffer.data(), static_cast<SIZE_T>(size), &copied) || copied != size)
		return 4;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_deallocate(
	darling_mach_port_name_t task, darling_mach_port_name_t name)
{
	if (task == 0 || name == 0) return 4; // KERN_INVALID_ARGUMENT
	{
		std::lock_guard lock(ports_mutex);
		ports.erase(name);
		for (auto& [set_name, members] : port_sets) {
			(void)set_name;
			members.erase(name);
		}
	}
	return 0; // Host tokens are borrowed pseudo-rights; nothing to close here.
}

extern "C" darling_kern_return_t darling_windows_mach_port_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* name)
{
	if (task == 0 || name == nullptr) return 4;
	*name = next_port.fetch_add(1, std::memory_order_relaxed);
	if (*name == 0) return 3;
	{
		std::lock_guard lock(ports_mutex);
		ports.emplace(*name, std::make_shared<PortQueue>());
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_destroy(
	darling_mach_port_name_t task, darling_mach_port_name_t name)
{
	return darling_windows_mach_port_deallocate(task, name);
}

extern "C" darling_kern_return_t darling_windows_mach_port_insert_right(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	darling_mach_port_name_t right, std::uint32_t disposition)
{
	if (task == 0 || name == 0 || right == 0 || disposition == 0) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	{
		std::lock_guard lock(port->mutex);
		if (port->refs == (std::numeric_limits<std::uint32_t>::max)()) return 3;
		++port->refs;
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_mod_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::int32_t delta)
{
	if (task == 0 || name == 0 || right == 0 || delta == 0) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::lock_guard lock(port->mutex);
	const auto current = static_cast<std::int64_t>(port->refs);
	const auto change = static_cast<std::int64_t>(delta);
	const auto updated = current + change;
	if (updated < 0 || updated > static_cast<std::int64_t>(
		(std::numeric_limits<std::uint32_t>::max)())) return 4;
	port->refs = static_cast<std::uint32_t>(updated);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_get_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::uint32_t* refs)
{
	if (task == 0 || name == 0 || right == 0 || refs == nullptr) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::lock_guard lock(port->mutex);
	*refs = port->refs;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_type(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t* type)
{
	if (task == 0 || name == 0 || type == nullptr) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) {
		*type = darling_mach_port_type_none;
		return 3;
	}
	*type = darling_mach_port_type_receive;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_set_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* set)
{
	if (task == 0 || set == nullptr) return 4;
	*set = next_port.fetch_add(1, std::memory_order_relaxed);
	if (*set == 0) return 3;
	std::lock_guard lock(ports_mutex);
	port_sets.emplace(*set, std::unordered_set<darling_mach_port_name_t>{});
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_move_member(
	darling_mach_port_name_t task, darling_mach_port_name_t member,
	darling_mach_port_name_t set)
{
	if (task == 0 || member == 0 || set == 0) return 4;
	std::lock_guard lock(ports_mutex);
	if (ports.find(member) == ports.end()) return 3;
	auto found = port_sets.find(set);
	if (found == port_sets.end()) return 3;
	found->second.insert(member);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_remove_member(
	darling_mach_port_name_t task, darling_mach_port_name_t member,
	darling_mach_port_name_t set)
{
	if (task == 0 || member == 0 || set == 0) return 4;
	std::lock_guard lock(ports_mutex);
	auto found = port_sets.find(set);
	if (found == port_sets.end()) return 3;
	return found->second.erase(member) == 1 ? 0 : 3;
}

extern "C" darling_kern_return_t darling_windows_mach_port_set_destroy(
	darling_mach_port_name_t task, darling_mach_port_name_t set)
{
	if (task == 0 || set == 0) return 4;
	std::lock_guard lock(ports_mutex);
	return port_sets.erase(set) == 1 ? 0 : 3;
}

extern "C" darling_kern_return_t darling_windows_mach_port_receive(
	darling_mach_port_name_t name, void* data, std::uint32_t capacity,
	std::uint32_t* size, std::uint32_t timeout_ms);

extern "C" darling_kern_return_t darling_windows_mach_port_set_receive(
	darling_mach_port_name_t set, void* data, std::uint32_t capacity,
	std::uint32_t* size, std::uint32_t timeout_ms)
{
	if (set == 0 || data == nullptr || size == nullptr) return 4;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeout_ms);
	for (;;) {
		std::unordered_set<darling_mach_port_name_t> members;
		{
			std::lock_guard lock(ports_mutex);
			auto found = port_sets.find(set);
			if (found == port_sets.end()) return 3;
			members = found->second;
		}
		for (const auto member : members) {
			const auto result = darling_windows_mach_port_receive(member, data,
				capacity, size, 0);
			if (result == 0 || result == 0x10004003) return result;
		}
		if (std::chrono::steady_clock::now() >= deadline) return 268;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

extern "C" darling_kern_return_t darling_windows_mach_port_send(
	darling_mach_port_name_t name, const void* data, std::uint32_t size)
{
	if (data == nullptr && size != 0) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::vector<std::uint8_t> message(size);
	if (size != 0) std::memcpy(message.data(), data, size);
	{
		std::lock_guard lock(port->mutex);
		port->messages.push_back(std::move(message));
	}
	port->condition.notify_one();
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_receive(
	darling_mach_port_name_t name, void* data, std::uint32_t capacity,
	std::uint32_t* size, std::uint32_t timeout_ms)
{
	if (data == nullptr || size == nullptr) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::unique_lock lock(port->mutex);
	if (!port->condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
		[&] { return !port->messages.empty(); })) return 268; // MACH_RCV_TIMED_OUT
	auto message = std::move(port->messages.front());
	port->messages.pop_front();
	if (message.size() > capacity) return 0x10004003; // MACH_MSG_TOO_LARGE
	std::memcpy(data, message.data(), message.size());
	*size = static_cast<std::uint32_t>(message.size());
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_msg(
	darling_mach_msg_header* message, std::uint32_t option,
	std::uint32_t send_size, std::uint32_t receive_size,
	darling_mach_port_name_t receive_name, std::uint32_t timeout_ms,
	darling_mach_port_name_t notify)
{
	(void)notify;
	if (message == nullptr) return 4;
	if ((option & darling_mach_send_msg) != 0) {
		if (send_size < sizeof(darling_mach_msg_header) ||
			message->msgh_remote_port == 0) return 4;
		const auto result = darling_windows_mach_port_send(message->msgh_remote_port,
			message, send_size);
		if (result != 0) return result;
	}
	if ((option & darling_mach_receive_msg) != 0) {
		if (receive_size < sizeof(darling_mach_msg_header) || receive_name == 0)
			return 4;
		std::uint32_t actual_size = 0;
		const auto result = darling_windows_mach_port_receive(receive_name, message,
			receive_size, &actual_size, timeout_ms);
		if (result != 0) return result;
		message->msgh_size = actual_size;
	}
	return 0;
}
