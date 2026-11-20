/* Darwin libdispatch queue subset for the Windows host. GPL-3.0-only. */
#pragma once

#include <cstdint>
#include "darling_windows_objc.h"

#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
#define DARLING_DISPATCH_GUEST_ABI __attribute__((sysv_abi))
#else
#define DARLING_DISPATCH_GUEST_ABI
#endif

using darling_dispatch_queue_t = void*;
using darling_dispatch_group_t = void*;
using darling_dispatch_source_t = void*;
using darling_dispatch_semaphore_t = void*;
using darling_dispatch_time_t = std::uint64_t;
struct darling_timespec;
#if defined(__clang__) && defined(_WIN64) && !defined(_M_ARM64) && !defined(__aarch64__)
using darling_dispatch_function_t = void(__attribute__((sysv_abi)) *)(void*);
#else
using darling_dispatch_function_t = void (*)(void*);
#endif

constexpr std::uint32_t darling_dispatch_main_queue_message = 0x8471;
constexpr darling_dispatch_time_t darling_dispatch_time_now = 0;
constexpr darling_dispatch_time_t darling_dispatch_time_forever =
	(static_cast<darling_dispatch_time_t>(-1));
extern "C" const unsigned char _dispatch_source_type_timer;
extern "C" const unsigned char _dispatch_source_type_data_add;
extern "C" const unsigned char _dispatch_source_type_data_or;
extern "C" const unsigned char _dispatch_source_type_data_replace;
extern "C" const unsigned char _dispatch_source_type_read;
extern "C" const unsigned char _dispatch_source_type_write;
extern "C" const unsigned char _dispatch_source_type_proc;
extern "C" const unsigned char _dispatch_source_type_signal;
extern "C" const unsigned char _dispatch_source_type_vnode;
extern "C" const unsigned char _dispatch_source_type_mach_recv;
extern "C" const unsigned char _dispatch_source_type_mach_send;
extern "C" const unsigned char _dispatch_queue_attr_concurrent;
extern "C" int darling_windows_dispatch_poll_descriptor(int descriptor,
	int writable, std::uint64_t* data);

extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_queue_t darling_windows_dispatch_get_main_queue();
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_queue_t darling_windows_dispatch_get_global_queue(
	std::int64_t identifier, std::uint64_t flags);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_queue_t darling_windows_dispatch_queue_create(
	const char* label, const void* attributes);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_queue_t darling_windows_dispatch_retain(
	darling_dispatch_queue_t queue);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_release(darling_dispatch_queue_t queue);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_async_f(darling_dispatch_queue_t queue,
	void* context, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_sync_f(darling_dispatch_queue_t queue,
	void* context, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_barrier_async_f(
	darling_dispatch_queue_t queue, void* context, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_barrier_sync_f(
	darling_dispatch_queue_t queue, void* context, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_async(darling_dispatch_queue_t queue,
	void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_sync(darling_dispatch_queue_t queue,
	void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_barrier_async(
	darling_dispatch_queue_t queue, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_barrier_sync(
	darling_dispatch_queue_t queue, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_after_f(
	darling_dispatch_time_t when, darling_dispatch_queue_t queue, void* context,
	darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_after(
	darling_dispatch_time_t when, darling_dispatch_queue_t queue, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_once_f(
	void* predicate, void* context, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_once(
	void* predicate, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_group_t
	darling_windows_dispatch_group_create();
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_group_t
	darling_windows_dispatch_group_retain(darling_dispatch_group_t group);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_release(darling_dispatch_group_t group);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_enter(darling_dispatch_group_t group);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_leave(darling_dispatch_group_t group);
extern "C" DARLING_DISPATCH_GUEST_ABI std::intptr_t
	darling_windows_dispatch_group_wait(darling_dispatch_group_t group,
	darling_dispatch_time_t timeout);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_async_f(darling_dispatch_group_t group,
	darling_dispatch_queue_t queue, void* context,
	darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_async(darling_dispatch_group_t group,
	darling_dispatch_queue_t queue, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_notify_f(darling_dispatch_group_t group,
	darling_dispatch_queue_t queue, void* context,
	darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_group_notify(darling_dispatch_group_t group,
	darling_dispatch_queue_t queue, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_semaphore_t
	darling_windows_dispatch_semaphore_create(std::intptr_t value);
extern "C" DARLING_DISPATCH_GUEST_ABI std::intptr_t
	darling_windows_dispatch_semaphore_signal(darling_dispatch_semaphore_t semaphore);
extern "C" DARLING_DISPATCH_GUEST_ABI std::intptr_t
	darling_windows_dispatch_semaphore_wait(darling_dispatch_semaphore_t semaphore,
	darling_dispatch_time_t timeout);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_semaphore_dispose(darling_dispatch_semaphore_t semaphore);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_time_t
	darling_windows_dispatch_time(darling_dispatch_time_t when,
	std::int64_t delta_nanoseconds);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_time_t
	darling_windows_dispatch_walltime(const darling_timespec* when,
	std::int64_t delta_nanoseconds);
extern "C" DARLING_DISPATCH_GUEST_ABI const char* darling_windows_dispatch_queue_get_label(
	darling_dispatch_queue_t queue);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_queue_set_specific(
	darling_dispatch_queue_t queue, const void* key, void* context,
	darling_dispatch_function_t destructor);
extern "C" DARLING_DISPATCH_GUEST_ABI void* darling_windows_dispatch_queue_get_specific(
	darling_dispatch_queue_t queue, const void* key);
extern "C" DARLING_DISPATCH_GUEST_ABI void* darling_windows_dispatch_get_specific(
	const void* key);
extern "C" DARLING_DISPATCH_GUEST_ABI darling_dispatch_source_t
	darling_windows_dispatch_source_create(const void* type, std::uintptr_t handle,
	std::uintptr_t mask, darling_dispatch_queue_t queue);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_set_event_handler_f(
	darling_dispatch_source_t source, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_set_event_handler(
	darling_dispatch_source_t source, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_set_cancel_handler_f(
	darling_dispatch_source_t source, darling_dispatch_function_t function);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_set_cancel_handler(
	darling_dispatch_source_t source, void* block);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_set_timer(darling_dispatch_source_t source,
	darling_dispatch_time_t start, std::uint64_t interval, std::uint64_t leeway);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_merge_data(darling_dispatch_source_t source,
	std::uint64_t value);
extern "C" DARLING_DISPATCH_GUEST_ABI void
	darling_windows_dispatch_source_cancel(darling_dispatch_source_t source);
extern "C" DARLING_DISPATCH_GUEST_ABI std::intptr_t
	darling_windows_dispatch_source_testcancel(darling_dispatch_source_t source);
extern "C" DARLING_DISPATCH_GUEST_ABI std::uintptr_t
	darling_windows_dispatch_source_get_handle(darling_dispatch_source_t source);
extern "C" DARLING_DISPATCH_GUEST_ABI std::uintptr_t
	darling_windows_dispatch_source_get_mask(darling_dispatch_source_t source);
extern "C" DARLING_DISPATCH_GUEST_ABI std::uint64_t
	darling_windows_dispatch_source_get_data(darling_dispatch_source_t source);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_set_context(
	darling_dispatch_queue_t object, void* context);
extern "C" DARLING_DISPATCH_GUEST_ABI void* darling_windows_dispatch_get_context(
	darling_dispatch_queue_t object);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_resume(
	darling_dispatch_queue_t object);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_suspend(
	darling_dispatch_queue_t object);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_activate(
	darling_dispatch_queue_t object);
extern "C" DARLING_DISPATCH_GUEST_ABI void darling_windows_dispatch_main_queue_set_thread(
	std::uint32_t thread_id);
extern "C" DARLING_DISPATCH_GUEST_ABI bool darling_windows_dispatch_main_queue_handle_message(
	std::uint32_t message);

#undef DARLING_DISPATCH_GUEST_ABI
