/* Darwin libdispatch queue subset for the Windows host. GPL-3.0-only. */
#include "darling_windows_dispatch.h"
#include "darling_windows_objc.h"
#include "darling_windows_mach.h"
#include "../platform/darling_windows_signals.h"
#include "../platform/darling_windows_syscalls.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" const unsigned char _dispatch_source_type_timer = 0;
extern "C" const unsigned char _dispatch_source_type_data_add = 0;
extern "C" const unsigned char _dispatch_source_type_data_or = 0;
extern "C" const unsigned char _dispatch_source_type_data_replace = 0;
extern "C" const unsigned char _dispatch_source_type_read = 0;
extern "C" const unsigned char _dispatch_source_type_write = 0;
extern "C" const unsigned char _dispatch_source_type_proc = 0;
extern "C" const unsigned char _dispatch_source_type_signal = 0;
extern "C" const unsigned char _dispatch_source_type_vnode = 0;
extern "C" const unsigned char _dispatch_source_type_mach_recv = 0;
extern "C" const unsigned char _dispatch_source_type_mach_send = 0;
extern "C" const unsigned char _dispatch_queue_attr_concurrent = 0;
extern "C" int darling_windows_dispatch_poll_descriptor(int descriptor,
	int writable, std::uint64_t* data);
extern "C" HANDLE darling_windows_dispatch_duplicate_vnode(int descriptor);
extern "C" int darling_windows_dispatch_snapshot_vnode(HANDLE handle,
	darling::windows_host::DarwinVnodeSnapshot* snapshot);

namespace {
enum class DispatchSourceKind : std::uint8_t {
	Timer,
	DataAdd,
	DataOr,
	DataReplace,
	Read,
	Write,
	Process,
	Signal,
	Vnode,
	MachReceive,
	MachSend,
};

constexpr std::uint64_t darling_dispatch_proc_exit = 0x80000000ull;
constexpr std::uint64_t darling_dispatch_mach_send_dead = 0x1ull;
constexpr darling_mach_msg_id_t darling_dispatch_mach_notify_dead_name = 0x48;
constexpr std::uint64_t darling_dispatch_vnode_delete = 0x01ull;
constexpr std::uint64_t darling_dispatch_vnode_write = 0x02ull;
constexpr std::uint64_t darling_dispatch_vnode_extend = 0x04ull;
constexpr std::uint64_t darling_dispatch_vnode_attrib = 0x08ull;
constexpr std::uint64_t darling_dispatch_vnode_link = 0x10ull;
constexpr std::uint64_t darling_dispatch_vnode_rename = 0x20ull;
constexpr std::uint64_t darling_dispatch_vnode_revoke = 0x40ull;
constexpr std::uint64_t darling_dispatch_vnode_supported =
	darling_dispatch_vnode_delete | darling_dispatch_vnode_write |
	darling_dispatch_vnode_extend | darling_dispatch_vnode_attrib |
	darling_dispatch_vnode_link | darling_dispatch_vnode_rename |
	darling_dispatch_vnode_revoke;

using DispatchDestructor = darling_dispatch_function_t;
struct DispatchSpecificValue final {
	void* context{};
	DispatchDestructor destructor{};
};

struct DispatchWorkItem final {
	std::function<void()> function;
	bool barrier{};
};

struct DispatchState final {
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<DispatchWorkItem> pending;
	std::mutex specific_mutex;
	std::unordered_map<const void*, DispatchSpecificValue> specific;
	std::string label;
	bool stopping{};
	bool main_queue{};
	bool global_queue{};
	bool concurrent_queue{};
	bool barrier_running{};
	std::size_t running_tasks{};
	std::size_t worker_limit{1};
	~DispatchState()
	{
		for (const auto& [key, value] : specific) {
			(void)key;
			if (value.context != nullptr && value.destructor != nullptr) {
				try { value.destructor(value.context); } catch (...) {}
			}
		}
	}
};

struct DispatchGroupState final {
	std::mutex mutex;
	std::condition_variable condition;
	std::uint64_t pending{};
	std::vector<std::pair<std::shared_ptr<DispatchState>,
		std::function<void()>>> notifications;
};

struct DispatchGroup final {
	std::shared_ptr<DispatchGroupState> state;
	std::atomic<std::uint32_t> references{1};
};

struct DispatchSourceState final {
	std::mutex mutex;
	std::mutex callback_mutex;
	std::condition_variable condition;
	std::shared_ptr<DispatchState> queue;
	darling_dispatch_function_t event_function{};
	darling_dispatch_function_t cancel_function{};
	void* event_block{};
	void* cancel_block{};
	void* context{};
	std::uint64_t deadline{darling_dispatch_time_forever};
	std::uint64_t interval{};
	std::uint64_t generation{};
	std::uint64_t current_data{};
	std::uint64_t pending_data{};
	std::atomic<std::uint64_t> data{};
	DispatchSourceKind kind{DispatchSourceKind::Timer};
	HANDLE process_handle{};
	HANDLE vnode_handle{};
	std::uint64_t signal_subscription{};
	bool mach_send_retained{};
	darling_mach_port_name_t mach_send_notification_port{};
	bool mach_send_notification_registered{};
	std::uint64_t handle{};
	std::uint64_t mask{};
	std::uint32_t suspend_count{1};
	bool active{};
	bool cancelled{};
	bool event_pending{};
	bool event_running{};
	bool cancel_handler_submitted{};
	~DispatchSourceState()
	{
		if (process_handle != nullptr) CloseHandle(process_handle);
		if (vnode_handle != nullptr) CloseHandle(vnode_handle);
		if (mach_send_retained)
			(void)darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_send, -1);
		if (mach_send_notification_registered) {
			darling_mach_port_name_t previous{};
			(void)darling_windows_mach_port_request_notification(
				darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_dispatch_mach_notify_dead_name, 0, &previous);
		}
		if (mach_send_notification_port != 0)
			(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(),
				mach_send_notification_port);
		if (event_block != nullptr) objc_releaseBlock(event_block);
		if (cancel_block != nullptr) objc_releaseBlock(cancel_block);
	}
};

struct DispatchSource final {
	std::shared_ptr<DispatchSourceState> state;
	darling_dispatch_queue_t queue_owner{};
	std::atomic<std::uint32_t> references{1};
	~DispatchSource()
	{
		if (queue_owner != nullptr)
			darling_windows_dispatch_release(queue_owner);
	}
};

struct DispatchQueue final {
	std::shared_ptr<DispatchState> state;
	std::atomic<std::uint32_t> references{1};
	bool immortal{};
};

struct DispatchSemaphore final {
	std::mutex mutex;
	std::condition_variable condition;
	std::int64_t value{};
	std::uint64_t waiting{};
	bool disposed{};
};

struct DarwinBlockHeader final {
	void* isa{};
	std::int32_t flags{};
	std::int32_t reserved{};
	void* invoke{};
	const void* descriptor{};
};

thread_local DispatchState* current_dispatch_state{};
std::atomic<std::uint32_t> main_thread_id{};
struct DispatchOnceState final {
	std::mutex mutex;
	std::condition_variable condition;
	std::thread::id owner{};
	bool running{};
	bool complete{};
};
std::mutex once_states_mutex;
std::unordered_map<void*, std::shared_ptr<DispatchOnceState>> once_states;
std::mutex dispatch_groups_mutex;
std::unordered_set<void*> dispatch_groups;
std::unordered_set<void*> dispatch_sources;

bool Enqueue(const std::shared_ptr<DispatchState>& state,
	std::function<void()> task, bool barrier = false);
void InvokeBlock(void* raw_block);
std::uint64_t SteadyNanoseconds();
void RunSourceEvent(const std::shared_ptr<DispatchSourceState>& state);

struct DelayedTask final {
	std::uint64_t deadline{};
	std::uint64_t order{};
	std::shared_ptr<DispatchState> queue;
	std::function<void()> work;
};

struct DelayedTaskLater final {
	bool operator()(const DelayedTask& left, const DelayedTask& right) const
	{
		return left.deadline != right.deadline
			? left.deadline > right.deadline : left.order > right.order;
	}
};

