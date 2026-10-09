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
constexpr std::size_t max_port_queue_depth = 1024;
constexpr std::size_t max_inline_message_size = 4 * 1024 * 1024;
struct PortQueue final {
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<std::vector<std::uint8_t>> messages;
	std::uint32_t refs = 1;
	std::uint32_t receive_refs = 1;
	std::uint32_t send_refs = 0;
	bool closed = false;
};
std::mutex ports_mutex;
std::mutex ports_wait_mutex;
std::condition_variable ports_condition;
std::unordered_map<darling_mach_port_name_t, std::shared_ptr<PortQueue>> ports;
std::unordered_map<darling_mach_port_name_t, std::unordered_set<darling_mach_port_name_t>> port_sets;
std::mutex read_buffers_mutex;
std::unordered_map<darling_mach_vm_address_t, SIZE_T> read_buffers;

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

HANDLE OpenVmProcess(darling_mach_port_name_t task, DWORD access, bool& owned)
{
	owned = false;
	if (task == 0) return nullptr;
	if (task == darling_windows_mach_task_self()) return GetCurrentProcess();
	const HANDLE process = OpenProcess(access, FALSE, static_cast<DWORD>(task));
	owned = process != nullptr;
	return process;
}

void CloseVmProcess(HANDLE process, bool owned) noexcept
{
	if (owned && process != nullptr) CloseHandle(process);
}
}

