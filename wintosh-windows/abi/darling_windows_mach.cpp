/*
 * Darwin Mach C-ABI entry points backed by the Windows host layer.
 * GPL-3.0-only; see the bundled license and source manifests.
 */
#include "darling_windows_mach.h"

#include <windows.h>

#include <atomic>
#include <algorithm>
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
struct ExceptionPort final { darling_exception_mask_t mask; darling_mach_port_name_t port; darling_exception_behavior_t behavior; darling_exception_flavor_t flavor; };
std::mutex exception_ports_mutex;
std::unordered_map<darling_mach_port_name_t, std::vector<ExceptionPort>> exception_ports;
void* exception_handler_cookie = nullptr;
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

extern "C" darling_kern_return_t darling_windows_thread_policy_set(
	darling_mach_port_name_t thread, std::uint32_t flavor, const void* policy,
	std::uint32_t count)
{
	if (thread == 0 || policy == nullptr) return 4;
	HANDLE handle = thread == darling_windows_mach_thread_self() ?
		GetCurrentThread() : OpenThread(THREAD_SET_INFORMATION, FALSE, thread);
	if (handle == nullptr) return 4;
	int priority = THREAD_PRIORITY_NORMAL;
	if (flavor == darling_thread_extended_policy) {
		if (count < 1) { if (handle != GetCurrentThread()) CloseHandle(handle); return 4; }
		const auto* value = static_cast<const darling_thread_extended_policy_info*>(policy);
		priority = value->timeshare ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_ABOVE_NORMAL;
	} else if (flavor == darling_thread_precedence_policy) {
		if (count < 1) { if (handle != GetCurrentThread()) CloseHandle(handle); return 4; }
		const auto* value = static_cast<const darling_thread_precedence_policy_info*>(policy);
		priority = (std::max)(THREAD_PRIORITY_LOWEST,
			(std::min)(THREAD_PRIORITY_HIGHEST, THREAD_PRIORITY_NORMAL + value->importance));
	} else if (flavor == darling_thread_time_constraint_policy) {
		if (handle != GetCurrentThread()) CloseHandle(handle);
		return darling_kern_not_supported;
	} else {
		if (handle != GetCurrentThread()) CloseHandle(handle);
		return darling_kern_not_supported;
	}
	const BOOL applied = SetThreadPriority(handle, priority);
	if (handle != GetCurrentThread()) CloseHandle(handle);
	return applied ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_thread_policy_get(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* policy,
	std::uint32_t* count, bool* get_default)
{
	if (thread == 0 || policy == nullptr || count == nullptr || *count < 1)
		return 4;
	if (flavor == darling_thread_time_constraint_policy)
		return darling_kern_not_supported;
	if (flavor != darling_thread_extended_policy && flavor != darling_thread_precedence_policy)
		return darling_kern_not_supported;
	HANDLE handle = thread == darling_windows_mach_thread_self() ?
		GetCurrentThread() : OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread);
	if (handle == nullptr) return 4;
	const auto priority = GetThreadPriority(handle);
	if (flavor == darling_thread_extended_policy) {
		auto* result = static_cast<darling_thread_extended_policy_info*>(policy);
		result->timeshare = priority <= THREAD_PRIORITY_NORMAL ? 1 : 0;
	} else {
		auto* result = static_cast<darling_thread_precedence_policy_info*>(policy);
		result->importance = priority - THREAD_PRIORITY_NORMAL;
	}
	*count = 1;
	if (get_default != nullptr) *get_default = false;
	if (handle != GetCurrentThread()) CloseHandle(handle);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_thread_suspend(darling_mach_port_name_t thread)
{
	if (thread == 0 || thread == darling_windows_mach_thread_self()) return 4;
	const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, thread);
	if (handle == nullptr) return 4;
	const DWORD result = SuspendThread(handle);
	CloseHandle(handle);
	return result == static_cast<DWORD>(-1) ? 4 : 0;
}