class DispatchAfterScheduler final {
public:
	DispatchAfterScheduler() { std::thread([this] { Run(); }).detach(); }
	bool Add(std::uint64_t deadline, std::shared_ptr<DispatchState> queue,
		std::function<void()> work)
	{
		try {
			{
				std::lock_guard lock(mutex);
				if (tasks.size() >= 1'000'000) return false;
				tasks.push({deadline, next_order++, std::move(queue),
					std::move(work)});
				++revision;
			}
			condition.notify_one();
			return true;
		} catch (...) { return false; }
	}
	static DispatchAfterScheduler& Instance()
	{
		static auto* scheduler = new DispatchAfterScheduler();
		return *scheduler;
	}
private:
	void Run()
	{
		std::unique_lock lock(mutex);
		for (;;) {
			if (tasks.empty()) {
				condition.wait(lock, [&] { return !tasks.empty(); });
				continue;
			}
			const auto observed_revision = revision;
			const auto deadline = tasks.top().deadline;
			if (deadline == darling_dispatch_time_forever) {
				condition.wait(lock, [&] { return revision != observed_revision; });
				continue;
			}
			const auto now = SteadyNanoseconds();
			if (deadline > now) {
				const auto remaining = (std::min)(deadline - now,
					static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()));
				condition.wait_for(lock, std::chrono::nanoseconds(
					static_cast<std::int64_t>(remaining)), [&] {
						return revision != observed_revision;
					});
				continue;
			}
			auto task = tasks.top();
			tasks.pop();
			lock.unlock();
			(void)Enqueue(task.queue, std::move(task.work));
			lock.lock();
		}
	}
	std::mutex mutex;
	std::condition_variable condition;
	std::priority_queue<DelayedTask, std::vector<DelayedTask>,
		DelayedTaskLater> tasks;
	std::uint64_t revision{};
	std::uint64_t next_order{};
};

void SubmitSourceCancelHandler(const std::shared_ptr<DispatchSourceState>& state)
{
	(void)Enqueue(state->queue, [state] {
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] { return !state->event_pending; });
		}
		std::lock_guard callback_lock(state->callback_mutex);
		darling_dispatch_function_t function{};
		void* block{};
		{
			std::lock_guard lock(state->mutex);
			function = state->cancel_function;
			if (state->cancel_block != nullptr)
				block = objc_retainBlock(state->cancel_block);
		}
		if (block != nullptr) {
			struct ReleaseBlock final {
				void* value;
				~ReleaseBlock() { objc_releaseBlock(value); }
			} release{block};
			InvokeBlock(block);
		} else if (function != nullptr) {
			void* context{};
			{
				std::lock_guard lock(state->mutex);
				context = state->context;
			}
			function(context);
		}
	});
}

void CancelSource(const std::shared_ptr<DispatchSourceState>& state)
{
	bool submit = false;
	std::uint64_t signal_subscription{};
	bool release_mach_send = false;
	darling_mach_port_name_t mach_send_notification_port{};
	bool unregister_mach_send_notification = false;
	{
		std::lock_guard lock(state->mutex);
		state->cancelled = true;
		signal_subscription = std::exchange(state->signal_subscription, 0);
		if (state->kind == DispatchSourceKind::MachSend && state->mach_send_retained) {
			state->mach_send_retained = false;
			release_mach_send = true;
		}
		if (state->kind == DispatchSourceKind::MachSend) {
			mach_send_notification_port = std::exchange(
				state->mach_send_notification_port, 0);
			unregister_mach_send_notification = std::exchange(
				state->mach_send_notification_registered, false);
		}
		if (state->active && state->suspend_count == 0 &&
			!state->event_pending &&
			!state->cancel_handler_submitted) {
			state->cancel_handler_submitted = true;
			submit = true;
		}
	}
	darling::windows_host::DarwinSignals::Unsubscribe(signal_subscription);
	if (release_mach_send)
		(void)darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
			static_cast<darling_mach_port_name_t>(state->handle),
			darling_mach_port_type_send, -1);
	if (unregister_mach_send_notification) {
		darling_mach_port_name_t previous{};
		(void)darling_windows_mach_port_request_notification(
			darling_windows_mach_task_self(),
			static_cast<darling_mach_port_name_t>(state->handle),
			darling_dispatch_mach_notify_dead_name, 0, &previous);
	}
	if (mach_send_notification_port != 0)
		(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(),
			mach_send_notification_port);
	state->condition.notify_all();
	if (submit) SubmitSourceCancelHandler(state);
}

std::uint64_t CombineSourceData(DispatchSourceKind kind, std::uint64_t current,
	std::uint64_t value)
{
	switch (kind) {
	case DispatchSourceKind::DataAdd: return current + value;
	case DispatchSourceKind::DataOr: return current | value;
	case DispatchSourceKind::DataReplace: return value;
	case DispatchSourceKind::Timer:
		return current;
	case DispatchSourceKind::Read:
	case DispatchSourceKind::Write: return (std::max)(current, value);
	case DispatchSourceKind::Process: return current | value;
	case DispatchSourceKind::Signal: return current + value;
	case DispatchSourceKind::Vnode: return current | value;
	case DispatchSourceKind::MachReceive: return (std::max)(current, value);
	case DispatchSourceKind::MachSend: return current | value;
	}
	return current;
}

bool PostSourceEvent(const std::shared_ptr<DispatchSourceState>& state)
{
	auto keep_alive = state;
	if (Enqueue(state->queue, [keep_alive] { RunSourceEvent(keep_alive); }))
		return true;
	{
		std::lock_guard lock(state->mutex);
		if (state->event_pending && !state->event_running) {
			state->pending_data = CombineSourceData(state->kind,
				state->pending_data, state->current_data);
			state->current_data = 0;
			state->event_pending = false;
			state->data.store(0, std::memory_order_release);
		}
	}
	state->condition.notify_all();
	return false;
}

bool QueuePendingSourceData(const std::shared_ptr<DispatchSourceState>& state)
{
	{
		std::lock_guard lock(state->mutex);
		if (state->kind == DispatchSourceKind::Timer || state->cancelled ||
			!state->active || state->suspend_count != 0 || state->event_pending ||
			state->pending_data == 0) return false;
		state->current_data = state->pending_data;
		state->pending_data = 0;
		state->event_pending = true;
		state->data.store(state->current_data, std::memory_order_release);
	}
	return PostSourceEvent(state);
}

void MergeSourceData(const std::shared_ptr<DispatchSourceState>& state,
	std::uint64_t value)
{
	{
		std::lock_guard lock(state->mutex);
		if (state->kind == DispatchSourceKind::Timer || state->cancelled) return;
		if (state->event_running)
			state->pending_data = CombineSourceData(state->kind,
				state->pending_data, value);
		else if (state->event_pending) {
			state->current_data = CombineSourceData(state->kind,
				state->current_data, value);
			state->data.store(state->current_data, std::memory_order_release);
		} else state->pending_data = CombineSourceData(state->kind,
			state->pending_data, value);
	}
	(void)QueuePendingSourceData(state);
}

void RunSourceEvent(const std::shared_ptr<DispatchSourceState>& state)
{
	std::lock_guard callback_lock(state->callback_mutex);
	darling_dispatch_function_t function{};
	void* context{};
	void* block{};
	bool invoke = true;
	{
		std::lock_guard lock(state->mutex);
		if (state->kind != DispatchSourceKind::Timer && state->current_data == 0)
			invoke = false;
		if (invoke) {
			state->event_running = true;
			function = state->event_function;
			context = state->context;
			if (state->event_block != nullptr)
				block = objc_retainBlock(state->event_block);
		}
	}
	try {
		if (invoke && block != nullptr) {
			struct ReleaseBlock final {
				void* value;
				~ReleaseBlock() { objc_releaseBlock(value); }
			} release{block};
			InvokeBlock(block);
		} else if (invoke && function != nullptr) function(context);
	} catch (...) {}
	bool submit_cancel = false;
	bool queue_pending = false;
	{
		std::lock_guard lock(state->mutex);
		state->event_running = false;
		state->event_pending = false;
		state->current_data = 0;
		state->data.store(0, std::memory_order_release);
		if (state->kind != DispatchSourceKind::Timer && !state->cancelled &&
			state->active && state->suspend_count == 0 &&
			state->pending_data != 0) queue_pending = true;
		if (state->cancelled && state->active && state->suspend_count == 0 &&
			!state->cancel_handler_submitted) {
			state->cancel_handler_submitted = true;
			submit_cancel = true;
		}
		if (state->cancelled) state->pending_data = 0;
	}
	state->condition.notify_all();
	if (queue_pending) (void)QueuePendingSourceData(state);
	else if (submit_cancel) SubmitSourceCancelHandler(state);
}