extern "C" darling_kern_return_t darling_windows_mach_vm_allocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t size, std::uint32_t flags)
{
	(void)flags;
	if (task == 0 || address == nullptr || size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_OPERATION, owned);
	if (process == nullptr) return 3;
	void* allocated = VirtualAllocEx(process, reinterpret_cast<void*>(static_cast<std::uintptr_t>(*address)),
		static_cast<SIZE_T>(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	CloseVmProcess(process, owned);
	if (allocated == nullptr) return 3;
	*address = static_cast<darling_mach_vm_address_t>(reinterpret_cast<std::uintptr_t>(allocated));
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_deallocate(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size)
{
	(void)size;
	if (address == 0) return 4;
	{
		std::lock_guard lock(read_buffers_mutex);
		const auto found = read_buffers.find(address);
		if (found != read_buffers.end()) {
			const BOOL released = VirtualFree(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), 0, MEM_RELEASE);
			if (released) read_buffers.erase(found);
			return released ? 0 : 4;
		}
	}
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_OPERATION, owned);
	if (process == nullptr) {
		CloseVmProcess(process, owned);
		return 4;
	}
	const BOOL released = VirtualFreeEx(process,
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), 0, MEM_RELEASE);
	CloseVmProcess(process, owned);
	return released ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_protect(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, bool set_maximum, std::uint32_t protection)
{
	(void)set_maximum;
	if (task == 0 || address == 0 || size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_OPERATION, owned);
	if (process == nullptr) return 4;
	DWORD old_protection = 0;
	const BOOL protected_ok = VirtualProtectEx(process,
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
		static_cast<SIZE_T>(size), VmProtection(protection), &old_protection);
	CloseVmProcess(process, owned);
	return protected_ok ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_read_overwrite(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination,
	darling_mach_vm_size_t* out_size)
{
	if (task == 0 || address == 0 || destination == 0 || out_size == nullptr ||
		size == 0 || size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_READ, owned);
	if (process == nullptr) return 4;
	SIZE_T copied = 0;
	if (!ReadProcessMemory(process,
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(destination)),
		static_cast<SIZE_T>(size), &copied)) {
		CloseVmProcess(process, owned);
		return 4;
	}
	CloseVmProcess(process, owned);
	*out_size = static_cast<darling_mach_vm_size_t>(copied);
	return copied == size ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_read(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	darling_mach_vm_size_t size, darling_mach_vm_address_t* data,
	darling_mach_vm_size_t* out_size)
{
	if (task == 0 || address == 0 || size == 0 ||
		size > (std::numeric_limits<SIZE_T>::max)() || data == nullptr ||
		out_size == nullptr)
		return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_READ, owned);
	if (process == nullptr) return 4;
	void* buffer = VirtualAlloc(nullptr, static_cast<SIZE_T>(size),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (buffer == nullptr) {
		CloseVmProcess(process, owned);
		return 3;
	}
	SIZE_T copied = 0;
	const BOOL read_ok = ReadProcessMemory(process,
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), buffer,
		static_cast<SIZE_T>(size), &copied);
	CloseVmProcess(process, owned);
	if (!read_ok || copied != size) {
		VirtualFree(buffer, 0, MEM_RELEASE);
		return 4;
	}
	*data = static_cast<darling_mach_vm_address_t>(
		reinterpret_cast<std::uintptr_t>(buffer));
	*out_size = static_cast<darling_mach_vm_size_t>(copied);
	{
		std::lock_guard lock(read_buffers_mutex);
		read_buffers.emplace(*data, static_cast<SIZE_T>(copied));
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_write(
	darling_mach_port_name_t task, darling_mach_vm_address_t address,
	const void* data, darling_mach_vm_size_t size)
{
	if (task == 0 || address == 0 || data == nullptr || size == 0 ||
		size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_VM_WRITE | PROCESS_VM_OPERATION, owned);
	if (process == nullptr) return 4;
	SIZE_T copied = 0;
	if (!WriteProcessMemory(process,
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), data,
		static_cast<SIZE_T>(size), &copied)) {
		CloseVmProcess(process, owned);
		return 4;
	}
	CloseVmProcess(process, owned);
	return copied == size ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_copy(
	darling_mach_port_name_t task, darling_mach_vm_address_t source,
	darling_mach_vm_size_t size, darling_mach_vm_address_t destination)
{
	if (task == 0 || source == 0 || destination == 0 || size == 0 ||
		size > (std::numeric_limits<SIZE_T>::max)()) return 4;
	bool source_owned = false;
	const HANDLE source_process = OpenVmProcess(task, PROCESS_VM_READ, source_owned);
	bool destination_owned = false;
	const HANDLE destination_process = OpenVmProcess(task,
		PROCESS_VM_WRITE | PROCESS_VM_OPERATION, destination_owned);
	if (source_process == nullptr || destination_process == nullptr) {
		CloseVmProcess(source_process, source_owned);
		CloseVmProcess(destination_process, destination_owned);
		return 4;
	}
	std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
	SIZE_T copied = 0;
	if (!ReadProcessMemory(source_process,
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(source)),
		buffer.data(), static_cast<SIZE_T>(size), &copied) || copied != size) {
		CloseVmProcess(source_process, source_owned);
		CloseVmProcess(destination_process, destination_owned);
		return 4;
	}
	if (!WriteProcessMemory(destination_process,
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(destination)),
		buffer.data(), static_cast<SIZE_T>(size), &copied) || copied != size) {
		CloseVmProcess(source_process, source_owned);
		CloseVmProcess(destination_process, destination_owned);
		return 4;
	}
	CloseVmProcess(source_process, source_owned);
	CloseVmProcess(destination_process, destination_owned);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_region(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t* size, std::uint32_t flavor, void* info,
	std::uint32_t* info_count)
{
	if (task == 0 || address == nullptr || size == nullptr || info == nullptr ||
		info_count == nullptr || flavor != darling_vm_region_basic_info ||
		*info_count < darling_vm_region_basic_info_count)
		return 4;
	bool owned = false;
	const HANDLE process = OpenVmProcess(task, PROCESS_QUERY_LIMITED_INFORMATION,
		owned);
	if (process == nullptr) return 3;
	MEMORY_BASIC_INFORMATION region{};
	const SIZE_T queried = VirtualQueryEx(process,
		reinterpret_cast<const void*>(static_cast<std::uintptr_t>(*address)),
		&region, sizeof(region));
	CloseVmProcess(process, owned);
	if (queried != sizeof(region)) return 4;
	const DWORD protection = region.Protect == 0 ? region.AllocationProtect : region.Protect;
	std::int32_t darwin_protection = 0;
	if ((protection & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0)
		darwin_protection |= darling_vm_prot_read;
	if ((protection & (PAGE_READWRITE | PAGE_WRITECOPY |
		PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0)
		darwin_protection |= darling_vm_prot_write;
	if ((protection & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
		PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0)
		darwin_protection |= darling_vm_prot_execute;
	auto* basic = static_cast<darling_mach_vm_region_basic_info*>(info);
	*basic = darling_mach_vm_region_basic_info{
		darwin_protection, darwin_protection, 2, 0, 0, 0, 0, 0};
	*address = static_cast<darling_mach_vm_address_t>(
		reinterpret_cast<std::uintptr_t>(region.BaseAddress));
	*size = static_cast<darling_mach_vm_size_t>(region.RegionSize);
	*info_count = darling_vm_region_basic_info_count;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_vm_region_recurse(
	darling_mach_port_name_t task, darling_mach_vm_address_t* address,
	darling_mach_vm_size_t* size, std::uint32_t* depth, void* info,
	std::uint32_t* info_count)
{
	if (depth == nullptr || *depth != 0) return 4;
	const auto result = darling_windows_mach_vm_region(task, address, size,
		darling_vm_region_basic_info, info, info_count);
	if (result == 0) *depth = 0;
	return result;
}

extern "C" darling_kern_return_t darling_windows_mach_port_deallocate(
	darling_mach_port_name_t task, darling_mach_port_name_t name)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0) return 4; // KERN_INVALID_ARGUMENT
	if (name == darling_windows_mach_task_self() ||
		name == darling_windows_mach_thread_self() ||
		name == darling_windows_mach_host_self())
		return 0;
	std::shared_ptr<PortQueue> port;
	{
		std::lock_guard lock(ports_mutex);
		const auto found = ports.find(name);
		if (found == ports.end()) return 3;
		port = found->second;
		{
			std::lock_guard port_lock(port->mutex);
			if (port->refs == 0) return 3;
			if (port->send_refs != 0) --port->send_refs;
			else if (port->receive_refs != 0) --port->receive_refs;
			else return 3;
			port->refs = port->receive_refs + port->send_refs;
			if (port->refs != 0) return 0;
		}
		ports.erase(found);
		for (auto& [set_name, members] : port_sets) {
			(void)set_name;
			members.erase(name);
		}
	}
	{
		std::lock_guard lock(port->mutex);
		port->closed = true;
	}
	port->condition.notify_all();
	ports_condition.notify_all();
	return 0; // Host tokens are borrowed pseudo-rights; nothing to close here.
}

extern "C" darling_kern_return_t darling_windows_mach_port_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* name)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == nullptr) return 4;
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
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0) return 4;
	if (name == darling_windows_mach_task_self() ||
		name == darling_windows_mach_thread_self() ||
		name == darling_windows_mach_host_self()) return 0;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	{
		std::lock_guard lock(port->mutex);
		port->receive_refs = 1;
		port->send_refs = 0;
		port->refs = 1;
	}
	return darling_windows_mach_port_deallocate(task, name);
}

extern "C" darling_kern_return_t darling_windows_mach_port_insert_right(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	darling_mach_port_name_t right, std::uint32_t disposition)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0 || right == 0 || disposition == 0) return 4;
	if (disposition != darling_mach_move_receive &&
		disposition != darling_mach_copy_send &&
		disposition != darling_mach_move_send &&
		disposition != darling_mach_make_send)
		return 4;
	if (FindPort(right) == nullptr && right != darling_windows_mach_task_self() &&
		right != darling_windows_mach_thread_self() &&
		right != darling_windows_mach_host_self()) return 3;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	const auto source = FindPort(right);
	{
		if (source == nullptr || disposition == darling_mach_copy_send ||
			disposition == darling_mach_make_send) {
			std::lock_guard lock(port->mutex);
			if (port->refs == (std::numeric_limits<std::uint32_t>::max)()) return 3;
			if (disposition == darling_mach_move_receive)
				++port->receive_refs;
			else
				++port->send_refs;
			port->refs = port->receive_refs + port->send_refs;
			return 0;
		}
		if (source == port) return 0;
		bool source_exhausted = false;
		std::unique_lock source_lock(source->mutex, std::defer_lock);
		std::unique_lock destination_lock(port->mutex, std::defer_lock);
		std::lock(source_lock, destination_lock);
		if (port->refs == (std::numeric_limits<std::uint32_t>::max)()) return 3;
		if (disposition == darling_mach_move_receive) {
			if (source->receive_refs == 0) return 3;
			--source->receive_refs;
			++port->receive_refs;
		} else {
			if (source->send_refs == 0) return 3;
			--source->send_refs;
			++port->send_refs;
		}
		source->refs = source->receive_refs + source->send_refs;
		port->refs = port->receive_refs + port->send_refs;
		source_exhausted = source->refs == 0;
		if (source_exhausted) source->closed = true;
		source_lock.unlock();
		destination_lock.unlock();
		if (source_exhausted) {
			std::lock_guard ports_lock(ports_mutex);
			const auto found = ports.find(right);
			if (found != ports.end() && found->second == source) {
				ports.erase(found);
				for (auto& [set_name, members] : port_sets) {
					(void)set_name;
					members.erase(right);
				}
			}
		}
		if (source_exhausted) {
			source->condition.notify_all();
			ports_condition.notify_all();
		}
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_extract_right(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t disposition, darling_mach_port_name_t* right,
	std::uint32_t* right_disposition)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0 ||
		right == nullptr || right_disposition == nullptr)
		return 4;
	if (disposition != darling_mach_move_receive &&
		disposition != darling_mach_copy_send &&
		disposition != darling_mach_move_send &&
		disposition != darling_mach_make_send)
		return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	bool exhausted = false;
	{
		std::lock_guard lock(port->mutex);
		if (disposition == darling_mach_move_receive) {
			if (port->receive_refs == 0) return 3;
			--port->receive_refs;
		} else if (disposition == darling_mach_move_send) {
			if (port->send_refs == 0) return 3;
			--port->send_refs;
		} else {
			if (port->send_refs == (std::numeric_limits<std::uint32_t>::max)()) return 3;
			++port->send_refs;
		}
		port->refs = port->receive_refs + port->send_refs;
		exhausted = port->refs == 0;
		if (exhausted) port->closed = true;
	}
	*right = name;
	*right_disposition = disposition;
	if (exhausted) {
		std::lock_guard ports_lock(ports_mutex);
		const auto found = ports.find(name);
		if (found != ports.end() && found->second == port) {
			ports.erase(found);
			for (auto& [set_name, members] : port_sets) {
				(void)set_name;
				members.erase(name);
			}
		}
		port->condition.notify_all();
		ports_condition.notify_all();
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_mod_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::int32_t delta)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0 || right == 0 || delta == 0) return 4;
	if (right != darling_mach_port_type_receive && right != darling_mach_port_type_send)
		return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	bool exhausted = false;
	{
		std::lock_guard lock(port->mutex);
		const auto current = static_cast<std::int64_t>(
			right == darling_mach_port_type_receive ? port->receive_refs : port->send_refs);
		const auto change = static_cast<std::int64_t>(delta);
		const auto updated = current + change;
		if (updated < 0 || updated > static_cast<std::int64_t>(
			(std::numeric_limits<std::uint32_t>::max)())) return 4;
		if (right == darling_mach_port_type_receive)
			port->receive_refs = static_cast<std::uint32_t>(updated);
		else
			port->send_refs = static_cast<std::uint32_t>(updated);
		port->refs = port->receive_refs + port->send_refs;
		exhausted = port->refs == 0;
		if (exhausted) port->closed = true;
	}
	if (exhausted) {
		std::lock_guard ports_lock(ports_mutex);
		const auto found = ports.find(name);
		if (found != ports.end() && found->second == port) {
			ports.erase(found);
			for (auto& [set_name, members] : port_sets) {
				(void)set_name;
				members.erase(name);
			}
		}
		port->condition.notify_all();
		ports_condition.notify_all();
	}
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_get_refs(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t right, std::uint32_t* refs)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0 || right == 0 || refs == nullptr) return 4;
	if (right != darling_mach_port_type_receive && right != darling_mach_port_type_send)
		return 4;
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::lock_guard lock(port->mutex);
	*refs = right == darling_mach_port_type_receive ? port->receive_refs : port->send_refs;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_type(
	darling_mach_port_name_t task, darling_mach_port_name_t name,
	std::uint32_t* type)
{
	if (task == 0 || task != darling_windows_mach_task_self() || name == 0 || type == nullptr) return 4;
	const auto port = FindPort(name);
	if (port == nullptr) {
		*type = darling_mach_port_type_none;
		return 3;
	}
	std::lock_guard lock(port->mutex);
	*type = (port->receive_refs != 0 ? darling_mach_port_type_receive : 0) |
		(port->send_refs != 0 ? darling_mach_port_type_send : 0);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_mach_port_set_allocate(
	darling_mach_port_name_t task, darling_mach_port_name_t* set)
{
	if (task == 0 || task != darling_windows_mach_task_self() || set == nullptr) return 4;
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
	if (task == 0 || task != darling_windows_mach_task_self() || member == 0 || set == 0) return 4;
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
	if (task == 0 || task != darling_windows_mach_task_self() || member == 0 || set == 0) return 4;
	std::lock_guard lock(ports_mutex);
	auto found = port_sets.find(set);
	if (found == port_sets.end()) return 3;
	const auto removed = found->second.erase(member) == 1;
	if (removed) ports_condition.notify_all();
	return removed ? 0 : 3;
}

extern "C" darling_kern_return_t darling_windows_mach_port_set_destroy(
	darling_mach_port_name_t task, darling_mach_port_name_t set)
{
	if (task == 0 || task != darling_windows_mach_task_self() || set == 0) return 4;
	std::lock_guard lock(ports_mutex);
	const auto removed = port_sets.erase(set) == 1;
	if (removed) ports_condition.notify_all();
	return removed ? 0 : 3;
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
		std::unique_lock wait_lock(ports_wait_mutex);
		ports_condition.wait_until(wait_lock, deadline);
	}
}