extern "C" darling_kern_return_t darling_windows_thread_resume(darling_mach_port_name_t thread)
{
	if (thread == 0 || thread == darling_windows_mach_thread_self()) return 4;
	const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, thread);
	if (handle == nullptr) return 4;
	const DWORD result = ResumeThread(handle);
	CloseHandle(handle);
	return result == static_cast<DWORD>(-1) ? 4 : 0;
}

extern "C" darling_kern_return_t darling_windows_thread_get_state(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* state,
	std::uint32_t* count)
{
	if (thread == 0 || state == nullptr || count == nullptr ||
		(flavor != darling_x86_thread_state64_flavor && flavor != darling_x86_float_state64_flavor && flavor != darling_x86_avx_state64_flavor && flavor != darling_x86_avx512_state64_flavor && flavor != darling_x86_exception_state64_flavor && flavor != darling_x86_debug_state64_flavor) ||
		*count < (flavor == darling_x86_thread_state64_flavor ? darling_x86_thread_state64_count : flavor == darling_x86_float_state64_flavor ? darling_x86_float_state64_count : flavor == darling_x86_avx_state64_flavor ? darling_x86_avx_state64_count : flavor == darling_x86_avx512_state64_flavor ? darling_x86_avx512_state64_count : flavor == darling_x86_exception_state64_flavor ? darling_x86_exception_state64_count : darling_x86_debug_state64_count))
		return 4;
	if (flavor == darling_x86_exception_state64_flavor) {
		auto* result = static_cast<darling_x86_exception_state64*>(state);
		result->cpu = static_cast<std::uint16_t>(GetCurrentProcessorNumber());
		*count = darling_x86_exception_state64_count;
		return 0;
	}
	if (flavor == darling_x86_debug_state64_flavor) {
		HANDLE debug_handle = thread == darling_windows_mach_thread_self() ? GetCurrentThread() : OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, thread);
		if (debug_handle == nullptr) return 4;
		const bool owned_debug = debug_handle != GetCurrentThread();
		if (owned_debug && SuspendThread(debug_handle) == static_cast<DWORD>(-1)) { CloseHandle(debug_handle); return 4; }
		CONTEXT debug_context{}; debug_context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		const BOOL ok = GetThreadContext(debug_handle, &debug_context);
		if (owned_debug) ResumeThread(debug_handle); if (owned_debug) CloseHandle(debug_handle);
		if (!ok) return 4;
		auto* result = static_cast<darling_x86_debug_state64*>(state);
		result->dr0 = debug_context.Dr0; result->dr1 = debug_context.Dr1; result->dr2 = debug_context.Dr2; result->dr3 = debug_context.Dr3;
		result->dr6 = debug_context.Dr6; result->dr7 = debug_context.Dr7;
		*count = darling_x86_debug_state64_count;
		return 0;
	}
	if (flavor == darling_x86_avx_state64_flavor || flavor == darling_x86_avx512_state64_flavor) {
		if (thread == darling_windows_mach_thread_self()) return darling_kern_not_supported;
		DWORD context_length = 0;
		if (InitializeContext(nullptr, CONTEXT_FULL | CONTEXT_XSTATE, nullptr, &context_length) ||
			context_length == 0) return 4;
		std::vector<std::uint8_t> context_storage(context_length);
		PCONTEXT xstate_context = nullptr;
		if (!InitializeContext(context_storage.data(), CONTEXT_FULL | CONTEXT_XSTATE,
			&xstate_context, &context_length) || xstate_context == nullptr) return 4;
		const HANDLE xstate_handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, thread);
		if (xstate_handle == nullptr) return 4;
		if (SuspendThread(xstate_handle) == static_cast<DWORD>(-1) || !GetThreadContext(xstate_handle, xstate_context)) {
			ResumeThread(xstate_handle); CloseHandle(xstate_handle); return 4;
		}
		DWORD64 features = 0;
		const auto* legacy = LocateXStateFeature(xstate_context, XSTATE_LEGACY_SSE, nullptr);
		const auto* avx = LocateXStateFeature(xstate_context, XSTATE_AVX, nullptr);
		const auto* kmask = LocateXStateFeature(xstate_context, XSTATE_AVX512_KMASK, nullptr);
		const auto* zmmh = LocateXStateFeature(xstate_context, XSTATE_AVX512_ZMM_H, nullptr);
		const auto* zmm = LocateXStateFeature(xstate_context, XSTATE_AVX512_ZMM, nullptr);
		const bool supported = GetXStateFeaturesMask(xstate_context, &features) &&
			(features & XSTATE_MASK_AVX) != 0 && legacy != nullptr && avx != nullptr &&
			(flavor == darling_x86_avx_state64_flavor || ((features & XSTATE_MASK_AVX512) == XSTATE_MASK_AVX512 && kmask != nullptr && zmmh != nullptr && zmm != nullptr));
		if (supported) {
			static_assert(sizeof(XSAVE_FORMAT) == darling_x86_float_state64_count * sizeof(std::uint32_t));
			std::memcpy(state, legacy, sizeof(XSAVE_FORMAT));
			std::memcpy(static_cast<std::uint8_t*>(state) + sizeof(XSAVE_FORMAT) + 64, avx, 16 * 16);
			if (flavor == darling_x86_avx512_state64_flavor) {
				auto* output = static_cast<std::uint8_t*>(state);
				std::memcpy(output + 832, kmask, 64);
				std::memcpy(output + 896, zmmh, 512);
				std::memcpy(output + 1408, zmm, 1024);
			}
		}
		ResumeThread(xstate_handle); CloseHandle(xstate_handle);
		if (!supported) return darling_kern_not_supported;
		*count = flavor == darling_x86_avx_state64_flavor ? darling_x86_avx_state64_count : darling_x86_avx512_state64_count;
		return 0;
	}
	CONTEXT context{};
		context.ContextFlags = (flavor == darling_x86_avx_state64_flavor || flavor == darling_x86_avx512_state64_flavor) ? CONTEXT_FULL | CONTEXT_XSTATE : flavor == darling_x86_float_state64_flavor ? CONTEXT_FLOATING_POINT : CONTEXT_CONTROL | CONTEXT_INTEGER;
	HANDLE handle = nullptr;
	bool suspended = false;
	bool owned = false;
	if (thread == darling_windows_mach_thread_self()) {
		RtlCaptureContext(&context);
	} else {
		handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, thread);
		if (handle == nullptr) return 4;
		owned = true;
		if (SuspendThread(handle) == static_cast<DWORD>(-1) || !GetThreadContext(handle, &context)) {
			ResumeThread(handle);
			CloseHandle(handle);
			return 4;
		}
		suspended = true;
	}
	if (flavor == darling_x86_float_state64_flavor) {
		static_assert(sizeof(context.FltSave) == darling_x86_float_state64_count * sizeof(std::uint32_t));
		std::memcpy(state, &context.FltSave, sizeof(context.FltSave));
		*count = darling_x86_float_state64_count;
		if (suspended) ResumeThread(handle);
		if (owned) CloseHandle(handle);
		return 0;
	}
	if (flavor == darling_x86_avx_state64_flavor) {
		DWORD64 features = 0;
		if (!GetXStateFeaturesMask(&context, &features) || (features & XSTATE_MASK_AVX) == 0) {
			if (suspended) ResumeThread(handle);
			if (owned) CloseHandle(handle);
			return darling_kern_not_supported;
		}
		const auto* legacy = LocateXStateFeature(&context, XSTATE_LEGACY_SSE, nullptr);
		const auto* avx = LocateXStateFeature(&context, XSTATE_AVX, nullptr);
		if (legacy == nullptr || avx == nullptr) {
			if (suspended) ResumeThread(handle);
			if (owned) CloseHandle(handle);
			return 4;
		}
		static_assert(sizeof(context.FltSave) == darling_x86_float_state64_count * sizeof(std::uint32_t));
		std::memcpy(state, legacy, sizeof(context.FltSave));
		std::memcpy(static_cast<std::uint8_t*>(state) + sizeof(context.FltSave) + 64, avx, 16 * 16);
		*count = darling_x86_avx_state64_count;
		if (suspended) ResumeThread(handle);
		if (owned) CloseHandle(handle);
		return 0;
	}
	auto* result = static_cast<darling_x86_thread_state64*>(state);
	result->rax = context.Rax; result->rbx = context.Rbx; result->rcx = context.Rcx; result->rdx = context.Rdx;
	result->rdi = context.Rdi; result->rsi = context.Rsi; result->rbp = context.Rbp; result->rsp = context.Rsp;
	result->r8 = context.R8; result->r9 = context.R9; result->r10 = context.R10; result->r11 = context.R11;
	result->r12 = context.R12; result->r13 = context.R13; result->r14 = context.R14; result->r15 = context.R15;
	result->rip = context.Rip; result->rflags = context.EFlags; result->cs = context.SegCs;
	result->fs = context.SegFs; result->gs = context.SegGs;
	*count = darling_x86_thread_state64_count;
	if (suspended) ResumeThread(handle);
	if (owned) CloseHandle(handle);
	return 0;
}