void TimerWorker(std::shared_ptr<DispatchSourceState> state)
{
	std::unique_lock lock(state->mutex);
	while (!state->cancelled) {
		state->condition.wait(lock, [&] {
			return state->cancelled || (state->active &&
				state->suspend_count == 0 &&
				state->deadline != darling_dispatch_time_forever &&
				!state->event_pending);
		});
		if (state->cancelled) break;
		const auto generation = state->generation;
		const auto deadline = state->deadline;
		const auto max_duration = static_cast<std::uint64_t>(
			(std::numeric_limits<std::int64_t>::max)());
		const auto clamped = (std::min)(deadline, max_duration);
		const auto time_point = std::chrono::steady_clock::time_point(
			std::chrono::nanoseconds(static_cast<std::int64_t>(clamped)));
		if (state->condition.wait_until(lock, time_point, [&] {
			return state->cancelled || state->generation != generation ||
				state->suspend_count != 0;
		})) continue;
		if (state->cancelled || state->suspend_count != 0) continue;
		const auto now = SteadyNanoseconds();
		if (state->interval == 0) state->deadline = darling_dispatch_time_forever;
		else if (now >= state->deadline) {
			const auto elapsed = now - state->deadline;
			const auto ticks = elapsed / state->interval + 1;
			if (ticks > ((std::numeric_limits<std::uint64_t>::max)() -
				state->deadline) / state->interval)
				state->deadline = darling_dispatch_time_forever;
			else state->deadline += ticks * state->interval;
		}
		state->event_pending = true;
		state->current_data = 1;
		state->data.store(1, std::memory_order_release);
		auto keep_alive = state;
		lock.unlock();
		if (!Enqueue(state->queue, [keep_alive] { RunSourceEvent(keep_alive); })) {
			bool submit_cancel = false;
			{
				std::lock_guard state_lock(state->mutex);
				state->event_pending = false;
				state->current_data = 0;
				state->data.store(0, std::memory_order_release);
				if (state->cancelled && state->active &&
					state->suspend_count == 0 &&
					!state->cancel_handler_submitted) {
					state->cancel_handler_submitted = true;
					submit_cancel = true;
				}
			}
			state->condition.notify_all();
			if (submit_cancel) SubmitSourceCancelHandler(state);
		}
		lock.lock();
	}
}

void FileDescriptorSourceWorker(std::shared_ptr<DispatchSourceState> state)
{
	for (;;) {
		int descriptor = -1;
		bool writable = false;
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				return state->cancelled || (state->active &&
					state->suspend_count == 0);
			});
			if (state->cancelled) return;
			if (!state->active || state->suspend_count != 0) continue;
			descriptor = static_cast<int>(state->handle);
			writable = state->kind == DispatchSourceKind::Write;
		}
		std::uint64_t data = 0;
		const int ready = darling_windows_dispatch_poll_descriptor(
			descriptor, writable ? 1 : 0, &data);
		if (ready > 0) MergeSourceData(state, data == 0 ? 1 : data);
		else if (ready < 0) {
			MergeSourceData(state, 1);
			return;
		}
		std::unique_lock lock(state->mutex);
		state->condition.wait_for(lock, std::chrono::milliseconds(20), [&] {
			return state->cancelled || !state->active || state->suspend_count != 0;
		});
		if (state->cancelled) return;
	}
}

void ProcessSourceWorker(std::shared_ptr<DispatchSourceState> state)
{
	for (;;) {
		HANDLE process_handle{};
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				return state->cancelled || (state->active &&
					state->suspend_count == 0);
			});
			if (state->cancelled) return;
			process_handle = state->process_handle;
		}
		const DWORD result = WaitForSingleObject(process_handle, 20);
		if (result == WAIT_OBJECT_0) {
			MergeSourceData(state, darling_dispatch_proc_exit);
			return;
		}
		if (result == WAIT_FAILED) return;
		std::unique_lock lock(state->mutex);
		if (state->condition.wait_for(lock, std::chrono::milliseconds(20), [&] {
			return state->cancelled || !state->active || state->suspend_count != 0;
		})) {
			if (state->cancelled) return;
		}
	}
}

std::uint64_t VnodeSize(const BY_HANDLE_FILE_INFORMATION& info)
{
	return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
		info.nFileSizeLow;
}

std::uint64_t VnodeFileTime(const FILETIME& time)
{
	return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
		time.dwLowDateTime;
}

std::uint64_t VnodeChanges(const darling::windows_host::DarwinVnodeSnapshot& before,
	const darling::windows_host::DarwinVnodeSnapshot& after)
{
	std::uint64_t changes = 0;
	const auto old_size = VnodeSize(before.file);
	const auto new_size = VnodeSize(after.file);
	if (new_size != old_size || VnodeFileTime(after.file.ftLastWriteTime) !=
		VnodeFileTime(before.file.ftLastWriteTime)) {
		changes |= darling_dispatch_vnode_write;
		if (new_size > old_size) changes |= darling_dispatch_vnode_extend;
	}
	if (after.file.dwFileAttributes != before.file.dwFileAttributes ||
		VnodeFileTime(after.file.ftCreationTime) !=
		VnodeFileTime(before.file.ftCreationTime))
		changes |= darling_dispatch_vnode_attrib;
	if (after.link_count != before.link_count)
		changes |= darling_dispatch_vnode_link;
	if (!before.path.empty() && !after.path.empty() && before.path != after.path)
		changes |= darling_dispatch_vnode_rename;
	if (after.delete_pending && !before.delete_pending)
		changes |= darling_dispatch_vnode_delete;
	return changes;
}

void VnodeSourceWorker(std::shared_ptr<DispatchSourceState> state)
{
	{
		std::unique_lock lock(state->mutex);
		state->condition.wait(lock, [&] {
			return state->cancelled || (state->active &&
				state->suspend_count == 0);
		});
		if (state->cancelled) return;
	}
	darling::windows_host::DarwinVnodeSnapshot previous{};
	if (!darling_windows_dispatch_snapshot_vnode(state->vnode_handle, &previous))
		return;
	for (;;) {
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				return state->cancelled || (state->active &&
					state->suspend_count == 0);
			});
			if (state->cancelled) return;
			if (state->condition.wait_for(lock, std::chrono::milliseconds(40), [&] {
				return state->cancelled || !state->active ||
					state->suspend_count != 0;
			})) {
				if (state->cancelled) return;
				continue;
			}
		}
		darling::windows_host::DarwinVnodeSnapshot current{};
		if (!darling_windows_dispatch_snapshot_vnode(state->vnode_handle, &current)) {
			MergeSourceData(state, darling_dispatch_vnode_revoke & state->mask);
			return;
		}
		const auto changes = VnodeChanges(previous, current) & state->mask;
		if (changes != 0) MergeSourceData(state, changes);
		previous = std::move(current);
	}
}

void MachReceiveSourceWorker(std::shared_ptr<DispatchSourceState> state)
{
	for (;;) {
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				return state->cancelled || (state->active && state->suspend_count == 0 &&
					!state->event_pending && !state->event_running &&
					state->pending_data == 0);
			});
			if (state->cancelled) return;
			if (state->condition.wait_for(lock, std::chrono::milliseconds(10), [&] {
				return state->cancelled || !state->active || state->suspend_count != 0 ||
					state->event_pending || state->event_running || state->pending_data != 0;
			})) {
				if (state->cancelled) return;
				continue;
			}
		}
		std::uint32_t count{};
		if (darling_windows_mach_port_pending_messages(
			static_cast<darling_mach_port_name_t>(state->handle), &count) != 0) {
			CancelSource(state);
			return;
		}
		if (count != 0) MergeSourceData(state, count);
	}
}

void MachSendSourceWorker(std::shared_ptr<DispatchSourceState> state)
{
	const auto notification_port = state->mach_send_notification_port;
	for (;;) {
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				return state->cancelled || (state->active && state->suspend_count == 0 &&
					!state->event_pending && !state->event_running &&
					state->pending_data == 0);
			});
			if (state->cancelled) return;
			if (state->condition.wait_for(lock, std::chrono::milliseconds(10), [&] {
				return state->cancelled || !state->active || state->suspend_count != 0 ||
					state->event_pending || state->event_running || state->pending_data != 0;
			})) {
				if (state->cancelled) return;
				continue;
			}
		}
		darling_mach_msg_header message{};
		std::uint32_t message_size{};
		const auto result = darling_windows_mach_port_receive(notification_port,
			&message, sizeof(message), &message_size, 10);
		if (result == 268) continue;
		if (result != 0) {
			CancelSource(state);
			return;
		}
		if (message_size == sizeof(message) &&
			message.msgh_id == darling_dispatch_mach_notify_dead_name)
			MergeSourceData(state, darling_dispatch_mach_send_dead & state->mask);
		return;
	}
}

std::uint64_t SteadyNanoseconds()
{
	const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	return value < 0 ? 0 : static_cast<std::uint64_t>(value);
}

void InvokeTask(std::function<void()>& task) noexcept
{
	void* pool = objc_autoreleasePoolPush();
	try { task(); } catch (...) {}
	objc_autoreleasePoolPop(pool);
}