extern "C" darling_kern_return_t darling_windows_mach_port_send(
	darling_mach_port_name_t name, const void* data, std::uint32_t size)
{
	if (data == nullptr && size != 0) return 4;
	if (size > max_inline_message_size) return 0x10004003; // MACH_MSG_TOO_LARGE
	const auto port = FindPort(name);
	if (port == nullptr) return 3;
	std::vector<std::uint8_t> message(size);
	if (size != 0) std::memcpy(message.data(), data, size);
	{
		std::lock_guard lock(port->mutex);
		if (port->closed) return 3;
		if (port->messages.size() >= max_port_queue_depth)
			return darling_mach_send_queue_full;
		port->messages.push_back(std::move(message));
	}
	port->condition.notify_one();
	ports_condition.notify_all();
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
		[&] { return port->closed || !port->messages.empty(); }))
		return 268; // MACH_RCV_TIMED_OUT
	if (port->closed) return 3;
	const auto& queued_message = port->messages.front();
	if (queued_message.size() > capacity) return 0x10004003; // MACH_MSG_TOO_LARGE
	auto message = std::move(port->messages.front());
	port->messages.pop_front();
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
	if ((option & ~(darling_mach_send_msg | darling_mach_receive_msg)) != 0 ||
		option == 0) return 4;
	if ((option & darling_mach_send_msg) != 0) {
		if (send_size < sizeof(darling_mach_msg_header) ||
			message->msgh_remote_port == 0 || message->msgh_size != send_size) return 4;
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