extern "C" darling_kern_return_t darling_windows_thread_set_state(
	darling_mach_port_name_t thread, std::uint32_t flavor, const void* state,
	std::uint32_t count)
{
	if (thread == 0 || thread == darling_windows_mach_thread_self() ||
		(flavor != darling_x86_thread_state64_flavor && flavor != darling_x86_float_state64_flavor && flavor != darling_x86_debug_state64_flavor) || state == nullptr ||
		count < (flavor == darling_x86_thread_state64_flavor ? darling_x86_thread_state64_count : flavor == darling_x86_float_state64_flavor ? darling_x86_float_state64_count : darling_x86_debug_state64_count))
		return 4;
	const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT, FALSE, thread);
	if (handle == nullptr) return 4;
	if (SuspendThread(handle) == static_cast<DWORD>(-1)) {
		CloseHandle(handle);
		return 4;
	}
	CONTEXT context{};
	context.ContextFlags = flavor == darling_x86_debug_state64_flavor ? CONTEXT_DEBUG_REGISTERS : flavor == darling_x86_float_state64_flavor ? CONTEXT_FULL : CONTEXT_CONTROL | CONTEXT_INTEGER;
	if (!GetThreadContext(handle, &context)) {
		ResumeThread(handle); CloseHandle(handle); return 4;
	}
	if (flavor == darling_x86_debug_state64_flavor) {
		const auto* source = static_cast<const darling_x86_debug_state64*>(state);
		context.Dr0 = source->dr0; context.Dr1 = source->dr1; context.Dr2 = source->dr2; context.Dr3 = source->dr3;
		context.Dr6 = source->dr6; context.Dr7 = source->dr7;
		const BOOL applied = SetThreadContext(handle, &context); ResumeThread(handle); CloseHandle(handle); return applied ? 0 : 4;
	}
	if (flavor == darling_x86_float_state64_flavor) {
		static_assert(sizeof(context.FltSave) == darling_x86_float_state64_count * sizeof(std::uint32_t));
		const auto* source = static_cast<const darling_x86_float_state64*>(state);
		std::memcpy(&context.FltSave, source, sizeof(context.FltSave));
		const BOOL applied = SetThreadContext(handle, &context);
		ResumeThread(handle); CloseHandle(handle);
		return applied ? 0 : 4;
	}
	const auto* source = static_cast<const darling_x86_thread_state64*>(state);
	context.Rax = source->rax; context.Rbx = source->rbx; context.Rcx = source->rcx; context.Rdx = source->rdx;
	context.Rdi = source->rdi; context.Rsi = source->rsi; context.Rbp = source->rbp; context.Rsp = source->rsp;
	context.R8 = source->r8; context.R9 = source->r9; context.R10 = source->r10; context.R11 = source->r11;
	context.R12 = source->r12; context.R13 = source->r13; context.R14 = source->r14; context.R15 = source->r15;
	context.Rip = source->rip; context.EFlags = static_cast<DWORD>(source->rflags);
	context.SegCs = static_cast<WORD>(source->cs); context.SegFs = static_cast<WORD>(source->fs);
	context.SegGs = static_cast<WORD>(source->gs);
	const BOOL applied = SetThreadContext(handle, &context);
	ResumeThread(handle);
	CloseHandle(handle);
	return applied ? 0 : 4;
}