void Worker(std::shared_ptr<DispatchState> state)
{
	for (;;) {
		DispatchWorkItem item{};
		{
			std::unique_lock lock(state->mutex);
			state->condition.wait(lock, [&] {
				if (state->pending.empty()) return state->stopping;
				if (state->barrier_running) return false;
				if (state->pending.front().barrier)
					return state->running_tasks == 0;
				return !state->concurrent_queue ||
					state->running_tasks < state->worker_limit;
			});
			if (state->pending.empty() && state->stopping) return;
			item = std::move(state->pending.front());
			state->pending.pop_front();
			++state->running_tasks;
			if (item.barrier) state->barrier_running = true;
		}
		auto* previous = current_dispatch_state;
		current_dispatch_state = state.get();
		InvokeTask(item.function);
		current_dispatch_state = previous;
		{
			std::lock_guard lock(state->mutex);
			if (state->running_tasks != 0) --state->running_tasks;
			if (item.barrier) state->barrier_running = false;
		}
		state->condition.notify_all();
	}
}

void StartWorkers(const std::shared_ptr<DispatchState>& state,
	std::size_t count)
{
	for (std::size_t index = 0; index < count; ++index) {
		std::thread([state] { Worker(state); }).detach();
	}
}

DispatchQueue* MakeQueue(const char* label, std::size_t workers,
	bool main_queue = false, bool concurrent_queue = false)
{
	auto state = std::make_shared<DispatchState>();
	state->label = label == nullptr ? "" : label;
	state->main_queue = main_queue;
	state->concurrent_queue = concurrent_queue;
	state->worker_limit = (std::max)(std::size_t{1}, workers);
	auto* queue = new DispatchQueue{};
	queue->state = state;
	if (workers != 0) StartWorkers(state, workers);
	return queue;
}

DispatchQueue* MainQueue()
{
	static DispatchQueue* queue = [] {
		auto* result = MakeQueue("com.apple.main-thread", 0, true);
		result->immortal = true;
		return result;
	}();
	return queue;
}

DispatchQueue* GlobalQueue()
{
	static DispatchQueue* queue = [] {
		const auto processors = std::max(2u, std::thread::hardware_concurrency());
		auto* result = MakeQueue("com.apple.global-queue",
			std::min<std::size_t>(processors, 8), false, true);
		result->state->global_queue = true;
		result->immortal = true;
		return result;
	}();
	return queue;
}

DispatchQueue* AsQueue(darling_dispatch_queue_t queue)
{
	return queue == nullptr ? GlobalQueue() : static_cast<DispatchQueue*>(queue);
}

bool Enqueue(const std::shared_ptr<DispatchState>& state,
	std::function<void()> task, bool barrier)
{
	try {
		{
			std::lock_guard lock(state->mutex);
			if (state->stopping || state->pending.size() >= 1'000'000)
				return false;
			state->pending.push_back(DispatchWorkItem{std::move(task), barrier});
		}
		if (state->main_queue) {
			const auto thread = main_thread_id.load(std::memory_order_acquire);
			if (thread != 0)
				(void)PostThreadMessageW(thread,
					darling_dispatch_main_queue_message, 0, 0);
		} else state->condition.notify_all();
		return true;
	} catch (...) { return false; }
}

bool EnqueueOrRun(const std::shared_ptr<DispatchState>& state,
	std::function<void()> callback)
{
	std::shared_ptr<std::function<void()>> retained;
	try { retained = std::make_shared<std::function<void()>>(std::move(callback)); }
	catch (...) {
		try { callback(); } catch (...) {}
		return false;
	}
	if (Enqueue(state, [retained] { (*retained)(); })) return true;
	void* pool = objc_autoreleasePoolPush();
	try { (*retained)(); } catch (...) {}
	objc_autoreleasePoolPop(pool);
	return true;
}

std::shared_ptr<DispatchGroupState> AsGroup(darling_dispatch_group_t raw_group)
{
	return raw_group == nullptr ? nullptr :
		static_cast<DispatchGroup*>(raw_group)->state;
}

bool EnterGroup(const std::shared_ptr<DispatchGroupState>& state)
{
	if (state == nullptr) return false;
	std::lock_guard lock(state->mutex);
	if (state->pending == (std::numeric_limits<std::uint64_t>::max)())
		return false;
	++state->pending;
	return true;
}

bool LeaveGroup(const std::shared_ptr<DispatchGroupState>& state)
{
	if (state == nullptr) return false;
	std::vector<std::pair<std::shared_ptr<DispatchState>, std::function<void()>>>
		notifications;
	{
		std::lock_guard lock(state->mutex);
		if (state->pending == 0) return false;
		--state->pending;
		if (state->pending != 0) return false;
		notifications.swap(state->notifications);
	}
	state->condition.notify_all();
	for (auto& [queue, callback] : notifications)
		(void)EnqueueOrRun(queue, std::move(callback));
	return true;
}

bool NotifyGroup(const std::shared_ptr<DispatchGroupState>& state,
	const std::shared_ptr<DispatchState>& queue, std::function<void()> callback)
{
	if (state == nullptr || queue == nullptr) return false;
	{
		std::lock_guard lock(state->mutex);
		if (state->pending != 0) {
			try { state->notifications.emplace_back(queue, std::move(callback)); }
			catch (...) { return false; }
			return true;
		}
	}
	return EnqueueOrRun(queue, std::move(callback));
}

void InvokeBlock(void* raw_block)
{
	if (raw_block == nullptr) return;
	auto* block = static_cast<DarwinBlockHeader*>(raw_block);
	if (block->invoke == nullptr) return;
	auto invoke = reinterpret_cast<darling_dispatch_function_t>(block->invoke);
	invoke(raw_block);
}

void RunSync(const std::shared_ptr<DispatchState>& state,
	std::function<void()> task, bool barrier = false)
{
	if (current_dispatch_state == state.get() || (state->main_queue &&
		main_thread_id.load(std::memory_order_acquire) == GetCurrentThreadId())) {
		auto* previous = current_dispatch_state;
		current_dispatch_state = state.get();
		InvokeTask(task);
		current_dispatch_state = previous;
		return;
	}
	auto completion = std::make_shared<std::promise<void>>();
	auto finished = completion->get_future();
	if (!Enqueue(state, [task, completion]() mutable {
		try { task(); } catch (...) {}
		completion->set_value();
	}, barrier)) {
		InvokeTask(task);
		return;
	}
	finished.wait();
}

void RunOnce(void* predicate, std::function<void()> task)
{
	if (predicate == nullptr) return;
	std::shared_ptr<DispatchOnceState> state;
	try {
		std::lock_guard lock(once_states_mutex);
		auto& entry = once_states[predicate];
		if (entry == nullptr) entry = std::make_shared<DispatchOnceState>();
		state = entry;
	} catch (...) {
		InvokeTask(task);
		return;
	}
	{
		std::unique_lock lock(state->mutex);
		while (state->running && !state->complete) {
			if (state->owner == std::this_thread::get_id()) return;
			state->condition.wait(lock);
		}
		if (state->complete) return;
		state->running = true;
		state->owner = std::this_thread::get_id();
	}
	InvokeTask(task);
	{
		std::lock_guard lock(state->mutex);
		state->complete = true;
		state->running = false;
		state->owner = {};
	}
	state->condition.notify_all();
}
}

extern "C" darling_dispatch_queue_t darling_windows_dispatch_get_main_queue()
{
	return MainQueue();
}

extern "C" darling_dispatch_queue_t darling_windows_dispatch_get_global_queue(
	std::int64_t, std::uint64_t)
{
	return GlobalQueue();
}

extern "C" darling_dispatch_queue_t darling_windows_dispatch_queue_create(
	const char* label, const void* attributes)
{
	try {
		const bool concurrent = attributes == &_dispatch_queue_attr_concurrent;
		const auto workers = concurrent ? std::min<std::size_t>(
			(std::max)(2u, std::thread::hardware_concurrency()), 8) : 1;
		return MakeQueue(label, workers, false, concurrent);
	}
	catch (...) { return nullptr; }
}

extern "C" darling_dispatch_queue_t darling_windows_dispatch_retain(
	darling_dispatch_queue_t raw_queue)
{
	if (raw_queue == nullptr) return GlobalQueue();
	{
		std::lock_guard lock(dispatch_groups_mutex);
		if (dispatch_groups.contains(raw_queue)) {
			auto* group = static_cast<DispatchGroup*>(raw_queue);
			group->references.fetch_add(1, std::memory_order_relaxed);
			return raw_queue;
		}
		if (dispatch_sources.contains(raw_queue)) {
			auto* source = static_cast<DispatchSource*>(raw_queue);
			source->references.fetch_add(1, std::memory_order_relaxed);
			return raw_queue;
		}
	}
	auto* queue = static_cast<DispatchQueue*>(raw_queue);
	if (!queue->immortal) queue->references.fetch_add(1, std::memory_order_relaxed);
	return queue;
}

extern "C" void darling_windows_dispatch_release(darling_dispatch_queue_t raw_queue)
{
	if (raw_queue == nullptr) return;
	std::shared_ptr<DispatchSourceState> released_source_state;
	darling_dispatch_queue_t released_source_queue{};
	bool released_source = false;
	{
		std::lock_guard lock(dispatch_groups_mutex);
		if (dispatch_groups.contains(raw_queue)) {
			auto* group = static_cast<DispatchGroup*>(raw_queue);
			if (group->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				dispatch_groups.erase(raw_queue);
				delete group;
			}
			return;
		}
		if (dispatch_sources.contains(raw_queue)) {
			auto* source = static_cast<DispatchSource*>(raw_queue);
			if (source->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				dispatch_sources.erase(raw_queue);
				released_source_state = source->state;
				released_source_queue = source->queue_owner;
				source->queue_owner = nullptr;
				delete source;
				released_source = true;
			}
			else return;
		}
	}
	if (released_source) {
		CancelSource(released_source_state);
		darling_windows_dispatch_release(released_source_queue);
		return;
	}
	auto* queue = static_cast<DispatchQueue*>(raw_queue);
	if (queue->immortal || queue->references.fetch_sub(1,
		std::memory_order_acq_rel) != 1) return;
	{
		std::lock_guard lock(queue->state->mutex);
		queue->state->stopping = true;
	}
	queue->state->condition.notify_all();
	delete queue;
}

extern "C" void darling_windows_dispatch_async_f(darling_dispatch_queue_t raw_queue,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	(void)Enqueue(AsQueue(raw_queue)->state, [context, function] {
		function(context);
	});
}

extern "C" void darling_windows_dispatch_sync_f(darling_dispatch_queue_t raw_queue,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	RunSync(AsQueue(raw_queue)->state, [context, function] {
		function(context);
	});
}

extern "C" void darling_windows_dispatch_barrier_async_f(
	darling_dispatch_queue_t raw_queue, void* context,
	darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	auto state = AsQueue(raw_queue)->state;
	const bool barrier = state->concurrent_queue && !state->global_queue;
	(void)Enqueue(state, [context, function] { function(context); }, barrier);
}

extern "C" void darling_windows_dispatch_barrier_sync_f(
	darling_dispatch_queue_t raw_queue, void* context,
	darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	auto state = AsQueue(raw_queue)->state;
	const bool barrier = state->concurrent_queue && !state->global_queue;
	RunSync(state, [context, function] { function(context); }, barrier);
}

extern "C" void darling_windows_dispatch_async(darling_dispatch_queue_t raw_queue,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	if (!Enqueue(AsQueue(raw_queue)->state, [block] {
		struct BlockRelease final {
			void* value;
			~BlockRelease() { objc_releaseBlock(value); }
		} release{block};
		InvokeBlock(block);
	})) objc_releaseBlock(block);
}

extern "C" void darling_windows_dispatch_sync(darling_dispatch_queue_t raw_queue,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	RunSync(AsQueue(raw_queue)->state, [block] {
		struct BlockRelease final {
			void* value;
			~BlockRelease() { objc_releaseBlock(value); }
		} release{block};
		InvokeBlock(block);
	});
}

extern "C" void darling_windows_dispatch_barrier_async(
	darling_dispatch_queue_t raw_queue, void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	auto state = AsQueue(raw_queue)->state;
	const bool barrier = state->concurrent_queue && !state->global_queue;
	if (!Enqueue(state, [block] {
		struct BlockRelease final {
			void* value;
			~BlockRelease() { objc_releaseBlock(value); }
		} release{block};
		InvokeBlock(block);
	}, barrier)) objc_releaseBlock(block);
}

extern "C" void darling_windows_dispatch_barrier_sync(
	darling_dispatch_queue_t raw_queue, void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	auto state = AsQueue(raw_queue)->state;
	const bool barrier = state->concurrent_queue && !state->global_queue;
	RunSync(state, [block] {
		struct BlockRelease final {
			void* value;
			~BlockRelease() { objc_releaseBlock(value); }
		} release{block};
		InvokeBlock(block);
	}, barrier);
}

extern "C" void darling_windows_dispatch_after_f(
	darling_dispatch_time_t when, darling_dispatch_queue_t raw_queue,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	try {
	auto* retained_queue = static_cast<DispatchQueue*>(
		darling_windows_dispatch_retain(raw_queue));
	std::shared_ptr<void> queue_owner(retained_queue, [](void* value) {
		darling_windows_dispatch_release(value);
	});
	auto queue = retained_queue->state;
	(void)DispatchAfterScheduler::Instance().Add(when, queue,
		[queue_owner, queue, context, function] {
			(void)Enqueue(queue, [queue_owner, context, function] {
				function(context);
			});
		});
	} catch (...) {}
}

extern "C" void darling_windows_dispatch_after(
	darling_dispatch_time_t when, darling_dispatch_queue_t raw_queue,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	try {
	void* retained_block = objc_retainBlock(raw_block);
	if (retained_block == nullptr) return;
	std::shared_ptr<void> block(retained_block, [](void* value) {
		objc_releaseBlock(value);
	});
	auto* retained_queue = static_cast<DispatchQueue*>(
		darling_windows_dispatch_retain(raw_queue));
	std::shared_ptr<void> queue_owner(retained_queue, [](void* value) {
		darling_windows_dispatch_release(value);
	});
	auto queue = retained_queue->state;
	(void)DispatchAfterScheduler::Instance().Add(when, queue,
		[queue_owner, queue, block] {
			(void)Enqueue(queue, [queue_owner, block] { InvokeBlock(block.get()); });
		});
	} catch (...) {}
}

extern "C" void darling_windows_dispatch_once_f(void* predicate,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	RunOnce(predicate, [context, function] { function(context); });
}

extern "C" void darling_windows_dispatch_once(void* predicate,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	RunOnce(predicate, [raw_block] {
		void* block = objc_retainBlock(raw_block);
		if (block == nullptr) return;
		struct BlockRelease final {
			void* value;
			~BlockRelease() { objc_releaseBlock(value); }
		} release{block};
		InvokeBlock(block);
	});
}

extern "C" darling_dispatch_group_t darling_windows_dispatch_group_create()
{
	try {
		auto group = std::make_unique<DispatchGroup>();
		group->state = std::make_shared<DispatchGroupState>();
		auto* raw_group = group.get();
		{
			std::lock_guard lock(dispatch_groups_mutex);
			dispatch_groups.insert(raw_group);
		}
		return group.release();
	} catch (...) { return nullptr; }
}

extern "C" darling_dispatch_group_t darling_windows_dispatch_group_retain(
	darling_dispatch_group_t raw_group)
{
	if (raw_group == nullptr) return nullptr;
	return darling_windows_dispatch_retain(raw_group);
}

extern "C" void darling_windows_dispatch_group_release(
	darling_dispatch_group_t raw_group)
{
	if (raw_group == nullptr) return;
	 darling_windows_dispatch_release(raw_group);
}

extern "C" void darling_windows_dispatch_group_enter(
	darling_dispatch_group_t raw_group)
{
	(void)EnterGroup(AsGroup(raw_group));
}

extern "C" void darling_windows_dispatch_group_leave(
	darling_dispatch_group_t raw_group)
{
	(void)LeaveGroup(AsGroup(raw_group));
}

extern "C" std::intptr_t darling_windows_dispatch_group_wait(
	darling_dispatch_group_t raw_group, darling_dispatch_time_t timeout)
{
	auto state = AsGroup(raw_group);
	if (state == nullptr) return 1;
	std::unique_lock lock(state->mutex);
	if (state->pending == 0) return 0;
	if (timeout == darling_dispatch_time_now) return 1;
	const auto ready = [&] { return state->pending == 0; };
	if (timeout == darling_dispatch_time_forever) {
		state->condition.wait(lock, ready);
		return 0;
	}
	const auto now = SteadyNanoseconds();
	const auto remaining = timeout > now ? timeout - now : 0;
	return state->condition.wait_for(lock,
		std::chrono::nanoseconds(remaining), ready) ? 0 : 1;
}

extern "C" void darling_windows_dispatch_group_async_f(
	darling_dispatch_group_t raw_group, darling_dispatch_queue_t raw_queue,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	auto group = AsGroup(raw_group);
	auto queue = AsQueue(raw_queue)->state;
	if (!EnterGroup(group)) return;
	if (!Enqueue(queue, [group, context, function] {
		struct GroupLeave final {
			std::shared_ptr<DispatchGroupState> state;
			~GroupLeave() { (void)LeaveGroup(state); }
		} leave{group};
		function(context);
	})) (void)LeaveGroup(group);
}