extern "C" darling_kern_return_t darling_windows_thread_set_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_mach_port_name_t port, darling_exception_behavior_t behavior,
	darling_exception_flavor_t flavor)
{
	if (thread == 0 || mask == 0) return 4;
	std::lock_guard lock(exception_ports_mutex);
	auto& entries = exception_ports[thread];
	entries.erase(std::remove_if(entries.begin(), entries.end(), [mask](const ExceptionPort& entry) { return (entry.mask & mask) != 0; }), entries.end());
	if (port != 0) entries.push_back({mask, port, behavior, flavor});
	return 0;
}

extern "C" darling_kern_return_t darling_windows_thread_get_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_exception_mask_t* masks, std::uint32_t* masks_count,
	darling_mach_port_name_t* handlers, darling_exception_behavior_t* behaviors,
	darling_exception_flavor_t* flavors)
{
	if (thread == 0 || mask == 0 || masks == nullptr || masks_count == nullptr ||
		handlers == nullptr || behaviors == nullptr || flavors == nullptr || *masks_count == 0)
		return 4;
	std::lock_guard lock(exception_ports_mutex);
	const auto found = exception_ports.find(thread);
	if (found == exception_ports.end()) { *masks_count = 0; return 0; }
	const auto capacity = *masks_count;
	std::uint32_t written = 0;
	for (const auto& entry : found->second) {
		if ((entry.mask & mask) == 0 || written == capacity) continue;
		masks[written] = entry.mask; handlers[written] = entry.port;
		behaviors[written] = entry.behavior; flavors[written] = entry.flavor; ++written;
	}
	*masks_count = written;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_thread_swap_exception_ports(
	darling_mach_port_name_t thread, darling_exception_mask_t mask,
	darling_mach_port_name_t new_port, darling_exception_behavior_t new_behavior,
	darling_exception_flavor_t new_flavor, darling_exception_mask_t* masks,
	std::uint32_t* masks_count, darling_mach_port_name_t* handlers,
	darling_exception_behavior_t* behaviors, darling_exception_flavor_t* flavors)
{
	if (thread == 0 || mask == 0 || masks == nullptr || masks_count == nullptr ||
		handlers == nullptr || behaviors == nullptr || flavors == nullptr || *masks_count == 0)
		return 4;
	std::lock_guard lock(exception_ports_mutex);
	const auto capacity = *masks_count;
	std::uint32_t written = 0;
	auto& entries = exception_ports[thread];
	for (auto it = entries.begin(); it != entries.end();) {
		if ((it->mask & mask) == 0 || written == capacity) { ++it; continue; }
		masks[written] = it->mask; handlers[written] = it->port;
		behaviors[written] = it->behavior; flavors[written] = it->flavor; ++written;
		it = entries.erase(it);
	}
	if (new_port != 0) entries.push_back({mask, new_port, new_behavior, new_flavor});
	*masks_count = written;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_thread_get_exception_ports_info(
	darling_mach_port_name_t port, darling_exception_mask_t mask,
	darling_exception_mask_t* masks, std::uint32_t* masks_count,
	darling_exception_handler_info* handlers_info,
	darling_exception_behavior_t* behaviors, darling_exception_flavor_t* flavors)
{
	if (port == 0 || mask == 0 || masks == nullptr || masks_count == nullptr ||
		handlers_info == nullptr || behaviors == nullptr || flavors == nullptr || *masks_count == 0)
		return 4;
	std::lock_guard lock(exception_ports_mutex);
	const auto capacity = *masks_count;
	std::uint32_t written = 0;
	for (const auto& [thread, entries] : exception_ports) {
		(void)thread;
		for (const auto& entry : entries) {
			if (entry.port != port || (entry.mask & mask) == 0 || written == capacity) continue;
			masks[written] = entry.mask;
			handlers_info[written] = {entry.port, 0};
			behaviors[written] = entry.behavior; flavors[written] = entry.flavor; ++written;
		}
	}
	*masks_count = written;
	return 0;
}

extern "C" darling_kern_return_t darling_windows_dispatch_mach_exception(
	std::uint32_t exception_type, std::uint64_t code0, std::uint64_t code1)
{
	const auto thread = darling_windows_mach_thread_self();
	const auto mask = exception_type < 32 ? (static_cast<darling_exception_mask_t>(1) << exception_type) : 0;
	if (mask == 0) return 4;
	std::vector<ExceptionPort> targets;
	{
		std::lock_guard lock(exception_ports_mutex);
		const auto found = exception_ports.find(thread);
		if (found == exception_ports.end()) return 4;
		for (const auto& entry : found->second)
			if ((entry.mask & mask) != 0) targets.push_back(entry);
	}
	if (targets.empty()) return 4;
	darling_mach_exception_message message{};
	message.header.msgh_size = sizeof(message);
	message.header.msgh_remote_port = targets.front().port;
	message.exception_type = exception_type;
	message.code0 = code0; message.code1 = code1; message.thread = thread;
	message.behavior = targets.front().behavior; message.flavor = targets.front().flavor;
	return darling_windows_mach_port_send(targets.front().port, &message, sizeof(message));
}

namespace {
LONG CALLBACK DarlingVectoredExceptionHandler(PEXCEPTION_POINTERS exception)
{
	if (exception == nullptr || exception->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
	std::uint32_t type = 0;
	switch (exception->ExceptionRecord->ExceptionCode) {
	case EXCEPTION_ACCESS_VIOLATION: type = 1; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION: type = 2; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO: type = 3; break;
	default: return EXCEPTION_CONTINUE_SEARCH;
	}
	const auto address = reinterpret_cast<std::uint64_t>(exception->ExceptionRecord->ExceptionAddress);
	const auto code = exception->ExceptionRecord->NumberParameters != 0 ?
		exception->ExceptionRecord->ExceptionInformation[0] : 0;
	(void)darling_windows_dispatch_mach_exception(type, code, address);
	return EXCEPTION_CONTINUE_SEARCH;
}
}

extern "C" darling_kern_return_t darling_windows_set_exception_dispatch_enabled(bool enabled)
{
	if (enabled) {
		if (exception_handler_cookie != nullptr) return 0;
		exception_handler_cookie = AddVectoredExceptionHandler(1, DarlingVectoredExceptionHandler);
		return exception_handler_cookie == nullptr ? 4 : 0;
	}
	if (exception_handler_cookie != nullptr) {
		RemoveVectoredExceptionHandler(exception_handler_cookie);
		exception_handler_cookie = nullptr;
	}
	return 0;
}

namespace {
bool OpenThreadForQuery(darling_mach_port_name_t thread, HANDLE& handle, bool& owned)
{
	owned = false;
	if (thread == 0) return false;
	if (thread == darling_windows_mach_thread_self()) {
		handle = GetCurrentThread();
		return true;
	}
	handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread);
	owned = handle != nullptr;
	return handle != nullptr;
}

darling_time_value FileTimeToDarling(const FILETIME& value)
{
	ULARGE_INTEGER ticks{};
	ticks.LowPart = value.dwLowDateTime;
	ticks.HighPart = value.dwHighDateTime;
	const auto micros = ticks.QuadPart / 10;
	return {static_cast<std::int32_t>(micros / 1000000),
		static_cast<std::int32_t>(micros % 1000000)};
}
}

extern "C" darling_kern_return_t darling_windows_thread_info(
	darling_mach_port_name_t thread, std::uint32_t flavor, void* info,
	std::uint32_t* count)
{
	if (info == nullptr || count == nullptr) return 4;
	const auto required = flavor == darling_thread_basic_info_flavor ?
		darling_thread_basic_info_count : flavor == darling_thread_identifier_info_flavor ?
		darling_thread_identifier_info_count : flavor == darling_thread_extended_info_flavor ?
		darling_thread_extended_info_count : flavor == darling_thread_sched_timeshare_info_flavor ?
		darling_thread_sched_timeshare_info_count : flavor == darling_thread_sched_rr_info_flavor ?
		darling_thread_sched_rr_info_count : flavor == darling_thread_sched_fifo_info_flavor ?
		darling_thread_sched_fifo_info_count : 0;
	if (required == 0 || *count < required) return 4;
	HANDLE handle = nullptr;
	bool owned = false;
	if (!OpenThreadForQuery(thread, handle, owned)) return 4;
	if (flavor == darling_thread_identifier_info_flavor) {
		auto* result = static_cast<darling_thread_identifier_info*>(info);
		result->thread_id = static_cast<std::uint64_t>(thread);
		result->thread_handle = static_cast<std::uint64_t>(thread);
		result->dispatch_qaddr = 0;
	} else {
		FILETIME creation{}, exit{}, kernel{}, user{};
		if (!GetThreadTimes(handle, &creation, &exit, &kernel, &user)) {
			if (owned) CloseHandle(handle);
			return 4;
		}
		if (flavor == darling_thread_sched_timeshare_info_flavor || flavor == darling_thread_sched_rr_info_flavor ||
			flavor == darling_thread_sched_fifo_info_flavor) {
			const auto priority = GetThreadPriority(handle);
			if (flavor == darling_thread_sched_timeshare_info_flavor) {
				auto* result = static_cast<darling_thread_sched_timeshare_info*>(info);
				result->max_priority = THREAD_PRIORITY_HIGHEST;
				result->base_priority = priority;
				result->current_priority = priority;
			} else if (flavor == darling_thread_sched_rr_info_flavor) {
				auto* result = static_cast<darling_thread_sched_rr_info*>(info);
				result->max_priority = THREAD_PRIORITY_HIGHEST;
				result->base_priority = priority;
			} else {
				auto* result = static_cast<darling_thread_sched_fifo_info*>(info);
				result->max_priority = THREAD_PRIORITY_HIGHEST;
				result->base_priority = priority;
			}
		} else if (flavor == darling_thread_extended_info_flavor) {
			auto* result = static_cast<darling_thread_extended_info*>(info);
			ULARGE_INTEGER user_ticks{}, kernel_ticks{};
			user_ticks.LowPart = user.dwLowDateTime; user_ticks.HighPart = user.dwHighDateTime;
			kernel_ticks.LowPart = kernel.dwLowDateTime; kernel_ticks.HighPart = kernel.dwHighDateTime;
			result->user_time = user_ticks.QuadPart * 100;
			result->system_time = kernel_ticks.QuadPart * 100;
			result->current_priority = GetThreadPriority(handle);
			result->priority = result->current_priority;
			result->max_priority = THREAD_PRIORITY_HIGHEST;
		} else {
			auto* result = static_cast<darling_thread_basic_info*>(info);
			result->user_time = FileTimeToDarling(user);
			result->system_time = FileTimeToDarling(kernel);
		}
	}
	*count = required;
	if (owned) CloseHandle(handle);
	return 0;
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