extern "C" void darling_windows_dispatch_group_async(
	darling_dispatch_group_t raw_group, darling_dispatch_queue_t raw_queue,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	auto group = AsGroup(raw_group);
	auto queue = AsQueue(raw_queue)->state;
	if (!EnterGroup(group)) {
		objc_releaseBlock(block);
		return;
	}
	if (!Enqueue(queue, [group, block] {
		struct GroupTaskCleanup final {
			std::shared_ptr<DispatchGroupState> state;
			void* block;
			~GroupTaskCleanup() {
				objc_releaseBlock(block);
				(void)LeaveGroup(state);
			}
		} cleanup{group, block};
		InvokeBlock(block);
	})) {
		objc_releaseBlock(block);
		(void)LeaveGroup(group);
	}
}

extern "C" void darling_windows_dispatch_group_notify_f(
	darling_dispatch_group_t raw_group, darling_dispatch_queue_t raw_queue,
	void* context, darling_dispatch_function_t function)
{
	if (function == nullptr) return;
	(void)NotifyGroup(AsGroup(raw_group), AsQueue(raw_queue)->state,
		[context, function] { function(context); });
}

extern "C" void darling_windows_dispatch_group_notify(
	darling_dispatch_group_t raw_group, darling_dispatch_queue_t raw_queue,
	void* raw_block)
{
	if (raw_block == nullptr) return;
	void* block = objc_retainBlock(raw_block);
	if (block == nullptr) return;
	const bool queued = NotifyGroup(AsGroup(raw_group), AsQueue(raw_queue)->state,
		[block] {
			struct BlockRelease final {
				void* value;
				~BlockRelease() { objc_releaseBlock(value); }
			} release{block};
			InvokeBlock(block);
		});
	if (!queued && block != nullptr) objc_releaseBlock(block);
}

extern "C" darling_dispatch_semaphore_t
darling_windows_dispatch_semaphore_create(std::intptr_t value)
{
	if (value < 0) return nullptr;
	try {
		auto* semaphore = new DispatchSemaphore{};
		semaphore->value = value;
		return semaphore;
	} catch (...) { return nullptr; }
}

extern "C" std::intptr_t darling_windows_dispatch_semaphore_signal(
	darling_dispatch_semaphore_t raw_semaphore)
{
	if (raw_semaphore == nullptr) return 1;
	auto* semaphore = static_cast<DispatchSemaphore*>(raw_semaphore);
	bool waiter_present = false;
	{
		std::lock_guard lock(semaphore->mutex);
		if (semaphore->disposed || semaphore->value ==
			(std::numeric_limits<std::int64_t>::max)()) return 1;
		waiter_present = semaphore->waiting != 0;
		++semaphore->value;
	}
	semaphore->condition.notify_one();
	return waiter_present ? 0 : 1;
}

extern "C" std::intptr_t darling_windows_dispatch_semaphore_wait(
	darling_dispatch_semaphore_t raw_semaphore,
	darling_dispatch_time_t timeout)
{
	if (raw_semaphore == nullptr) return 1;
	auto* semaphore = static_cast<DispatchSemaphore*>(raw_semaphore);
	std::unique_lock lock(semaphore->mutex);
	if (semaphore->disposed) return 1;
	if (timeout == darling_dispatch_time_now && semaphore->value == 0) return 1;
	const bool registered_waiter = semaphore->value == 0;
	if (registered_waiter) ++semaphore->waiting;
	const auto ready = [&] { return semaphore->disposed || semaphore->value > 0; };
	bool acquired = true;
	if (!ready()) {
		if (timeout == darling_dispatch_time_forever)
			semaphore->condition.wait(lock, ready);
		else {
			const auto now = SteadyNanoseconds();
			const auto remaining = timeout > now ? timeout - now : 0;
			acquired = semaphore->condition.wait_for(lock,
				std::chrono::nanoseconds(remaining), ready);
		}
	}
	if (registered_waiter) --semaphore->waiting;
	const bool delete_after_wait = semaphore->disposed && semaphore->waiting == 0;
	if (!acquired || semaphore->disposed || semaphore->value == 0) {
		lock.unlock();
		if (delete_after_wait) delete semaphore;
		return 1;
	}
	--semaphore->value;
	return 0;
}

extern "C" void darling_windows_dispatch_semaphore_dispose(
	darling_dispatch_semaphore_t raw_semaphore)
{
	if (raw_semaphore == nullptr) return;
	auto* semaphore = static_cast<DispatchSemaphore*>(raw_semaphore);
	bool delete_now = false;
	{
		std::lock_guard lock(semaphore->mutex);
		semaphore->disposed = true;
		delete_now = semaphore->waiting == 0;
	}
	semaphore->condition.notify_all();
	// A semaphore must outlive its waiters. Keep the adapter state allocated
	// when disposal races with active waits so those waiters can return safely.
	if (delete_now) delete semaphore;
}

extern "C" darling_dispatch_time_t darling_windows_dispatch_time(
	darling_dispatch_time_t when, std::int64_t delta_nanoseconds)
{
	const auto base = when == darling_dispatch_time_now ? SteadyNanoseconds() : when;
	if (delta_nanoseconds >= 0) {
		const auto delta = static_cast<std::uint64_t>(delta_nanoseconds);
		const auto maximum = (std::numeric_limits<darling_dispatch_time_t>::max)() - 1;
		return base > maximum - delta ? maximum : base + delta;
	}
	const auto magnitude = static_cast<std::uint64_t>(-(delta_nanoseconds + 1)) + 1;
	return magnitude > base ? 0 : base - magnitude;
}

extern "C" darling_dispatch_time_t darling_windows_dispatch_walltime(
	const darling_timespec* when, std::int64_t delta_nanoseconds)
{
	struct TimespecLayout final {
		std::int64_t tv_sec;
		std::int64_t tv_nsec;
	};
	const auto maximum_signed = (std::numeric_limits<std::int64_t>::max)();
	const auto minimum_signed = (std::numeric_limits<std::int64_t>::min)();
	const auto saturating_add = [&](std::int64_t left, std::int64_t right) {
		if (right > 0 && left > maximum_signed - right) return maximum_signed;
		if (right < 0 && left < minimum_signed - right) return minimum_signed;
		return static_cast<std::int64_t>(left + right);
	};
	const auto wall_now = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	auto wall_target = static_cast<std::int64_t>(wall_now);
	if (when != nullptr) {
		const auto* value = reinterpret_cast<const TimespecLayout*>(when);
		constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
		if (value->tv_sec > maximum_signed / nanoseconds_per_second)
			wall_target = maximum_signed;
		else if (value->tv_sec < minimum_signed / nanoseconds_per_second)
			wall_target = minimum_signed;
		else wall_target = saturating_add(
			value->tv_sec * nanoseconds_per_second, value->tv_nsec);
	}
	wall_target = saturating_add(wall_target, delta_nanoseconds);
	const auto monotonic_now = SteadyNanoseconds();
	if (wall_target <= wall_now) {
		const auto elapsed = static_cast<std::uint64_t>(wall_now) -
			static_cast<std::uint64_t>(wall_target);
		return elapsed >= monotonic_now ? 0 : monotonic_now - elapsed;
	}
	const auto remaining = static_cast<std::uint64_t>(wall_target) -
		static_cast<std::uint64_t>(wall_now);
	const auto maximum_deadline =
		(std::numeric_limits<darling_dispatch_time_t>::max)() - 1;
	return remaining > maximum_deadline - monotonic_now
		? maximum_deadline : monotonic_now + remaining;
}

extern "C" const char* darling_windows_dispatch_queue_get_label(
	darling_dispatch_queue_t raw_queue)
{
	if (raw_queue == nullptr) return "com.apple.global-queue";
	return AsQueue(raw_queue)->state->label.c_str();
}

extern "C" darling_dispatch_source_t darling_windows_dispatch_source_create(
	const void* type, std::uintptr_t handle, std::uintptr_t mask,
	darling_dispatch_queue_t raw_queue)
{
	DispatchSourceKind kind{};
	if (type == &_dispatch_source_type_timer) kind = DispatchSourceKind::Timer;
	else if (type == &_dispatch_source_type_data_add)
		kind = DispatchSourceKind::DataAdd;
	else if (type == &_dispatch_source_type_data_or)
		kind = DispatchSourceKind::DataOr;
	else if (type == &_dispatch_source_type_data_replace)
		kind = DispatchSourceKind::DataReplace;
	else if (type == &_dispatch_source_type_read)
		kind = DispatchSourceKind::Read;
	else if (type == &_dispatch_source_type_write)
		kind = DispatchSourceKind::Write;
	else if (type == &_dispatch_source_type_proc)
		kind = DispatchSourceKind::Process;
	else if (type == &_dispatch_source_type_signal)
		kind = DispatchSourceKind::Signal;
	else if (type == &_dispatch_source_type_vnode)
		kind = DispatchSourceKind::Vnode;
	else if (type == &_dispatch_source_type_mach_recv)
		kind = DispatchSourceKind::MachReceive;
	else if (type == &_dispatch_source_type_mach_send)
		kind = DispatchSourceKind::MachSend;
	else return nullptr;
	if (kind == DispatchSourceKind::Timer && handle != 0) return nullptr;
	if (kind == DispatchSourceKind::Process &&
		(mask != darling_dispatch_proc_exit || handle == 0 ||
		 handle > (std::numeric_limits<DWORD>::max)())) return nullptr;
	if (kind == DispatchSourceKind::Signal &&
		(mask != 0 || handle == 0 || handle >= 64)) return nullptr;
	if (kind == DispatchSourceKind::MachReceive) {
		std::uint32_t pending{};
		if (mask != 0 || handle == 0 ||
			handle > (std::numeric_limits<darling_mach_port_name_t>::max)() ||
			darling_windows_mach_port_pending_messages(
				static_cast<darling_mach_port_name_t>(handle), &pending) != 0)
			return nullptr;
	}
	if (kind == DispatchSourceKind::MachSend) {
		std::uint32_t send_refs{}, receive_refs{};
		if (mask != darling_dispatch_mach_send_dead || handle == 0 ||
			handle > (std::numeric_limits<darling_mach_port_name_t>::max)() ||
			darling_windows_mach_port_get_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_send, &send_refs) != 0 || send_refs == 0 ||
			darling_windows_mach_port_get_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_receive, &receive_refs) != 0 || receive_refs == 0)
			return nullptr;
	}
	if (kind == DispatchSourceKind::Vnode &&
		(mask == 0 || (mask & ~darling_dispatch_vnode_supported) != 0 ||
		 handle > static_cast<std::uintptr_t>((std::numeric_limits<int>::max)())))
		return nullptr;
	if (kind != DispatchSourceKind::Timer && kind != DispatchSourceKind::Process &&
		kind != DispatchSourceKind::Vnode && kind != DispatchSourceKind::MachSend &&
		mask != 0) return nullptr;
	if ((kind == DispatchSourceKind::Read || kind == DispatchSourceKind::Write) &&
		handle > static_cast<std::uintptr_t>((std::numeric_limits<int>::max)()))
		return nullptr;
	if ((kind == DispatchSourceKind::DataAdd || kind == DispatchSourceKind::DataOr ||
		kind == DispatchSourceKind::DataReplace) && handle != 0) return nullptr;
	HANDLE process_handle{};
	HANDLE vnode_handle{};
	bool mach_send_retained = false;
	darling_mach_port_name_t mach_send_notification_port{};
	bool mach_send_notification_registered = false;
	if (kind == DispatchSourceKind::MachSend) {
		if (darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
			static_cast<darling_mach_port_name_t>(handle),
			darling_mach_port_type_send, 1) != 0) return nullptr;
		mach_send_retained = true;
		if (darling_windows_mach_port_allocate(darling_windows_mach_task_self(),
			&mach_send_notification_port) != 0) {
			(void)darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_send, -1);
			return nullptr;
		}
		darling_mach_port_name_t previous_notification{};
		if (darling_windows_mach_port_request_notification(
			darling_windows_mach_task_self(),
			static_cast<darling_mach_port_name_t>(handle),
			darling_dispatch_mach_notify_dead_name,
			mach_send_notification_port, &previous_notification) != 0) {
			(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(),
				mach_send_notification_port);
			(void)darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_send, -1);
			return nullptr;
		}
		mach_send_notification_registered = true;
	}
	if (kind == DispatchSourceKind::Process) {
		process_handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(handle));
		if (process_handle == nullptr) return nullptr;
	}
	if (kind == DispatchSourceKind::Vnode) {
		vnode_handle = darling_windows_dispatch_duplicate_vnode(
			static_cast<int>(handle));
		if (vnode_handle == nullptr) return nullptr;
		darling::windows_host::DarwinVnodeSnapshot initial_snapshot{};
		if (!darling_windows_dispatch_snapshot_vnode(vnode_handle,
			&initial_snapshot)) {
			CloseHandle(vnode_handle);
			return nullptr;
		}
	}
	try {
		auto source = std::make_unique<DispatchSource>();
		source->queue_owner = darling_windows_dispatch_retain(raw_queue);
		source->state = std::make_shared<DispatchSourceState>();
		source->state->queue = AsQueue(source->queue_owner)->state;
		source->state->mask = mask;
		source->state->kind = kind;
		source->state->handle = handle;
		source->state->mach_send_retained = mach_send_retained;
		mach_send_retained = false;
		source->state->mach_send_notification_port = mach_send_notification_port;
		mach_send_notification_port = 0;
		source->state->mach_send_notification_registered =
			mach_send_notification_registered;
		mach_send_notification_registered = false;
		source->state->process_handle = process_handle;
		process_handle = nullptr;
		source->state->vnode_handle = vnode_handle;
		vnode_handle = nullptr;
		auto* raw_source = source.get();
		{
			std::lock_guard lock(dispatch_groups_mutex);
			dispatch_sources.insert(raw_source);
		}
		if (kind == DispatchSourceKind::Signal) {
			const auto state = source->state;
			state->signal_subscription =
				darling::windows_host::DarwinSignals::Subscribe(
					static_cast<int>(handle), [state](int) {
						MergeSourceData(state, 1);
					});
			if (state->signal_subscription == 0) {
				std::lock_guard lock(dispatch_groups_mutex);
				dispatch_sources.erase(raw_source);
				return nullptr;
			}
		}
		if (kind == DispatchSourceKind::Timer || kind == DispatchSourceKind::Read ||
			kind == DispatchSourceKind::Write || kind == DispatchSourceKind::Process ||
			kind == DispatchSourceKind::Vnode || kind == DispatchSourceKind::MachReceive ||
			kind == DispatchSourceKind::MachSend) {
			try {
				if (kind == DispatchSourceKind::Timer)
					std::thread(TimerWorker, source->state).detach();
				else if (kind == DispatchSourceKind::Process)
					std::thread(ProcessSourceWorker, source->state).detach();
				else if (kind == DispatchSourceKind::Vnode)
					std::thread(VnodeSourceWorker, source->state).detach();
				else if (kind == DispatchSourceKind::MachReceive)
					std::thread(MachReceiveSourceWorker, source->state).detach();
				else if (kind == DispatchSourceKind::MachSend)
					std::thread(MachSendSourceWorker, source->state).detach();
				else std::thread(FileDescriptorSourceWorker, source->state).detach();
			}
			catch (...) {
				std::lock_guard lock(dispatch_groups_mutex);
				dispatch_sources.erase(raw_source);
				throw;
			}
		}
		return source.release();
	} catch (...) {
		if (process_handle != nullptr) CloseHandle(process_handle);
		if (vnode_handle != nullptr) CloseHandle(vnode_handle);
		if (mach_send_notification_registered) {
			darling_mach_port_name_t previous_notification{};
			(void)darling_windows_mach_port_request_notification(
				darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_dispatch_mach_notify_dead_name, 0, &previous_notification);
		}
		if (mach_send_notification_port != 0)
			(void)darling_windows_mach_port_destroy(darling_windows_mach_task_self(),
				mach_send_notification_port);
		if (mach_send_retained)
			(void)darling_windows_mach_port_mod_refs(darling_windows_mach_task_self(),
				static_cast<darling_mach_port_name_t>(handle),
				darling_mach_port_type_send, -1);
		return nullptr;
	}
}

extern "C" void darling_windows_dispatch_source_set_event_handler_f(
	darling_dispatch_source_t raw_source, darling_dispatch_function_t function)
{
	if (raw_source == nullptr) return;
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	void* old_block{};
	{
		std::lock_guard lock(state->mutex);
		old_block = std::exchange(state->event_block, nullptr);
		state->event_function = function;
	}
	if (old_block != nullptr) objc_releaseBlock(old_block);
}

extern "C" void darling_windows_dispatch_source_set_event_handler(
	darling_dispatch_source_t raw_source, void* raw_block)
{
	if (raw_source == nullptr) return;
	void* block = raw_block == nullptr ? nullptr : objc_retainBlock(raw_block);
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	void* old_block{};
	{
		std::lock_guard lock(state->mutex);
		old_block = std::exchange(state->event_block, block);
		state->event_function = nullptr;
	}
	if (old_block != nullptr) objc_releaseBlock(old_block);
}

extern "C" void darling_windows_dispatch_source_set_cancel_handler_f(
	darling_dispatch_source_t raw_source, darling_dispatch_function_t function)
{
	if (raw_source == nullptr) return;
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	void* old_block{};
	{
		std::lock_guard lock(state->mutex);
		old_block = std::exchange(state->cancel_block, nullptr);
		state->cancel_function = function;
	}
	if (old_block != nullptr) objc_releaseBlock(old_block);
}

extern "C" void darling_windows_dispatch_source_set_cancel_handler(
	darling_dispatch_source_t raw_source, void* raw_block)
{
	if (raw_source == nullptr) return;
	void* block = raw_block == nullptr ? nullptr : objc_retainBlock(raw_block);
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	void* old_block{};
	{
		std::lock_guard lock(state->mutex);
		old_block = std::exchange(state->cancel_block, block);
		state->cancel_function = nullptr;
	}
	if (old_block != nullptr) objc_releaseBlock(old_block);
}

extern "C" void darling_windows_dispatch_source_set_timer(
	darling_dispatch_source_t raw_source, darling_dispatch_time_t start,
	std::uint64_t interval, std::uint64_t)
{
	if (raw_source == nullptr) return;
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	{
		std::lock_guard lock(state->mutex);
		if (state->kind != DispatchSourceKind::Timer) return;
		state->deadline = start == darling_dispatch_time_now
			? SteadyNanoseconds() : start;
		state->interval = interval;
		++state->generation;
	}
	state->condition.notify_all();
}

extern "C" void darling_windows_dispatch_source_merge_data(
	darling_dispatch_source_t raw_source, std::uint64_t value)
{
	if (raw_source == nullptr) return;
	MergeSourceData(static_cast<DispatchSource*>(raw_source)->state, value);
}

extern "C" void darling_windows_dispatch_source_cancel(
	darling_dispatch_source_t raw_source)
{
	if (raw_source != nullptr)
		CancelSource(static_cast<DispatchSource*>(raw_source)->state);
}

extern "C" std::intptr_t darling_windows_dispatch_source_testcancel(
	darling_dispatch_source_t raw_source)
{
	if (raw_source == nullptr) return 1;
	auto state = static_cast<DispatchSource*>(raw_source)->state;
	std::lock_guard lock(state->mutex);
	return state->cancelled ? 1 : 0;
}

extern "C" std::uintptr_t darling_windows_dispatch_source_get_handle(
	darling_dispatch_source_t raw_source)
{
	if (raw_source == nullptr) return 0;
	return static_cast<DispatchSource*>(raw_source)->state->handle;
}

extern "C" std::uintptr_t darling_windows_dispatch_source_get_mask(
	darling_dispatch_source_t raw_source)
{
	if (raw_source == nullptr) return 0;
	return static_cast<DispatchSource*>(raw_source)->state->mask;
}

extern "C" std::uint64_t darling_windows_dispatch_source_get_data(
	darling_dispatch_source_t raw_source)
{
	if (raw_source == nullptr) return 0;
	return static_cast<DispatchSource*>(raw_source)->state->data.load(
		std::memory_order_acquire);
}

extern "C" void darling_windows_dispatch_set_context(
	darling_dispatch_queue_t raw_object, void* context)
{
	if (raw_object == nullptr) return;
	std::shared_ptr<DispatchSourceState> state;
	{
		std::lock_guard lock(dispatch_groups_mutex);
		if (!dispatch_sources.contains(raw_object)) return;
		state = static_cast<DispatchSource*>(raw_object)->state;
	}
	std::lock_guard lock(state->mutex);
	state->context = context;
}

extern "C" void* darling_windows_dispatch_get_context(
	darling_dispatch_queue_t raw_object)
{
	if (raw_object == nullptr) return nullptr;
	std::shared_ptr<DispatchSourceState> state;
	{
		std::lock_guard lock(dispatch_groups_mutex);
		if (!dispatch_sources.contains(raw_object)) return nullptr;
		state = static_cast<DispatchSource*>(raw_object)->state;
	}
	std::lock_guard lock(state->mutex);
	return state->context;
}

extern "C" void darling_windows_dispatch_resume(
	darling_dispatch_queue_t raw_object)
{
	if (raw_object == nullptr) return;
	std::shared_ptr<DispatchSourceState> state;
	{
		std::lock_guard lock(dispatch_groups_mutex);
		if (!dispatch_sources.contains(raw_object)) return;
		state = static_cast<DispatchSource*>(raw_object)->state;
	}
	bool submit_cancel = false;
	bool queue_pending = false;
	{
		std::lock_guard lock(state->mutex);
		if (state->suspend_count != 0) --state->suspend_count;
		state->active = true;
		if (state->cancelled && state->suspend_count == 0 &&
			!state->event_pending &&
			!state->cancel_handler_submitted) {
			state->cancel_handler_submitted = true;
			submit_cancel = true;
		}
		queue_pending = !state->cancelled && state->suspend_count == 0 &&
			state->kind != DispatchSourceKind::Timer && state->pending_data != 0;
	}
	state->condition.notify_all();
	if (submit_cancel) SubmitSourceCancelHandler(state);
	else if (queue_pending) (void)QueuePendingSourceData(state);
}

extern "C" void darling_windows_dispatch_suspend(
	darling_dispatch_queue_t raw_object)
{
	if (raw_object == nullptr) return;
	std::lock_guard object_lock(dispatch_groups_mutex);
	if (!dispatch_sources.contains(raw_object)) return;
	auto state = static_cast<DispatchSource*>(raw_object)->state;
	std::lock_guard state_lock(state->mutex);
	if (state->suspend_count != (std::numeric_limits<std::uint32_t>::max)())
		++state->suspend_count;
}

extern "C" void darling_windows_dispatch_activate(
	darling_dispatch_queue_t raw_object)
{
	darling_windows_dispatch_resume(raw_object);
}

extern "C" void darling_windows_dispatch_queue_set_specific(
	darling_dispatch_queue_t raw_queue, const void* key, void* context,
	darling_dispatch_function_t destructor)
{
	if (raw_queue == nullptr || key == nullptr) return;
	auto state = AsQueue(raw_queue)->state;
	DispatchSpecificValue previous{};
	{
		std::lock_guard lock(state->specific_mutex);
		auto found = state->specific.find(key);
		if (found != state->specific.end()) {
			previous = found->second;
			state->specific.erase(found);
		}
		if (context != nullptr) {
			try { state->specific.emplace(key, DispatchSpecificValue{context, destructor}); }
			catch (...) {
				if (previous.context != nullptr)
					state->specific.emplace(key, previous);
				return;
			}
		}
	}
	if (previous.context != nullptr && previous.destructor != nullptr) {
		try { previous.destructor(previous.context); } catch (...) {}
	}
}

extern "C" void* darling_windows_dispatch_queue_get_specific(
	darling_dispatch_queue_t raw_queue, const void* key)
{
	if (raw_queue == nullptr || key == nullptr) return nullptr;
	auto state = AsQueue(raw_queue)->state;
	std::lock_guard lock(state->specific_mutex);
	auto found = state->specific.find(key);
	return found == state->specific.end() ? nullptr : found->second.context;
}

extern "C" void* darling_windows_dispatch_get_specific(const void* key)
{
	if (key == nullptr || current_dispatch_state == nullptr ||
		current_dispatch_state->global_queue) return nullptr;
	std::lock_guard lock(current_dispatch_state->specific_mutex);
	auto found = current_dispatch_state->specific.find(key);
	return found == current_dispatch_state->specific.end() ? nullptr :
		found->second.context;
}

extern "C" void darling_windows_dispatch_main_queue_set_thread(
	std::uint32_t thread_id)
{
	MSG ignored{};
	(void)PeekMessageW(&ignored, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
	main_thread_id.store(thread_id, std::memory_order_release);
	auto* queue = MainQueue();
	std::lock_guard lock(queue->state->mutex);
	if (!queue->state->pending.empty())
		(void)PostThreadMessageW(thread_id, darling_dispatch_main_queue_message, 0, 0);
}

extern "C" bool darling_windows_dispatch_main_queue_handle_message(
	std::uint32_t message)
{
	if (message != darling_dispatch_main_queue_message) return false;
	auto state = MainQueue()->state;
	auto* previous = current_dispatch_state;
	current_dispatch_state = state.get();
	for (;;) {
		DispatchWorkItem item{};
		{
			std::lock_guard lock(state->mutex);
			if (state->pending.empty()) break;
			item = std::move(state->pending.front());
			state->pending.pop_front();
		}
		InvokeTask(item.function);
	}
	current_dispatch_state = previous;
	return true;
}
