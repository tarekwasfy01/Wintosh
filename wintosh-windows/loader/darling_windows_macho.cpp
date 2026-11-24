/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_macho.h"
#include "darling_windows_stdio.h"
#include "../platform/darling_windows_errno.h"
#include "../platform/darling_windows_signals.h"
#include "darling_windows_abi_bridge.h"
#include "../abi/darling_windows_objc.h"
#include "../abi/darling_windows_mach.h"
#include "darling_windows_tlv.h"
#include "../frameworks/darling_windows_corefoundation.h"
#if defined(WINTOSH_HAS_DYNARMIC)
#include "darling_windows_a64_backend.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace darling::windows_host {

extern "C" void darling_windows_objc_arm64_message_dispatch(
	const void*, SEL, const void*, void*, unsigned, void*);

bool DecodeDarwinX86_64StubFrame(const std::uintptr_t* metadata_stack,
	DarwinX86_64StubFrame* frame) noexcept
{
	if (metadata_stack == nullptr || frame == nullptr)
		return false;
	frame->image_cache = metadata_stack[0];
	frame->lazy_binding_info = metadata_stack[1];
	frame->return_address = metadata_stack[2];
	return frame->image_cache != 0 && frame->lazy_binding_info != 0 &&
		frame->return_address != 0;
}

namespace {
thread_local const MachOLazyBindingSession* active_lazy_binding_session = nullptr;
std::mutex active_lazy_binding_sessions_mutex;
std::vector<const MachOLazyBindingSession*> active_lazy_binding_sessions;
std::atomic<std::uint32_t> next_arm64_import_identifier{1};
#if defined(WINTOSH_HAS_DYNARMIC)
thread_local DarlingWindowsA64Backend* active_arm64_backend = nullptr;
thread_local std::vector<MachOImage::Mapping::Arm64HostImport>*
	active_arm64_host_imports = nullptr;
using Arm64GuestCallbackRunner = std::function<A64ExecutionResult(A64CpuState, unsigned)>;
thread_local Arm64GuestCallbackRunner* active_arm64_callback_runner = nullptr;
thread_local A64CpuState* active_arm64_seed = nullptr;
thread_local unsigned* active_arm64_depth = nullptr;
thread_local const void* active_arm64_dso_handle = nullptr;

bool DispatchArm64MachMsgImport(const std::string& name, std::uintptr_t target,
	const A64CpuState& state, A64CpuState& resumed_state,
	DarlingWindowsA64Backend& backend)
{
	if (name != "_mach_msg") return false;
	const auto& x = state.x;
	constexpr std::uint64_t max_message_bytes = 1ull << 30;
	const auto option = static_cast<std::uint32_t>(x[1]);
	const auto send_size = static_cast<std::uint32_t>(x[2]);
	const auto receive_size = static_cast<std::uint32_t>(x[3]);
	if (x[2] > max_message_bytes || x[3] > max_message_bytes || x[4] >
		std::numeric_limits<darling_mach_port_name_t>::max() || x[5] >
		std::numeric_limits<std::uint32_t>::max() || x[6] >
		std::numeric_limits<darling_mach_port_name_t>::max())
		throw std::runtime_error("ARM64 mach_msg import has an out-of-range argument");
	if ((option & darling_mach_send_msg) != 0 &&
		(send_size < sizeof(darling_mach_msg_header) ||
			!backend.ContainsGuestRange(x[0], send_size, false)))
		throw std::runtime_error("ARM64 mach_msg send has an invalid guest message range");
	if ((option & darling_mach_receive_msg) != 0 &&
		(receive_size < sizeof(darling_mach_msg_header) ||
			!backend.ContainsGuestRange(x[0], receive_size, true)))
		throw std::runtime_error("ARM64 mach_msg receive has an invalid guest message range");
	using Call = darling_kern_return_t (*)(darling_mach_msg_header*, std::uint32_t,
		std::uint32_t, std::uint32_t, darling_mach_port_name_t, std::uint32_t,
		darling_mach_port_name_t);
	resumed_state.x[0] = static_cast<std::uint64_t>(static_cast<std::int64_t>(
		reinterpret_cast<Call>(target)(reinterpret_cast<darling_mach_msg_header*>(
			static_cast<std::uintptr_t>(x[0])), option, send_size, receive_size,
			static_cast<darling_mach_port_name_t>(x[4]),
			static_cast<std::uint32_t>(x[5]),
			static_cast<darling_mach_port_name_t>(x[6]))));
	return true;
}

bool DispatchArm64CoreFoundationRunLoopImport(const std::string& name,
	std::uintptr_t target, const A64CpuState& state, A64CpuState& resumed_state,
	DarlingWindowsA64Backend& backend)
{
	const auto& x = state.x;
	if (name == "_CFURLGetTypeID") {
		using Call = darling_windows_CFTypeID (DARLING_CF_GUEST_ABI *)();
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)());
		return true;
	}
	if (name == "_CFArrayGetTypeID" || name == "_CFDictionaryGetTypeID" ||
		name == "_CFNumberGetTypeID" || name == "_CFDateGetTypeID" ||
		name == "_CFBooleanGetTypeID" || name == "_CFRunLoopGetTypeID" ||
		name == "_CFRunLoopSourceGetTypeID" ||
		name == "_CFRunLoopTimerGetTypeID" ||
		name == "_CFRunLoopObserverGetTypeID" || name == "_CFStringGetTypeID" ||
		name == "_CFMachPortGetTypeID" || name == "_CFAllocatorGetTypeID") {
		using Call = darling_windows_CFTypeID (DARLING_CF_GUEST_ABI *)();
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)());
		return true;
	}
	if (name == "_CFBooleanGetValue") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFBooleanRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFAllocatorCreate") {
		if (x[1] == 0 || !backend.ContainsGuestRange(x[1],
			sizeof(DarlingCFAllocatorContext), false))
			throw std::runtime_error("ARM64 CFAllocatorCreate import received an invalid context range");
		const auto* context = reinterpret_cast<const DarlingCFAllocatorContext*>(
			static_cast<std::uintptr_t>(x[1]));
		const std::array<std::uintptr_t, 7> callbacks{
			reinterpret_cast<std::uintptr_t>(context->retain),
			reinterpret_cast<std::uintptr_t>(context->release),
			reinterpret_cast<std::uintptr_t>(context->copy_description),
			reinterpret_cast<std::uintptr_t>(context->allocate),
			reinterpret_cast<std::uintptr_t>(context->reallocate),
			reinterpret_cast<std::uintptr_t>(context->deallocate),
			reinterpret_cast<std::uintptr_t>(context->preferred_size)};
		for (const auto callback : callbacks)
			if (callback != 0 && !backend.ContainsExecutableGuestAddress(callback))
				throw std::runtime_error("ARM64 CFAllocatorCreate import received a non-executable callback");
		using Call = darling_windows_CFAllocatorRef (DARLING_CF_GUEST_ABI *)(
			const void*, const DarlingCFAllocatorContext*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const DarlingCFAllocatorContext*>(
				static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFAllocatorGetContext") {
		if (x[1] == 0 || !backend.ContainsGuestRange(x[1],
			sizeof(DarlingCFAllocatorContext), true))
			throw std::runtime_error("ARM64 CFAllocatorGetContext import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFAllocatorRef,
			DarlingCFAllocatorContext*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<DarlingCFAllocatorContext*>(static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFAllocatorAllocate") {
		const auto size = static_cast<darling_windows_CFIndex>(x[1]);
		if (size > (1ll << 30))
			throw std::runtime_error("ARM64 CFAllocatorAllocate import exceeds the allocation bound");
		using Call = void* (DARLING_CF_GUEST_ABI *)(darling_windows_CFAllocatorRef,
			darling_windows_CFIndex, std::uint64_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			size, x[2]));
		return true;
	}
	if (name == "_CFAllocatorDeallocate") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFAllocatorRef, void*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFAllocatorReallocate") {
		const auto size = static_cast<darling_windows_CFIndex>(x[2]);
		if (size > (1ll << 30))
			throw std::runtime_error("ARM64 CFAllocatorReallocate import exceeds the allocation bound");
		using Call = void* (DARLING_CF_GUEST_ABI *)(darling_windows_CFAllocatorRef,
			void*, darling_windows_CFIndex, std::uint64_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			size, x[3]));
		return true;
	}
	if (name == "_CFAllocatorGetPreferredSizeForSize") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFAllocatorRef, darling_windows_CFIndex, std::uint64_t);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]), x[2]));
		return true;
	}
	if (name == "_CFAllocatorGetDefault") {
		using Call = darling_windows_CFAllocatorRef (DARLING_CF_GUEST_ABI *)();
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)());
		return true;
	}
	if (name == "_CFAllocatorSetDefault") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFAllocatorRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFURLCreateStringByReplacingPercentEscapes") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFStringRef, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFURLCreateStringByReplacingPercentEscapesUsingEncoding") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFStringRef, darling_windows_CFStringRef, std::uint32_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
			static_cast<std::uint32_t>(x[3])));
		return true;
	}
	if (name == "_CFURLCreateStringByAddingPercentEscapes") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFStringRef, darling_windows_CFStringRef,
			darling_windows_CFStringRef, std::uint32_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])),
			static_cast<std::uint32_t>(x[4])));
		return true;
	}
	if (name == "_CFURLGetByteRangeForComponent") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(darling_windows_CFRange), true))
			throw std::runtime_error("ARM64 CFURLGetByteRangeForComponent import received an invalid output range");
		using Call = darling_windows_CFRange (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFURLRef, darling_windows_CFURLComponentType,
			darling_windows_CFRange*);
		const auto result = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFURLComponentType>(x[1]),
			reinterpret_cast<darling_windows_CFRange*>(static_cast<std::uintptr_t>(x[2])));
		resumed_state.x[0] = static_cast<std::uint64_t>(result.location);
		resumed_state.x[1] = static_cast<std::uint64_t>(result.length);
		return true;
	}
	const auto set_double_return = [&](double value) {
		std::memcpy(&resumed_state.v[0], &value, sizeof(value));
	};
	if (name == "_CFCopyDescription") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(const void*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(
			reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFNumberCreate" || name == "_CFNumberGetValue") {
		const auto type = static_cast<std::int32_t>(x[1]);
		std::size_t scalar_bytes = 0;
		switch (type) {
		case 1: case 7: scalar_bytes = sizeof(std::int8_t); break;
		case 2: case 8: scalar_bytes = sizeof(std::int16_t); break;
		case 3: case 9: scalar_bytes = sizeof(std::int32_t); break;
		case 4: case 10: case 11: case 14: case 15:
			scalar_bytes = sizeof(std::int64_t); break;
		case 5: case 12: scalar_bytes = sizeof(float); break;
		case 6: case 13: case 16: scalar_bytes = sizeof(double); break;
		default: break;
		}
		if (name == "_CFNumberCreate") {
			if (scalar_bytes != 0 && (x[2] == 0 || !backend.ContainsGuestRange(
				x[2], scalar_bytes, false)))
				throw std::runtime_error("ARM64 CFNumberCreate import received an invalid scalar range");
			using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFNumberType, const void*);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])), type,
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2]))));
		} else {
			if (scalar_bytes != 0 && (x[2] == 0 || !backend.ContainsGuestRange(
				x[2], scalar_bytes, true)))
				throw std::runtime_error("ARM64 CFNumberGetValue import received an invalid output range");
			using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFTypeRef, darling_windows_CFNumberType, void*);
			resumed_state.x[0] = reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])), type,
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])));
		}
		return true;
	}
	if (name == "_CFNumberIsFloatType") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFNumberCompare") {
		using Call = std::intptr_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef,
			darling_windows_CFTypeRef, const void*);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFURLCreateWithString") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFStringRef, darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFURLCreateWithFileSystemPath") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFStringRef, std::int32_t, std::uint8_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<std::int32_t>(x[2]), static_cast<std::uint8_t>(x[3])));
		return true;
	}
	if (name == "_CFURLCreateWithFileSystemPathRelativeToBase") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFStringRef, std::int32_t, std::uint8_t,
			darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<std::int32_t>(x[2]), static_cast<std::uint8_t>(x[3]),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4]))));
		return true;
	}
	if (name == "_CFURLCreateFromFileSystemRepresentation") {
		constexpr std::uint64_t max_url_bytes = 1u << 20;
		if (x[2] > max_url_bytes || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), false))))
			throw std::runtime_error("ARM64 CFURLCreateFromFileSystemRepresentation import received an invalid guest byte range");
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
			const std::uint8_t*, darling_windows_CFIndex, std::uint8_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2]), static_cast<std::uint8_t>(x[3])));
		return true;
	}
	if (name == "_CFURLCreateFromFileSystemRepresentationRelativeToBase" ||
		name == "_CFURLCreateWithBytes" || name == "_CFURLCreateAbsoluteURLWithBytes") {
		constexpr std::uint64_t max_url_bytes = 1u << 20;
		constexpr unsigned length_index = 2;
		if (x[length_index] > max_url_bytes || (x[length_index] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[length_index]), false))))
			throw std::runtime_error("ARM64 CFURL byte-based creation import received an invalid guest byte range");
		if (name == "_CFURLCreateFromFileSystemRepresentationRelativeToBase") {
			using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
				const std::uint8_t*, darling_windows_CFIndex, std::uint8_t,
				darling_windows_CFURLRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(x[1])),
				static_cast<darling_windows_CFIndex>(x[2]), static_cast<std::uint8_t>(x[3]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4]))));
		} else if (name == "_CFURLCreateWithBytes") {
			using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
				const std::uint8_t*, darling_windows_CFIndex, std::uint32_t,
				darling_windows_CFURLRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(x[1])),
				static_cast<darling_windows_CFIndex>(x[2]), static_cast<std::uint32_t>(x[3]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4]))));
		} else {
			using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
				const std::uint8_t*, darling_windows_CFIndex, std::uint32_t,
				darling_windows_CFURLRef, std::uint8_t);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(x[1])),
				static_cast<darling_windows_CFIndex>(x[2]), static_cast<std::uint32_t>(x[3]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4])),
				static_cast<std::uint8_t>(x[5])));
		}
		return true;
	}
	if (name == "_CFURLGetString" || name == "_CFURLGetBaseURL" ||
		name == "_CFURLCopyAbsoluteURL") {
		if (name == "_CFURLGetString") {
			using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFURLRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		} else if (name == "_CFURLGetBaseURL") {
			using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFURLRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		} else {
			using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		}
		return true;
	}
	if (name == "_CFURLCanBeDecomposed" || name == "_CFURLHasDirectoryPath") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFURLCopyFileSystemPath") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFURLRef, std::int32_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<std::int32_t>(x[1])));
		return true;
	}
	if (name == "_CFURLCopyScheme") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFURLCopyPath" || name == "_CFURLCopyLastPathComponent" ||
		name == "_CFURLCopyPathExtension" || name == "_CFURLCopyResourceSpecifier" ||
		name == "_CFURLCopyHostName" || name == "_CFURLCopyNetLocation" ||
		name == "_CFURLCopyUserName" || name == "_CFURLCopyPassword") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFURLCopyStrictPath") {
		if (x[1] != 0 && !backend.ContainsGuestRange(x[1], sizeof(std::uint8_t), true))
			throw std::runtime_error("ARM64 CFURLCopyStrictPath import received an invalid output range");
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFURLRef, std::uint8_t*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFURLCopyParameterString" || name == "_CFURLCopyQueryString" ||
		name == "_CFURLCopyFragment") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFURLRef, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFURLCreateCopyAppendingPathExtension") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFURLRef, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFURLGetPortNumber") {
		using Call = std::int32_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFURLGetBytes") {
		constexpr std::uint64_t max_url_bytes = 1u << 20;
		if (x[2] > max_url_bytes || (x[1] != 0 && x[2] != 0 &&
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), true)))
			throw std::runtime_error("ARM64 CFURLGetBytes import received an invalid output range");
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef,
			std::uint8_t*, darling_windows_CFIndex);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2])));
		return true;
	}
	if (name == "_CFURLGetFileSystemRepresentation") {
		constexpr std::uint64_t max_url_bytes = 1u << 20;
		if (x[3] > max_url_bytes || (x[3] != 0 && (x[2] == 0 ||
			!backend.ContainsGuestRange(x[2], static_cast<std::size_t>(x[3]), true))))
			throw std::runtime_error("ARM64 CFURLGetFileSystemRepresentation import received an invalid output range");
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFURLRef,
			std::uint8_t, std::uint8_t*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), static_cast<std::uint8_t>(x[1]),
			reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[2])),
			static_cast<darling_windows_CFIndex>(x[3]));
		return true;
	}
	if (name == "_CFURLCreateCopyAppendingPathComponent") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(const void*,
			darling_windows_CFURLRef, darling_windows_CFStringRef, std::uint8_t);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
			static_cast<std::uint8_t>(x[3])));
		return true;
	}
	if (name == "_CFURLCreateCopyDeletingLastPathComponent" ||
		name == "_CFURLCreateCopyDeletingPathExtension") {
		using Call = darling_windows_CFURLRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFURLRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFStringCreateMutable") {
		using Call = darling_windows_CFMutableStringRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1])));
		return true;
	}
	if (name == "_CFStringCreateMutableCopy") {
		using Call = darling_windows_CFMutableStringRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFStringAppend") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableStringRef,
			darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFStringAppendCString") {
		if (x[1] != 0 && !backend.ContainsGuestCString(x[1], 1u << 20))
			throw std::runtime_error("ARM64 CFStringAppendCString import received an invalid guest string");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableStringRef,
			const char*, std::uint32_t);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const char*>(
			static_cast<std::uintptr_t>(x[1])), static_cast<std::uint32_t>(x[2]));
		return true;
	}
	if (name == "_CFStringGetLength") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
		darling_windows_CFStringRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFStringGetCStringPtr") {
		// A host std::string address is not automatically mapped into guest memory.
		// CoreFoundation permits a null direct pointer, so require the caller to
		// use CFStringGetCString's checked copy-out path instead.
		resumed_state.x[0] = 0;
		return true;
	}
	if (name == "_CFStringCreateCopy") {
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFStringCreateWithCharacters") {
		constexpr std::uint64_t max_string_characters = 1u << 20;
		if (x[2] > max_string_characters || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]) *
				sizeof(std::uint16_t), false))))
			throw std::runtime_error("ARM64 CFStringCreateWithCharacters import received an invalid guest range");
		using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
			const void*, const std::uint16_t*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2])));
		return true;
	}
	if (name == "_CFStringGetCString") {
		constexpr std::uint64_t max_string_buffer_bytes = 1u << 20;
		if (x[2] > max_string_buffer_bytes || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), true))))
			throw std::runtime_error("ARM64 CFStringGetCString import received an invalid output range");
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			char*, darling_windows_CFIndex, std::uint32_t);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<char*>(
			static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2]), static_cast<std::uint32_t>(x[3]));
		return true;
	}
	if (name == "_CFStringGetCharacterAtIndex") {
		using Call = std::uint16_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]));
		return true;
	}
	if (name == "_CFStringGetCharacters") {
		constexpr std::uint64_t max_string_characters = 1u << 20;
		const auto length = static_cast<darling_windows_CFIndex>(x[2]);
		if (length > static_cast<darling_windows_CFIndex>(max_string_characters) ||
			(length > 0 && (x[3] == 0 || !backend.ContainsGuestRange(x[3],
				static_cast<std::size_t>(length) * sizeof(std::uint16_t), true))))
			throw std::runtime_error("ARM64 CFStringGetCharacters import received an invalid output range");
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			darling_windows_CFRange, std::uint16_t*);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]), length},
			reinterpret_cast<std::uint16_t*>(static_cast<std::uintptr_t>(x[3])));
		return true;
	}
	if (name == "_CFStringGetMaximumSizeForEncoding") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFIndex, std::uint32_t);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			static_cast<darling_windows_CFIndex>(x[0]), static_cast<std::uint32_t>(x[1])));
		return true;
	}
	if (name == "_CFStringGetMaximumSizeOfFileSystemRepresentation") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFStringRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFStringGetFileSystemRepresentation") {
		constexpr std::uint64_t max_string_buffer_bytes = 1u << 20;
		if (x[2] > max_string_buffer_bytes || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), true))))
			throw std::runtime_error("ARM64 CFStringGetFileSystemRepresentation import received an invalid output range");
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			char*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<char*>(
			static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2]));
		return true;
	}
	if (name == "_CFDataCreate") {
		constexpr std::uint64_t max_data_bytes = 1ull << 30;
		if (x[2] > max_data_bytes || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), false))))
			throw std::runtime_error("ARM64 CFDataCreate import received an invalid guest byte range");
		using Call = darling_windows_CFDataRef (DARLING_CF_GUEST_ABI *)(
			const void*, const unsigned char*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2])));
		return true;
	}
	if (name == "_CFDataCreateMutable") {
		using Call = darling_windows_CFMutableDataRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1])));
		return true;
	}
	if (name == "_CFDataCreateCopy") {
		using Call = darling_windows_CFDataRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFDataRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFDataGetLength") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(darling_windows_CFDataRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFDataGetBytePtr" || name == "_CFDataGetMutableBytePtr") {
		if (name == "_CFDataGetBytePtr") {
			using Call = const unsigned char* (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFDataRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		} else {
			using Call = unsigned char* (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFMutableDataRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		}
		return true;
	}
	if (name == "_CFDataGetBytes") {
		constexpr std::uint64_t max_data_bytes = 1ull << 30;
		if (x[2] > max_data_bytes || (x[2] != 0 && (x[3] == 0 ||
			!backend.ContainsGuestRange(x[3], static_cast<std::size_t>(x[2]), true))))
			throw std::runtime_error("ARM64 CFDataGetBytes import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDataRef,
			darling_windows_CFRange, unsigned char*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(x[3])));
		return true;
	}
	if (name == "_CFDataAppendBytes") {
		constexpr std::uint64_t max_data_bytes = 1ull << 30;
		if (x[2] > max_data_bytes || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), false))))
			throw std::runtime_error("ARM64 CFDataAppendBytes import received an invalid guest byte range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableDataRef,
			const unsigned char*, darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const unsigned char*>(
			static_cast<std::uintptr_t>(x[1])), static_cast<darling_windows_CFIndex>(x[2]));
		return true;
	}
	if (name == "_CFDataReplaceBytes") {
		constexpr std::uint64_t max_data_bytes = 1ull << 30;
		if (x[4] > max_data_bytes || (x[4] != 0 && (x[3] == 0 ||
			!backend.ContainsGuestRange(x[3], static_cast<std::size_t>(x[4]), false))))
			throw std::runtime_error("ARM64 CFDataReplaceBytes import received an invalid replacement range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableDataRef,
			darling_windows_CFRange, const unsigned char*, darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(x[3])),
			static_cast<darling_windows_CFIndex>(x[4]));
		return true;
	}
	if (name == "_CFDataSetLength") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableDataRef,
			darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]));
		return true;
	}
	if (name == "_CFStringAppendCharacters") {
		constexpr std::uint64_t max_string_characters = 1u << 20;
		if (x[2] > max_string_characters || (x[2] != 0 && (x[1] == 0 ||
			!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]) *
				sizeof(std::uint16_t), false))))
			throw std::runtime_error("ARM64 CFStringAppendCharacters import received an invalid guest range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableStringRef,
			const std::uint16_t*, darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const std::uint16_t*>(
			static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2]));
		return true;
	}
	if (name == "_CFStringReplace") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMutableStringRef,
			darling_windows_CFRange, darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])));
		return true;
	}
	if (name == "_CFPreferencesCopyAppValue" || name == "_CFPreferencesCopyValue") {
		if (name == "_CFPreferencesCopyAppValue") {
			using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFStringRef, darling_windows_CFStringRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFStringRef, darling_windows_CFStringRef,
				darling_windows_CFStringRef, darling_windows_CFStringRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3]))));
		}
		return true;
	}
	if (name == "_CFPreferencesSetAppValue" || name == "_CFPreferencesSetValue") {
		if (name == "_CFPreferencesSetAppValue") {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
				darling_windows_CFTypeRef, darling_windows_CFStringRef);
			reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])));
		} else {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
				darling_windows_CFTypeRef, darling_windows_CFStringRef,
				darling_windows_CFStringRef, darling_windows_CFStringRef);
			reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4])));
		}
		return true;
	}
	if (name == "_CFPreferencesAppSynchronize") {
		using Call = bool (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))) ? 1u : 0u;
		return true;
	}
	if (name == "_CFPreferencesSynchronize") {
		using Call = bool (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			darling_windows_CFStringRef, darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))) ? 1u : 0u;
		return true;
	}
	if (name == "_CFPreferencesCopyMultiple" || name == "_CFPreferencesCopyKeyList") {
		if (name == "_CFPreferencesCopyMultiple") {
			using Call = darling_windows_CFDictionaryRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFArrayRef, darling_windows_CFStringRef,
				darling_windows_CFStringRef, darling_windows_CFStringRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3]))));
		} else {
			using Call = darling_windows_CFArrayRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFStringRef, darling_windows_CFStringRef,
				darling_windows_CFStringRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		}
		return true;
	}
	if (name == "_CFPreferencesSetMultiple") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDictionaryRef,
			darling_windows_CFArrayRef, darling_windows_CFStringRef,
			darling_windows_CFStringRef, darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4])));
		return true;
	}
	if (name == "_CFPreferencesGetAppBooleanValue" ||
		name == "_CFPreferencesGetAppIntegerValue") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2], sizeof(std::uint8_t), true))
			throw std::runtime_error("ARM64 CFPreferences scalar import received an invalid output pointer");
		if (name == "_CFPreferencesGetAppBooleanValue") {
			using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
				darling_windows_CFStringRef, std::uint8_t*);
			resumed_state.x[0] = reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[2])));
		} else {
			using Call = std::intptr_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
				darling_windows_CFStringRef, std::uint8_t*);
			resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[2]))));
		}
		return true;
	}
	if (name == "_CFPreferencesAppValueIsForced") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFPreferencesAddSuitePreferencesToApp" ||
		name == "_CFPreferencesRemoveSuitePreferencesFromApp") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFStringRef,
			darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFDateCreate") {
		double absolute_time{};
		std::memcpy(&absolute_time, &state.v[0], sizeof(absolute_time));
		using Call = darling_windows_CFDateRef (DARLING_CF_GUEST_ABI *)(
			const void*, double);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			absolute_time));
		return true;
	}
	if (name == "_CFDateGetAbsoluteTime") {
		using Call = double (DARLING_CF_GUEST_ABI *)(darling_windows_CFDateRef);
		set_double_return(reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFDateCompare") {
		using Call = std::intptr_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFDateRef,
			darling_windows_CFDateRef, const void*);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFDateGetTimeIntervalSinceDate") {
		using Call = double (DARLING_CF_GUEST_ABI *)(darling_windows_CFDateRef,
			darling_windows_CFDateRef);
		set_double_return(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFRunLoopGetCurrent" || name == "_CFRunLoopGetMain") {
		using Call = darling_windows_CFRunLoopRef (DARLING_CF_GUEST_ABI *)();
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(
			reinterpret_cast<Call>(target)());
		return true;
	}
	if (name == "_CFRunLoopCopyCurrentMode" || name == "_CFRunLoopCopyAllModes") {
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
		darling_windows_CFRunLoopRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFRunLoopStop" || name == "_CFRunLoopWakeUp") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopIsWaiting") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFRunLoopRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopAddCommonMode") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef,
			darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFArrayGetCount") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFArrayRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFArrayExchangeValuesAtIndices") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
			darling_windows_CFIndex, darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			static_cast<darling_windows_CFIndex>(x[2]));
		return true;
	}
	if (name == "_CFArrayCreateMutable") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(DarlingCFArrayCallbacks), false))
			throw std::runtime_error("ARM64 CFArrayCreateMutable import received an invalid callback range");
		using Call = darling_windows_CFArrayRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex, const DarlingCFArrayCallbacks*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			reinterpret_cast<const DarlingCFArrayCallbacks*>(
				static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFArrayCreateCopy" || name == "_CFArrayCreateMutableCopy") {
		using Call = darling_windows_CFArrayRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex, darling_windows_CFArrayRef);
		if (name == "_CFArrayCreateCopy") {
			using CopyCall = darling_windows_CFArrayRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFArrayRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<CopyCall>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				static_cast<darling_windows_CFIndex>(x[1]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		}
		return true;
	}
	if (name == "_CFArrayGetValueAtIndex") {
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFArrayRef, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1])));
		return true;
	}
	if (name == "_CFArrayAppendValue" || name == "_CFArrayInsertValueAtIndex" ||
		name == "_CFArraySetValueAtIndex" || name == "_CFArrayRemoveValueAtIndex") {
		if (name == "_CFArrayAppendValue") {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
				darling_windows_CFTypeRef);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[1])));
		} else if (name == "_CFArrayRemoveValueAtIndex") {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
				darling_windows_CFIndex);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), static_cast<darling_windows_CFIndex>(x[1]));
		} else {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
				darling_windows_CFIndex, darling_windows_CFTypeRef);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), static_cast<darling_windows_CFIndex>(x[1]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])));
		}
		return true;
	}
	if (name == "_CFArrayGetValues") {
		const auto start = static_cast<darling_windows_CFIndex>(x[1]);
		const auto length = static_cast<darling_windows_CFIndex>(x[2]);
		if (length < 0 || length > (1 << 20) || (length != 0 &&
			(x[3] == 0 || !backend.ContainsGuestRange(x[3],
				static_cast<std::size_t>(length) * sizeof(void*), true))))
			throw std::runtime_error("ARM64 CFArrayGetValues import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
			darling_windows_CFRange, const void**);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), {start, length},
			reinterpret_cast<const void**>(static_cast<std::uintptr_t>(x[3])));
		return true;
	}
	if (name == "_CFArrayContainsValue") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
			darling_windows_CFRange, darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])));
		return true;
	}
	if (name == "_CFArrayGetCountOfValue" ||
		name == "_CFArrayGetFirstIndexOfValue" ||
		name == "_CFArrayGetLastIndexOfValue") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFArrayRef, darling_windows_CFRange,
			darling_windows_CFTypeRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3]))));
		return true;
	}
	if (name == "_CFArrayReplaceValues") {
		constexpr std::uint64_t max_replacement_elements = 1u << 20;
		const auto count = x[4];
		if (count > max_replacement_elements || (count != 0 && (x[3] == 0 ||
			!backend.ContainsGuestRange(x[3],
				static_cast<std::size_t>(count) * sizeof(void*), false))))
			throw std::runtime_error("ARM64 CFArrayReplaceValues import received an invalid replacement range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
			darling_windows_CFRange, const void* const*, darling_windows_CFIndex);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(x[3])),
			static_cast<darling_windows_CFIndex>(count));
		return true;
	}
	if (name == "_CFArrayAppendArray") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
			darling_windows_CFArrayRef, darling_windows_CFRange);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])),
			{static_cast<darling_windows_CFIndex>(x[2]),
			 static_cast<darling_windows_CFIndex>(x[3])});
		return true;
	}
	if (name == "_CFArrayApplyFunction" || name == "_CFArraySortValues") {
		if (name == "_CFArrayApplyFunction") {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
				darling_windows_CFRange, darling_windows_CFArrayGuestApplierFunction,
				void*);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])),
				{static_cast<darling_windows_CFIndex>(x[1]),
				 static_cast<darling_windows_CFIndex>(x[2])},
				reinterpret_cast<darling_windows_CFArrayGuestApplierFunction>(
					static_cast<std::uintptr_t>(x[3])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4])));
		} else {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef,
				darling_windows_CFRange, darling_windows_CFComparatorFunction, void*);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])),
				{static_cast<darling_windows_CFIndex>(x[1]),
				 static_cast<darling_windows_CFIndex>(x[2])},
				reinterpret_cast<darling_windows_CFComparatorFunction>(
					static_cast<std::uintptr_t>(x[3])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[4])));
		}
		return true;
	}
	if (name == "_CFArrayBSearchValues") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFArrayRef, darling_windows_CFRange,
			darling_windows_CFTypeRef, darling_windows_CFComparatorFunction, void*);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			{static_cast<darling_windows_CFIndex>(x[1]),
			 static_cast<darling_windows_CFIndex>(x[2])},
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[3])),
			reinterpret_cast<darling_windows_CFComparatorFunction>(
				static_cast<std::uintptr_t>(x[4])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[5]))));
		return true;
	}
	if (name == "_CFArrayRemoveAllValues") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFArrayRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFArrayCreate") {
		constexpr std::uint64_t max_array_elements = 1u << 20;
		if (x[2] > max_array_elements || (x[2] != 0 &&
			(x[1] == 0 || !backend.ContainsGuestRange(x[1],
				static_cast<std::size_t>(x[2]) * sizeof(void*), false))) ||
			(x[3] != 0 && !backend.ContainsGuestRange(x[3],
				sizeof(DarlingCFArrayCallbacks), false)))
			throw std::runtime_error("ARM64 CFArrayCreate import received an invalid guest range");
		using Call = darling_windows_CFArrayRef (DARLING_CF_GUEST_ABI *)(
			const void*, const void* const*, darling_windows_CFIndex,
			const DarlingCFArrayCallbacks*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2]),
			reinterpret_cast<const DarlingCFArrayCallbacks*>(
				static_cast<std::uintptr_t>(x[3]))));
		return true;
	}
	if (name == "_CFDictionaryCreate") {
		constexpr std::uint64_t max_dictionary_entries = 1u << 20;
		const auto count = x[3];
		const auto pointer_bytes = count <= max_dictionary_entries ?
			static_cast<std::size_t>(count) * sizeof(void*) : 0;
		if (count > max_dictionary_entries || (count != 0 &&
			(x[1] == 0 || x[2] == 0 ||
				!backend.ContainsGuestRange(x[1], pointer_bytes, false) ||
				!backend.ContainsGuestRange(x[2], pointer_bytes, false))) ||
			(x[4] != 0 && !backend.ContainsGuestRange(x[4],
				sizeof(DarlingCFDictionaryKeyCallbacks), false)) ||
			(x[5] != 0 && !backend.ContainsGuestRange(x[5],
				sizeof(DarlingCFDictionaryValueCallbacks), false)))
			throw std::runtime_error("ARM64 CFDictionaryCreate import received an invalid guest range");
		using Call = darling_windows_CFDictionaryRef (DARLING_CF_GUEST_ABI *)(
			const void*, const void* const*, const void* const*,
			darling_windows_CFIndex, const void*, const void*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(x[2])),
			static_cast<darling_windows_CFIndex>(count),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[4])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[5]))));
		return true;
	}
	if (name == "_CFDictionaryGetCount") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFDictionaryRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFDictionaryCreateCopy" ||
		name == "_CFDictionaryCreateMutableCopy") {
		if (name == "_CFDictionaryCreateCopy") {
			using Call = darling_windows_CFDictionaryRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFDictionaryRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			using Call = darling_windows_CFDictionaryRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFIndex, darling_windows_CFDictionaryRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				static_cast<darling_windows_CFIndex>(x[1]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		}
		return true;
	}
	if (name == "_CFDictionaryGetValueIfPresent") {
		if (x[2] == 0 || !backend.ContainsGuestRange(x[2], sizeof(void*), true))
			throw std::runtime_error("ARM64 CFDictionaryGetValueIfPresent import received an invalid output pointer");
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFDictionaryRef, const void*, const void**);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void**>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFDictionaryContainsValue") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFDictionaryRef, const void*);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFDictionaryGetKeysAndValues") {
		const auto count = darling_windows_CFDictionaryGetCount(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		if (count < 0 || count > (1 << 20) || (count != 0 &&
			((x[1] == 0 || !backend.ContainsGuestRange(x[1],
				static_cast<std::size_t>(count) * sizeof(void*), true)) ||
			 (x[2] == 0 || !backend.ContainsGuestRange(x[2],
				static_cast<std::size_t>(count) * sizeof(void*), true)))))
			throw std::runtime_error("ARM64 CFDictionaryGetKeysAndValues import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDictionaryRef,
			const void**, const void**);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void**>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void**>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFDictionaryApplyFunction") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDictionaryRef,
			darling_windows_CFDictionaryGuestApplierFunction, void*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
		reinterpret_cast<darling_windows_CFDictionaryGuestApplierFunction>(
				static_cast<std::uintptr_t>(x[1])),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFDictionaryCreateMutable") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(DarlingCFDictionaryKeyCallbacks), false))
			throw std::runtime_error("ARM64 CFDictionaryCreateMutable import received an invalid callback range");
		if (x[3] != 0 && !backend.ContainsGuestRange(x[3],
			sizeof(DarlingCFDictionaryValueCallbacks), false))
			throw std::runtime_error("ARM64 CFDictionaryCreateMutable import received an invalid value callback range");
		using Call = darling_windows_CFDictionaryRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex, const void*, const void*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[3]))));
		return true;
	}
	if (name == "_CFDictionaryGetValue" || name == "_CFDictionaryContainsKey" ||
		name == "_CFDictionarySetValue" || name == "_CFDictionaryAddValue" ||
		name == "_CFDictionaryReplaceValue" || name == "_CFDictionaryRemoveValue") {
		if (name == "_CFDictionaryContainsKey") {
			using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFDictionaryRef, const void*);
			resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[1])));
		} else if (name == "_CFDictionaryGetValue") {
			using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFDictionaryRef, const void*);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDictionaryRef,
				const void*, const void*);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[2])));
		}
		return true;
	}
	if (name == "_CFDictionaryRemoveAllValues") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFDictionaryRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFSetCreate") {
		constexpr std::uint64_t max_set_elements = 1u << 20;
		const auto count = x[2];
		const auto pointer_bytes = count <= max_set_elements ?
			static_cast<std::size_t>(count) * sizeof(void*) : 0;
		if (count > max_set_elements || (count != 0 &&
			(x[1] == 0 || !backend.ContainsGuestRange(x[1], pointer_bytes, false))) ||
			(x[3] != 0 && !backend.ContainsGuestRange(x[3],
				sizeof(DarlingCFDictionaryKeyCallbacks), false)))
			throw std::runtime_error("ARM64 CFSetCreate import received an invalid guest range");
		using Call = darling_windows_CFSetRef (DARLING_CF_GUEST_ABI *)(
			const void*, const void* const*, darling_windows_CFIndex, const void*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(count),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[3]))));
		return true;
	}
	if (name == "_CFSetGetTypeID") {
		using Call = darling_windows_CFTypeID (DARLING_CF_GUEST_ABI *)();
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)());
		return true;
	}
	if (name == "_CFSetGetCount") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFSetRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFSetCreateCopy" || name == "_CFSetCreateMutableCopy") {
		if (name == "_CFSetCreateCopy") {
			using Call = darling_windows_CFSetRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFSetRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			using Call = darling_windows_CFSetRef (DARLING_CF_GUEST_ABI *)(
				const void*, darling_windows_CFIndex, darling_windows_CFSetRef);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
				static_cast<darling_windows_CFIndex>(x[1]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
		}
		return true;
	}
	if (name == "_CFSetGetValue" || name == "_CFSetGetValueIfPresent") {
		if (name == "_CFSetGetValueIfPresent" && (x[2] == 0 ||
			!backend.ContainsGuestRange(x[2], sizeof(void*), true)))
			throw std::runtime_error("ARM64 CFSetGetValueIfPresent import received an invalid output pointer");
		if (name == "_CFSetGetValue") {
			using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
				darling_windows_CFSetRef, const void*);
			resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
				reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1]))));
		} else {
			using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef,
				const void*, const void**);
			resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void**>(
				static_cast<std::uintptr_t>(x[2])));
		}
		return true;
	}
	if (name == "_CFSetGetCountOfValue") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFSetRef, const void*);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFSetGetValues") {
		const auto count = darling_windows_CFSetGetCount(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		if (count < 0 || count > (1 << 20) || (count != 0 &&
			(x[1] == 0 || !backend.ContainsGuestRange(x[1],
				static_cast<std::size_t>(count) * sizeof(void*), true))))
			throw std::runtime_error("ARM64 CFSetGetValues import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef, const void**);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void**>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFSetApplyFunction") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef,
			darling_windows_CFSetGuestApplierFunction, void*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
		reinterpret_cast<darling_windows_CFSetGuestApplierFunction>(
				static_cast<std::uintptr_t>(x[1])),
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFSetCreateMutable") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(DarlingCFDictionaryKeyCallbacks), false))
			throw std::runtime_error("ARM64 CFSetCreateMutable import received an invalid callback range");
		using Call = darling_windows_CFSetRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex, const void*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFSetContainsValue" || name == "_CFSetAddValue" ||
		name == "_CFSetSetValue" || name == "_CFSetReplaceValue" ||
		name == "_CFSetRemoveValue") {
		if (name == "_CFSetContainsValue") {
			using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef,
				const void*);
			resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[1])));
		} else {
			using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef,
				const void*);
			reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
				static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(x[1])));
		}
		return true;
	}
	if (name == "_CFSetRemoveAllValues") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFSetUnion" || name == "_CFSetIntersect" ||
		name == "_CFSetSubtract") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFSetRef,
			darling_windows_CFSetRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFRunLoopPerformBlock") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef,
			darling_windows_CFTypeRef, void*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFRunLoopContainsSource" ||
		name == "_CFRunLoopContainsTimer" ||
		name == "_CFRunLoopContainsObserver") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(
		darling_windows_CFRunLoopRef, darling_windows_CFTypeRef,
		darling_windows_CFStringRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFRunLoopTimerCreate") {
		if (x[4] != 0 && !backend.ContainsGuestRange(x[4],
			sizeof(DarlingCFRunLoopTimerContext), false))
			throw std::runtime_error("ARM64 CoreFoundation timer import received an invalid context");
		double fire_date{};
		double interval{};
		std::memcpy(&fire_date, &state.v[0], sizeof(fire_date));
		std::memcpy(&interval, &state.v[1], sizeof(interval));
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
			const void*, double, double, std::uint64_t, darling_windows_CFIndex,
			DarlingCFRunLoopTimerCallout, const DarlingCFRunLoopTimerContext*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			fire_date, interval, x[1], static_cast<darling_windows_CFIndex>(x[2]),
			reinterpret_cast<DarlingCFRunLoopTimerCallout>(
				static_cast<std::uintptr_t>(x[3])),
				reinterpret_cast<const DarlingCFRunLoopTimerContext*>(
				static_cast<std::uintptr_t>(x[4]))));
		return true;
	}
	if (name == "_CFRunLoopAddTimer" || name == "_CFRunLoopRemoveTimer") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef,
			darling_windows_CFTypeRef, darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFRunLoopTimerInvalidate") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopSourceSignal" ||
		name == "_CFRunLoopSourceInvalidate") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopSourceIsValid") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopSourceGetOrder") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFTypeRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFRunLoopSourceGetContext") {
		if (x[1] == 0 || !backend.ContainsGuestRange(x[1],
			sizeof(DarlingCFRunLoopSourceContext), true))
			throw std::runtime_error("ARM64 CoreFoundation source context import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef,
			DarlingCFRunLoopSourceContext*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<DarlingCFRunLoopSourceContext*>(
				static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFRunLoopSourceCreate") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(DarlingCFRunLoopSourceContext), false))
			throw std::runtime_error("ARM64 CoreFoundation source import received an invalid context");
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFIndex,
			const DarlingCFRunLoopSourceContext*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<darling_windows_CFIndex>(x[1]),
			reinterpret_cast<const DarlingCFRunLoopSourceContext*>(
				static_cast<std::uintptr_t>(x[2]))));
		return true;
	}
	if (name == "_CFRunLoopAddSource" || name == "_CFRunLoopRemoveSource") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef,
			darling_windows_CFTypeRef, darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFMachPortCreate") {
		if (x[2] != 0 && !backend.ContainsGuestRange(x[2],
			sizeof(DarlingCFMachPortContext), false))
			throw std::runtime_error("ARM64 CFMachPortCreate received an invalid context");
		if (x[3] != 0 && !backend.ContainsGuestRange(x[3], sizeof(std::uint8_t), true))
			throw std::runtime_error("ARM64 CFMachPortCreate received an invalid output range");
		using Call = darling_windows_CFMachPortRef (DARLING_CF_GUEST_ABI *)(
			const void*, DarlingCFMachPortCallout, const DarlingCFMachPortContext*,
			std::uint8_t*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<DarlingCFMachPortCallout>(static_cast<std::uintptr_t>(x[1])),
			reinterpret_cast<const DarlingCFMachPortContext*>(static_cast<std::uintptr_t>(x[2])),
			reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[3]))));
		return true;
	}
	if (name == "_CFMachPortCreateWithPort") {
		if (x[3] != 0 && !backend.ContainsGuestRange(x[3],
			sizeof(DarlingCFMachPortContext), false))
			throw std::runtime_error("ARM64 CFMachPortCreateWithPort received an invalid context");
		if (x[4] != 0 && !backend.ContainsGuestRange(x[4], sizeof(std::uint8_t), true))
			throw std::runtime_error("ARM64 CFMachPortCreateWithPort received an invalid output range");
		using Call = darling_windows_CFMachPortRef (DARLING_CF_GUEST_ABI *)(
			const void*, std::uint32_t, DarlingCFMachPortCallout,
			const DarlingCFMachPortContext*, std::uint8_t*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			static_cast<std::uint32_t>(x[1]),
			reinterpret_cast<DarlingCFMachPortCallout>(static_cast<std::uintptr_t>(x[2])),
			reinterpret_cast<const DarlingCFMachPortContext*>(static_cast<std::uintptr_t>(x[3])),
			reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(x[4]))));
		return true;
	}
	if (name == "_CFMachPortCreateRunLoopSource") {
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
			const void*, darling_windows_CFMachPortRef, darling_windows_CFIndex);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1])),
			static_cast<darling_windows_CFIndex>(x[2])));
		return true;
	}
	if (name == "_CFMachPortGetContext") {
		if (x[1] == 0 || !backend.ContainsGuestRange(x[1],
			sizeof(DarlingCFMachPortContext), true))
			throw std::runtime_error("ARM64 CFMachPortGetContext received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFMachPortRef, DarlingCFMachPortContext*);
		reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<DarlingCFMachPortContext*>(static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFMachPortGetPort") {
		using Call = std::uint32_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFMachPortRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFMachPortIsValid") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFMachPortRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFMachPortInvalidate") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMachPortRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFMachPortSetInvalidationCallBack") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFMachPortRef,
			DarlingCFMachPortInvalidationCallout);
		reinterpret_cast<Call>(target)(reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<DarlingCFMachPortInvalidationCallout>(
				static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFMachPortGetInvalidationCallBack") {
		using Call = DarlingCFMachPortInvalidationCallout (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFMachPortRef);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFRunLoopTimerGetContext" ||
		name == "_CFRunLoopObserverGetContext") {
		const auto context_size = name == "_CFRunLoopTimerGetContext" ?
			sizeof(DarlingCFRunLoopTimerContext) :
			sizeof(DarlingCFRunLoopObserverContext);
		if (x[1] == 0 || !backend.ContainsGuestRange(x[1], context_size, true))
			throw std::runtime_error("ARM64 CoreFoundation run-loop context import received an invalid output range");
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef, void*);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])));
		return true;
	}
	if (name == "_CFRunLoopObserverCreate") {
		if (x[5] != 0 && !backend.ContainsGuestRange(x[5],
			sizeof(DarlingCFRunLoopObserverContext), false))
			throw std::runtime_error("ARM64 CoreFoundation observer import received an invalid context");
		using Call = darling_windows_CFTypeRef (DARLING_CF_GUEST_ABI *)(
			const void*, std::uint64_t, std::uint8_t, darling_windows_CFIndex,
			DarlingCFRunLoopObserverCallout, const DarlingCFRunLoopObserverContext*);
		resumed_state.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])), x[1],
			static_cast<std::uint8_t>(x[2]), static_cast<darling_windows_CFIndex>(x[3]),
			reinterpret_cast<DarlingCFRunLoopObserverCallout>(
				static_cast<std::uintptr_t>(x[4])),
			reinterpret_cast<const DarlingCFRunLoopObserverContext*>(
				static_cast<std::uintptr_t>(x[5]))));
		return true;
	}
	if (name == "_CFRunLoopAddObserver" || name == "_CFRunLoopRemoveObserver") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFRunLoopRef,
			darling_windows_CFTypeRef, darling_windows_CFStringRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])), reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[1])), reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(x[2])));
		return true;
	}
	if (name == "_CFRunLoopObserverInvalidate") {
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		reinterpret_cast<Call>(target)(reinterpret_cast<void*>(
			static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopRunInMode") {
		double seconds{};
		std::memcpy(&seconds, &state.v[0], sizeof(seconds));
		using Call = std::int32_t (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFStringRef, double, std::uint8_t);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])), seconds,
			static_cast<std::uint8_t>(x[1])));
		return true;
	}
	if (name == "_CFRunLoopTimerGetNextFireDate" ||
		name == "_CFRunLoopTimerGetTolerance" ||
		name == "_CFRunLoopTimerGetInterval") {
		using Call = double (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		set_double_return(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFRunLoopTimerSetNextFireDate" ||
		name == "_CFRunLoopTimerSetTolerance") {
		double value{};
		std::memcpy(&value, &state.v[0], sizeof(value));
		using Call = void (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef, double);
		reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])), value);
		return true;
	}
	if (name == "_CFRunLoopGetNextTimerFireDate") {
		using Call = double (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFRunLoopRef, darling_windows_CFStringRef);
		set_double_return(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1]))));
		return true;
	}
	if (name == "_CFRunLoopTimerDoesRepeat" ||
		name == "_CFRunLoopTimerIsValid") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopObserverGetActivities") {
		using Call = std::uint64_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopObserverDoesRepeat" ||
		name == "_CFRunLoopObserverIsValid") {
		using Call = std::uint8_t (DARLING_CF_GUEST_ABI *)(darling_windows_CFTypeRef);
		resumed_state.x[0] = reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
		return true;
	}
	if (name == "_CFRunLoopObserverGetOrder") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFTypeRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	if (name == "_CFRunLoopTimerGetOrder") {
		using Call = darling_windows_CFIndex (DARLING_CF_GUEST_ABI *)(
			darling_windows_CFTypeRef);
		resumed_state.x[0] = static_cast<std::uint64_t>(reinterpret_cast<Call>(target)(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
		return true;
	}
	return false;
}
#endif
using DarlingHostMallocImport = void* (DARLING_HOST_GUEST_ABI *)(std::size_t);
using DarlingHostFreeImport = void (DARLING_HOST_GUEST_ABI *)(void*);
using DarlingHostReallocImport = void* (DARLING_HOST_GUEST_ABI *)(
	void*, std::size_t);
using DarlingHostMemcpyImport = void* (DARLING_HOST_GUEST_ABI *)(
	void*, const void*, std::size_t);
using DarlingObjcUnaryImport = void* (DARLING_HOST_GUEST_ABI *)(void*);
using DarlingObjcReleaseImport = void (DARLING_HOST_GUEST_ABI *)(void*);
using DarlingObjcNameImport = std::uintptr_t (DARLING_HOST_GUEST_ABI *)(
	const char*);
using DarlingBlockCopyImport = void* (DARLING_HOST_GUEST_ABI *)(const void*);
using DarlingBlockReleaseImport = void (DARLING_HOST_GUEST_ABI *)(const void*);
using DarlingBlockObjectAssignImport = void (DARLING_HOST_GUEST_ABI *)(
	void*, const void*, int);
using DarlingBlockObjectDisposeImport = void (DARLING_HOST_GUEST_ABI *)(
	const void*, int);

void DARLING_HOST_GUEST_ABI FreeStandardHostAllocation(void* allocation)
{
	std::free(allocation);
}

void RemoveActiveLazyBindingSession(const MachOLazyBindingSession* session) noexcept
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	const auto found = std::find(active_lazy_binding_sessions.rbegin(),
		active_lazy_binding_sessions.rend(), session);
	if (found != active_lazy_binding_sessions.rend())
		active_lazy_binding_sessions.erase(std::next(found).base());
}
}

#if (defined(_WIN64) && defined(__clang__)) || defined(_M_IX86)
extern "C" void darling_windows_enter_darwin_start(void* entry, void* darwin_stack);
#endif

namespace {

#pragma pack(push, 1)
struct ChainedFixupsHeader final {
	std::uint32_t version;
	std::uint32_t starts_offset;
	std::uint32_t imports_offset;
	std::uint32_t symbols_offset;
	std::uint32_t imports_count;
	std::uint32_t imports_format;
	std::uint32_t symbols_format;
};

struct ChainedStartsSegment final {
	std::uint32_t size;
	std::uint16_t page_size;
	std::uint16_t pointer_format;
	std::uint64_t segment_offset;
	std::uint32_t max_valid_pointer;
	std::uint16_t page_count;
};
#pragma pack(pop)

class DeflateBitReader final {
public:
	explicit DeflateBitReader(const std::uint8_t* bytes, std::size_t size) :
		m_bytes(bytes), m_size(size) {}

	std::uint32_t Read(unsigned count)
	{
		if (count > 16)
			throw std::runtime_error("invalid zlib deflate bit count");
		std::uint32_t value = 0;
		for (unsigned bit = 0; bit < count; ++bit) {
			if (m_offset / 8 >= m_size)
				throw std::runtime_error("truncated zlib deflate stream");
			value |= static_cast<std::uint32_t>(
				(m_bytes[m_offset / 8] >> (m_offset % 8)) & 1u) << bit;
			++m_offset;
		}
		return value;
	}

	void AlignByte() noexcept { m_offset = (m_offset + 7) & ~std::size_t(7); }
	[[nodiscard]] std::size_t ByteOffset() const noexcept { return m_offset / 8; }

private:
	const std::uint8_t* m_bytes;
	std::size_t m_size;
	std::size_t m_offset = 0;
};

class DeflateHuffman final {
public:
	explicit DeflateHuffman(const std::vector<std::uint8_t>& lengths,
		bool allow_empty = false)
	{
		std::array<std::uint16_t, 16> counts{};
		for (const auto length : lengths) {
			if (length > 15)
				throw std::runtime_error("invalid zlib deflate Huffman code length");
			if (length != 0)
				++counts[length];
		}
		std::int32_t remaining = 1;
		for (std::size_t length = 1; length <= 15; ++length) {
			remaining = (remaining << 1) - counts[length];
			if (remaining < 0)
				throw std::runtime_error("oversubscribed zlib deflate Huffman tree");
		}
		if (std::all_of(lengths.begin(), lengths.end(),
			[](std::uint8_t length) { return length == 0; })) {
			if (allow_empty)
				return;
			throw std::runtime_error("empty zlib deflate Huffman tree");
		}
		std::array<std::uint16_t, 16> next_code{};
		std::uint16_t code = 0;
		for (std::size_t length = 1; length <= 15; ++length) {
			code = static_cast<std::uint16_t>((code + counts[length - 1]) << 1);
			next_code[length] = code;
		}
		for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
			const auto length = lengths[symbol];
			if (length != 0)
				m_codes[length].emplace_back(next_code[length]++,
					static_cast<std::uint16_t>(symbol));
		}
	}

	std::uint16_t Decode(DeflateBitReader& bits) const
	{
		std::uint16_t code = 0;
		for (std::uint16_t length = 1; length <= 15; ++length) {
			code = static_cast<std::uint16_t>((code << 1) | bits.Read(1));
			const auto& entries = m_codes[length];
			const auto found = std::lower_bound(entries.begin(), entries.end(), code,
				[](const auto& entry, std::uint16_t value) { return entry.first < value; });
			if (found != entries.end() && found->first == code)
				return found->second;
		}
		throw std::runtime_error("invalid zlib deflate Huffman code");
	}

private:
	std::vector<std::pair<std::uint16_t, std::uint16_t>> m_codes[16];
};

std::vector<std::uint8_t> InflateZlib(const std::uint8_t* bytes,
	std::size_t size)
{
	constexpr std::size_t MaximumOutputBytes = 0x10000000;
	if (bytes == nullptr || size < 6)
		throw std::runtime_error("truncated zlib symbol pool");
	const auto cmf = bytes[0];
	const auto flg = bytes[1];
	if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 ||
		((static_cast<unsigned>(cmf) << 8) | flg) % 31 != 0 || (flg & 0x20) != 0)
		throw std::runtime_error("invalid or unsupported zlib symbol-pool header");
	DeflateBitReader bits(bytes + 2, size - 6);
	std::vector<std::uint8_t> output;
	output.reserve(size < MaximumOutputBytes / 2 ? size * 2 : MaximumOutputBytes);
	const auto emit = [&](std::uint8_t byte) {
		if (output.size() >= MaximumOutputBytes)
			throw std::runtime_error("zlib symbol pool exceeds the 256 MiB limit");
		output.push_back(byte);
	};
	static constexpr std::array<std::uint16_t, 29> length_base = {
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258};
	static constexpr std::array<std::uint8_t, 29> length_extra = {
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
	static constexpr std::array<std::uint16_t, 30> distance_base = {
		1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,
		1537,2049,3073,4097,6145,8193,12289,16385,24577};
	static constexpr std::array<std::uint8_t, 30> distance_extra = {
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
	const auto decode_compressed_block = [&](const DeflateHuffman& literals,
		const DeflateHuffman& distances) {
		for (;;) {
			const auto symbol = literals.Decode(bits);
			if (symbol < 256) {
				emit(static_cast<std::uint8_t>(symbol));
				continue;
			}
			if (symbol == 256)
				return;
			if (symbol < 257 || symbol > 285)
				throw std::runtime_error("invalid zlib deflate length symbol");
			const auto length_index = symbol - 257;
			const auto length = length_base[length_index] +
				bits.Read(length_extra[length_index]);
			const auto distance_symbol = distances.Decode(bits);
			if (distance_symbol >= distance_base.size())
				throw std::runtime_error("invalid zlib deflate distance symbol");
			const auto distance = distance_base[distance_symbol] +
				bits.Read(distance_extra[distance_symbol]);
			if (distance == 0 || distance > output.size() ||
				length > MaximumOutputBytes - output.size())
				throw std::runtime_error("invalid or excessive zlib deflate back-reference");
			for (std::uint32_t index = 0; index < length; ++index)
				emit(output[output.size() - distance]);
		}
	};
	bool final_block = false;
	while (!final_block) {
		final_block = bits.Read(1) != 0;
		const auto type = bits.Read(2);
		if (type == 0) {
			bits.AlignByte();
			const auto length = bits.Read(16);
			const auto inverse = bits.Read(16);
			if ((length ^ 0xffffu) != inverse || length > MaximumOutputBytes - output.size())
				throw std::runtime_error("invalid or excessive zlib stored block");
			for (std::uint32_t index = 0; index < length; ++index)
				emit(static_cast<std::uint8_t>(bits.Read(8)));
			continue;
		}
		if (type == 3)
			throw std::runtime_error("reserved zlib deflate block type");
		std::vector<std::uint8_t> literal_lengths;
		std::vector<std::uint8_t> distance_lengths;
		if (type == 1) {
			literal_lengths.resize(288);
			for (std::size_t index = 0; index <= 143; ++index) literal_lengths[index] = 8;
			for (std::size_t index = 144; index <= 255; ++index) literal_lengths[index] = 9;
			for (std::size_t index = 256; index <= 279; ++index) literal_lengths[index] = 7;
			for (std::size_t index = 280; index <= 287; ++index) literal_lengths[index] = 8;
			distance_lengths.assign(32, 5);
		} else {
			const auto literal_count = bits.Read(5) + 257;
			const auto distance_count = bits.Read(5) + 1;
			const auto code_count = bits.Read(4) + 4;
			if (literal_count > 286)
				throw std::runtime_error("reserved zlib literal/length code count");
			static constexpr std::array<std::uint8_t, 19> order = {
				16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
			std::vector<std::uint8_t> code_lengths(19, 0);
			for (std::uint32_t index = 0; index < code_count; ++index)
				code_lengths[order[index]] = static_cast<std::uint8_t>(bits.Read(3));
			const DeflateHuffman code_tree(code_lengths);
			std::vector<std::uint8_t> combined;
			combined.reserve(literal_count + distance_count);
			while (combined.size() < literal_count + distance_count) {
				const auto symbol = code_tree.Decode(bits);
				if (symbol <= 15) {
					combined.push_back(static_cast<std::uint8_t>(symbol));
				} else {
					std::uint32_t repeat = 0;
					std::uint8_t value = 0;
					if (symbol == 16) {
						if (combined.empty())
							throw std::runtime_error("invalid zlib repeated code length");
						repeat = bits.Read(2) + 3;
						value = combined.back();
					} else if (symbol == 17) {
						repeat = bits.Read(3) + 3;
					} else if (symbol == 18) {
						repeat = bits.Read(7) + 11;
					} else {
						throw std::runtime_error("invalid zlib code-length symbol");
					}
					if (repeat > literal_count + distance_count - combined.size())
						throw std::runtime_error("zlib code-length repeat exceeds its table");
					combined.insert(combined.end(), repeat, value);
				}
			}
			literal_lengths.assign(combined.begin(), combined.begin() + literal_count);
			distance_lengths.assign(combined.begin() + literal_count, combined.end());
			if (literal_lengths.size() <= 256 || literal_lengths[256] == 0)
				throw std::runtime_error("zlib literal tree has no end-of-block code");
		}
		const DeflateHuffman literals(literal_lengths);
		const DeflateHuffman distances(distance_lengths, true);
		decode_compressed_block(literals, distances);
	}
	bits.AlignByte();
	const auto trailer = 2 + bits.ByteOffset();
	if (trailer > size || size - trailer < 4)
		throw std::runtime_error("truncated zlib Adler-32 trailer");
	const std::uint32_t expected = (static_cast<std::uint32_t>(bytes[trailer]) << 24) |
		(static_cast<std::uint32_t>(bytes[trailer + 1]) << 16) |
		(static_cast<std::uint32_t>(bytes[trailer + 2]) << 8) | bytes[trailer + 3];
	std::uint32_t a = 1;
	std::uint32_t b = 0;
	for (const auto byte : output) {
		a = (a + byte) % 65521;
		b = (b + a) % 65521;
	}
	if (((b << 16) | a) != expected)
		throw std::runtime_error("zlib symbol-pool Adler-32 mismatch");
	return output;
}

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

void Require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void RequireNativeExecution(const MachOImage& image)
{
#if defined(_M_ARM64) || defined(__aarch64__)
	if (!image.IsArm64()) {
		throw std::runtime_error(
			"non-ARM64 Mach-O execution requires a CPU translation engine on this ARM64 Windows host");
	}
#elif defined(_WIN64)
	if (image.Is32Bit()) {
		throw std::runtime_error(
			"32-bit Mach-O execution is not implemented on this x86_64 Windows host");
	}
	if (image.IsArm64()) {
		throw std::runtime_error(
			"ARM64 Mach-O execution is not implemented on this x86_64 Windows host");
	}
#else
	if (!image.Is32Bit()) {
		throw std::runtime_error(
			"64-bit Mach-O execution is not implemented on this Win32 host");
	}
#endif
}

bool RangeFits(std::size_t file_bytes, std::uint64_t offset, std::uint64_t bytes)
{
	return offset <= file_bytes && bytes <= file_bytes - offset;
}

std::uint64_t CheckedAddOffset(std::uint64_t left, std::uint64_t right)
{
	if (right > std::numeric_limits<std::uint64_t>::max() - left)
		throw std::overflow_error("Mach-O segment offset addition overflow");
	return left + right;
}

std::uint64_t CheckedMultiplyOffset(std::uint64_t left, std::uint64_t right)
{
	if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
		throw std::overflow_error("Mach-O segment offset multiplication overflow");
	return left * right;
}

std::uintptr_t AddSignedAddress(std::uintptr_t address, std::int64_t addend)
{
	if (addend >= 0) {
		const auto positive = static_cast<std::uint64_t>(addend);
		if (positive > std::numeric_limits<std::uintptr_t>::max() - address)
			throw std::overflow_error("Mach-O bind target address overflow");
		return address + static_cast<std::uintptr_t>(positive);
	}
	const auto magnitude = static_cast<std::uint64_t>(-(addend + 1)) + 1;
	if (magnitude > address)
		throw std::overflow_error("Mach-O bind target address underflow");
	return address - static_cast<std::uintptr_t>(magnitude);
}

std::uint64_t ReadULEB(const std::uint8_t* bytes, std::size_t size, std::size_t& cursor)
{
	std::uint64_t value = 0;
	unsigned shift = 0;
	while (cursor < size && shift < 64) {
		const auto byte = bytes[cursor++];
		if (shift == 63 && (byte & 0x7e) != 0) {
			throw std::runtime_error("Mach-O ULEB128 overflow");
		}
		value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0) {
			return value;
		}
		shift += 7;
	}
	throw std::runtime_error("truncated Mach-O ULEB128 value");
}

std::int64_t ReadSLEB(const std::uint8_t* bytes, std::size_t size, std::size_t& cursor)
{
	std::uint64_t value = 0;
	unsigned shift = 0;
	std::uint8_t byte = 0;
	while (cursor < size && shift < 64) {
		byte = bytes[cursor++];
		value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
		shift += 7;
		if ((byte & 0x80) == 0) {
			if (shift < 64 && (byte & 0x40) != 0) {
				value |= ~std::uint64_t(0) << shift;
			}
			return static_cast<std::int64_t>(value);
		}
	}
	throw std::runtime_error("truncated Mach-O SLEB128 value");
}

std::int32_t DecodeSpecialDylibOrdinal(std::uint8_t immediate)
{
	return static_cast<std::int32_t>(static_cast<std::int8_t>(immediate | 0xf0));
}

void ParseSimpleBindStream(const std::uint8_t* bytes, std::size_t size,
	std::vector<DyldBindAction>& actions, bool weak, bool lazy)
{
	std::size_t cursor = 0;
	// Weak-bind streams coalesce definitions across loaded images instead of
	// naming one LC_LOAD_*_DYLIB dependency.
	std::int32_t ordinal = weak ? -2 : 0;
	std::string symbol;
	std::uint8_t type = 1;
	std::int64_t addend = 0;
	std::uint32_t segment_index = 0;
	std::uint64_t segment_offset = 0;
	bool weak_import = false;
	std::size_t lazy_sequence_offset = 0;
	while (cursor < size) {
		const auto opcode = bytes[cursor++];
		const auto action_offset = cursor - 1;
		const auto operation = opcode & 0xf0;
		const auto immediate = opcode & 0x0f;
		switch (operation) {
		case 0x00:
			Require(opcode == 0, "invalid Mach-O weak/lazy bind terminator");
			// A lazy-bind stream concatenates one DONE-terminated opcode
			// sequence per symbol. DONE terminates only the current symbol;
			// dyld continues until it reaches the end of the whole stream.
			if (lazy) lazy_sequence_offset = cursor;
			else cursor = size;
			break;
		case 0x10:
			ordinal = immediate;
			break;
		case 0x20:
			ordinal = static_cast<std::int32_t>(ReadULEB(bytes, size, cursor));
			break;
		case 0x30:
			ordinal = DecodeSpecialDylibOrdinal(immediate);
			break;
		case 0x40: {
			weak_import = (immediate & 1) != 0;
			const auto start = cursor;
			while (cursor < size && bytes[cursor] != 0) {
				++cursor;
			}
			Require(cursor < size, "unterminated Mach-O weak/lazy symbol");
			symbol.assign(reinterpret_cast<const char*>(bytes + start), cursor - start);
			++cursor;
			break;
		}
		case 0x50:
			type = immediate;
			break;
		case 0x60:
			addend = ReadSLEB(bytes, size, cursor);
			break;
		case 0x70:
			segment_index = immediate;
			segment_offset = ReadULEB(bytes, size, cursor);
			break;
		case 0x80:
			segment_offset = CheckedAddOffset(segment_offset,
				ReadULEB(bytes, size, cursor));
			break;
		case 0x90:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset, 8);
			break;
		case 0xa0:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset,
				CheckedAddOffset(ReadULEB(bytes, size, cursor), 8));
			break;
		case 0xb0:
			actions.push_back({symbol, segment_index, segment_offset, type, addend,
				ordinal, weak || weak_import, lazy, false, 0,
				lazy ? lazy_sequence_offset : action_offset, weak});
			segment_offset = CheckedAddOffset(segment_offset,
				static_cast<std::uint64_t>(immediate) * 8 + 8);
			break;
		case 0xc0: {
			const auto repeat = ReadULEB(bytes, size, cursor);
			const auto skip = ReadULEB(bytes, size, cursor);
			Require(repeat <= 0x100000,
				"Mach-O weak/lazy bind repeat count is unreasonable");
			for (std::uint64_t index = 0; index < repeat; ++index) {
				actions.push_back({symbol, segment_index, segment_offset, type, addend,
					ordinal, weak || weak_import, lazy, false, 0,
					lazy ? lazy_sequence_offset : action_offset, weak});
				segment_offset = CheckedAddOffset(segment_offset,
					CheckedAddOffset(skip, 8));
			}
			break;
		}
		default:
			throw std::runtime_error("unsupported Mach-O weak/lazy bind opcode");
		}
	}
}

constexpr std::uint64_t kPageSize = 0x1000;
constexpr std::uint8_t N_TYPE = 0x0e;
constexpr std::uint8_t N_UNDF = 0x00;
constexpr std::uint8_t N_ABS = 0x02;
constexpr std::uint8_t N_INDR = 0x0a;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04;
constexpr std::uint64_t EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10;
constexpr std::uint32_t R_SYMBOL_MASK = 0x00ffffff;
constexpr std::uint32_t R_PCREL = 0x01000000;
constexpr std::uint32_t R_LENGTH = 0x06000000;
constexpr std::uint32_t R_EXTERN = 0x08000000;
constexpr std::uint32_t R_TYPE = 0xf0000000;

std::uint64_t AlignDown(std::uint64_t value)
{
	return value & ~(kPageSize - 1);
}

std::uint64_t AlignUp(std::uint64_t value)
{
	if (value > std::numeric_limits<std::uint64_t>::max() - (kPageSize - 1)) {
		throw std::overflow_error("Mach-O address alignment overflow");
	}
	return (value + kPageSize - 1) & ~(kPageSize - 1);
}

DWORD ProtectionFor(std::int32_t protection)
{
	const bool read = (protection & 1) != 0;
	const bool write = (protection & 2) != 0;
	const bool execute = (protection & 4) != 0;
	if (execute) {
		return write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	}
	return write ? PAGE_READWRITE : (read ? PAGE_READONLY : PAGE_NOACCESS);
}

std::string SegmentName(const char (&name)[16])
{
	const auto length = std::find(std::begin(name), std::end(name), '\0');
	return std::string(name, length);
}

std::uint32_t ReadBigEndian32(const std::uint8_t* bytes)
{
	return (static_cast<std::uint32_t>(bytes[0]) << 24) |
		(static_cast<std::uint32_t>(bytes[1]) << 16) |
		(static_cast<std::uint32_t>(bytes[2]) << 8) |
		static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t ReadBigEndian64(const std::uint8_t* bytes)
{
	return (static_cast<std::uint64_t>(ReadBigEndian32(bytes)) << 32) |
		ReadBigEndian32(bytes + 4);
}

} // namespace

MachOImage MachOImage::Open(const std::wstring& path,
	std::int32_t preferred_cpu_type)
{
	const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		ThrowLastError("CreateFileW(Mach-O)");
	}

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
		static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
		CloseHandle(file);
		ThrowLastError("GetFileSizeEx(Mach-O)");
	}
	const auto mapped_file_bytes = static_cast<std::size_t>(size.QuadPart);
	const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (mapping == nullptr) {
		CloseHandle(file);
		ThrowLastError("CreateFileMappingW(Mach-O)");
	}
	const auto* mapped_view = static_cast<const std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
	if (mapped_view == nullptr) {
		CloseHandle(mapping);
		CloseHandle(file);
		ThrowLastError("MapViewOfFile(Mach-O)");
	}

	try {
		std::size_t image_offset = 0;
		std::size_t image_bytes = mapped_file_bytes;
		const auto raw_magic = mapped_file_bytes >= sizeof(std::uint32_t) ?
			*reinterpret_cast<const std::uint32_t*>(mapped_view) : 0;
		if (raw_magic == FAT_MAGIC_BE || raw_magic == FAT_MAGIC_64_BE) {
			Require(mapped_file_bytes >= 8, "Fat Mach-O header is truncated");
			const auto architecture_count = ReadBigEndian32(mapped_view + 4);
			Require(architecture_count > 0 && architecture_count <= 128,
				"Fat Mach-O has an unreasonable architecture count");
			const auto architecture_bytes = raw_magic == FAT_MAGIC_64_BE ? 32u : 20u;
			Require(RangeFits(mapped_file_bytes, 8,
				static_cast<std::uint64_t>(architecture_count) * architecture_bytes),
				"Fat Mach-O architecture table is outside the file");
			bool found_x86_64 = false;
			bool found_x86 = false;
			bool found_arm64 = false;
			std::size_t x86_64_offset = 0;
			std::size_t x86_64_bytes = 0;
			std::size_t x86_offset = 0;
			std::size_t x86_bytes = 0;
			std::size_t arm64_offset = 0;
			std::size_t arm64_bytes = 0;
			for (std::uint32_t index = 0; index < architecture_count; ++index) {
				const auto* architecture = mapped_view + 8 +
					static_cast<std::size_t>(index) * architecture_bytes;
				const auto cpu_type = static_cast<std::int32_t>(ReadBigEndian32(architecture));
				if (cpu_type != CPU_TYPE_X86_64 && cpu_type != CPU_TYPE_X86 &&
					cpu_type != CPU_TYPE_ARM64) {
					continue;
				}
				const auto candidate_offset = raw_magic == FAT_MAGIC_64_BE ?
					static_cast<std::size_t>(ReadBigEndian64(architecture + 8)) :
					static_cast<std::size_t>(ReadBigEndian32(architecture + 8));
				const auto candidate_bytes = raw_magic == FAT_MAGIC_64_BE ?
					static_cast<std::size_t>(ReadBigEndian64(architecture + 16)) :
					static_cast<std::size_t>(ReadBigEndian32(architecture + 12));
				Require(RangeFits(mapped_file_bytes, candidate_offset, candidate_bytes),
					"Fat Mach-O slice is outside the file");
				if (cpu_type == CPU_TYPE_X86_64) {
					found_x86_64 = true;
					x86_64_offset = candidate_offset;
					x86_64_bytes = candidate_bytes;
					continue;
				}
				if (cpu_type == CPU_TYPE_X86) {
					if (!found_x86) {
						x86_offset = candidate_offset;
						x86_bytes = candidate_bytes;
						found_x86 = true;
					}
					continue;
				}
				found_arm64 = true;
				arm64_offset = candidate_offset;
				arm64_bytes = candidate_bytes;
			}
#if defined(_M_ARM64) || defined(__aarch64__)
			if (preferred_cpu_type == CPU_TYPE_ARM64) {
				Require(found_arm64, "Fat Mach-O has no requested ARM64 slice");
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else if (preferred_cpu_type == CPU_TYPE_X86_64) {
				Require(found_x86_64, "Fat Mach-O has no requested x86_64 slice");
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else if (preferred_cpu_type == CPU_TYPE_X86) {
				Require(found_x86, "Fat Mach-O has no requested x86 slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			} else if (preferred_cpu_type != 0) {
				throw std::runtime_error("requested Mach-O CPU type is unsupported");
			} else if (found_arm64) {
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else if (found_x86_64) {
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else {
				Require(found_x86, "Fat Mach-O has no supported slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			}
#else
			if (preferred_cpu_type == CPU_TYPE_ARM64) {
				Require(found_arm64, "Fat Mach-O has no requested ARM64 slice");
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else if (preferred_cpu_type == CPU_TYPE_X86_64) {
				Require(found_x86_64, "Fat Mach-O has no requested x86_64 slice");
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else if (preferred_cpu_type == CPU_TYPE_X86) {
				Require(found_x86, "Fat Mach-O has no requested x86 slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			} else if (preferred_cpu_type != 0) {
				throw std::runtime_error("requested Mach-O CPU type is unsupported");
			} else if (found_x86_64) {
				image_offset = x86_64_offset;
				image_bytes = x86_64_bytes;
			} else if (found_arm64) {
				image_offset = arm64_offset;
				image_bytes = arm64_bytes;
			} else {
				Require(found_x86, "Fat Mach-O has no supported slice");
				image_offset = x86_offset;
				image_bytes = x86_bytes;
			}
#endif
		}
		const auto file_bytes = image_bytes;
		const auto slice_magic = image_bytes >= sizeof(std::uint32_t) ?
			*reinterpret_cast<const std::uint32_t*>(mapped_view + image_offset) : 0;
		const bool is_32_bit = slice_magic == MH_MAGIC;
		Require(image_bytes >= (is_32_bit ? sizeof(MachHeader32) : sizeof(MachHeader64)),
			"Mach-O file is smaller than its header");
		const auto* view = mapped_view + image_offset;
		MachHeader64 normalized_header{};
		if (is_32_bit) {
			const auto* header32 = reinterpret_cast<const MachHeader32*>(view);
			normalized_header = {header32->magic, header32->cpu_type, header32->cpu_subtype,
				header32->file_type, header32->command_count, header32->command_bytes,
				header32->flags, 0};
		} else {
			const auto* header64 = reinterpret_cast<const MachHeader64*>(view);
			Require(header64->magic == MH_MAGIC_64,
				"unsupported Mach-O magic; expected 32-bit or 64-bit little-endian");
			normalized_header = *header64;
		}
		const auto* header = &normalized_header;
		Require(header->cpu_type == CPU_TYPE_X86_64 || header->cpu_type == CPU_TYPE_X86 ||
			header->cpu_type == CPU_TYPE_ARM64,
			"unsupported Mach-O CPU type; expected x86, x86_64, or ARM64");
		Require(preferred_cpu_type == 0 || header->cpu_type == preferred_cpu_type,
			"Mach-O does not contain the requested CPU type");
		Require(header->file_type == MH_EXECUTE || header->file_type == MH_DYLIB ||
			header->file_type == MH_BUNDLE,
			"Mach-O is neither an executable, dylib, nor bundle");
		Require(header->command_count <= 4096, "Mach-O has an unreasonable load-command count");
		Require(RangeFits(file_bytes, sizeof(MachHeader64), header->command_bytes),
			"Mach-O load-command table is outside the file");

		std::vector<MachOSegment> segments;
		std::vector<MachOSection> sections;
		std::vector<std::string> dependencies;
		std::vector<std::string> weak_dependencies;
		std::vector<std::string> rpaths;
		std::vector<MachOSymbol> symbols;
		std::vector<std::uint32_t> indirect_symbols;
		std::vector<MachOReexport> reexports;
		std::array<std::uint8_t, 16> uuid{};
		std::string install_name;
		std::string dylinker_path;
		std::vector<MachORelocation> relocations;
		const SymtabCommand* symtab = nullptr;
		const DYSymtabCommand* dysymtab = nullptr;
		const DyldInfoCommand* dyld_info = nullptr;
		const LinkeditDataCommand* chained_command = nullptr;
		const LinkeditDataCommand* exports_command = nullptr;
		const LinkeditDataCommand* code_signature_command = nullptr;
		std::uint64_t entry_offset = 0;
		bool has_main = false;
		std::uint64_t thread_rip = 0;
		bool has_thread = false;
		std::size_t cursor = is_32_bit ? sizeof(MachHeader32) : sizeof(MachHeader64);
		const auto commands_end = cursor + header->command_bytes;
		for (std::uint32_t index = 0; index < header->command_count; ++index) {
			Require(cursor <= commands_end && commands_end - cursor >= sizeof(LoadCommand),
				"Mach-O load command header is truncated");
			const auto* command = reinterpret_cast<const LoadCommand*>(view + cursor);
			Require(command->command_bytes >= sizeof(LoadCommand) &&
				command->command_bytes <= commands_end - cursor,
				"Mach-O load command has an invalid size");

			if (command->command == LC_SEGMENT_64) {
				Require(command->command_bytes >= sizeof(SegmentCommand64),
					"Mach-O segment command is truncated");
				const auto* segment = reinterpret_cast<const SegmentCommand64*>(command);
				Require(segment->section_count <=
					(command->command_bytes - sizeof(SegmentCommand64)) / sizeof(Section64),
					"Mach-O section table is truncated");
				Require(RangeFits(file_bytes, segment->file_offset, segment->file_size),
					"Mach-O segment points outside the file");
				segments.push_back({SegmentName(segment->segment_name), segment->vm_address,
					segment->vm_size, segment->file_offset, segment->file_size,
					segment->max_protection, segment->initial_protection});
				const auto* section = reinterpret_cast<const Section64*>(
					reinterpret_cast<const std::uint8_t*>(segment) + sizeof(SegmentCommand64));
				for (std::uint32_t section_index = 0;
					section_index < segment->section_count; ++section_index) {
					sections.push_back({SegmentName(section[section_index].segment_name),
						SegmentName(section[section_index].section_name),
						section[section_index].address, section[section_index].size,
						section[section_index].file_offset, section[section_index].flags,
						section[section_index].reserved1,
						section[section_index].reserved2});
				}
			} else if (command->command == LC_SEGMENT) {
				Require(is_32_bit && command->command_bytes >= sizeof(SegmentCommand32),
					"Mach-O 32-bit segment command is truncated");
				const auto* segment = reinterpret_cast<const SegmentCommand32*>(command);
				Require(segment->section_count <=
					(command->command_bytes - sizeof(SegmentCommand32)) / sizeof(Section32),
					"Mach-O 32-bit section table is truncated");
				Require(RangeFits(file_bytes, segment->file_offset, segment->file_size),
					"Mach-O 32-bit segment points outside the file");
				segments.push_back({SegmentName(segment->segment_name), segment->vm_address,
					segment->vm_size, segment->file_offset, segment->file_size,
					segment->max_protection, segment->initial_protection});
				const auto* section = reinterpret_cast<const Section32*>(
					reinterpret_cast<const std::uint8_t*>(segment) + sizeof(SegmentCommand32));
				for (std::uint32_t section_index = 0;
					section_index < segment->section_count; ++section_index) {
					sections.push_back({SegmentName(section[section_index].segment_name),
						SegmentName(section[section_index].section_name),
						section[section_index].address, section[section_index].size,
						section[section_index].file_offset, section[section_index].flags,
						section[section_index].reserved1,
						section[section_index].reserved2});
				}
			} else if (command->command == LC_SYMTAB) {
				Require(command->command_bytes >= sizeof(SymtabCommand),
					"Mach-O symbol-table command is truncated");
				symtab = reinterpret_cast<const SymtabCommand*>(command);
			} else if (command->command == LC_RPATH) {
				Require(command->command_bytes >= sizeof(RPathCommand),
					"Mach-O rpath command is truncated");
				const auto* rpath = reinterpret_cast<const RPathCommand*>(command);
				Require(rpath->path_offset >= sizeof(RPathCommand) &&
					rpath->path_offset < command->command_bytes,
					"Mach-O rpath offset is invalid");
				const auto* path = reinterpret_cast<const char*>(command) + rpath->path_offset;
				const auto path_bytes = command->command_bytes - rpath->path_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(path, '\0', path_bytes));
				Require(terminator != nullptr, "Mach-O rpath is not terminated");
				Require(terminator != path, "Mach-O rpath is empty");
				rpaths.emplace_back(path, terminator);
			} else if (command->command == LC_DYSYMTAB) {
				Require(command->command_bytes >= sizeof(DYSymtabCommand),
					"Mach-O dynamic symbol-table command is truncated");
				dysymtab = reinterpret_cast<const DYSymtabCommand*>(command);
			} else if (command->command == LC_DYLD_INFO ||
				command->command == LC_DYLD_INFO_ONLY) {
				Require(command->command_bytes >= sizeof(DyldInfoCommand),
					"Mach-O dyld-info command is truncated");
				dyld_info = reinterpret_cast<const DyldInfoCommand*>(command);
			} else if (command->command == LC_DYLD_CHAINED_FIXUPS) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O chained-fixups command is truncated");
				chained_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_DYLD_EXPORTS_TRIE) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O exports-trie command is truncated");
				exports_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_CODE_SIGNATURE) {
				Require(command->command_bytes >= sizeof(LinkeditDataCommand),
					"Mach-O code-signature command is truncated");
				code_signature_command = reinterpret_cast<const LinkeditDataCommand*>(command);
			} else if (command->command == LC_MAIN) {
				Require(command->command_bytes >= sizeof(MainCommand),
					"Mach-O main command is truncated");
				const auto* main = reinterpret_cast<const MainCommand*>(command);
				entry_offset = main->entry_offset;
				has_main = true;
			} else if (command->command == LC_UUID) {
				Require(command->command_bytes >= 24, "Mach-O UUID command is truncated");
				std::memcpy(uuid.data(), reinterpret_cast<const std::uint8_t*>(command) + 8, 16);
			} else if (command->command == LC_UNIXTHREAD) {
				std::uint32_t flavor = 0;
				std::uint32_t count = 0;
				std::memcpy(&flavor, reinterpret_cast<const std::uint8_t*>(command) + 8,
					sizeof(flavor));
				std::memcpy(&count, reinterpret_cast<const std::uint8_t*>(command) + 12,
					sizeof(count));
				if (header->cpu_type == CPU_TYPE_X86_64) {
					Require(command->command_bytes >= 16 + 17 * sizeof(std::uint64_t) &&
						flavor == 4 && count >= 17,
						"unsupported Mach-O UNIXTHREAD flavor; expected x86_64");
					std::memcpy(&thread_rip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 16 * sizeof(std::uint64_t), sizeof(thread_rip));
				} else if (header->cpu_type == CPU_TYPE_X86) {
					Require(command->command_bytes >= 16 + 16 * sizeof(std::uint32_t) &&
						flavor == 1 && count >= 16,
						"unsupported Mach-O UNIXTHREAD flavor; expected i386");
					std::uint32_t thread_eip = 0;
					std::memcpy(&thread_eip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 10 * sizeof(std::uint32_t), sizeof(thread_eip));
					thread_rip = thread_eip;
				} else {
					// ARM_THREAD_STATE64 contains 68 32-bit words; PC is
					// the 33rd 64-bit register in its payload.
					Require(command->command_bytes >= 16 + 68 * sizeof(std::uint32_t) &&
						flavor == 6 && count >= 68,
						"unsupported Mach-O UNIXTHREAD flavor; expected ARM64");
					std::memcpy(&thread_rip, reinterpret_cast<const std::uint8_t*>(command) +
						16 + 32 * sizeof(std::uint64_t), sizeof(thread_rip));
				}
				has_thread = true;
			} else if (command->command == LC_LOAD_DYLINKER) {
				Require(command->command_bytes >= sizeof(DylinkerCommand),
					"Mach-O dylinker command is truncated");
				const auto* dylinker = reinterpret_cast<const DylinkerCommand*>(command);
				Require(dylinker->name_offset >= sizeof(DylinkerCommand) &&
					dylinker->name_offset < command->command_bytes,
					"Mach-O dylinker name offset is invalid");
				const auto* name = reinterpret_cast<const char*>(command) + dylinker->name_offset;
				const auto name_bytes = command->command_bytes - dylinker->name_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', name_bytes));
				Require(terminator != nullptr, "Mach-O dylinker name is not terminated");
				Require(terminator != name, "Mach-O dylinker name is empty");
				dylinker_path.assign(name, terminator);
			} else if (command->command == LC_ID_DYLIB || command->command == LC_LOAD_DYLIB ||
				command->command == LC_LOAD_WEAK_DYLIB ||
				command->command == LC_REEXPORT_DYLIB ||
				command->command == LC_LOAD_UPWARD_DYLIB) {
				Require(command->command_bytes >= sizeof(DylibCommand),
					"Mach-O dylib command is truncated");
				const auto* dylib = reinterpret_cast<const DylibCommand*>(command);
				Require(dylib->name_offset >= sizeof(DylibCommand) &&
					dylib->name_offset < command->command_bytes,
					"Mach-O dylib name offset is invalid");
				const auto* name = reinterpret_cast<const char*>(command) + dylib->name_offset;
				const auto name_bytes = command->command_bytes - dylib->name_offset;
				const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', name_bytes));
				Require(terminator != nullptr, "Mach-O dylib name is not terminated");
				Require(terminator != name, "Mach-O dylib name is empty");
				if (command->command == LC_ID_DYLIB)
					install_name.assign(name, terminator);
				else
					dependencies.emplace_back(name, terminator);
				if (command->command == LC_LOAD_WEAK_DYLIB) {
					weak_dependencies.emplace_back(name, terminator);
				}
			}
			cursor += command->command_bytes;
		}
		Require(cursor == commands_end, "Mach-O load-command table has trailing bytes");
		if (!has_main && has_thread) {
			for (const auto& segment : segments) {
				if (thread_rip >= segment.vm_address &&
					thread_rip - segment.vm_address < segment.file_size) {
					entry_offset = segment.file_offset + (thread_rip - segment.vm_address);
					has_main = true;
					break;
				}
			}
		}
		Require(header->file_type != MH_EXECUTE || has_main,
			"Mach-O executable has no LC_MAIN entry point");
		if (code_signature_command != nullptr) {
			Require(RangeFits(file_bytes, code_signature_command->data_offset,
				code_signature_command->data_size),
				"Mach-O code-signature payload is outside the file");
		}
		if (symtab != nullptr) {
			Require(RangeFits(file_bytes, symtab->string_offset, symtab->string_bytes),
				"Mach-O string table is outside the file");
			const auto* strings = reinterpret_cast<const char*>(view + symtab->string_offset);
			const auto make_symbol = [&](std::string name, std::uint8_t type,
				std::uint8_t section, std::uint16_t description,
				std::uint64_t value) {
				MachOSymbol symbol{std::move(name), type, section, description, value};
				if ((type & N_TYPE) == N_INDR) {
					Require(value < symtab->string_bytes,
						"Mach-O indirect symbol target is outside the string table");
					const auto* target = strings + value;
					const auto remaining = symtab->string_bytes -
						static_cast<std::uint32_t>(value);
					const auto* terminator = static_cast<const char*>(
						std::memchr(target, '\0', remaining));
					Require(terminator != nullptr,
						"Mach-O indirect symbol target is not terminated");
					symbol.indirect_target.assign(target, terminator);
				}
				return symbol;
			};
			if (is_32_bit) {
				Require(RangeFits(file_bytes, symtab->symbol_offset,
					static_cast<std::uint64_t>(symtab->symbol_count) * sizeof(NList32)),
					"Mach-O 32-bit symbol table is outside the file");
				const auto* entries = reinterpret_cast<const NList32*>(view + symtab->symbol_offset);
				for (std::uint32_t index = 0; index < symtab->symbol_count; ++index) {
					const auto& entry = entries[index];
					Require(entry.name_offset < symtab->string_bytes,
						"Mach-O symbol name offset is outside the string table");
					const auto* name = strings + entry.name_offset;
					const auto remaining = symtab->string_bytes - entry.name_offset;
					const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
					Require(terminator != nullptr, "Mach-O symbol name is not terminated");
					symbols.push_back(make_symbol(std::string(name, terminator),
						entry.type, entry.section, entry.description, entry.value));
				}
			} else {
				Require(RangeFits(file_bytes, symtab->symbol_offset,
					static_cast<std::uint64_t>(symtab->symbol_count) * sizeof(NList64)),
					"Mach-O symbol table is outside the file");
				const auto* entries = reinterpret_cast<const NList64*>(view + symtab->symbol_offset);
				for (std::uint32_t index = 0; index < symtab->symbol_count; ++index) {
					const auto& entry = entries[index];
					Require(entry.name_offset < symtab->string_bytes,
						"Mach-O symbol name offset is outside the string table");
					const auto* name = strings + entry.name_offset;
					const auto remaining = symtab->string_bytes - entry.name_offset;
					const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
					Require(terminator != nullptr, "Mach-O symbol name is not terminated");
					symbols.push_back(make_symbol(std::string(name, terminator),
						entry.type, entry.section, entry.description, entry.value));
				}
			}
		}
		if (dysymtab != nullptr) {
			if (dysymtab->indirect_symbol_count != 0) {
				Require(symtab != nullptr, "Mach-O indirect symbols require LC_SYMTAB");
				Require(RangeFits(file_bytes, dysymtab->indirect_symbol_offset,
					static_cast<std::uint64_t>(dysymtab->indirect_symbol_count) * sizeof(std::uint32_t)),
					"Mach-O indirect symbol table is outside the file");
				const auto* entries = reinterpret_cast<const std::uint32_t*>(
					view + dysymtab->indirect_symbol_offset);
				for (std::uint32_t index = 0; index < dysymtab->indirect_symbol_count; ++index) {
					const auto symbol_index = entries[index];
					const auto special = symbol_index & 0xC0000000u;
					Require(special == 0 || special == 0xC0000000u ||
						symbol_index < symtab->symbol_count,
						"Mach-O indirect symbol index is outside LC_SYMTAB");
					indirect_symbols.push_back(symbol_index);
				}
			}
			const auto read_relocations = [&](std::uint32_t offset, std::uint32_t count) {
				Require(RangeFits(file_bytes, offset,
					static_cast<std::uint64_t>(count) * sizeof(RelocationInfo)),
					"Mach-O relocation table is outside the file");
				const auto* entries = reinterpret_cast<const RelocationInfo*>(view + offset);
				bool pending_addend = false;
				std::int32_t pending_address = 0;
				std::int64_t pending_value = 0;
				for (std::uint32_t index = 0; index < count; ++index) {
					const auto raw_address = static_cast<std::uint32_t>(entries[index].address);
					if ((raw_address & 0x80000000u) != 0) {
						const auto type = static_cast<std::uint8_t>((raw_address >> 24) & 0x0f);
						MachORelocation relocation{
							static_cast<std::int32_t>(raw_address & 0x00ffffffu), 0,
							(raw_address & 0x40000000u) != 0,
							static_cast<std::uint8_t>((raw_address >> 28) & 0x3), false,
							type, false, 0, true,
							static_cast<std::int32_t>(entries[index].info)};
						relocations.push_back(relocation);
						continue;
					}
					const auto info = entries[index].info;
					const auto type = static_cast<std::uint8_t>((info & R_TYPE) >> 28);
					const auto external = (info & R_EXTERN) != 0;
					if (header->cpu_type == CPU_TYPE_ARM64 && type == 10) {
						Require(!external && (info & R_PCREL) == 0 &&
							((info & R_LENGTH) >> 25) == 2 && !pending_addend,
							"invalid ARM64_RELOC_ADDEND record");
						const auto raw = info & R_SYMBOL_MASK;
						pending_value = (raw & 0x00800000u) != 0 ?
							static_cast<std::int64_t>(raw | 0xff000000u) :
							static_cast<std::int64_t>(raw);
						pending_address = entries[index].address;
						pending_addend = true;
						continue;
					}
					MachORelocation relocation{entries[index].address,
						info & R_SYMBOL_MASK, (info & R_PCREL) != 0,
						static_cast<std::uint8_t>((info & R_LENGTH) >> 25),
						external, type};
					if (pending_addend) {
						Require(entries[index].address == pending_address,
							"ARM64_RELOC_ADDEND does not precede its paired relocation");
						relocation.has_explicit_addend = true;
						relocation.explicit_addend = pending_value;
						pending_addend = false;
					}
					relocations.push_back(relocation);
				}
				Require(!pending_addend, "ARM64_RELOC_ADDEND has no paired relocation");
			};
			read_relocations(dysymtab->external_relocation_offset,
				dysymtab->external_relocation_count);
			read_relocations(dysymtab->local_relocation_offset,
				dysymtab->local_relocation_count);
		}
		std::vector<DyldBindAction> bind_actions;
		std::vector<DyldBindAction> threaded_bind_targets;
		if (dyld_info != nullptr && dyld_info->binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->binding_offset, dyld_info->binding_size),
				"Mach-O dyld binding stream is outside the file");
			const auto* bytes = view + dyld_info->binding_offset;
			const auto size = static_cast<std::size_t>(dyld_info->binding_size);
			std::size_t cursor = 0;
			std::int32_t ordinal = 0;
			std::string symbol;
			std::uint8_t type = 1;
			std::int64_t addend = 0;
			std::uint32_t segment_index = 0;
			std::uint64_t segment_offset = 0;
			std::uint64_t threaded_table_size = 0;
			bool weak_import = false;
			bool collecting_threaded_targets = false;
			const auto emit_bind = [&]() {
				const DyldBindAction action{symbol, segment_index, segment_offset, type,
					addend, ordinal, weak_import, false, false, 0};
				if (collecting_threaded_targets) {
					threaded_bind_targets.push_back(action);
				} else {
					bind_actions.push_back(action);
					segment_offset = CheckedAddOffset(segment_offset, 8);
				}
			};
			while (cursor < size) {
				const auto opcode = bytes[cursor++];
				const auto operation = opcode & 0xf0;
				const auto immediate = opcode & 0x0f;
				switch (operation) {
				case 0x00:
					Require(opcode == 0, "invalid Mach-O dyld bind terminator");
					cursor = size;
					break;
				case 0x10:
					ordinal = immediate;
					break;
				case 0x20:
					ordinal = static_cast<std::int32_t>(ReadULEB(bytes, size, cursor));
					break;
				case 0x30:
					ordinal = DecodeSpecialDylibOrdinal(immediate);
					break;
				case 0x40: {
					weak_import = (immediate & 1) != 0;
					const auto start = cursor;
					while (cursor < size && bytes[cursor] != 0) {
						++cursor;
					}
					Require(cursor < size, "unterminated Mach-O dyld symbol");
					symbol.assign(reinterpret_cast<const char*>(bytes + start), cursor - start);
					++cursor;
					break;
				}
				case 0x50:
					type = immediate;
					break;
				case 0x60:
					addend = ReadSLEB(bytes, size, cursor);
					break;
				case 0x70:
					segment_index = immediate;
					segment_offset = ReadULEB(bytes, size, cursor);
					break;
				case 0x80:
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x90:
					emit_bind();
					break;
				case 0xa0:
					emit_bind();
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0xb0:
					emit_bind();
					segment_offset = CheckedAddOffset(segment_offset,
						static_cast<std::uint64_t>(immediate) * 8);
					break;
				case 0xc0:
				{
					const auto repeat = ReadULEB(bytes, size, cursor);
					const auto skip = ReadULEB(bytes, size, cursor);
					Require(repeat <= 0x100000,
						"Mach-O bind repeat count is unreasonable");
					for (std::uint64_t index = 0; index < repeat; ++index) {
						emit_bind();
						segment_offset = CheckedAddOffset(segment_offset, skip);
					}
					break;
				}
					break;
				case 0xd0:
					if (immediate == 0) {
						threaded_table_size = ReadULEB(bytes, size, cursor);
						Require(threaded_table_size <= 0x100000,
							"Mach-O threaded bind table is unreasonable");
						threaded_bind_targets.clear();
						collecting_threaded_targets = true;
					} else if (immediate == 1) {
						Require(threaded_table_size != 0,
							"Mach-O threaded bind apply has no ordinal table");
						Require(threaded_bind_targets.size() == threaded_table_size,
							"Mach-O threaded bind ordinal table is incomplete");
						bind_actions.push_back({symbol, segment_index, segment_offset, type,
							addend, ordinal, false, false, true,
							static_cast<std::size_t>(threaded_table_size)});
						collecting_threaded_targets = false;
					} else {
						throw std::runtime_error("unsupported Mach-O threaded bind opcode");
					}
					break;
				default:
					throw std::runtime_error("unsupported Mach-O dyld bind opcode");
				}
			}
		}
		if (dyld_info != nullptr && dyld_info->weak_binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->weak_binding_offset,
				dyld_info->weak_binding_size),
				"Mach-O weak-binding stream is outside the file");
			ParseSimpleBindStream(view + dyld_info->weak_binding_offset,
				dyld_info->weak_binding_size, bind_actions, true, false);
		}
		if (dyld_info != nullptr && dyld_info->lazy_binding_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->lazy_binding_offset,
				dyld_info->lazy_binding_size),
				"Mach-O lazy-binding stream is outside the file");
			ParseSimpleBindStream(view + dyld_info->lazy_binding_offset,
				dyld_info->lazy_binding_size, bind_actions, false, true);
		}
		std::vector<DyldRebaseAction> rebase_actions;
		if (dyld_info != nullptr && dyld_info->rebase_size != 0) {
			Require(RangeFits(file_bytes, dyld_info->rebase_offset, dyld_info->rebase_size),
				"Mach-O dyld rebase stream is outside the file");
			const auto* bytes = view + dyld_info->rebase_offset;
			const auto size = static_cast<std::size_t>(dyld_info->rebase_size);
			std::size_t cursor = 0;
			std::uint32_t segment_index = 0;
			std::uint64_t segment_offset = 0;
			std::uint8_t type = 1;
			const std::uint64_t pointer_size = is_32_bit ? 4u : 8u;
			const auto emit_rebases = [&](std::uint64_t count) {
				Require(count <= 0x100000,
					"Mach-O dyld rebase count is unreasonable");
				if (count == 0) return;
				Require(segment_index < segments.size(),
					"Mach-O dyld rebase segment index is invalid");
				const auto& segment = segments[segment_index];
				const std::uint64_t width = type == 2 || type == 3 ? 4u : pointer_size;
				Require(segment_offset <= segment.vm_size &&
					segment.vm_size - segment_offset >= width,
					"Mach-O dyld rebase starts outside its segment");
				const auto available = 1 +
					(segment.vm_size - segment_offset - width) / pointer_size;
				Require(count <= available,
					"Mach-O dyld rebase sequence exceeds its segment");
				for (std::uint64_t index = 0; index < count; ++index) {
					rebase_actions.push_back({segment_index, segment_offset, type});
					segment_offset = CheckedAddOffset(segment_offset, pointer_size);
				}
			};
			while (cursor < size) {
				const auto opcode = bytes[cursor++];
				const auto operation = opcode & 0xf0;
				const auto immediate = opcode & 0x0f;
				switch (operation) {
				case 0x00:
					Require(opcode == 0, "invalid Mach-O dyld rebase terminator");
					cursor = size;
					break;
				case 0x10:
					type = immediate;
					break;
				case 0x20:
					segment_index = immediate;
					segment_offset = ReadULEB(bytes, size, cursor);
					break;
				case 0x30:
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x40:
					segment_offset = CheckedAddOffset(segment_offset,
						static_cast<std::uint64_t>(immediate) * pointer_size);
					break;
				case 0x50:
					emit_rebases(immediate);
					break;
				case 0x60:
					emit_rebases(ReadULEB(bytes, size, cursor));
					break;
				case 0x70:
					emit_rebases(1);
					segment_offset = CheckedAddOffset(segment_offset,
						ReadULEB(bytes, size, cursor));
					break;
				case 0x80: {
					const auto count = ReadULEB(bytes, size, cursor);
					const auto skip = ReadULEB(bytes, size, cursor);
					emit_rebases(count);
					segment_offset = CheckedAddOffset(segment_offset,
						CheckedMultiplyOffset(skip, count));
					break;
				}
				default:
					throw std::runtime_error("unsupported Mach-O dyld rebase opcode");
				}
			}
		}
		std::vector<DyldChainedFixup> chained_fixups;
		std::vector<DyldChainedImport> chained_imports;
		if (chained_command != nullptr) {
			Require(RangeFits(file_bytes, chained_command->data_offset,
				chained_command->data_size), "Mach-O chained-fixups payload is outside the file");
			Require(chained_command->data_size >= sizeof(ChainedFixupsHeader),
				"Mach-O chained-fixups header is truncated");
			const auto* payload = view + chained_command->data_offset;
			const auto* header = reinterpret_cast<const ChainedFixupsHeader*>(payload);
			Require(header->version == 0, "unsupported Mach-O chained-fixups version");
			Require(header->imports_format >= 1 && header->imports_format <= 3,
				"unsupported Mach-O chained import format");
			Require(header->symbols_format <= 1,
				"unsupported Mach-O chained symbol format");
			const std::uint64_t chained_import_size =
				header->imports_format == 1 ? 4u :
				header->imports_format == 2 ? 8u : 16u;
			Require(header->imports_offset <= chained_command->data_size &&
				chained_command->data_size - header->imports_offset >=
				static_cast<std::uint64_t>(header->imports_count) *
					chained_import_size,
				"Mach-O chained import table is outside the payload");
			Require(header->symbols_offset < chained_command->data_size,
				"Mach-O chained symbol pool is outside the payload");
			const auto* import_bytes = payload + header->imports_offset;
			std::vector<std::uint8_t> decompressed_symbols;
			const char* symbol_bytes = nullptr;
			std::size_t symbol_pool_size = 0;
			if (header->symbols_format == 0) {
				symbol_bytes = reinterpret_cast<const char*>(payload + header->symbols_offset);
				symbol_pool_size = chained_command->data_size - header->symbols_offset;
			} else {
				decompressed_symbols = InflateZlib(payload + header->symbols_offset,
					chained_command->data_size - header->symbols_offset);
				symbol_bytes = reinterpret_cast<const char*>(decompressed_symbols.data());
				symbol_pool_size = decompressed_symbols.size();
			}
			const auto decode_ordinal8 = [](std::uint32_t raw_ordinal) {
				const auto ordinal = static_cast<std::uint8_t>(raw_ordinal & 0xffu);
				return ordinal > 0xf0u ? static_cast<std::int32_t>(ordinal) - 0x100 :
					static_cast<std::int32_t>(ordinal);
			};
			const auto decode_ordinal16 = [](std::uint64_t raw_ordinal) {
				const auto ordinal = static_cast<std::uint16_t>(raw_ordinal & 0xffffu);
				return ordinal > 0xfff0u ? static_cast<std::int32_t>(ordinal) - 0x10000 :
					static_cast<std::int32_t>(ordinal);
			};
			for (std::uint32_t index = 0; index < header->imports_count; ++index) {
				std::uint32_t name_offset = 0;
				std::int32_t dylib_ordinal = 0;
				bool weak = false;
				std::int64_t addend = 0;
				if (header->imports_format == 1) {
					std::uint32_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 4, sizeof(raw));
					dylib_ordinal = decode_ordinal8(raw);
					weak = (raw & 0x100) != 0;
					name_offset = raw >> 9;
				} else if (header->imports_format == 2) {
					std::uint32_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 8, sizeof(raw));
					std::int32_t addend32 = 0;
					std::memcpy(&addend32, import_bytes + index * 8 + 4,
						sizeof(addend32));
					dylib_ordinal = decode_ordinal8(raw);
					weak = (raw & 0x100) != 0;
					name_offset = raw >> 9;
					addend = addend32;
				} else {
					std::uint64_t raw = 0;
					std::memcpy(&raw, import_bytes + index * 16, sizeof(raw));
					std::memcpy(&addend, import_bytes + index * 16 + 8,
						sizeof(addend));
					dylib_ordinal = decode_ordinal16(raw);
					weak = (raw & (std::uint64_t{1} << 16)) != 0;
					name_offset = static_cast<std::uint32_t>(raw >> 32);
				}
			Require(name_offset < symbol_pool_size,
				"Mach-O chained import name offset is outside the symbol pool");
			const auto* name = symbol_bytes + name_offset;
			const auto* end = static_cast<const char*>(std::memchr(name, '\0',
				symbol_pool_size - name_offset));
			Require(end != nullptr, "unterminated Mach-O chained import symbol");
			chained_imports.push_back({dylib_ordinal, weak, std::string(name, end), addend});
		}
		if (exports_command != nullptr) {
			Require(RangeFits(file_bytes, exports_command->data_offset,
				exports_command->data_size), "Mach-O exports trie is outside the file");
			const auto* trie = view + exports_command->data_offset;
			const auto trie_size = static_cast<std::size_t>(exports_command->data_size);
			std::set<std::uint32_t> visited;
			const std::function<void(std::uint32_t, const std::string&)> visit =
				[&](std::uint32_t node_offset, const std::string& prefix) {
					Require(node_offset < trie_size && visited.insert(node_offset).second,
						"invalid or cyclic Mach-O exports trie node");
					std::size_t cursor = node_offset;
					const auto terminal_size = ReadULEB(trie, trie_size, cursor);
					Require(terminal_size <= trie_size - cursor,
						"Mach-O exports trie terminal is outside the payload");
					const auto terminal_end = cursor + static_cast<std::size_t>(terminal_size);
					if (terminal_size != 0) {
						const auto flags = ReadULEB(trie, terminal_end, cursor);
						if ((flags & 0x8) != 0) {
							const auto ordinal = ReadULEB(trie, terminal_end, cursor);
							const auto target_start = cursor;
							while (cursor < terminal_end && trie[cursor] != 0)
								++cursor;
							Require(cursor < terminal_end,
								"unterminated Mach-O re-export target");
							reexports.push_back({prefix,
								std::string(reinterpret_cast<const char*>(trie + target_start),
									cursor - target_start),
								static_cast<std::uint32_t>(ordinal)});
						} else {
							const auto address = ReadULEB(trie, terminal_end, cursor);
							const bool has_resolver = (flags &
								EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) != 0;
							const auto resolver = has_resolver ?
								ReadULEB(trie, terminal_end, cursor) : 0;
							if (!prefix.empty()) {
								const bool absolute = (flags &
									EXPORT_SYMBOL_FLAGS_KIND_MASK) ==
									EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE;
								const auto type = absolute ? 0x03u : 0x0fu;
								const auto description = (flags &
									EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION) != 0 ? 0x0080u : 0u;
								std::uint64_t image_base = 0;
								if (!absolute || has_resolver) {
									const auto text_segment = std::find_if(segments.begin(),
										segments.end(), [](const MachOSegment& segment) {
											return segment.name == "__TEXT";
										});
									Require(text_segment != segments.end(),
										"Mach-O export trie requires an __TEXT image base");
									image_base = text_segment->vm_address;
								}
								const auto preferred_address = absolute ? address :
									image_base + address;
								Require(absolute || preferred_address >= image_base,
									"Mach-O export address overflows its image base");
								const auto preferred_resolver = has_resolver ?
									image_base + resolver : 0;
								Require(!has_resolver || preferred_resolver >= image_base,
									"Mach-O resolver address overflows its image base");
								auto existing = std::find_if(symbols.begin(), symbols.end(),
									[&prefix](const MachOSymbol& symbol) {
										return symbol.name == prefix &&
											(symbol.type & N_TYPE) != N_UNDF;
									});
								if (existing == symbols.end()) {
									symbols.push_back({prefix,
										static_cast<std::uint8_t>(type), 1,
										static_cast<std::uint16_t>(description), preferred_address});
									existing = std::prev(symbols.end());
								} else {
									existing->type = static_cast<std::uint8_t>(type);
									existing->description = static_cast<std::uint16_t>(
										(existing->description & ~0x0080u) | description);
								}
								existing->has_resolver = has_resolver;
								existing->resolver_value = preferred_resolver;
							}
						}
						cursor = terminal_end;
					}
					Require(cursor < trie_size, "Mach-O exports trie node has no child count");
					const auto child_count = trie[cursor++];
					for (std::uint8_t child = 0; child < child_count; ++child) {
						const auto edge_start = cursor;
						while (cursor < trie_size && trie[cursor] != 0) {
							++cursor;
						}
						Require(cursor < trie_size, "unterminated Mach-O exports trie edge");
						const std::string edge(reinterpret_cast<const char*>(trie + edge_start),
							cursor - edge_start);
						++cursor;
						const auto child_offset = ReadULEB(trie, trie_size, cursor);
						visit(static_cast<std::uint32_t>(child_offset), prefix + edge);
					}
				};
			visit(0, {});
		}
			Require(header->starts_offset <= chained_command->data_size &&
				chained_command->data_size - header->starts_offset >= sizeof(std::uint32_t),
				"Mach-O chained-fixups starts table is outside the payload");
			const auto* starts = payload + header->starts_offset;
			const auto segment_count = *reinterpret_cast<const std::uint32_t*>(starts);
			Require(segment_count <= 4096 &&
				header->starts_offset + sizeof(std::uint32_t) +
				static_cast<std::uint64_t>(segment_count) * sizeof(std::uint32_t) <=
				chained_command->data_size, "Mach-O chained-fixups segment table is invalid");
			const auto* segment_offsets = reinterpret_cast<const std::uint32_t*>(starts + sizeof(std::uint32_t));
			for (std::uint32_t segment_index = 0; segment_index < segment_count; ++segment_index) {
				const auto segment_offset = segment_offsets[segment_index];
				if (segment_offset == 0) {
					continue;
				}
				const auto starts_bytes_available = header->starts_offset <=
					chained_command->data_size ?
					chained_command->data_size - header->starts_offset : 0;
				const bool starts_relative = segment_offset < starts_bytes_available &&
					starts_bytes_available - segment_offset >= sizeof(ChainedStartsSegment);
				const bool payload_relative = segment_offset < chained_command->data_size &&
					chained_command->data_size - segment_offset >= sizeof(ChainedStartsSegment);
				Require(starts_relative || payload_relative,
					"Mach-O chained-fixups segment starts are invalid");
				const auto* starts_candidate = reinterpret_cast<const ChainedStartsSegment*>(
					starts + segment_offset);
				const auto* payload_candidate = reinterpret_cast<const ChainedStartsSegment*>(
					payload + segment_offset);
				auto supported_pointer_format = [](std::uint16_t format) {
					return format == 1 || format == 2 || format == 3 || format == 6 || format == 7 ||
						format == 9 || format == 10 || format == 12;
				};
				const auto* segment_starts = starts_relative &&
					supported_pointer_format(starts_candidate->pointer_format) ?
					starts_candidate : payload_candidate;
				const auto starts_location = segment_starts == starts_candidate ?
					static_cast<std::uint64_t>(header->starts_offset) + segment_offset :
					static_cast<std::uint64_t>(segment_offset);
				const auto pointer_format = segment_starts->pointer_format;
				Require(pointer_format == 1 || pointer_format == 2 || pointer_format == 3 ||
					pointer_format == 6 ||
					pointer_format == 7 || pointer_format == 9 || pointer_format == 10 ||
					pointer_format == 12,
					"unsupported Mach-O chained userland pointer format");
				Require((pointer_format == 3) == is_32_bit,
					"Mach-O chained pointer width does not match the image architecture");
				const auto starts_bytes = segment_starts->size;
				Require(starts_bytes >= sizeof(ChainedStartsSegment) +
					static_cast<std::uint64_t>(segment_starts->page_count) * sizeof(std::uint16_t) &&
					starts_location <= chained_command->data_size &&
					starts_bytes <= chained_command->data_size - starts_location,
					"Mach-O chained-fixups page starts are invalid");
				Require(segment_starts->page_size == 0x1000 ||
					segment_starts->page_size == 0x4000,
					"Mach-O chained-fixups page size is unsupported");
				const auto* page_starts = reinterpret_cast<const std::uint16_t*>(
					payload + starts_location + sizeof(ChainedStartsSegment));
				Require(segment_index < segments.size(), "Mach-O chained-fixups segment index is invalid");
				const auto segment_page_count = segments[segment_index].vm_size /
					segment_starts->page_size +
					(segments[segment_index].vm_size % segment_starts->page_size != 0);
				Require(segment_starts->page_count <= segment_page_count,
					"Mach-O chained-fixups page count exceeds its segment");
				const auto chain_starts_count = (starts_bytes -
					sizeof(ChainedStartsSegment) -
					static_cast<std::uint64_t>(segment_starts->page_count) *
					sizeof(std::uint16_t)) / sizeof(std::uint16_t);
				const auto* chain_starts = page_starts + segment_starts->page_count;
				const bool arm64e_format = pointer_format == 1 || pointer_format == 7 ||
					pointer_format == 9 || pointer_format == 10 || pointer_format == 12;
				const std::uint64_t chain_stride = pointer_format == 3 ||
					(arm64e_format && (pointer_format == 7 || pointer_format == 10)) ? 4u : 8u;
				const auto pointer_size = pointer_format == 3 ? sizeof(std::uint32_t) :
					sizeof(std::uint64_t);
				const auto walk_chain = [&](std::uint16_t start, std::uint16_t page) {
					Require(start < segment_starts->page_size,
						"Mach-O chained-fixup start exceeds its page");
					const auto page_vm = segments[segment_index].vm_address +
						static_cast<std::uint64_t>(page) * segment_starts->page_size;
					auto vm = page_vm + start;
					const auto max_nodes = segment_starts->page_size / chain_stride + 1;
					for (std::uint64_t chain = 0; chain < max_nodes; ++chain) {
						const auto page_offset = vm - page_vm;
						Require(page_offset <= segment_starts->page_size &&
							segment_starts->page_size - page_offset >= pointer_size,
							"Mach-O chained-fixup pointer crosses its page");
						const auto* segment = &segments[segment_index];
						Require(vm >= segment->vm_address &&
							vm - segment->vm_address < segment->file_size,
							"Mach-O chained-fixup location is not file-backed");
						const auto file_offset = segment->file_offset +
							(vm - segment->vm_address);
						Require(RangeFits(file_bytes, file_offset, pointer_size),
							"Mach-O chained-fixup pointer is outside the file");
						std::uint64_t raw = 0;
						std::memcpy(&raw, view + file_offset, pointer_size);
						const bool authenticated = arm64e_format && (raw >> 63) != 0;
						const bool bind = pointer_format == 3 ? ((raw >> 31) & 1) != 0 :
							arm64e_format ?
							((raw >> 62) & 1) != 0 : (raw >> 63) != 0;
						const auto next = static_cast<std::uint16_t>(pointer_format == 3 ?
							((raw >> 26) & 0x1f) : arm64e_format ?
							((raw >> 51) & 0x7ff) : ((raw >> 51) & 0xfff));
						std::uint32_t bind_ordinal = 0;
						std::int64_t addend = 0;
						std::uint64_t target = 0;
						bool non_pointer = false;
						if (pointer_format == 3) {
							bind_ordinal = static_cast<std::uint32_t>(raw & 0xfffff);
							addend = static_cast<std::int64_t>((raw >> 20) & 0x3f);
							target = raw & 0x3ffffff;
							if (!bind && target > segment_starts->max_valid_pointer) {
								const auto bias = (std::uint64_t(0x4000000) +
									segment_starts->max_valid_pointer) / 2;
								Require(target >= bias,
									"Mach-O chained 32-bit non-pointer bias underflows");
								target -= bias;
								non_pointer = true;
							}
						} else if (arm64e_format) {
							bind_ordinal = static_cast<std::uint32_t>(raw &
								(pointer_format == 12 ? 0xffffff : 0xffff));
							if (authenticated) {
								// In authenticated layouts, bits 32..50 contain PAC
								// diversity/key metadata, not a bind addend.
								addend = 0;
								if (!bind) target = raw & 0xffffffffu;
							} else if (bind) {
								const auto encoded_addend = (raw >> 32) & 0x7ffff;
								addend = static_cast<std::int64_t>(encoded_addend);
								if ((encoded_addend & 0x40000) != 0)
									addend -= 0x80000;
							} else {
								target = (raw & ((1ull << 43) - 1)) |
									(((raw >> 43) & 0xff) << 56);
							}
						} else {
							bind_ordinal = static_cast<std::uint32_t>(raw & 0xffffff);
							addend = static_cast<std::int64_t>((raw >> 24) & 0xff);
							target = (raw & 0xfffffffffull) |
								(((raw >> 36) & 0xff) << 56);
						}
						chained_fixups.push_back({segment_index, vm, bind,
							bind_ordinal, addend, target, next, pointer_format,
							authenticated, non_pointer});
						if (next == 0) return;
						const auto advance = static_cast<std::uint64_t>(next) * chain_stride;
						Require(advance <= segment_starts->page_size - page_offset,
							"Mach-O chained-fixup link exceeds its page");
						vm += advance;
					}
					throw std::runtime_error("Mach-O chained-fixup chain does not terminate");
				};
				for (std::uint16_t page = 0; page < segment_starts->page_count; ++page) {
					const auto page_start = page_starts[page];
					if (page_start == 0xffff) {
						continue;
					}
					if ((page_start & 0x8000u) == 0) {
						walk_chain(page_start, page);
						continue;
					}
					std::uint64_t chain_start_index = page_start & 0x7fffu;
					bool last_start = false;
					while (chain_start_index < chain_starts_count) {
						const auto chain_start = chain_starts[chain_start_index];
						walk_chain(static_cast<std::uint16_t>(chain_start & 0x7fffu), page);
						last_start = (chain_start & 0x8000u) != 0;
						if (last_start) break;
						++chain_start_index;
					}
					Require(last_start,
						"Mach-O chained-fixup multi-start list has no terminator");
				}
			}
		}

		return MachOImage(file, mapping, mapped_view, file_bytes, image_offset,
			normalized_header, is_32_bit,
			std::move(segments), std::move(dependencies), std::move(sections),
			std::move(weak_dependencies),
			std::move(rpaths), std::move(symbols),
		std::move(relocations), std::move(indirect_symbols), std::move(bind_actions),
			std::move(threaded_bind_targets),
		std::move(rebase_actions),
			std::move(chained_fixups), std::move(chained_imports),
			std::move(reexports), entry_offset, uuid, std::move(install_name),
			std::move(dylinker_path));
	} catch (...) {
		UnmapViewOfFile(mapped_view);
		CloseHandle(mapping);
		CloseHandle(file);
		throw;
	}
}

std::vector<MachOSymbol> MachOImage::UndefinedSymbols() const
{
	std::vector<MachOSymbol> result;
	for (const auto& symbol : m_symbols) {
		if ((symbol.type & N_TYPE) == N_UNDF && !symbol.name.empty()) {
			result.push_back(symbol);
		}
	}
	return result;
}

bool MachOImage::IsWeakUndefined(const MachOSymbol& symbol) noexcept
{
	// N_WEAK_REF in n_desc. The caller already restricts this query to an
	// N_UNDF symbol; keeping the bit test here makes the Darwin rule explicit.
	return (symbol.description & 0x0040u) != 0;
}

bool MachOImage::IsFlatLookupUndefined(const MachOSymbol& symbol) noexcept
{
	// N_REF_TO_WEAK requests flat namespace lookup; it is distinct from
	// N_WEAK_REF, which only makes a missing symbol resolve to NULL.
	return (symbol.description & 0x0080u) != 0;
}

std::vector<MachOImage::IndirectImportSlot> MachOImage::IndirectImportSlots() const
{
	std::vector<IndirectImportSlot> slots;
	const auto pointer_size = Is32Bit() ? sizeof(std::uint32_t) : sizeof(std::uint64_t);
	for (const auto& section : m_sections) {
		const auto section_type = section.flags & 0xffu;
		if (section_type == 0x8u) {
			if (section.reserved2 == 0 || section.size % section.reserved2 != 0)
				throw std::runtime_error("Mach-O symbol-stub section is misaligned");
			continue;
		}
		if (section_type != 0x6u && section_type != 0x7u &&
			section.section_name != "__la_symbol_ptr" &&
			section.section_name != "__nl_symbol_ptr")
			continue;
		if (section.size % pointer_size != 0)
			throw std::runtime_error("Mach-O indirect symbol section is misaligned");
		const bool lazy = section_type == 0x7u ||
			section.section_name == "__la_symbol_ptr";
		for (std::uint64_t offset = 0; offset < section.size; offset += pointer_size) {
			const auto indirect_index = static_cast<std::uint64_t>(section.reserved1) +
				offset / pointer_size;
			if (indirect_index > std::numeric_limits<std::uint32_t>::max())
				throw std::out_of_range("Mach-O indirect symbol index overflows");
			slots.push_back({section.address + offset,
				static_cast<std::uint32_t>(indirect_index), lazy});
		}
	}
	return slots;
}

void MachOImage::ApplyIndirectImportBindings(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings, bool bind_lazy) const
{
	// Modern chained-fixups images carry their import locations in the chained
	// starts table.  Some toolchains retain a legacy indirect-symbol table for
	// stub metadata as well; it is not a second set of pointer slots and must
	// not be forced through the legacy __la/__nl slot walk.
	if (!m_chained_fixups.empty())
		return;
	const auto slots = IndirectImportSlots();
	if (slots.empty())
		return;
	if (m_indirect_symbols.size() > m_symbols.size() + 0x1000000u)
		throw std::runtime_error("Mach-O indirect symbol table is unreasonable");
	for (const auto& slot : slots) {
		if (slot.symbol_index >= m_indirect_symbols.size())
			throw std::out_of_range("Mach-O indirect symbol table exceeds import slots");
		const auto slot_address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(slot.address) + mapping.Slide());
		const bool trace_lazy_slot = slot.lazy &&
			std::getenv("WINTOSH_TRACE_LAZY_BIND") != nullptr;
		std::uintptr_t old_value = 0;
		if (trace_lazy_slot) {
			old_value = Is32Bit() ? static_cast<std::uintptr_t>(
				*reinterpret_cast<const std::uint32_t*>(slot_address)) :
				*reinterpret_cast<const std::uintptr_t*>(slot_address);
		}
		if (slot.lazy && !bind_lazy) {
			if (trace_lazy_slot)
				std::fprintf(stderr,
					"WINTOSH_LAZY_SLOT address=0x%llx value=0x%llx deferred=1\n",
					static_cast<unsigned long long>(slot_address),
					static_cast<unsigned long long>(old_value));
			continue;
		}
		const auto symbol_index = m_indirect_symbols[slot.symbol_index];
		if ((symbol_index & 0xC0000000u) != 0)
			continue; // LOCAL and ABSOLUTE entries are not imported bindings.
		if (symbol_index >= m_symbols.size())
			throw std::out_of_range("Mach-O indirect symbol index is invalid");
		const auto& symbol = m_symbols[symbol_index];
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&symbol](const auto& binding) {
				return binding.symbol == symbol.name &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						MachOLibraryOrdinalFromSymbolDescription(
							symbol.description));
			});
		if (match == bindings.end()) {
			if (IsWeakUndefined(symbol)) {
				if (Is32Bit()) ApplyAbsolute32(mapping, slot.address, 0);
				else ApplyAbsolute64(mapping, slot.address, 0);
				continue;
			}
			throw std::runtime_error("unresolved Mach-O indirect import: " + symbol.name);
		}
		if (Is32Bit()) ApplyAbsolute32(mapping, slot.address, match->address);
		else ApplyAbsolute64(mapping, slot.address, match->address);
		if (trace_lazy_slot)
			std::fprintf(stderr,
				"WINTOSH_LAZY_SLOT address=0x%llx old=0x%llx target=0x%llx deferred=0\n",
				static_cast<unsigned long long>(slot_address),
				static_cast<unsigned long long>(old_value),
				static_cast<unsigned long long>(match->address));
	}
}

bool MachOImage::CodeSignatureRange(std::uint32_t& offset,
	std::uint32_t& size) const noexcept
{
	offset = 0;
	size = 0;
	if (m_view == nullptr || m_header == nullptr) return false;
	const auto command_offset = m_image_offset + (m_is_32_bit ?
		sizeof(MachHeader32) : sizeof(MachHeader64));
	if (command_offset > m_file_bytes ||
		m_header->command_bytes > m_file_bytes - command_offset)
		return false;
	const auto* cursor = m_view + command_offset;
	const auto* end = cursor + m_header->command_bytes;
	for (std::uint32_t index = 0; index < m_header->command_count; ++index) {
		if (cursor + sizeof(LoadCommand) > end) return false;
		const auto* command = reinterpret_cast<const LoadCommand*>(cursor);
		if (command->command_bytes < sizeof(LoadCommand) ||
			command->command_bytes > static_cast<std::size_t>(end - cursor))
			return false;
		if (command->command == LC_CODE_SIGNATURE) {
			if (command->command_bytes < sizeof(LinkeditDataCommand)) return false;
			const auto* signature = reinterpret_cast<const LinkeditDataCommand*>(command);
			offset = signature->data_offset;
			size = signature->data_size;
			return true;
		}
		cursor += command->command_bytes;
	}
	return false;
}

bool MachOImage::HasCodeSignature() const noexcept
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
	return CodeSignatureRange(offset, size);
}

bool MachOImage::HasValidCodeSignatureSuperBlob() const noexcept
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
	if (!CodeSignatureRange(offset, size) || size < 12 ||
		offset > m_file_bytes || size > m_file_bytes - offset)
		return false;
	const auto* blob = m_view + offset;
	const auto read_be32 = [](const std::uint8_t* value) {
		return (static_cast<std::uint32_t>(value[0]) << 24) |
			(static_cast<std::uint32_t>(value[1]) << 16) |
			(static_cast<std::uint32_t>(value[2]) << 8) | value[3];
	};
	if (read_be32(blob) != 0xfade0cc0u) return false;
	const auto length = read_be32(blob + 4);
	const auto count = read_be32(blob + 8);
	if (length < 12 || length > size || count > (length - 12) / 8) return false;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto blob_offset = read_be32(blob + 12 + index * 8 + 4);
		if (blob_offset > length || length - blob_offset < 8) return false;
		const auto blob_length = read_be32(blob + blob_offset + 4);
		if (blob_length < 8 || blob_length > length - blob_offset) return false;
		if (read_be32(blob + 12 + index * 8) == 0 &&
			(blob_length < 44 || read_be32(blob + blob_offset) != 0xfade0c02u))
			return false;
	}
	return true;
}

bool MachOImage::IsWeakDependency(const std::string& name) const noexcept
{
	return std::find(m_weak_dependencies.begin(), m_weak_dependencies.end(), name)
		!= m_weak_dependencies.end();
}

std::uintptr_t MachOImage::SymbolAddress(
	const Mapping& mapping, const std::string& name) const
{
	std::string resolved_name = name;
	std::set<std::string> visited_indirect_symbols;
	for (;;) {
		if (!visited_indirect_symbols.insert(resolved_name).second)
			throw std::runtime_error("Mach-O indirect symbol cycle: " + name);
		bool followed_indirect = false;
		for (const auto& symbol : m_symbols) {
			if (symbol.name != resolved_name ||
				(symbol.type & N_TYPE) == N_UNDF)
				continue;
			if ((symbol.type & N_TYPE) == N_INDR) {
				if (symbol.indirect_target.empty())
					throw std::runtime_error("Mach-O indirect symbol has no target: " +
						resolved_name);
				resolved_name = symbol.indirect_target;
				followed_indirect = true;
				break;
			}
			if ((symbol.type & N_TYPE) == N_ABS) {
				if (symbol.value > std::numeric_limits<std::uintptr_t>::max())
					throw std::overflow_error("Mach-O absolute symbol exceeds host address width");
				return static_cast<std::uintptr_t>(symbol.value);
			}
			if (symbol.value > static_cast<std::uint64_t>(
				std::numeric_limits<std::intptr_t>::max())) {
				throw std::overflow_error("Mach-O symbol value cannot be slid safely");
			}
			const auto address = static_cast<std::intptr_t>(symbol.value) + mapping.Slide();
			if (address <= 0) {
				throw std::runtime_error("Mach-O symbol address is invalid");
			}
			return static_cast<std::uintptr_t>(address);
		}
		if (!followed_indirect)
			break;
	}
	throw std::runtime_error("Mach-O defined symbol not found: " + resolved_name);
}

std::uintptr_t MachOImage::SymbolResolverAddress(
	const Mapping& mapping, const std::string& name) const
{
	const auto symbol = std::find_if(m_symbols.begin(), m_symbols.end(),
		[&name](const MachOSymbol& candidate) {
			return candidate.name == name && candidate.has_resolver;
		});
	if (symbol == m_symbols.end())
		throw std::runtime_error("Mach-O export resolver not found: " + name);
	const auto segment = std::find_if(m_segments.begin(), m_segments.end(),
		[&symbol](const MachOSegment& candidate) {
			return symbol->resolver_value >= candidate.vm_address &&
				symbol->resolver_value - candidate.vm_address < candidate.vm_size &&
				(candidate.initial_protection & 4) != 0;
		});
	if (segment == m_segments.end())
		throw std::runtime_error(
			"Mach-O export resolver is outside executable image segments");
	if (symbol->resolver_value > std::numeric_limits<std::uintptr_t>::max())
		throw std::overflow_error("Mach-O export resolver exceeds host address width");
	const auto preferred = static_cast<std::uintptr_t>(symbol->resolver_value);
	const auto slide = mapping.Slide();
	if (slide >= 0) {
		const auto amount = static_cast<std::uintptr_t>(slide);
		if (preferred > std::numeric_limits<std::uintptr_t>::max() - amount)
			throw std::overflow_error("Mach-O export resolver cannot be slid safely");
		const auto address = preferred + amount;
		if (address == 0)
			throw std::runtime_error("Mach-O export resolver address is null");
		return address;
	}
	const auto amount = static_cast<std::uintptr_t>(-(slide + 1)) + 1;
	if (preferred < amount)
		throw std::runtime_error("Mach-O export resolver address is invalid after sliding");
	const auto address = preferred - amount;
	if (address == 0)
		throw std::runtime_error("Mach-O export resolver address is null");
	return address;
}

std::uintptr_t MachOImage::SectionAddress(const Mapping& mapping,
	const std::string& segment_name, const std::string& section_name) const
{
	for (const auto& section : m_sections) {
		if (section.segment_name != segment_name ||
			section.section_name != section_name)
			continue;
		if (section.address > static_cast<std::uint64_t>(
			std::numeric_limits<std::intptr_t>::max()))
			throw std::overflow_error("Mach-O section address cannot be slid safely");
		const auto address = static_cast<std::intptr_t>(section.address) + mapping.Slide();
		if (address <= 0)
			throw std::runtime_error("Mach-O section address is invalid");
		return static_cast<std::uintptr_t>(address);
	}
	throw std::runtime_error("Mach-O section not found: " + segment_name + "," +
		section_name);
}

void MachOImage::ApplyAbsolute64(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(target)) {
			continue;
		}
		const auto address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(target),
			PAGE_READWRITE, &old_protection)) {
			ThrowLastError("VirtualProtect(Mach-O relocation)");
		}
		std::memcpy(reinterpret_cast<void*>(address), &target, sizeof(target));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(target),
			ProtectionFor(segment.initial_protection), &restored_protection)) {
			ThrowLastError("VirtualProtect(Mach-O relocation restore)");
		}
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(target));
		return;
	}
	throw std::out_of_range("Mach-O absolute relocation is outside a segment");
}

void MachOImage::ApplyAbsolute32(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	if (target > std::numeric_limits<std::uint32_t>::max())
		throw std::overflow_error("Mach-O 32-bit absolute bind target is out of range");
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::uint32_t))
			continue;
		const auto address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::uint32_t),
			PAGE_READWRITE, &old_protection))
			ThrowLastError("VirtualProtect(Mach-O 32-bit bind)");
		const auto value = static_cast<std::uint32_t>(target);
		std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::uint32_t),
			ProtectionFor(segment.initial_protection), &restored_protection))
			ThrowLastError("VirtualProtect(Mach-O 32-bit bind restore)");
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(value));
		return;
	}
	throw std::out_of_range("Mach-O 32-bit bind is outside a segment");
}

void MachOImage::ApplyRelocations(const Mapping& mapping,
	const std::vector<std::uintptr_t>& symbol_addresses,
	bool defer_unreachable_external) const
{
	if (mapping.m_relocation_original_widths.empty()) {
		mapping.m_relocation_original_values.resize(m_relocations.size());
		mapping.m_relocation_original_sites.resize(m_relocations.size());
		mapping.m_relocation_original_widths.resize(m_relocations.size());
	} else if (mapping.m_relocation_original_widths.size() != m_relocations.size() ||
		mapping.m_relocation_original_values.size() != m_relocations.size() ||
		mapping.m_relocation_original_sites.size() != m_relocations.size()) {
		throw std::logic_error("Mach-O relocation snapshot does not match image");
	}
	const auto original_relocation_value = [&mapping](std::size_t index,
		std::uintptr_t site, std::size_t width) {
		if (index >= mapping.m_relocation_original_widths.size() || width == 0 ||
			width > sizeof(std::uint64_t))
			throw std::logic_error("invalid Mach-O relocation snapshot request");
		auto& saved_width = mapping.m_relocation_original_widths[index];
		if (saved_width == 0) {
			std::uint64_t value = 0;
			std::memcpy(&value, reinterpret_cast<const void*>(site), width);
			mapping.m_relocation_original_values[index] = value;
			mapping.m_relocation_original_sites[index] = site;
			saved_width = static_cast<std::uint8_t>(width);
		} else if (saved_width != width || mapping.m_relocation_original_sites[index] != site) {
			throw std::logic_error("Mach-O relocation site changed after snapshot");
		}
		return mapping.m_relocation_original_values[index];
	};
	if (Is32Bit()) {
		for (std::size_t index = 0; index < m_relocations.size(); ++index) {
			const auto& relocation = m_relocations[index];
			if (!relocation.scattered && relocation.external &&
				relocation.type == 5 && relocation.pc_relative &&
				relocation.length == 2) {
				if (relocation.address < 0 ||
					relocation.symbol_index >= symbol_addresses.size()) {
					throw std::runtime_error(
						"invalid 32-bit i386 TLV relocation record");
				}
				const auto descriptor_address = symbol_addresses[relocation.symbol_index];
				const auto pointer_address = TLVPointerAddress(mapping, descriptor_address);
				if (pointer_address == 0)
					throw std::runtime_error(
						"32-bit i386 TLV relocation has no __thread_ptrs slot");
				ApplyRelative32(mapping,
					static_cast<std::uint32_t>(relocation.address), pointer_address);
				continue;
			}
			if (relocation.type == 3 && relocation.scattered &&
				relocation.length == 2 && !relocation.pc_relative) {
				const auto target = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(relocation.scattered_value) + mapping.Slide());
				ApplyAbsolute32(mapping, static_cast<std::uint32_t>(relocation.address), target);
				continue;
			}
			if ((relocation.type == 2 || relocation.type == 4) &&
				relocation.scattered) {
				if (index + 1 >= m_relocations.size())
					throw std::runtime_error("32-bit i386 SECTDIFF relocation has no pair");
				const auto& pair = m_relocations[index + 1];
				if (!pair.scattered || pair.type != 1 || pair.length != relocation.length ||
					pair.pc_relative || relocation.pc_relative || relocation.length != 2) {
					throw std::runtime_error("invalid 32-bit i386 SECTDIFF relocation pair");
				}
				const auto vm_address = static_cast<std::uint32_t>(relocation.address);
				const auto site = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto addend_bits = static_cast<std::uint32_t>(
					original_relocation_value(index, site, sizeof(std::uint32_t)));
				const auto value = static_cast<std::uintptr_t>(
					static_cast<std::int64_t>(relocation.scattered_value) -
					static_cast<std::int64_t>(pair.scattered_value) +
					static_cast<std::int32_t>(addend_bits));
				ApplyAbsolute32(mapping, vm_address, value);
				++index;
				continue;
			}
			if (relocation.type == 1 && relocation.scattered)
				throw std::runtime_error("stray 32-bit i386 SECTDIFF pair relocation");
			if (relocation.scattered || relocation.type != 0 || relocation.length != 2 ||
				relocation.address < 0) {
				throw std::runtime_error(
					"unsupported 32-bit i386 Mach-O relocation record");
			}
			if (relocation.external && relocation.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid 32-bit i386 relocation symbol index");
			}
			const auto vm_address = static_cast<std::uint32_t>(relocation.address);
			const auto site = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(vm_address) + mapping.Slide());
			const auto addend_bits = static_cast<std::uint32_t>(
				original_relocation_value(index, site, sizeof(std::uint32_t)));
			const auto addend = static_cast<std::int32_t>(addend_bits);
			std::uintptr_t target = 0;
			if (relocation.external) {
				target = symbol_addresses[relocation.symbol_index];
			} else if (relocation.symbol_index < m_symbols.size()) {
				target = SymbolAddress(mapping, m_symbols[relocation.symbol_index].name);
			} else {
				throw std::runtime_error("invalid 32-bit i386 local relocation symbol");
			}
			const auto adjusted = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(target) + addend);
			if (relocation.pc_relative)
				ApplyRelative32(mapping, vm_address, adjusted);
			else
				ApplyAbsolute32(mapping, vm_address, adjusted);
		}
		return;
	}
	if (IsArm64()) {
		const auto resolve_vm_address = [&](std::uint64_t value) {
			for (const auto& segment : m_segments) {
				if (value >= segment.vm_address &&
					value - segment.vm_address <= segment.vm_size) {
					return value;
				}
			}
			for (const auto& segment : m_segments) {
				if (value <= segment.vm_size &&
					segment.vm_address <= std::numeric_limits<std::uint64_t>::max() - value) {
					return segment.vm_address + value;
				}
			}
			throw std::out_of_range("ARM64 Mach-O relocation address is outside all segments");
		};
		const auto find_address = [&](std::uint64_t vm_address, std::size_t bytes) {
			vm_address = resolve_vm_address(vm_address);
			for (const auto& segment : m_segments) {
				if (vm_address < segment.vm_address ||
					vm_address - segment.vm_address > segment.vm_size ||
					segment.vm_size - (vm_address - segment.vm_address) < bytes) {
					continue;
				}
				return std::pair<std::uintptr_t, const MachOSegment*>(
					static_cast<std::uintptr_t>(static_cast<std::intptr_t>(vm_address) + mapping.Slide()),
					&segment);
			}
			throw std::out_of_range("ARM64 Mach-O relocation is outside a segment");
		};
		const auto read32 = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto [address, segment] = find_address(vm_address, sizeof(std::uint32_t));
			(void)segment;
			return static_cast<std::uint32_t>(original_relocation_value(
				relocation_index, address, sizeof(std::uint32_t)));
		};
		const auto read64 = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto [address, segment] = find_address(vm_address, sizeof(std::uint64_t));
			(void)segment;
			return original_relocation_value(relocation_index, address,
				sizeof(std::uint64_t));
		};
		const auto reject_authenticated_pointer = [&](std::size_t relocation_index,
			std::uint64_t vm_address) {
			const auto encoded = read64(relocation_index, vm_address);
			if ((encoded >> 63) == 0 || ((encoded >> 62) & 1) != 0 ||
				((encoded >> 51) & 0x7ff) != 0) {
				throw std::runtime_error("invalid ARM64 authenticated-pointer metadata");
			}
			const auto key = (encoded >> 49) & 0x3;
			const auto address_diversity = (encoded >> 48) & 1;
			const auto discriminator = (encoded >> 32) & 0xffff;
			const auto addend = encoded & 0xffffffffu;
			throw std::runtime_error(
				"ARM64 authenticated pointer requires PAC backend (key=" +
				std::to_string(key) + ", address_diversity=" +
				std::to_string(address_diversity) + ", discriminator=" +
				std::to_string(discriminator) + ", addend=" +
				std::to_string(addend) + ")");
		};
		const auto write32 = [&](std::uint64_t vm_address, std::uint32_t value) {
			const auto [address, segment] = find_address(vm_address, sizeof(value));
			DWORD old_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value),
				PAGE_READWRITE, &old_protection)) {
				ThrowLastError("VirtualProtect(ARM64 Mach-O relocation)");
			}
			std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
			DWORD restored_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value),
				ProtectionFor(segment->initial_protection), &restored_protection)) {
				ThrowLastError("VirtualProtect(ARM64 Mach-O relocation restore)");
			}
			FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), sizeof(value));
		};
		const auto page = [](std::uint64_t address) { return address & ~std::uint64_t(0xfff); };
		for (std::size_t index = 0; index < m_relocations.size(); ++index) {
			const auto& relocation = m_relocations[index];
			if (!relocation.external) {
				if (relocation.type == 11) {
					if (relocation.pc_relative || relocation.length != 3 || relocation.address < 0) {
						throw std::runtime_error("invalid ARM64 authenticated-pointer relocation");
					}
					reject_authenticated_pointer(index, resolve_vm_address(
						static_cast<std::uint32_t>(relocation.address)));
				}
				if (relocation.type != 0 || relocation.pc_relative || relocation.length != 3 ||
					relocation.address < 0) {
					throw std::runtime_error("unsupported ARM64 local Mach-O relocation record");
				}
				const auto vm_address = resolve_vm_address(
					static_cast<std::uint32_t>(relocation.address));
				const auto rebased = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(read64(index, vm_address)) + mapping.Slide() +
					(relocation.has_explicit_addend ? relocation.explicit_addend : 0));
				ApplyAbsolute64(mapping, vm_address, rebased);
				continue;
			}
			if (relocation.address < 0 || relocation.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid ARM64 Mach-O relocation record");
			}
			const auto vm_address = resolve_vm_address(
				static_cast<std::uint32_t>(relocation.address));
			if (relocation.type == 11) {
				if (relocation.pc_relative || relocation.length != 3) {
					throw std::runtime_error("invalid ARM64 authenticated-pointer relocation");
				}
				reject_authenticated_pointer(index, vm_address);
			}
			const auto target = symbol_addresses[relocation.symbol_index];
			const auto target_with_addend = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(target) +
				(relocation.has_explicit_addend ? relocation.explicit_addend : 0));
			if (relocation.type == 1 && !relocation.pc_relative && relocation.length == 3) {
				if (index + 1 >= m_relocations.size()) {
					throw std::runtime_error("ARM64 SUBTRACTOR relocation has no pair");
				}
				const auto& pair = m_relocations[index + 1];
				if (!pair.external || pair.type != 0 || pair.pc_relative || pair.length != 3 ||
					pair.address != relocation.address ||
					pair.symbol_index >= symbol_addresses.size()) {
					throw std::runtime_error("invalid ARM64 SUBTRACTOR relocation pair");
				}
				const auto value = target_with_addend - symbol_addresses[pair.symbol_index];
				ApplyAbsolute64(mapping, vm_address, value);
				++index;
			} else if (relocation.type == 0 && !relocation.pc_relative && relocation.length == 3) {
				ApplyAbsolute64(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 7 && !relocation.pc_relative && relocation.length == 3) {
				ApplyAbsolute64(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 7 && relocation.pc_relative && relocation.length == 2) {
				ApplyRelative32(mapping, vm_address, target_with_addend);
			} else if (relocation.type == 2 && relocation.pc_relative && relocation.length == 2) {
				const auto place = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto delta = static_cast<std::int64_t>(target_with_addend) -
					static_cast<std::int64_t>(place);
				if ((delta & 3) != 0) {
					throw std::runtime_error("ARM64 BRANCH26 target is not instruction aligned");
				}
				if (delta < -(std::int64_t(1) << 27) ||
					delta >= (std::int64_t(1) << 27)) {
					ApplyArm64BranchThunk(mapping, vm_address, target_with_addend);
				} else {
					const auto instruction = read32(index, vm_address);
					const auto immediate = static_cast<std::uint32_t>((delta >> 2) & 0x03ffffff);
					write32(vm_address, (instruction & 0xfc000000u) | immediate);
				}
			} else if ((relocation.type == 3 || relocation.type == 5 || relocation.type == 8) &&
				relocation.pc_relative && relocation.length == 2) {
				const auto place = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto page_delta = static_cast<std::int64_t>(page(target_with_addend)) -
					static_cast<std::int64_t>(page(place));
				const auto page_delta_units = page_delta >> 12;
				if (page_delta_units < -(std::int64_t(1) << 20) ||
					page_delta_units >= (std::int64_t(1) << 20)) {
					throw std::overflow_error("ARM64 PAGE21 relocation is out of range");
				}
				const auto instruction = read32(index, vm_address);
				const auto encoded = static_cast<std::uint32_t>(page_delta_units);
				const auto immlo = (encoded & 3u) << 29;
				const auto immhi = ((encoded >> 2) & 0x7ffffu) << 5;
				write32(vm_address, (instruction & 0x9f00001fu) | immlo | immhi);
			} else if ((relocation.type == 4 || relocation.type == 6 || relocation.type == 9) &&
				!relocation.pc_relative && relocation.length == 2) {
				const auto instruction = read32(index, vm_address);
				const auto offset = static_cast<std::uint32_t>(target_with_addend & 0xfff);
				std::uint32_t encoded = offset;
				if ((instruction & 0x1f000000u) == 0x11000000u) {
					if ((instruction & (1u << 22)) != 0 && offset != 0) {
						throw std::runtime_error("ARM64 PAGEOFF12 ADD instruction has an unsupported shift");
					}
				} else {
					const auto scale = 1u << ((instruction >> 30) & 3u);
					if ((offset % scale) != 0) {
						throw std::runtime_error("ARM64 PAGEOFF12 load/store offset is unaligned");
					}
					encoded = offset / scale;
				}
				if (encoded > 0xfff) {
					throw std::overflow_error("ARM64 PAGEOFF12 relocation is out of range");
				}
				write32(vm_address, (instruction & 0xffc003ffu) | (encoded << 10));
			} else {
				throw std::runtime_error("unsupported ARM64 Mach-O relocation record");
			}
		}
		return;
	}
	RequireNativeExecution(*this);
	const auto relocation_addend = [&](std::size_t index, std::uint64_t vm_address,
		std::size_t width) {
		const auto site = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		const auto raw = original_relocation_value(index, site, width);
		if (width == sizeof(std::int32_t)) {
			std::int32_t value = 0;
			std::memcpy(&value, &raw, sizeof(value));
			return static_cast<std::int64_t>(value);
		}
		if (width == sizeof(std::int64_t)) {
			std::int64_t value = 0;
			std::memcpy(&value, &raw, sizeof(value));
			return value;
		}
		throw std::logic_error("unsupported x86-64 Mach-O relocation addend width");
	};
	const auto got_slot_address = [&](std::uint32_t symbol_index) {
		std::uintptr_t lazy_slot = 0;
		for (const auto& slot : IndirectImportSlots()) {
			if (slot.symbol_index >= m_indirect_symbols.size())
				throw std::out_of_range("Mach-O indirect symbol table exceeds GOT slots");
			const auto indirect_symbol = m_indirect_symbols[slot.symbol_index];
			if ((indirect_symbol & 0xC0000000u) == 0 &&
				indirect_symbol == symbol_index) {
				const auto address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(slot.address) + mapping.Slide());
				if (!slot.lazy)
					return address;
				if (lazy_slot == 0)
					lazy_slot = address;
			}
		}
		if (lazy_slot != 0)
			return lazy_slot;
		throw std::runtime_error("Mach-O GOT relocation has no indirect pointer slot");
	};
	for (std::size_t index = 0; index < m_relocations.size(); ++index) {
		const auto& relocation = m_relocations[index];
		if (!relocation.external) {
			continue;
		}
		if (relocation.address < 0 ||
			relocation.symbol_index >= symbol_addresses.size()) {
			throw std::runtime_error("unsupported x86-64 Mach-O relocation record");
		}
		if (relocation.type == 5 && !relocation.pc_relative && relocation.length == 3) {
			if (index + 1 >= m_relocations.size()) {
				throw std::runtime_error("Mach-O SUBTRACTOR relocation has no pair");
			}
			const auto& pair = m_relocations[index + 1];
			if (!pair.external || pair.type != 0 || pair.pc_relative || pair.length != 3 ||
				pair.address != relocation.address ||
				pair.symbol_index >= symbol_addresses.size()) {
				throw std::runtime_error("invalid Mach-O SUBTRACTOR relocation pair");
			}
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int64_t));
			const auto value = AddSignedAddress(
				symbol_addresses[relocation.symbol_index] -
				symbol_addresses[pair.symbol_index], addend);
			ApplyAbsolute64(mapping, static_cast<std::uint32_t>(relocation.address), value);
			++index;
		} else if (!relocation.pc_relative && relocation.length == 3 &&
			(relocation.type == 0 || relocation.type == 3 || relocation.type == 4)) {
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int64_t));
			ApplyAbsolute64(mapping, static_cast<std::uint32_t>(relocation.address),
				AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 1 || relocation.type == 2)) {
			if (IsOptimizedTLVLEA(mapping,
				static_cast<std::uint32_t>(relocation.address),
				symbol_addresses[relocation.symbol_index]))
				throw std::runtime_error(
					"optimized Mach-O TLV LEA requires a per-thread direct-address bridge");
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			try {
				ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
			} catch (const std::overflow_error&) {
				if (relocation.type == 2) {
					ApplyBranchThunk(mapping,
						static_cast<std::uint32_t>(relocation.address),
						AddSignedAddress(symbol_addresses[relocation.symbol_index], addend));
				} else if (!defer_unreachable_external) {
					throw;
				}
			}
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 3 || relocation.type == 4)) {
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			const auto slot = got_slot_address(relocation.symbol_index);
			try {
				ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(slot, addend));
			} catch (const std::overflow_error&) {
				if (!defer_unreachable_external)
					throw;
			}
		} else if (relocation.pc_relative && relocation.length == 2 &&
			(relocation.type == 6 || relocation.type == 7 || relocation.type == 8)) {
			const auto addend = relocation.type == 6 ? 1 :
				relocation.type == 7 ? 2 : 4;
			const auto encoded_addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			try {
				ApplyRelative32WithAddend(mapping,
					static_cast<std::uint32_t>(relocation.address),
					AddSignedAddress(symbol_addresses[relocation.symbol_index],
						encoded_addend), addend);
			} catch (const std::overflow_error&) {
				if (!defer_unreachable_external)
					throw;
			}
		} else if (relocation.type == 9) {
			const auto local_pointer_address = TLVPointerAddress(mapping,
				symbol_addresses[relocation.symbol_index]);
			const auto pointer_address = local_pointer_address == 0 ?
				symbol_addresses[relocation.symbol_index] : local_pointer_address;
			if (pointer_address == 0 || !relocation.pc_relative || relocation.length != 2)
				throw std::runtime_error(
					"Mach-O x86-64 TLV relocation has no local __thread_ptrs slot");
			const auto addend = relocation_addend(index,
				static_cast<std::uint32_t>(relocation.address), sizeof(std::int32_t));
			ApplyRelative32(mapping, static_cast<std::uint32_t>(relocation.address),
				AddSignedAddress(pointer_address, addend));
		} else {
			throw std::runtime_error("unsupported x86-64 Mach-O relocation record");
		}
	}
}

std::uintptr_t MachOImage::TLVPointerAddress(const Mapping& mapping,
	std::uintptr_t descriptor_address) const
{
	const MachOSection* variables = nullptr;
	const MachOSection* pointers = nullptr;
	for (const auto& section : m_sections) {
		if (section.section_name == "__thread_vars")
			variables = &section;
		else if (section.section_name == "__thread_ptrs")
			pointers = &section;
	}
	if (variables == nullptr || pointers == nullptr ||
		variables->size == 0 || pointers->size < sizeof(std::uintptr_t))
		return 0;
	const auto variables_address = SectionAddress(mapping, variables->segment_name,
		variables->section_name);
	if (descriptor_address < variables_address ||
		descriptor_address - variables_address >= variables->size ||
		(descriptor_address - variables_address) % sizeof(DarwinTLVDescriptor) != 0)
		return 0;
	const auto index = (descriptor_address - variables_address) /
		sizeof(DarwinTLVDescriptor);
	if (index > std::numeric_limits<std::size_t>::max() / sizeof(std::uintptr_t) ||
		index * sizeof(std::uintptr_t) >= pointers->size)
		return 0;
	const auto pointers_address = SectionAddress(mapping, pointers->segment_name,
		pointers->section_name);
	return pointers_address + index * sizeof(std::uintptr_t);
}

bool MachOImage::IsOptimizedTLVLEA(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	if (TLVPointerAddress(mapping, target) == 0)
		return false;
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address + 2 ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t))
			continue;
		const auto instruction = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address - 2) + mapping.Slide());
		return *reinterpret_cast<const std::uint8_t*>(instruction) == 0x8d;
	}
	return false;
}

void MachOImage::ApplyBindActions(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	if (!IsArm64())
		RequireNativeExecution(*this);
	for (const auto& action : m_bind_actions) {
		if (action.lazy) {
			continue;
		}
		if (action.threaded) {
			if (action.type != 1 || action.segment_index >= m_segments.size() ||
				action.threaded_table_size != m_threaded_bind_targets.size()) {
				throw std::runtime_error("invalid Mach-O threaded bind action");
			}
			const auto& segment = m_segments[action.segment_index];
			if (action.segment_offset > segment.vm_size ||
				segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
				throw std::out_of_range("Mach-O threaded bind offset is outside its segment");
			}
			auto vm_address = segment.vm_address + action.segment_offset;
			// The ordinal table contains unique bind targets, not the number of
			// pointers in the chain. A binary may reuse one target across many
			// slots, so bound traversal by the segment's pointer capacity instead.
			const auto maximum_nodes = (segment.vm_size - action.segment_offset) /
				sizeof(std::uint64_t);
			for (std::uint64_t node = 0; node < maximum_nodes; ++node) {
				if (vm_address < segment.vm_address ||
					vm_address - segment.vm_address > segment.vm_size ||
					segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::uint64_t)) {
					throw std::out_of_range("Mach-O threaded bind chain leaves its segment");
				}
				const auto host_address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(vm_address) + mapping.Slide());
				const auto raw = *reinterpret_cast<const std::uint64_t*>(host_address);
				const auto ordinal = static_cast<std::size_t>(raw & 0x00ffffffu);
				const auto next = static_cast<std::uint16_t>((raw >> 51) & 0x0fffu);
				if ((raw >> 63) == 0 || ordinal >= m_threaded_bind_targets.size()) {
					throw std::runtime_error("unsupported Mach-O threaded bind pointer");
				}
				const auto& target_action = m_threaded_bind_targets[ordinal];
				const auto match = std::find_if(bindings.begin(), bindings.end(),
					[&target_action](const auto& binding) {
						return binding.symbol == target_action.symbol &&
							(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
								target_action.dylib_ordinal);
					});
				if (match == bindings.end() && !target_action.weak) {
					throw std::runtime_error("unresolved Mach-O threaded bind action: " +
						target_action.symbol);
				}
				const auto pointer_addend = static_cast<std::int8_t>((raw >> 24) & 0xff);
				auto target = AddSignedAddress(match == bindings.end() ? 0 :
					match->address, target_action.addend);
				target = AddSignedAddress(target, pointer_addend);
				ApplyAbsolute64(mapping, vm_address, target);
				if (next == 0) {
					break;
				}
				const auto relative = vm_address - segment.vm_address;
				const auto next_bytes = static_cast<std::uint64_t>(next) * 8;
				if (next_bytes > segment.vm_size - relative ||
					vm_address > std::numeric_limits<std::uint64_t>::max() - next_bytes) {
					throw std::out_of_range(
						"Mach-O threaded bind chain leaves its segment");
				}
				vm_address += next_bytes;
				if (node + 1 == maximum_nodes) {
					throw std::runtime_error(
						"Mach-O threaded bind chain has no terminator");
				}
			}
			continue;
		}
		if ((action.type < 1 || action.type > 3) ||
			action.segment_index >= m_segments.size()) {
			throw std::runtime_error("unsupported Mach-O dyld bind action");
		}
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&action](const auto& binding) {
				return binding.symbol == action.symbol &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						action.dylib_ordinal);
			});
		if (match == bindings.end()) {
			if (!action.weak) {
				throw std::runtime_error("unresolved Mach-O dyld bind action: " + action.symbol);
			}
		}
		const auto& segment = m_segments[action.segment_index];
		if (action.segment_offset > segment.vm_size ||
			segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
			throw std::out_of_range("Mach-O dyld bind offset is outside its segment");
		}
		if (action.segment_offset > segment.file_size ||
			segment.file_size - action.segment_offset < sizeof(std::uintptr_t)) {
			throw std::out_of_range(
				"Mach-O dyld bind offset is outside its file-backed segment");
		}
		const auto target = AddSignedAddress(match == bindings.end() ? 0 :
			match->address, action.addend);
		const auto vm_address = segment.vm_address + action.segment_offset;
		if (std::getenv("WINTOSH_TRACE_LAZY_BIND") != nullptr &&
			action.symbol.find("dyld_stub_binder") != std::string::npos) {
			std::fprintf(stderr,
				"WINTOSH_STUB_BINDER_BIND symbol=%s type=%u target=0x%llx site=0x%llx\n",
				action.symbol.c_str(), static_cast<unsigned>(action.type),
				static_cast<unsigned long long>(target),
				static_cast<unsigned long long>(static_cast<std::intptr_t>(vm_address) + mapping.Slide()));
		}
		if (action.type == 1)
			ApplyAbsolute64(mapping, vm_address, target);
		else if (action.type == 2)
			ApplyAbsolute32(mapping, vm_address, target);
		else
			ApplyRelative32(mapping, vm_address, target);
		if (std::getenv("WINTOSH_TRACE_LAZY_BIND") != nullptr &&
			action.symbol.find("dyld_stub_binder") != std::string::npos &&
			!Is32Bit()) {
			const auto site = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(vm_address) + mapping.Slide());
			std::fprintf(stderr,
				"WINTOSH_STUB_BINDER_AFTER value=0x%llx\n",
				static_cast<unsigned long long>(
					*reinterpret_cast<const std::uintptr_t*>(site)));
		}
	}
}

void MachOImage::ApplyLazyBindAction(std::size_t action_index, const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	if (action_index >= m_bind_actions.size() || !m_bind_actions[action_index].lazy) {
		throw std::out_of_range("Mach-O lazy bind action index is invalid");
	}
	const auto& action = m_bind_actions[action_index];
	if (action.type < 1 || action.type > 3 || action.segment_index >= m_segments.size()) {
		throw std::runtime_error("unsupported Mach-O lazy bind action");
	}
	const auto same_binding_symbol = [](const std::string& left,
		const std::string& right) {
		return left == right || (left.size() > 1 && left.front() == '_' &&
			left.substr(1) == right);
	};
	const auto match = std::find_if(bindings.begin(), bindings.end(),
		[&action, &same_binding_symbol](const auto& binding) {
			return same_binding_symbol(action.symbol, binding.symbol) &&
				(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
					action.dylib_ordinal);
		});
	if (match == bindings.end()) {
		if (!action.weak) {
			throw std::runtime_error("unresolved Mach-O lazy bind action: " + action.symbol);
		}
	}
	const auto& segment = m_segments[action.segment_index];
	if (action.segment_offset > segment.vm_size ||
		segment.vm_size - action.segment_offset < sizeof(std::uintptr_t)) {
		throw std::out_of_range("Mach-O lazy bind offset is outside its segment");
	}
	if (action.segment_offset > segment.file_size ||
		segment.file_size - action.segment_offset < sizeof(std::uintptr_t)) {
		throw std::out_of_range(
			"Mach-O lazy bind offset is outside its file-backed segment");
	}
	const auto target = AddSignedAddress(match == bindings.end() ? 0 :
		match->address, action.addend);
	const auto vm_address = segment.vm_address + action.segment_offset;
	if (action.type == 1)
		ApplyAbsolute64(mapping, vm_address, target);
	else if (action.type == 2)
		ApplyAbsolute32(mapping, vm_address, target);
	else
		ApplyRelative32(mapping, vm_address, target);
}

void MachOImage::ApplyLazyBindActions(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	for (std::size_t index = 0; index < m_bind_actions.size(); ++index) {
		if (m_bind_actions[index].lazy)
			ApplyLazyBindAction(index, mapping, bindings);
	}
}

void MachOImage::ApplyChainedFixups(const Mapping& mapping,
	const std::vector<std::uintptr_t>& bind_addresses) const
{
	if (!IsArm64()) {
		RequireNativeExecution(*this);
	}
	for (const auto& fixup : m_chained_fixups) {
		if (fixup.authenticated) {
			throw std::runtime_error(
				"ARM64e chained authenticated pointer requires PAC backend");
		}
		if (fixup.segment_index >= m_segments.size()) {
			throw std::runtime_error("Mach-O chained fixup segment index is invalid");
		}
		std::uintptr_t value = 0;
		if (fixup.non_pointer) {
			value = static_cast<std::uintptr_t>(fixup.target);
		} else if (fixup.bind) {
			if (fixup.bind_ordinal >= bind_addresses.size()) {
				throw std::runtime_error("Mach-O chained bind ordinal is out of range");
			}
			value = AddSignedAddress(bind_addresses[fixup.bind_ordinal], fixup.addend);
		} else if (fixup.pointer_format == 3) {
			value = AddSignedAddress(static_cast<std::uintptr_t>(fixup.target),
				mapping.Slide());
		} else {
			if (fixup.pointer_format == 6 || fixup.pointer_format == 7 ||
				fixup.pointer_format == 9 || fixup.pointer_format == 12) {
				const auto text_segment = std::find_if(mapping.Segments().begin(),
					mapping.Segments().end(), [](const auto& segment) {
						return segment.name == "__TEXT";
					});
				if (text_segment == mapping.Segments().end())
					throw std::runtime_error("Mach-O chained offset fixup has no __TEXT base");
				if (fixup.target > std::numeric_limits<std::uintptr_t>::max() -
					text_segment->address)
					throw std::overflow_error("Mach-O chained offset target overflows");
				value = text_segment->address + static_cast<std::uintptr_t>(fixup.target);
			} else if (fixup.pointer_format == 2) {
				const auto high8 = fixup.target & 0xff00000000000000ull;
				const auto unslid_target = static_cast<std::uintptr_t>(
					fixup.target & ((1ull << 36) - 1));
				const auto slid_target = AddSignedAddress(unslid_target,
					mapping.Slide());
				value = (slid_target & 0x00ffffffffffffffull) | high8;
			} else {
				value = AddSignedAddress(static_cast<std::uintptr_t>(fixup.target),
					mapping.Slide());
			}
		}
		if (fixup.pointer_format == 3)
			ApplyAbsolute32(mapping, fixup.vm_address, value);
		else
			ApplyAbsolute64(mapping, fixup.vm_address, value);
	}
}

void MachOImage::ApplyChainedFixups(const Mapping& mapping,
	const std::vector<DyldResolvedBinding>& bindings) const
{
	if (!IsArm64()) {
		RequireNativeExecution(*this);
	}
	std::vector<std::uintptr_t> addresses;
	addresses.reserve(m_chained_imports.size());
	for (const auto& import : m_chained_imports) {
		const auto match = std::find_if(bindings.begin(), bindings.end(),
			[&import](const auto& binding) {
				return binding.symbol == import.symbol &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						import.dylib_ordinal);
			});
		if (match == bindings.end() && !import.weak) {
			throw std::runtime_error("unresolved chained import: " + import.symbol);
		}
		addresses.push_back(AddSignedAddress(match == bindings.end() ? 0 :
			match->address, import.addend));
	}
	ApplyChainedFixups(mapping, addresses);
}

void MachOImage::ApplyRebaseActions(const Mapping& mapping) const
{
	if (!IsArm64())
		RequireNativeExecution(*this);
	for (const auto& action : m_rebase_actions) {
		if ((action.type != 1 && action.type != 2) ||
			action.segment_index >= m_segments.size()) {
			throw std::runtime_error("unsupported Mach-O dyld rebase action");
		}
		const auto& segment = m_segments[action.segment_index];
		const auto width = action.type == 2 ? sizeof(std::uint32_t) :
			sizeof(std::uintptr_t);
		if (action.segment_offset > segment.vm_size ||
			segment.vm_size - action.segment_offset < width) {
			throw std::out_of_range("Mach-O dyld rebase offset is outside its segment");
		}
		const auto vm_address = segment.vm_address + action.segment_offset;
		const auto host_address = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(vm_address) + mapping.Slide());
		if (action.type == 2) {
			std::uint32_t old_value = 0;
			std::memcpy(&old_value, reinterpret_cast<const void*>(host_address),
				sizeof(old_value));
			if (old_value == 0)
				continue;
			const auto rebased_value = static_cast<std::int64_t>(old_value) +
				static_cast<std::int64_t>(mapping.Slide());
			if (rebased_value < 0 || rebased_value >
				static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
				throw std::overflow_error("Mach-O 32-bit absolute rebase is out of range (value=" +
					std::to_string(old_value) + ", slide=" +
					std::to_string(mapping.Slide()) + ")");
			}
			ApplyAbsolute32(mapping, vm_address,
				static_cast<std::uintptr_t>(static_cast<std::uint32_t>(rebased_value)));
		} else {
			std::uintptr_t old_value = 0;
			std::memcpy(&old_value, reinterpret_cast<const void*>(host_address),
				sizeof(old_value));
			if (old_value == 0)
				continue;
			const auto rebased_value = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(old_value) + mapping.Slide());
			ApplyAbsolute64(mapping, vm_address, rebased_value);
		}
	}
}

void MachOImage::ApplyRelative32(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	const auto site = static_cast<std::intptr_t>(vm_address) + mapping.Slide();
	const auto displacement = static_cast<std::int64_t>(target) -
		(static_cast<std::int64_t>(site) + 4);
	if (displacement < std::numeric_limits<std::int32_t>::min() ||
		displacement > std::numeric_limits<std::int32_t>::max()) {
		throw std::overflow_error("x86-64 PC-relative relocation is out of range");
	}
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t)) {
			continue;
		}
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(site), sizeof(std::int32_t),
			PAGE_READWRITE, &old_protection)) {
			ThrowLastError("VirtualProtect(Mach-O PC-relative relocation)");
		}
		const auto value = static_cast<std::int32_t>(displacement);
		std::memcpy(reinterpret_cast<void*>(site), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(site), sizeof(std::int32_t),
			ProtectionFor(segment.initial_protection), &restored_protection)) {
			ThrowLastError("VirtualProtect(Mach-O PC-relative relocation restore)");
		}
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site),
			sizeof(value));
		return;
	}
	throw std::out_of_range("Mach-O PC-relative relocation is outside a segment");
}

void MachOImage::ApplyBranchThunk(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	constexpr std::size_t ThunkBytes = 16;
	constexpr std::size_t ThunkCapacity = 0x10000 / ThunkBytes;
	const auto existing = std::find_if(mapping.m_branch_thunks.begin(),
		mapping.m_branch_thunks.end(), [target](const auto& thunk) {
			return thunk.first == target;
		});
	if (existing != mapping.m_branch_thunks.end()) {
		ApplyRelative32(mapping, vm_address, existing->second);
		return;
	}
	if (mapping.m_reservation == nullptr || mapping.m_thunk_pool_used >= ThunkCapacity)
		throw std::runtime_error("Mach-O x86-64 branch thunk pool is exhausted");
	const auto pool_base = reinterpret_cast<std::uintptr_t>(mapping.m_reservation) +
		mapping.m_thunk_pool_offset;
	const auto thunk_address = pool_base + mapping.m_thunk_pool_used * ThunkBytes;
	const auto page_address = thunk_address & ~std::uintptr_t(0xfff);
	const auto page_offset = thunk_address - page_address;
	const bool first_on_page = (page_offset == 0);
	if (first_on_page && VirtualAlloc(reinterpret_cast<void*>(page_address), 0x1000,
		MEM_COMMIT, PAGE_READWRITE) == nullptr) {
		ThrowLastError("VirtualAlloc(Mach-O branch thunk)");
	}
	DWORD old_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
		PAGE_READWRITE, &old_protection)) {
		ThrowLastError("VirtualProtect(Mach-O branch thunk write)");
	}
	std::array<std::uint8_t, 14> code{};
	code[0] = 0xff;
	code[1] = 0x25; // jmp qword ptr [rip + 0]
	std::memcpy(code.data() + 6, &target, sizeof(target));
	std::memcpy(reinterpret_cast<void*>(thunk_address), code.data(), code.size());
	DWORD restored_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
		PAGE_EXECUTE_READ, &restored_protection)) {
		ThrowLastError("VirtualProtect(Mach-O branch thunk execute)");
	}
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(thunk_address),
		code.size());
	mapping.m_branch_thunks.emplace_back(target, thunk_address);
	++mapping.m_thunk_pool_used;
	ApplyRelative32(mapping, vm_address, thunk_address);
}

void MachOImage::ApplyArm64BranchThunk(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target) const
{
	constexpr std::size_t ThunkBytes = 16;
	constexpr std::size_t ThunkCapacity = 0x10000 / ThunkBytes;
	const auto existing = std::find_if(mapping.m_branch_thunks.begin(),
		mapping.m_branch_thunks.end(), [target](const auto& thunk) {
			return thunk.first == target;
		});
	std::uintptr_t thunk_address = 0;
	if (existing != mapping.m_branch_thunks.end()) {
		thunk_address = existing->second;
	} else {
		if (mapping.m_reservation == nullptr || mapping.m_thunk_pool_used >= ThunkCapacity)
			throw std::runtime_error("Mach-O ARM64 branch thunk pool is exhausted");
		const auto pool_base = reinterpret_cast<std::uintptr_t>(mapping.m_reservation) +
			mapping.m_thunk_pool_offset;
		thunk_address = pool_base + mapping.m_thunk_pool_used * ThunkBytes;
		const auto page_address = thunk_address & ~std::uintptr_t(0xfff);
		const auto page_offset = thunk_address - page_address;
		if (page_offset == 0 && VirtualAlloc(reinterpret_cast<void*>(page_address), 0x1000,
			MEM_COMMIT, PAGE_READWRITE) == nullptr) {
			ThrowLastError("VirtualAlloc(Mach-O ARM64 branch thunk)");
		}
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
			PAGE_READWRITE, &old_protection)) {
			ThrowLastError("VirtualProtect(Mach-O ARM64 branch thunk write)");
		}
		// ldr x16, #8; br x16; .quad target. The source BL/B instruction
		// transfers control here; BR preserves the source BL's LR value.
		const std::array<std::uint32_t, 2> instructions{
			0x58000050u, // LDR X16, [PC, #8]
			0xd61f0200u  // BR X16
		};
		std::memcpy(reinterpret_cast<void*>(thunk_address), instructions.data(),
			sizeof(instructions));
		std::memcpy(reinterpret_cast<void*>(thunk_address + sizeof(instructions)),
			&target, sizeof(target));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(page_address), 0x1000,
			PAGE_EXECUTE_READ, &restored_protection)) {
			ThrowLastError("VirtualProtect(Mach-O ARM64 branch thunk execute)");
		}
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(thunk_address),
			ThunkBytes);
		mapping.m_branch_thunks.emplace_back(target, thunk_address);
		++mapping.m_thunk_pool_used;
	}
	const auto place = static_cast<std::uintptr_t>(
		static_cast<std::intptr_t>(vm_address) + mapping.Slide());
	const auto segment = std::find_if(m_segments.begin(), m_segments.end(),
		[vm_address](const auto& candidate) {
			return vm_address >= candidate.vm_address &&
				vm_address - candidate.vm_address <= candidate.vm_size &&
				candidate.vm_size - (vm_address - candidate.vm_address) >=
					sizeof(std::uint32_t);
		});
	if (segment == m_segments.end())
		throw std::out_of_range("Mach-O ARM64 branch relocation is outside a segment");
	const auto delta = static_cast<std::int64_t>(thunk_address) -
		static_cast<std::int64_t>(place);
	if ((delta & 3) != 0 || delta < -(std::int64_t(1) << 27) ||
		delta >= (std::int64_t(1) << 27)) {
		throw std::overflow_error("Mach-O ARM64 branch thunk is out of BRANCH26 range");
	}
	DWORD old_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(place), sizeof(std::uint32_t),
		PAGE_READWRITE, &old_protection)) {
		ThrowLastError("VirtualProtect(Mach-O ARM64 branch relocation)");
	}
	std::uint32_t instruction = 0;
	std::memcpy(&instruction, reinterpret_cast<const void*>(place), sizeof(instruction));
	const auto immediate = static_cast<std::uint32_t>((delta >> 2) & 0x03ffffff);
	instruction = (instruction & 0xfc000000u) | immediate;
	std::memcpy(reinterpret_cast<void*>(place), &instruction, sizeof(instruction));
	DWORD restored_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(place), sizeof(instruction),
		ProtectionFor(segment->initial_protection), &restored_protection)) {
		ThrowLastError("VirtualProtect(Mach-O ARM64 branch relocation restore)");
	}
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(place), sizeof(instruction));
}

void MachOImage::ApplyRelative32WithAddend(const Mapping& mapping,
	std::uint64_t vm_address, std::uintptr_t target, std::int32_t addend) const
{
	const auto site = static_cast<std::intptr_t>(vm_address) + mapping.Slide();
	const auto displacement = static_cast<std::int64_t>(target) -
		(static_cast<std::int64_t>(site) + static_cast<std::int64_t>(addend));
	const auto minimum = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
	const auto maximum = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
	if (displacement < minimum || displacement > maximum)
		throw std::overflow_error("Mach-O signed 32-bit relocation is out of range");
	for (const auto& segment : m_segments) {
		if (vm_address < segment.vm_address ||
			vm_address - segment.vm_address > segment.vm_size ||
			segment.vm_size - (vm_address - segment.vm_address) < sizeof(std::int32_t))
			continue;
		const auto address = static_cast<std::uintptr_t>(site);
		DWORD old_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::int32_t),
			PAGE_READWRITE, &old_protection))
			ThrowLastError("VirtualProtect(Mach-O signed 32-bit relocation)");
		const auto value = static_cast<std::int32_t>(displacement);
		std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		DWORD restored_protection = PAGE_NOACCESS;
		if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(std::int32_t),
			ProtectionFor(segment.initial_protection), &restored_protection))
			ThrowLastError("VirtualProtect(Mach-O signed 32-bit relocation restore)");
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address),
			sizeof(std::int32_t));
		return;
	}
	throw std::out_of_range("Mach-O sized PC-relative relocation is outside a segment");
}

const std::uint8_t* MachOImage::FileData(std::uint64_t offset, std::size_t bytes) const
{
	if (!RangeFits(m_file_bytes, offset, bytes)) {
		throw std::out_of_range("Mach-O file data range is outside the mapped file");
	}
	return m_view + m_image_offset + static_cast<std::size_t>(offset);
}

MachOImage::Mapping MachOImage::MapSegments(std::uintptr_t near_address) const
{
	Require(!m_segments.empty(), "Mach-O image has no loadable segments");

	std::uint64_t min_vm = std::numeric_limits<std::uint64_t>::max();
	std::uint64_t max_vm = 0;
	for (const auto& segment : m_segments) {
		Require(segment.vm_size != 0, "Mach-O segment has zero VM size");
		Require(segment.vm_address <= std::numeric_limits<std::uint64_t>::max() - segment.vm_size,
			"Mach-O segment VM range overflows");
		min_vm = std::min(min_vm, AlignDown(segment.vm_address));
		max_vm = std::max(max_vm, AlignUp(segment.vm_address + segment.vm_size));
	}
	Require(max_vm > min_vm && max_vm - min_vm <= std::numeric_limits<std::size_t>::max(),
		"Mach-O VM range is invalid");
	const auto image_reservation_bytes = static_cast<std::size_t>(max_vm - min_vm);
	constexpr std::size_t BranchThunkPoolBytes = 0x10000;
	Require(image_reservation_bytes <= std::numeric_limits<std::size_t>::max() -
		BranchThunkPoolBytes, "Mach-O reservation including branch thunks overflows");
	const auto reservation_bytes = image_reservation_bytes + BranchThunkPoolBytes;
	void* reservation = nullptr;
	if (near_address != 0) {
		constexpr std::uintptr_t AllocationGranularity = 0x10000;
		constexpr std::uintptr_t SearchStep = 0x1000000;
		// Keep every dependency reservation, including its thunk pool, inside
		// one shared window narrower than the signed x86-64 PC-relative range.
		constexpr std::uintptr_t SearchLimit = 0x3f000000;
		constexpr std::uintptr_t WindowBytes = SearchLimit * 2;
		const auto aligned_reference = near_address & ~(AllocationGranularity - 1);
		const auto lower_bound = aligned_reference > SearchLimit ?
			aligned_reference - SearchLimit : AllocationGranularity;
		const auto upper_center = aligned_reference <=
			std::numeric_limits<std::uintptr_t>::max() - SearchLimit ?
			aligned_reference + SearchLimit :
			std::numeric_limits<std::uintptr_t>::max();
		const auto upper_bound = reservation_bytes <= WindowBytes &&
			upper_center >= reservation_bytes ?
			upper_center - reservation_bytes : 0;
		for (std::uintptr_t distance = 0;
			distance <= SearchLimit && reservation == nullptr && upper_bound >= lower_bound;
			distance += SearchStep) {
			const std::uintptr_t candidates[] = {
				aligned_reference + distance,
				distance <= aligned_reference ? aligned_reference - distance : 0};
			for (const auto candidate : candidates) {
				if (candidate < lower_bound || candidate > upper_bound ||
					candidate > std::numeric_limits<std::uintptr_t>::max() - reservation_bytes)
					continue;
				reservation = VirtualAlloc(reinterpret_cast<void*>(candidate),
					reservation_bytes, MEM_RESERVE, PAGE_NOACCESS);
				if (reservation != nullptr)
					break;
			}
		}
	}
	if (reservation == nullptr)
		reservation = VirtualAlloc(nullptr, reservation_bytes, MEM_RESERVE, PAGE_NOACCESS);
	if (reservation == nullptr) {
		ThrowLastError("VirtualAlloc(Mach-O reservation)");
	}

	try {
		const auto base = reinterpret_cast<std::uintptr_t>(reservation);
		const auto slide = static_cast<std::intptr_t>(base) - static_cast<std::intptr_t>(min_vm);
		std::vector<MappedMachOSegment> mapped;
		mapped.reserve(m_segments.size());
		for (const auto& segment : m_segments) {
			const auto address = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(segment.vm_address) + slide);
			if (segment.file_size == 0 && segment.initial_protection == 0) {
				mapped.push_back({segment.name, address,
					static_cast<std::size_t>(AlignUp(segment.vm_size))});
				continue;
			}
			const auto commit_bytes = static_cast<std::size_t>(AlignUp(segment.vm_size));
			if (VirtualAlloc(reinterpret_cast<void*>(address), commit_bytes,
				MEM_COMMIT, PAGE_READWRITE) == nullptr) {
				ThrowLastError("VirtualAlloc(Mach-O segment)");
			}
			if (segment.file_size != 0) {
				const auto* source = FileData(segment.file_offset,
					static_cast<std::size_t>(segment.file_size));
				std::memcpy(reinterpret_cast<void*>(address), source,
					static_cast<std::size_t>(segment.file_size));
			}
			DWORD old_protection = PAGE_NOACCESS;
			if (!VirtualProtect(reinterpret_cast<void*>(address), commit_bytes,
				ProtectionFor(segment.initial_protection), &old_protection)) {
				ThrowLastError("VirtualProtect(Mach-O segment)");
			}
			mapped.push_back({segment.name, address, commit_bytes});
		}

		std::uintptr_t entry_address = 0;
		for (const auto& segment : m_segments) {
			if (m_entry_offset >= segment.file_offset &&
				m_entry_offset - segment.file_offset < segment.file_size) {
				entry_address = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(segment.vm_address) + slide +
					static_cast<std::intptr_t>(m_entry_offset - segment.file_offset));
				break;
			}
		}
		Require(entry_address != 0 || m_header->file_type != MH_EXECUTE,
			"Mach-O executable entry point is not inside a file-backed segment");
		return Mapping(reservation, std::move(mapped), entry_address, slide,
			reservation_bytes, image_reservation_bytes);
	} catch (...) {
		VirtualFree(reservation, 0, MEM_RELEASE);
		throw;
	}
}

void MachOImage::ExecuteEntryNoArgs(const Mapping& mapping) const
{
	RequireNativeExecution(*this);
	if (mapping.EntryAddress() == 0) {
		throw std::runtime_error("Mach-O mapping has no executable entry address");
	}
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<void*>(mapping.EntryAddress()), 1);
	using EntryFunction = abi::GuestVoidFn;
	auto entry = reinterpret_cast<EntryFunction>(mapping.EntryAddress());
	entry();
}

void MachOImage::ExecuteInitializers(const Mapping& mapping) const
{
#if defined(WINTOSH_HAS_DYNARMIC)
	if (IsArm64()) {
		for (const auto& section : m_sections) {
			if (section.section_name != "__mod_init_func") continue;
			if (section.size % sizeof(std::uintptr_t) != 0)
				throw std::runtime_error("ARM64 Mach-O __mod_init_func section is not pointer-aligned");
			const auto address = SectionAddress(mapping, section.segment_name,
				section.section_name);
			const auto count = static_cast<std::size_t>(section.size /
				sizeof(std::uintptr_t));
			for (std::size_t index = 0; index < count; ++index) {
				const auto function_address = *reinterpret_cast<const std::uintptr_t*>(
					address + index * sizeof(std::uintptr_t));
				if (function_address == 0 ||
					function_address == static_cast<std::uintptr_t>(-1)) continue;
				darling_objc_queue_guest_callback(
					reinterpret_cast<IMP>(function_address), nullptr, nullptr);
			}
		}
		return;
	}
#endif
	RequireNativeExecution(*this);
	for (const auto& section : m_sections) {
		if (section.section_name != "__mod_init_func")
			continue;
		if (section.size % sizeof(std::uintptr_t) != 0)
			throw std::runtime_error("Mach-O __mod_init_func section is not pointer-aligned");
		const auto address = SectionAddress(mapping, section.segment_name,
			section.section_name);
		const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
		for (std::size_t index = 0; index < count; ++index) {
			const auto function_address = *reinterpret_cast<const std::uintptr_t*>(
				address + index * sizeof(std::uintptr_t));
			if (function_address == 0 || function_address == static_cast<std::uintptr_t>(-1))
				continue;
			FlushInstructionCache(GetCurrentProcess(),
				reinterpret_cast<void*>(function_address), 1);
			using Initializer = abi::GuestVoidFn;
			auto initializer = reinterpret_cast<Initializer>(function_address);
			initializer();
		}
	}
}

void RegisterActiveArm64GuestImage(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
#if defined(WINTOSH_HAS_DYNARMIC)
	if (!image.IsArm64() || active_arm64_backend == nullptr ||
		active_arm64_host_imports == nullptr)
		return;
	if (image.Segments().size() != mapping.Segments().size())
		throw std::logic_error("ARM64 dynamic image segment metadata is inconsistent");
	std::vector<std::uintptr_t> registered_addresses;
	const auto imports_before = active_arm64_host_imports->size();
	try {
		for (std::size_t index = 0; index < mapping.Segments().size(); ++index) {
			const auto& segment = mapping.Segments()[index];
			const auto protection = image.Segments()[index].initial_protection;
			if (!active_arm64_backend->RegisterDynamicGuestMapping({segment.address,
				segment.size, (protection & 1) != 0, (protection & 2) != 0,
				(protection & 4) != 0, true, segment.address, segment.size, true}))
				throw std::runtime_error("ARM64 dynamic image segment overlaps guest memory");
			registered_addresses.push_back(segment.address);
		}
		const auto [thunk_address, thunk_bytes] = mapping.Arm64ThunkPoolRegion();
		if (thunk_address != 0 && thunk_bytes != 0) {
			if (!active_arm64_backend->RegisterDynamicGuestMapping({thunk_address,
				thunk_bytes, true, false, true, true, thunk_address, thunk_bytes, true}))
				throw std::runtime_error("ARM64 dynamic image import thunks overlap guest memory");
			registered_addresses.push_back(thunk_address);
		}
		const auto& imports = mapping.Arm64HostImports();
		active_arm64_host_imports->insert(active_arm64_host_imports->end(),
			imports.begin(), imports.end());
	} catch (...) {
		for (const auto address : registered_addresses)
			(void)active_arm64_backend->RemoveDynamicGuestMapping(address);
		active_arm64_host_imports->resize(imports_before);
		throw;
	}
#else
	(void)image;
	(void)mapping;
#endif
}

const void* SetActiveArm64GuestDso(const void* dso_handle) noexcept
{
#if defined(WINTOSH_HAS_DYNARMIC)
	const auto* previous = active_arm64_dso_handle;
	active_arm64_dso_handle = dso_handle;
	return previous;
#else
	(void)dso_handle;
	return nullptr;
#endif
}

void FinalizeActiveArm64GuestImage(const MachOImage& image,
	const MachOImage::Mapping& mapping)
{
#if defined(WINTOSH_HAS_DYNARMIC)
	if (!image.IsArm64() || active_arm64_callback_runner == nullptr ||
		active_arm64_seed == nullptr || active_arm64_depth == nullptr) {
		darling_windows_cxa_finalize_guest(
			const_cast<void*>(static_cast<const void*>(&mapping)), nullptr, nullptr);
		return;
	}
	const auto invoke = +[](void* function, void* argument, bool has_argument,
		void* opaque) {
		(void)opaque;
		A64CpuState callback = *active_arm64_seed;
		callback.pc = reinterpret_cast<std::uintptr_t>(function);
		callback.x[0] = has_argument ? reinterpret_cast<std::uintptr_t>(argument) : 0;
		callback.x[1] = 0;
		const auto result = (*active_arm64_callback_runner)(callback,
			*active_arm64_depth + 1);
		*active_arm64_seed = result.state;
	};
	*active_arm64_depth = 0;
	darling_windows_cxa_finalize_guest(
		const_cast<void*>(static_cast<const void*>(&mapping)), invoke, nullptr);
#else
	(void)image;
	(void)mapping;
#endif
}

void UnregisterActiveArm64GuestImage(const MachOImage& image,
	const MachOImage::Mapping& mapping) noexcept
{
#if defined(WINTOSH_HAS_DYNARMIC)
	if (!image.IsArm64() || active_arm64_backend == nullptr ||
		active_arm64_host_imports == nullptr)
		return;
	for (const auto& segment : mapping.Segments())
		(void)active_arm64_backend->RemoveDynamicGuestMapping(segment.address);
	const auto [thunk_address, thunk_bytes] = mapping.Arm64ThunkPoolRegion();
	if (thunk_address != 0 && thunk_bytes != 0)
		(void)active_arm64_backend->RemoveDynamicGuestMapping(thunk_address);
	const auto& imports = mapping.Arm64HostImports();
	active_arm64_host_imports->erase(std::remove_if(active_arm64_host_imports->begin(),
		active_arm64_host_imports->end(), [&imports](const auto& candidate) {
			return std::any_of(imports.begin(), imports.end(), [&candidate](const auto& item) {
				return item.identifier == candidate.identifier;
			});
		}), active_arm64_host_imports->end());
#else
	(void)image;
	(void)mapping;
#endif
}

void MachOImage::ExecuteTerminators(const Mapping& mapping) const
{
	if (mapping.TerminatorsExecuted())
		return;
#if defined(WINTOSH_HAS_DYNARMIC)
	if (IsArm64()) {
		const auto has_terminator = std::any_of(m_sections.begin(), m_sections.end(),
			[](const auto& section) {
				return section.section_name == "__mod_term_func" && section.size != 0;
			});
		if (has_terminator && !darling_objc_has_guest_method_invoker())
			throw std::runtime_error("ARM64 Mach-O terminators require the guest-call/import dispatcher");
		for (const auto& section : m_sections) {
			if (section.section_name != "__mod_term_func")
				continue;
			if (section.size % sizeof(std::uintptr_t) != 0)
				throw std::runtime_error("Mach-O __mod_term_func section is not pointer-aligned");
			const auto address = SectionAddress(mapping, section.segment_name, section.section_name);
			const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
			for (std::size_t index = count; index != 0; --index) {
				const auto function_address = *reinterpret_cast<const std::uintptr_t*>(address +
					(index - 1) * sizeof(std::uintptr_t));
				if (function_address == 0 || function_address == static_cast<std::uintptr_t>(-1))
					continue;
				darling_objc_queue_guest_callback(reinterpret_cast<IMP>(function_address), nullptr, nullptr);
			}
		}
		if (has_terminator && !darling_objc_run_pending_guest_callbacks())
			throw std::runtime_error("ARM64 Mach-O terminators could not be dispatched");
		mapping.MarkTerminatorsExecuted();
		return;
	}
#endif
	RequireNativeExecution(*this);
	for (const auto& section : m_sections) {
		if (section.section_name != "__mod_term_func")
			continue;
		if (section.size % sizeof(std::uintptr_t) != 0)
			throw std::runtime_error("Mach-O __mod_term_func section is not pointer-aligned");
		const auto address = SectionAddress(mapping, section.segment_name,
			section.section_name);
		const auto count = static_cast<std::size_t>(section.size / sizeof(std::uintptr_t));
		for (std::size_t index = count; index != 0; --index) {
			const auto function_address = *reinterpret_cast<const std::uintptr_t*>(
				address + (index - 1) * sizeof(std::uintptr_t));
			if (function_address == 0 || function_address == static_cast<std::uintptr_t>(-1))
				continue;
			FlushInstructionCache(GetCurrentProcess(),
				reinterpret_cast<void*>(function_address), 1);
			using Terminator = abi::GuestVoidFn;
			auto terminator = reinterpret_cast<Terminator>(function_address);
			terminator();
		}
	}
	mapping.MarkTerminatorsExecuted();
}

int MachOImage::ExecuteEntry(const Mapping& mapping,
	const std::vector<std::string>& arguments,
	const std::vector<std::string>& environment,
	const std::vector<std::string>& apple_parameters,
	DarwinEntryAbi entry_abi,
	const std::vector<std::pair<const MachOImage*, const Mapping*>>& additional_images,
	bool finalize_process) const
{
#if defined(WINTOSH_HAS_DYNARMIC)
	if (!IsArm64())
		RequireNativeExecution(*this);
#else
	RequireNativeExecution(*this);
#endif
	if (mapping.EntryAddress() == 0) {
		throw std::runtime_error("Mach-O mapping has no executable entry address");
	}
	if (arguments.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		throw std::overflow_error("Mach-O argument count exceeds int range");
	}
	std::vector<std::vector<char>> argument_storage;
	argument_storage.reserve(arguments.size());
	std::vector<char*> argv;
	argv.reserve(arguments.size() + 1);
	for (const auto& argument : arguments) {
		argument_storage.emplace_back(argument.begin(), argument.end());
		argument_storage.back().push_back('\0');
		argv.push_back(argument_storage.back().data());
	}
	argv.push_back(nullptr);
	std::vector<std::vector<char>> environment_storage;
	environment_storage.reserve(environment.size());
	std::vector<char*> envp;
	envp.reserve(environment.size() + 1);
	for (const auto& value : environment) {
		environment_storage.emplace_back(value.begin(), value.end());
		environment_storage.back().push_back('\0');
		envp.push_back(environment_storage.back().data());
	}
	envp.push_back(nullptr);
	if (argv.empty() || argv.back() != nullptr ||
		envp.empty() || envp.back() != nullptr) {
		throw std::logic_error("Darwin startup vectors must be null terminated");
	}
	// Darwin exposes the same launch-owned vector through `environ` and
	// NSGetEnviron. Bind it before any guest startup code can observe it.
	darling_windows_set_environ_vector(
		const_cast<const char* const*>(envp.data()));
	// Materialize Apple parameters with the same lifetime as argv/envp. The
	// DarwinStart path places this vector after the null-terminated envp vector;
	// compatible-main entry keeps the parameters available to the launch scope.
	std::vector<std::vector<char>> apple_storage;
	apple_storage.reserve(apple_parameters.size());
	std::vector<char*> apple;
	apple.reserve(apple_parameters.size() + 1);
	for (const auto& parameter : apple_parameters) {
		apple_storage.emplace_back(parameter.begin(), parameter.end());
		apple_storage.back().push_back('\0');
		apple.push_back(apple_storage.back().data());
	}
	apple.push_back(nullptr);
	if (apple.empty() || apple.back() != nullptr) {
		throw std::logic_error("Darwin Apple parameter vector must be null terminated");
	}
#if defined(WINTOSH_HAS_DYNARMIC)
	if (IsArm64()) {
		if (mapping.Segments().size() != m_segments.size())
			throw std::logic_error("ARM64 Mach-O segment metadata does not match its mapping");
		std::size_t additional_segment_count = 0;
		for (const auto& [image, image_mapping] : additional_images) {
			if (image == nullptr || image_mapping == nullptr || !image->IsArm64() ||
				image->Segments().size() != image_mapping->Segments().size()) {
				throw std::logic_error("ARM64 dependency mapping metadata is inconsistent");
			}
			additional_segment_count += image_mapping->Segments().size();
		}
		std::vector<A64GuestMemoryRegion> guest_memory;
		std::vector<MachOImage::Mapping::Arm64HostImport> arm64_host_imports;
		guest_memory.reserve(mapping.Segments().size() + additional_segment_count + arguments.size() +
			environment.size() + apple_parameters.size() + 5);
		for (std::size_t segment_index = 0;
			segment_index < mapping.Segments().size(); ++segment_index) {
			const auto& mapped = mapping.Segments()[segment_index];
			const auto protection = m_segments[segment_index].initial_protection;
			guest_memory.push_back({mapped.address, mapped.size,
				(protection & 1) != 0, (protection & 2) != 0,
				(protection & 4) != 0, false, 0, 0, true});
		}
		const auto register_thunk_pool = [&guest_memory, &arm64_host_imports](
			const Mapping& candidate) {
			const auto [address, bytes] = candidate.Arm64ThunkPoolRegion();
			if (address != 0 && bytes != 0)
				guest_memory.push_back({address, bytes, true, false, true});
			const auto& imports = candidate.Arm64HostImports();
			for (const auto& import : imports) {
				if (import.identifier == 0) {
					const auto overlaps = std::find_if(guest_memory.begin(),
						guest_memory.end(), [&import](const auto& region) {
							return import.data_region < region.address + region.bytes &&
								region.address < import.data_region + import.data_region_bytes;
						});
					if (overlaps == guest_memory.end())
						guest_memory.push_back({import.data_region,
							import.data_region_bytes, true, import.data_writable, false});
				} else {
					arm64_host_imports.push_back(import);
				}
			}
		};
		register_thunk_pool(mapping);
		for (const auto& [image, image_mapping] : additional_images) {
			for (std::size_t segment_index = 0;
				segment_index < image_mapping->Segments().size(); ++segment_index) {
				const auto& mapped = image_mapping->Segments()[segment_index];
				const auto protection = image->Segments()[segment_index].initial_protection;
				guest_memory.push_back({mapped.address, mapped.size,
					(protection & 1) != 0, (protection & 2) != 0,
					(protection & 4) != 0, false, 0, 0, true});
			}
			register_thunk_pool(*image_mapping);
		}
		const auto add_host_buffer = [&guest_memory](const void* address,
			std::size_t bytes) {
			if (address != nullptr && bytes != 0) {
				guest_memory.push_back({reinterpret_cast<std::uintptr_t>(address),
				bytes, true, true, false});
			}
		};
		add_host_buffer(argv.data(), argv.size() * sizeof(argv.front()));
		add_host_buffer(envp.data(), envp.size() * sizeof(envp.front()));
		add_host_buffer(apple.data(), apple.size() * sizeof(apple.front()));
		for (const auto& value : argument_storage)
			add_host_buffer(value.data(), value.size());
		for (const auto& value : environment_storage)
			add_host_buffer(value.data(), value.size());
		for (const auto& value : apple_storage)
			add_host_buffer(value.data(), value.size());

		constexpr std::size_t startup_stack_guard_words = 512;
		constexpr std::size_t startup_stack_capacity = 2048;
		alignas(16) std::array<std::uintptr_t, startup_stack_capacity> startup_stack{};
		std::uintptr_t stack_pointer = reinterpret_cast<std::uintptr_t>(
			startup_stack.data() + startup_stack.size());
		if (entry_abi == DarwinEntryAbi::DarwinStart) {
			const auto startup_words = startup_stack_guard_words + 1 + argv.size() +
				envp.size() + apple.size();
			if (startup_words > startup_stack.size())
				throw std::runtime_error("Darwin startup stack exceeds backend fixture capacity");
			std::size_t stack_index = startup_stack_guard_words;
			startup_stack[stack_index++] = static_cast<std::uintptr_t>(arguments.size());
			for (const auto value : argv)
				startup_stack[stack_index++] = reinterpret_cast<std::uintptr_t>(value);
			for (const auto value : envp)
				startup_stack[stack_index++] = reinterpret_cast<std::uintptr_t>(value);
			for (const auto value : apple)
				startup_stack[stack_index++] = reinterpret_cast<std::uintptr_t>(value);
			stack_pointer = reinterpret_cast<std::uintptr_t>(
				startup_stack.data() + startup_stack_guard_words);
		}
		add_host_buffer(startup_stack.data(), sizeof(startup_stack));

		std::array<std::uint64_t, 31> registers{};
		if (entry_abi == DarwinEntryAbi::CompatibleMain) {
			registers[0] = arguments.size();
			registers[1] = reinterpret_cast<std::uintptr_t>(argv.data());
			registers[2] = reinterpret_cast<std::uintptr_t>(envp.data());
		}
		constexpr std::uint64_t return_sentinel = 0xfffffffffffff000ull;
		registers[30] = return_sentinel;

		DarlingWindowsA64Backend backend(std::move(guest_memory));
		auto* const previous_active_backend = active_arm64_backend;
		auto* const previous_active_imports = active_arm64_host_imports;
		active_arm64_backend = &backend;
		active_arm64_host_imports = &arm64_host_imports;
		struct RestoreActiveArm64Execution final {
			DarlingWindowsA64Backend* backend;
			std::vector<Mapping::Arm64HostImport>* imports;
			~RestoreActiveArm64Execution()
			{
				active_arm64_backend = backend;
				active_arm64_host_imports = imports;
			}
		} restore_active_arm64_execution{previous_active_backend,
			previous_active_imports};
		std::vector<std::uintptr_t> guest_malloc_allocations;
		std::vector<std::pair<std::uintptr_t, std::size_t>> guest_mmap_allocations;
		std::vector<std::unique_ptr<char[]>> guest_loader_error_buffers;
		struct GuestBlockStorageContext final {
			DarlingWindowsA64Backend* backend{};
			std::vector<void*> allocations;
			~GuestBlockStorageContext()
			{
				for (void* allocation : allocations) {
					if (allocation == nullptr)
						continue;
					(void)backend->RemoveDynamicGuestMapping(
						reinterpret_cast<std::uintptr_t>(allocation));
					HeapFree(GetProcessHeap(), 0, allocation);
				}
			}
		} guest_block_storage{&backend};
		const auto allocate_guest_block = +[](std::size_t bytes,
			void* opaque) -> void* {
			auto& storage = *static_cast<GuestBlockStorageContext*>(opaque);
			if (bytes == 0 || bytes > 64u * 1024u * 1024u)
				return nullptr;
			void* allocation = HeapAlloc(GetProcessHeap(), 0, bytes);
			if (allocation == nullptr)
				return nullptr;
			const auto address = reinterpret_cast<std::uintptr_t>(allocation);
			if (!storage.backend->RegisterDynamicGuestMapping({address, bytes,
				true, true, false, true, address, bytes, false})) {
				HeapFree(GetProcessHeap(), 0, allocation);
				throw std::runtime_error(
					"ARM64 block allocation overlaps registered guest memory");
			}
			try {
				storage.allocations.push_back(allocation);
			} catch (...) {
				(void)storage.backend->RemoveDynamicGuestMapping(address);
				HeapFree(GetProcessHeap(), 0, allocation);
				throw;
			}
			return allocation;
		};
		const auto deallocate_guest_block = +[](void* allocation,
			void* opaque) {
			if (allocation == nullptr)
				return;
			auto& storage = *static_cast<GuestBlockStorageContext*>(opaque);
			const auto found = std::find(storage.allocations.begin(),
				storage.allocations.end(), allocation);
			if (found == storage.allocations.end())
				throw std::runtime_error(
					"ARM64 block release does not own its guest mapping");
			(void)storage.backend->RemoveDynamicGuestMapping(
				reinterpret_cast<std::uintptr_t>(allocation));
			storage.allocations.erase(found);
			HeapFree(GetProcessHeap(), 0, allocation);
		};
		const auto copy_guest_loader_error = [&](const char* error) {
			if (error == nullptr)
				return std::uintptr_t{0};
			const auto* terminator = static_cast<const char*>(std::memchr(error, 0, 4096));
			if (terminator == nullptr)
				throw std::runtime_error("Darwin loader error string is not terminated");
			const auto bytes = static_cast<std::size_t>(terminator - error) + 1;
			auto buffer = std::make_unique<char[]>(bytes);
			std::memcpy(buffer.get(), error, bytes);
			const auto address = reinterpret_cast<std::uintptr_t>(buffer.get());
			if (!backend.RegisterDynamicGuestMapping({address, bytes, true, false,
				false, true, address, bytes, false}))
				throw std::runtime_error("cannot register ARM64 Darwin loader error string");
			try {
				guest_loader_error_buffers.push_back(std::move(buffer));
			} catch (...) {
				(void)backend.RemoveDynamicGuestMapping(address);
				throw;
			}
			return address;
		};
		const auto register_guest_malloc = [&](void* allocation, std::size_t requested_bytes,
			DarlingHostFreeImport free_target) {
			const auto address = reinterpret_cast<std::uintptr_t>(allocation);
			if (address == 0)
				return std::uintptr_t{0};
			const auto bytes = (std::max)(requested_bytes, std::size_t{1});
			A64GuestMemoryRegion region{address, bytes, true, true, false,
				true, address, bytes, true};
			if (!backend.RegisterDynamicGuestMapping(region)) {
				free_target(allocation);
				throw std::runtime_error("ARM64 malloc result overlaps a registered guest region");
			}
			guest_malloc_allocations.push_back(address);
			return address;
		};
		const auto release_guest_malloc = [&](void* allocation,
			DarlingHostFreeImport free_target) {
			const auto address = reinterpret_cast<std::uintptr_t>(allocation);
			const auto found = std::find(guest_malloc_allocations.begin(),
				guest_malloc_allocations.end(), address);
			if (found != guest_malloc_allocations.end()) {
				(void)backend.RemoveDynamicGuestMapping(address);
				guest_malloc_allocations.erase(found);
			} else if (backend.HasDynamicGuestMapping(address)) {
				throw std::runtime_error("ARM64 free received a non-malloc guest mapping");
			}
			free_target(allocation);
		};
		const auto resize_guest_malloc = [&](void* allocation,
			std::size_t requested_bytes, DarlingHostReallocImport realloc_target,
			DarlingHostFreeImport free_target) {
			const auto old_address = reinterpret_cast<std::uintptr_t>(allocation);
			if (allocation == nullptr)
				return register_guest_malloc(realloc_target(nullptr, requested_bytes),
					requested_bytes, free_target);
			const auto found = std::find(guest_malloc_allocations.begin(),
				guest_malloc_allocations.end(), old_address);
			if (found == guest_malloc_allocations.end())
				throw std::runtime_error("ARM64 realloc received an untracked guest pointer");
			if (requested_bytes == 0) {
				release_guest_malloc(allocation, free_target);
				return std::uintptr_t{0};
			}
			void* resized = realloc_target(allocation, requested_bytes);
			if (resized == nullptr)
				return std::uintptr_t{0};
			(void)backend.RemoveDynamicGuestMapping(old_address);
			guest_malloc_allocations.erase(found);
			return register_guest_malloc(resized, requested_bytes, free_target);
		};
		const auto open_guest_file = [&](const A64CpuState& state) {
			const auto& x = state.x;
			if (!backend.ContainsGuestCString(x[0], 1u << 20) ||
				x[1] > std::numeric_limits<std::uint32_t>::max() ||
				x[2] > std::numeric_limits<std::uint32_t>::max()) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto descriptor = darling_windows_open(
				reinterpret_cast<const char*>(static_cast<std::uintptr_t>(x[0])),
				static_cast<int>(static_cast<std::uint32_t>(x[1])),
				static_cast<int>(static_cast<std::uint32_t>(x[2])));
			return static_cast<std::uint64_t>(static_cast<std::int64_t>(descriptor));
		};
		const auto close_guest_file = [&](const A64CpuState& state) {
			const auto descriptor = static_cast<std::int32_t>(state.x[0]);
			return static_cast<std::uint64_t>(static_cast<std::int64_t>(
				darling_windows_close(descriptor)));
		};
		const auto seek_guest_file = [&](const A64CpuState& state) {
			if (state.x[2] > std::numeric_limits<std::uint32_t>::max()) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto position = darling_windows_lseek(
				static_cast<std::int32_t>(state.x[0]),
				static_cast<std::int64_t>(state.x[1]),
				static_cast<int>(static_cast<std::uint32_t>(state.x[2])));
			return static_cast<std::uint64_t>(position);
		};
		const auto read_guest_file = [&](const A64CpuState& state) {
			const auto& x = state.x;
			if (x[2] > (1u << 20) || x[2] > std::numeric_limits<std::size_t>::max() ||
				!backend.ContainsGuestRange(x[1], static_cast<std::size_t>(x[2]), true)) {
				darling::windows_host::DarwinErrno::Set(EFAULT);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto result = darling_windows_read(static_cast<std::int32_t>(x[0]),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
				static_cast<std::size_t>(x[2]));
			return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
		};
		const auto map_guest_memory = [&](const A64CpuState& state) {
			const auto& x = state.x;
			if (x[1] == 0 || x[1] > std::numeric_limits<std::size_t>::max() ||
				x[2] > 7 || x[3] > std::numeric_limits<int>::max() ||
				x[4] > std::numeric_limits<std::uint32_t>::max()) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uintptr_t{static_cast<std::uint64_t>(-1)};
			}
			const auto length = static_cast<std::size_t>(x[1]);
			const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
			if (page_size == 0 || length >
				std::numeric_limits<std::size_t>::max() - (page_size - 1)) {
				darling::windows_host::DarwinErrno::Set(75);
				return std::uintptr_t{static_cast<std::uint64_t>(-1)};
			}
			const auto rounded_length = ((length + page_size - 1) / page_size) * page_size;
			constexpr std::uint64_t map_fixed = 0x0010;
			if ((x[3] & map_fixed) != 0 && x[0] == 0) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uintptr_t{static_cast<std::uint64_t>(-1)};
			}
			std::optional<std::uint64_t> replaced_allocation_base;
			if ((x[3] & map_fixed) != 0 && x[0] != 0 &&
				backend.HasGuestRangeOverlap(x[0], rounded_length)) {
				replaced_allocation_base = backend.DynamicGuestMappingBaseForRange(
					x[0], rounded_length);
				if (!replaced_allocation_base || std::none_of(guest_mmap_allocations.begin(),
					guest_mmap_allocations.end(), [replaced_allocation_base](const auto& allocation) {
						return allocation.first == *replaced_allocation_base;
					})) {
					darling::windows_host::DarwinErrno::Set(EINVAL);
					return std::uintptr_t{static_cast<std::uint64_t>(-1)};
				}
			}
			void* mapped = darling_windows_mmap(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])), length,
				static_cast<int>(x[2]), static_cast<int>(x[3]),
				static_cast<int>(static_cast<std::int32_t>(x[4])),
				static_cast<std::int64_t>(x[5]));
			if (mapped == reinterpret_cast<void*>(-1))
				return std::uintptr_t{static_cast<std::uint64_t>(-1)};
			const auto address = reinterpret_cast<std::uintptr_t>(mapped);
			A64GuestMemoryRegion region{address, rounded_length, (x[2] & 1) != 0,
				(x[2] & 2) != 0, (x[2] & 4) != 0, true, address, length, true};
			const bool registered = replaced_allocation_base ?
				backend.ReplaceDynamicGuestMappingRange(address, rounded_length, region) :
				backend.RegisterDynamicGuestMapping(region);
			if (!registered) {
				(void)darling_windows_munmap(mapped, length);
				if (replaced_allocation_base)
					(void)backend.RemoveDynamicGuestMappingRange(address, rounded_length);
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uintptr_t{static_cast<std::uint64_t>(-1)};
			}
			if (!replaced_allocation_base) {
				try {
					guest_mmap_allocations.emplace_back(address, length);
				} catch (...) {
					(void)backend.RemoveDynamicGuestMapping(address);
					(void)darling_windows_munmap(mapped, length);
					throw;
				}
			}
			return address;
		};
		const auto unmap_guest_memory = [&](const A64CpuState& state) {
			const auto address = static_cast<std::uintptr_t>(state.x[0]);
			if (state.x[0] > std::numeric_limits<std::uintptr_t>::max() ||
				state.x[1] > std::numeric_limits<std::size_t>::max()) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto length = static_cast<std::size_t>(state.x[1]);
			const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
			if (page_size == 0 || length == 0 ||
				length > (std::numeric_limits<std::size_t>::max)() - (page_size - 1)) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto rounded_length = ((length + page_size - 1) / page_size) * page_size;
			const auto allocation_base = backend.DynamicGuestMappingBaseForRange(
				address, rounded_length);
			if (!allocation_base || std::none_of(guest_mmap_allocations.begin(),
				guest_mmap_allocations.end(), [allocation_base](const auto& allocation) {
					return allocation.first == *allocation_base;
				})) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto result = darling_windows_munmap(
				reinterpret_cast<void*>(address), rounded_length);
			if (result == 0) {
				(void)backend.RemoveDynamicGuestMappingRange(address, rounded_length);
				if (!backend.HasDynamicGuestMapping(*allocation_base)) {
					std::erase_if(guest_mmap_allocations, [allocation_base](const auto& allocation) {
						return allocation.first == *allocation_base;
					});
				}
			}
			return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
		};
		const auto protect_guest_memory = [&](const A64CpuState& state) {
			const auto address = state.x[0];
			if (address > std::numeric_limits<std::uintptr_t>::max() ||
				state.x[1] == 0 || state.x[1] > std::numeric_limits<std::size_t>::max() ||
				state.x[2] > 7) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto length = static_cast<std::size_t>(state.x[1]);
			const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
			if (page_size == 0 || length >
				std::numeric_limits<std::size_t>::max() - (page_size - 1)) {
				darling::windows_host::DarwinErrno::Set(75);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto rounded_length = ((length + page_size - 1) / page_size) * page_size;
			if (!backend.CanProtectGuestRange(address, rounded_length)) {
				darling::windows_host::DarwinErrno::Set(EINVAL);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			const auto result = darling_windows_mprotect(
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), length,
				static_cast<int>(state.x[2]));
			if (result == 0 && !backend.ProtectGuestRange(address, rounded_length,
				(state.x[2] & 1) != 0, (state.x[2] & 2) != 0,
				(state.x[2] & 4) != 0)) {
				darling::windows_host::DarwinErrno::Set(EFAULT);
				return std::uint64_t{static_cast<std::uint64_t>(-1)};
			}
			return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
		};
		A64CpuState cpu{};
		cpu.x = registers;
		cpu.sp = stack_pointer;
		cpu.pc = mapping.EntryAddress();
		std::function<A64ExecutionResult(A64CpuState, unsigned)> run_guest_callback;
		std::function<A64CpuState(const A64ExecutionResult&, unsigned)>
			dispatch_guest_import;
		A64CpuState initializer_seed = cpu;
		unsigned initializer_depth = 0;
		struct GuestInitializerContext final {
			A64CpuState* seed;
			unsigned* depth;
			DarlingWindowsA64Backend* backend;
			std::function<A64ExecutionResult(A64CpuState, unsigned)>* run;
		} initializer_context{&initializer_seed, &initializer_depth, &backend,
			&run_guest_callback};
		const auto invoke_guest_initializer = +[](IMP implementation, id receiver,
			SEL selector, void* opaque) {
			auto& context = *static_cast<GuestInitializerContext*>(opaque);
			A64CpuState callback = *context.seed;
			callback.pc = reinterpret_cast<std::uintptr_t>(implementation);
			callback.x[0] = reinterpret_cast<std::uintptr_t>(receiver);
			callback.x[1] = reinterpret_cast<std::uintptr_t>(selector);
			const auto result = (*context.run)(callback, *context.depth + 1);
			*context.seed = result.state;
		};
		const auto invoke_guest_function = +[](std::uintptr_t function,
			const std::uintptr_t* arguments, std::size_t argument_count,
			std::uintptr_t* returned_value, void* opaque) -> bool {
			auto& context = *static_cast<GuestInitializerContext*>(opaque);
			if (!context.run || context.backend == nullptr ||
				!context.backend->ContainsExecutableGuestAddress(function))
				return false;
			if (argument_count > 8 || (argument_count != 0 && arguments == nullptr))
				throw std::runtime_error("ARM64 guest callback has an unsupported register argument count");
			if (*context.depth >= 64)
				throw std::runtime_error("ARM64 guest function callback depth exceeded");
			A64CpuState callback = *context.seed;
			callback.pc = function;
			for (std::size_t index = 0; index < argument_count; ++index)
				callback.x[index] = arguments[index];
			const auto result = (*context.run)(callback, *context.depth + 1);
			*context.seed = result.state;
			if (returned_value != nullptr)
				*returned_value = static_cast<std::uintptr_t>(result.state.x[0]);
			return true;
		};
		const auto map_guest_callback_memory = +[](void* address,
			std::size_t bytes, bool guest_writes, bool map, void* opaque) -> bool {
			auto& context = *static_cast<GuestInitializerContext*>(opaque);
			if (context.backend == nullptr || address == nullptr || bytes == 0)
				return false;
			const auto guest_address = reinterpret_cast<std::uintptr_t>(address);
			if (!map)
				return context.backend->RemoveDynamicGuestMapping(guest_address);
			if (context.backend->ContainsGuestRange(guest_address, bytes, false))
				return false;
			return context.backend->RegisterDynamicGuestMapping({guest_address,
				bytes, true, guest_writes, false, true, guest_address, bytes, false});
		};
		dispatch_guest_import = [&](const A64ExecutionResult& trap,
			unsigned depth) -> A64CpuState {
			if (!trap.svc || *trap.svc != 0x82)
				throw std::runtime_error("ARM64 Objective-C guest callback issued an "
					"unsupported nested SVC");
			const auto identifier = static_cast<std::uint32_t>(trap.state.x[16]);
			const auto imported = std::find_if(arm64_host_imports.begin(),
				arm64_host_imports.end(), [identifier](const auto& item) {
					return item.identifier == identifier;
				});
			if (imported == arm64_host_imports.end())
				throw std::runtime_error("ARM64 nested import trap references unknown thunk " +
					std::to_string(identifier));
			A64CpuState resumed = trap.state;
			initializer_seed = trap.state;
			initializer_depth = depth;
			const auto& x = trap.state.x;
			const auto& name = imported->symbol;
			if (DispatchArm64MachMsgImport(name, imported->target, trap.state,
				resumed, backend)) {
				initializer_seed = trap.state;
				initializer_depth = depth;
				return resumed;
			}
			if (DispatchArm64CoreFoundationRunLoopImport(name, imported->target,
				trap.state, resumed, backend)) {
				initializer_seed = trap.state;
				initializer_depth = depth;
				return resumed;
			}
			if (name == "_dlerror") {
				resumed.x[0] = copy_guest_loader_error(
					reinterpret_cast<const char* (*)()>(imported->target)());
			} else if (name == "_dlopen") {
				if (x[0] != 0 && !backend.ContainsGuestCString(x[0], 1u << 20))
					throw std::runtime_error("ARM64 nested dlopen import received an invalid path");
				darling_windows_set_guest_callsite_pc(trap.state.pc);
				resumed.x[0] = reinterpret_cast<std::uintptr_t (*)(const char*, int)>(
					imported->target)(reinterpret_cast<const char*>(
						static_cast<std::uintptr_t>(x[0])), static_cast<int>(x[1]));
				darling_windows_set_guest_callsite_pc(trap.state.x[30]);
			} else if (name == "_dlsym") {
				if (!backend.ContainsGuestCString(x[1], 4096))
					throw std::runtime_error("ARM64 nested dlsym import received an invalid symbol name");
				resumed.x[0] = reinterpret_cast<std::uintptr_t (*)(void*, const char*)>(
					imported->target)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(x[1])));
			} else if (name == "_dlclose") {
				resumed.x[0] = static_cast<std::uint64_t>(
					reinterpret_cast<int (*)(void*)>(imported->target)(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
			} else if (name == "_open" || name == "open" ||
				name == "_open_nocancel" || name == "open_nocancel") {
				resumed.x[0] = open_guest_file(trap.state);
			} else if (name == "_close" || name == "close" ||
				name == "_close_nocancel" || name == "close_nocancel") {
				resumed.x[0] = close_guest_file(trap.state);
			} else if (name == "_lseek" || name == "lseek") {
				resumed.x[0] = seek_guest_file(trap.state);
			} else if (name == "_read" || name == "read" ||
				name == "_read_nocancel" || name == "read_nocancel") {
				resumed.x[0] = read_guest_file(trap.state);
			} else if (name == "_mmap" || name == "mmap") {
				resumed.x[0] = map_guest_memory(trap.state);
			} else if (name == "_munmap" || name == "munmap") {
				resumed.x[0] = unmap_guest_memory(trap.state);
			} else if (name == "_mprotect" || name == "mprotect") {
				resumed.x[0] = protect_guest_memory(trap.state);
			} else if (name == "_write") {
				const auto bytes = static_cast<std::size_t>(x[2]);
				if (bytes > (1u << 20) || !backend.ContainsGuestRange(x[1], bytes, false))
					throw std::runtime_error("ARM64 nested write import received an invalid guest range");
				resumed.x[0] = static_cast<std::uint64_t>(darling_windows_write(
					static_cast<int>(x[0]), reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(x[1])), bytes));
			} else if (name == "_CFStringCreateWithCString") {
				if (x[1] != 0 && !backend.ContainsGuestCString(x[1], 1u << 20))
					throw std::runtime_error("ARM64 CoreFoundation string import received an invalid C string");
				using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
					const void*, const char*, std::uint32_t);
				resumed.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(
					imported->target)(reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const char*>(
						static_cast<std::uintptr_t>(x[1])), static_cast<std::uint32_t>(x[2])));
			} else if (name == "_CFRelease") {
				using Call = void (DARLING_CF_GUEST_ABI *)(const void*);
				reinterpret_cast<Call>(imported->target)(reinterpret_cast<const void*>(
					static_cast<std::uintptr_t>(x[0])));
			} else if (name == "_atexit") {
				resumed.x[0] = static_cast<std::uint64_t>(
					darling_windows_atexit_guest_for_dso(
						reinterpret_cast<void (*)()>(static_cast<std::uintptr_t>(x[0])),
						active_arm64_dso_handle ? const_cast<void*>(active_arm64_dso_handle) : nullptr));
			} else if (name == "___cxa_atexit" || name == "_cxa_atexit") {
				resumed.x[0] = static_cast<std::uint64_t>(darling_windows_cxa_atexit_guest(
					reinterpret_cast<void (*)(void*)>(static_cast<std::uintptr_t>(x[0])),
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
					active_arm64_dso_handle ? const_cast<void*>(active_arm64_dso_handle) :
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
			} else if (name == "_objc_msgSend") {
				const auto receiver = reinterpret_cast<id>(
					static_cast<std::uintptr_t>(x[0]));
				const auto selector = reinterpret_cast<SEL>(
					static_cast<std::uintptr_t>(x[1]));
				if (receiver) {
					initializer_seed = trap.state;
					initializer_depth = depth;
					darling_objc_ensure_class_initialized(object_isClass(receiver) ?
						reinterpret_cast<Class>(receiver) : object_getClass(receiver));
				}
				const auto cls = receiver && object_isClass(receiver) ?
					reinterpret_cast<Class>(receiver) : nullptr;
				const auto method = cls ? class_getClassMethod(cls, selector) :
					receiver ? class_getInstanceMethod(object_getClass(receiver), selector) : nullptr;
				const auto implementation = method ?
					method_getImplementation(method) : nullptr;
				const auto implementation_address =
					reinterpret_cast<std::uintptr_t>(implementation);
				if (backend.ContainsExecutableGuestAddress(implementation_address)) {
					if (depth >= 64)
						throw std::runtime_error("ARM64 Objective-C guest callback depth exceeded");
					A64CpuState callback = trap.state;
					callback.pc = implementation_address;
					const auto result = run_guest_callback(callback, depth + 1);
					resumed.x[0] = result.state.x[0];
					resumed.x[1] = result.state.x[1];
					for (std::size_t index = 0; index < 4; ++index)
						resumed.v[index] = result.state.v[index];
				} else {
					struct alignas(16) ObjcReturn final {
						std::uint64_t gp[2]{};
						double fp[4]{};
					} returned{};
					darling_windows_objc_arm64_message_dispatch(
						reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
						selector,
						reinterpret_cast<const void*>(static_cast<std::uintptr_t>(trap.state.sp)),
						&returned, 0,
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[8])));
					resumed.x[0] = returned.gp[0];
					resumed.x[1] = returned.gp[1];
					for (std::size_t index = 0; index < 4; ++index)
						std::memcpy(&resumed.v[index], &returned.fp[index], sizeof(double));
				}
			} else if (name == "_sel_registerName" || name == "_objc_getClass") {
				if (!backend.ContainsGuestCString(x[0], 4096))
					throw std::runtime_error("ARM64 nested Objective-C name import received "
						"an invalid guest string");
				resumed.x[0] = reinterpret_cast<DarlingObjcNameImport>(imported->target)(
					reinterpret_cast<const char*>(static_cast<std::uintptr_t>(x[0])));
			} else if (name == "_malloc") {
				const auto requested_bytes = static_cast<std::size_t>(x[0]);
				void* allocation = reinterpret_cast<DarlingHostMallocImport>(
					imported->target)(requested_bytes);
				const auto free_import = std::find_if(arm64_host_imports.begin(),
					arm64_host_imports.end(), [](const auto& item) {
						return item.symbol == "_free";
					});
				const auto free_target = free_import == arm64_host_imports.end() ?
					&FreeStandardHostAllocation :
					reinterpret_cast<DarlingHostFreeImport>(free_import->target);
				resumed.x[0] = register_guest_malloc(allocation, requested_bytes,
					free_target);
			} else if (name == "_realloc") {
				const auto free_import = std::find_if(arm64_host_imports.begin(),
					arm64_host_imports.end(), [](const auto& item) {
						return item.symbol == "_free";
					});
				const auto free_target = free_import == arm64_host_imports.end() ?
					&FreeStandardHostAllocation :
					reinterpret_cast<DarlingHostFreeImport>(free_import->target);
				resumed.x[0] = resize_guest_malloc(
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
					static_cast<std::size_t>(x[1]),
					reinterpret_cast<DarlingHostReallocImport>(imported->target),
					free_target);
			} else if (name == "_free") {
				release_guest_malloc(
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
					reinterpret_cast<DarlingHostFreeImport>(imported->target));
				resumed.x[0] = 0;
			} else if (name == "__Block_copy" || name == "_Block_copy") {
				resumed.x[0] = reinterpret_cast<std::uintptr_t>(
					reinterpret_cast<DarlingBlockCopyImport>(
					imported->target)(reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(x[0]))));
			} else if (name == "__Block_release" || name == "_Block_release") {
				reinterpret_cast<DarlingBlockReleaseImport>(imported->target)(
					reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])));
				resumed.x[0] = 0;
			} else if (name == "__Block_object_assign" ||
				name == "_Block_object_assign") {
				reinterpret_cast<DarlingBlockObjectAssignImport>(imported->target)(
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
					reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1])),
					static_cast<int>(x[2]));
				resumed.x[0] = 0;
			} else if (name == "__Block_object_dispose" ||
				name == "_Block_object_dispose") {
				reinterpret_cast<DarlingBlockObjectDisposeImport>(imported->target)(
					reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
					static_cast<int>(x[1]));
				resumed.x[0] = 0;
			} else if (name == "_objc_release" || name == "_CFRelease") {
				reinterpret_cast<DarlingObjcReleaseImport>(imported->target)(
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
				resumed.x[0] = 0;
			} else if (name == "_objc_retain" ||
				name == "_objc_autoreleaseReturnValue" ||
				name == "_objc_retainAutoreleaseReturnValue") {
				resumed.x[0] = reinterpret_cast<std::uintptr_t>(
					reinterpret_cast<DarlingObjcUnaryImport>(imported->target)(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
			} else if (name == "_strlen") {
				if (!backend.ContainsGuestCString(x[0], 1u << 20))
					throw std::runtime_error("ARM64 nested strlen import received an invalid "
						"guest string");
				resumed.x[0] = reinterpret_cast<std::size_t (*)(const char*)>(
					imported->target)(reinterpret_cast<const char*>(
						static_cast<std::uintptr_t>(x[0])));
			} else if (name == "_memcpy" || name == "_memmove" ||
				name == "_memcmp" || name == "_memset") {
				const auto count = static_cast<std::size_t>(x[2]);
				if (count > (1u << 30) ||
					!backend.ContainsGuestRange(x[0], count, name != "_memcmp") ||
					(name != "_memset" && !backend.ContainsGuestRange(x[1], count, false)))
					throw std::runtime_error("ARM64 nested memory import received an invalid "
						"guest range");
				if (name == "_memcpy")
					resumed.x[0] = reinterpret_cast<std::uintptr_t>(
						reinterpret_cast<DarlingHostMemcpyImport>(imported->target)(
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1])), count));
				else if (name == "_memmove")
					resumed.x[0] = reinterpret_cast<std::uintptr_t>(
						reinterpret_cast<void* (*)(void*, const void*, std::size_t)>(
							imported->target)(reinterpret_cast<void*>(
								static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[1])), count));
				else if (name == "_memcmp")
					resumed.x[0] = static_cast<std::uint64_t>(static_cast<std::int64_t>(
						reinterpret_cast<int (*)(const void*, const void*, std::size_t)>(
							imported->target)(reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[1])), count)));
				else
					resumed.x[0] = reinterpret_cast<std::uintptr_t (*)(void*, int,
						std::size_t)>(imported->target)(reinterpret_cast<void*>(
						static_cast<std::uintptr_t>(x[0])), static_cast<int>(x[1]), count);
			} else {
				throw std::runtime_error("ARM64 nested host import has no ABI adapter: " + name);
			}
			return resumed;
		};
		run_guest_callback = [&](A64CpuState callback, unsigned depth) {
			callback.x[30] = return_sentinel;
			for (;;) {
				const auto result = backend.Run(callback);
				if (result.state.pc == return_sentinel ||
					(result.fault_address == return_sentinel && result.fault ==
						"AArch64 instruction fetch is outside executable guest memory"))
					return result;
				if (result.svc && *result.svc == 0x82) {
					callback = dispatch_guest_import(result, depth);
					continue;
				}
				if (!result.fault.empty())
					throw std::runtime_error("ARM64 Objective-C guest callback failed: " +
						result.fault + " pc=" + std::to_string(result.state.pc) +
						(result.fault_address ? " address=" +
							std::to_string(*result.fault_address) : std::string{}));
				if (result.svc)
					throw std::runtime_error("ARM64 Objective-C guest callback issued unsupported SVC #" +
						std::to_string(*result.svc));
				throw std::runtime_error("ARM64 Objective-C guest callback stopped before return");
			}
		};
		auto* const previous_callback_runner = active_arm64_callback_runner;
		auto* const previous_seed = active_arm64_seed;
		auto* const previous_depth = active_arm64_depth;
		active_arm64_callback_runner = &run_guest_callback;
		active_arm64_seed = &initializer_seed;
		active_arm64_depth = &initializer_depth;
		struct RestoreActiveArm64Callbacks final {
			Arm64GuestCallbackRunner* runner;
			A64CpuState* seed;
			unsigned* depth;
			~RestoreActiveArm64Callbacks()
			{
				active_arm64_callback_runner = runner;
				active_arm64_seed = seed;
				active_arm64_depth = depth;
			}
		} restore_active_arm64_callbacks{previous_callback_runner,
			previous_seed, previous_depth};
		const auto invoke_guest_exit = +[](void* function, void* argument,
			bool has_argument, void* opaque) {
			auto& context = *static_cast<GuestInitializerContext*>(opaque);
			A64CpuState callback = *context.seed;
			callback.pc = reinterpret_cast<std::uintptr_t>(function);
			callback.x[0] = has_argument ? reinterpret_cast<std::uintptr_t>(argument) : 0;
			callback.x[1] = 0;
			const auto result = (*context.run)(callback, *context.depth + 1);
			*context.seed = result.state;
		};
		const auto finalize_guest_process = [&]() {
			initializer_depth = 0;
			darling_windows_cxa_finalize_guest(nullptr, invoke_guest_exit,
				&initializer_context);
			ExecuteTerminators(mapping);
			for (auto image = additional_images.rbegin(); image != additional_images.rend(); ++image) {
				if (image->first && image->second)
					image->first->ExecuteTerminators(*image->second);
			}
		};
		void* previous_invoker_context = nullptr;
		const auto previous_invoker = darling_objc_set_guest_method_invoker(
			invoke_guest_initializer, &initializer_context,
			&previous_invoker_context);
		struct RestoreGuestInitializerInvoker final {
			darling_objc_guest_method_invoker invoker;
			void* context;
			~RestoreGuestInitializerInvoker()
			{
				(void)darling_objc_set_guest_method_invoker(invoker, context,
					nullptr);
			}
		} restore_initializer_invoker{previous_invoker,
			previous_invoker_context};
		void* previous_function_invoker_context = nullptr;
		const auto previous_function_invoker = darling_objc_set_guest_function_invoker(
			invoke_guest_function, &initializer_context,
			&previous_function_invoker_context);
		struct RestoreGuestFunctionInvoker final {
			darling_objc_guest_function_invoker invoker;
			void* context;
			~RestoreGuestFunctionInvoker()
			{
				(void)darling_objc_set_guest_function_invoker(invoker, context,
					nullptr);
			}
		} restore_function_invoker{previous_function_invoker,
			previous_function_invoker_context};
		void* previous_memory_mapper_context = nullptr;
		const auto previous_memory_mapper = darling_objc_set_guest_memory_mapper(
			map_guest_callback_memory, &initializer_context,
			&previous_memory_mapper_context);
		struct RestoreGuestMemoryMapper final {
			darling_objc_guest_memory_mapper mapper;
			void* context;
			~RestoreGuestMemoryMapper()
			{
				(void)darling_objc_set_guest_memory_mapper(mapper, context, nullptr);
			}
		} restore_memory_mapper{previous_memory_mapper,
			previous_memory_mapper_context};
		darling_objc_guest_block_allocate previous_block_allocate = nullptr;
		darling_objc_guest_block_deallocate previous_block_deallocate = nullptr;
		void* previous_block_allocator_context = nullptr;
		darling_objc_set_guest_block_allocator(allocate_guest_block,
			deallocate_guest_block, &guest_block_storage, &previous_block_allocate,
			&previous_block_deallocate, &previous_block_allocator_context);
		struct RestoreGuestBlockAllocator final {
			darling_objc_guest_block_allocate allocate;
			darling_objc_guest_block_deallocate deallocate;
			void* context;
			~RestoreGuestBlockAllocator()
			{
				darling_objc_set_guest_block_allocator(allocate, deallocate,
					context, nullptr, nullptr, nullptr);
			}
		} restore_guest_block_allocator{previous_block_allocate,
			previous_block_deallocate, previous_block_allocator_context};
		initializer_seed = cpu;
		initializer_depth = 0;
		if (!darling_objc_run_pending_guest_callbacks())
			throw std::runtime_error(
				"ARM64 startup callbacks require the guest-call dispatcher");
		for (;;) {
			const auto result = backend.Run(cpu);
			if (result.state.pc == return_sentinel ||
				(result.fault_address == return_sentinel &&
					result.fault == "AArch64 instruction fetch is outside executable guest memory")) {
				if (finalize_process) {
					initializer_seed = result.state;
					finalize_guest_process();
				}
				return static_cast<int>(result.state.x[0]);
			}
			if (result.svc) {
				if (*result.svc == 0x82) {
					const auto identifier = static_cast<std::uint32_t>(result.state.x[16]);
					const auto imported = std::find_if(arm64_host_imports.begin(),
						arm64_host_imports.end(), [identifier](const auto& item) {
							return item.identifier == identifier;
						});
					if (imported == arm64_host_imports.end())
						throw std::runtime_error("ARM64 import trap references unknown thunk " +
							std::to_string(identifier));
					cpu = result.state;
					initializer_seed = result.state;
					initializer_depth = 0;
					const auto& x = result.state.x;
					const auto& name = imported->symbol;
					if (DispatchArm64MachMsgImport(name, imported->target,
						result.state, cpu, backend)) {
						initializer_seed = result.state;
						initializer_depth = 0;
						continue;
					}
					if (DispatchArm64CoreFoundationRunLoopImport(name, imported->target,
						result.state, cpu, backend)) {
						initializer_seed = result.state;
						initializer_depth = 0;
						continue;
					}
					if (name == "_dlerror") {
						cpu.x[0] = copy_guest_loader_error(
							reinterpret_cast<const char* (*)()>(imported->target)());
					} else if (name == "_dlopen") {
						if (x[0] != 0 && !backend.ContainsGuestCString(x[0], 1u << 20))
							throw std::runtime_error("ARM64 dlopen import received an invalid path");
						darling_windows_set_guest_callsite_pc(result.state.pc);
						cpu.x[0] = reinterpret_cast<std::uintptr_t (*)(const char*, int)>(
							imported->target)(reinterpret_cast<const char*>(
								static_cast<std::uintptr_t>(x[0])), static_cast<int>(x[1]));
						darling_windows_set_guest_callsite_pc(result.state.x[30]);
					} else if (name == "_dlsym") {
						if (!backend.ContainsGuestCString(x[1], 4096))
							throw std::runtime_error("ARM64 dlsym import received an invalid symbol name");
						cpu.x[0] = reinterpret_cast<std::uintptr_t (*)(void*, const char*)>(
							imported->target)(reinterpret_cast<void*>(
								static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const char*>(
									static_cast<std::uintptr_t>(x[1])));
					} else if (name == "_dlclose") {
						cpu.x[0] = static_cast<std::uint64_t>(
							reinterpret_cast<int (*)(void*)>(imported->target)(
								reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
					} else if (name == "_open" || name == "open" ||
						name == "_open_nocancel" || name == "open_nocancel") {
						cpu.x[0] = open_guest_file(result.state);
					} else if (name == "_close" || name == "close" ||
						name == "_close_nocancel" || name == "close_nocancel") {
						cpu.x[0] = close_guest_file(result.state);
					} else if (name == "_lseek" || name == "lseek") {
						cpu.x[0] = seek_guest_file(result.state);
					} else if (name == "_read" || name == "read" ||
						name == "_read_nocancel" || name == "read_nocancel") {
						cpu.x[0] = read_guest_file(result.state);
					} else if (name == "_mmap" || name == "mmap") {
						cpu.x[0] = map_guest_memory(result.state);
					} else if (name == "_munmap" || name == "munmap") {
						cpu.x[0] = unmap_guest_memory(result.state);
					} else if (name == "_mprotect" || name == "mprotect") {
						cpu.x[0] = protect_guest_memory(result.state);
					} else if (name == "_write") {
						const auto bytes = static_cast<std::size_t>(x[2]);
						if (bytes > (1u << 20) || !backend.ContainsGuestRange(x[1], bytes, false))
							throw std::runtime_error("ARM64 write import received an invalid guest range");
						cpu.x[0] = static_cast<std::uint64_t>(darling_windows_write(
							static_cast<int>(x[0]), reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[1])), bytes));
					} else if (name == "_CFStringCreateWithCString") {
						if (x[1] != 0 && !backend.ContainsGuestCString(x[1], 1u << 20))
							throw std::runtime_error("ARM64 CoreFoundation string import received an invalid C string");
						using Call = darling_windows_CFStringRef (DARLING_CF_GUEST_ABI *)(
							const void*, const char*, std::uint32_t);
						cpu.x[0] = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Call>(
							imported->target)(reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const char*>(
								static_cast<std::uintptr_t>(x[1])), static_cast<std::uint32_t>(x[2])));
					} else if (name == "_atexit") {
						cpu.x[0] = static_cast<std::uint64_t>(
							darling_windows_atexit_guest_for_dso(
								reinterpret_cast<void (*)()>(static_cast<std::uintptr_t>(x[0])),
								active_arm64_dso_handle ? const_cast<void*>(active_arm64_dso_handle) : nullptr));
					} else if (name == "___cxa_atexit" || name == "_cxa_atexit") {
						cpu.x[0] = static_cast<std::uint64_t>(darling_windows_cxa_atexit_guest(
							reinterpret_cast<void (*)(void*)>(static_cast<std::uintptr_t>(x[0])),
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[1])),
							active_arm64_dso_handle ? const_cast<void*>(active_arm64_dso_handle) :
								reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[2]))));
					} else if (name == "_objc_msgSend" || name == "_objc_msgSendSuper" ||
						name == "_objc_msgSendSuper2") {
						if (name == "_objc_msgSend") {
							const auto receiver = reinterpret_cast<id>(
								static_cast<std::uintptr_t>(x[0]));
							const auto selector = reinterpret_cast<SEL>(
								static_cast<std::uintptr_t>(x[1]));
							if (receiver) {
								initializer_seed = result.state;
								initializer_depth = 0;
								darling_objc_ensure_class_initialized(
									object_isClass(receiver) ? reinterpret_cast<Class>(receiver) :
									object_getClass(receiver));
							}
							const auto cls = receiver && object_isClass(receiver) ?
								reinterpret_cast<Class>(receiver) : nullptr;
							const auto method = cls ? class_getClassMethod(cls, selector) :
								receiver ? class_getInstanceMethod(object_getClass(receiver), selector) : nullptr;
						const auto implementation = method ?
							method_getImplementation(method) : nullptr;
						const auto implementation_address =
							reinterpret_cast<std::uintptr_t>(implementation);
						if (backend.ContainsExecutableGuestAddress(implementation_address)) {
							A64CpuState nested = result.state;
							nested.pc = implementation_address;
							const auto nested_result = run_guest_callback(nested, 0);
							cpu = result.state;
							cpu.x[0] = nested_result.state.x[0];
							cpu.x[1] = nested_result.state.x[1];
							for (std::size_t index = 0; index < 4; ++index)
								cpu.v[index] = nested_result.state.v[index];
							continue;
						}
						}
						struct alignas(16) ObjcReturn final {
							std::uint64_t gp[2]{};
							double fp[4]{};
						} returned{};
						const unsigned kind = name == "_objc_msgSend" ? 0u :
							name == "_objc_msgSendSuper" ? 1u : 2u;
						darling_windows_objc_arm64_message_dispatch(
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
							reinterpret_cast<SEL>(static_cast<std::uintptr_t>(x[1])),
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(result.state.sp)),
							&returned, kind,
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[8])));
						cpu.x[0] = returned.gp[0];
						cpu.x[1] = returned.gp[1];
						for (std::size_t i = 0; i < 4; ++i)
							std::memcpy(&cpu.v[i], &returned.fp[i], sizeof(double));
					} else if (name == "_malloc") {
						const auto requested_bytes = static_cast<std::size_t>(x[0]);
						void* allocation = reinterpret_cast<DarlingHostMallocImport>(
							imported->target)(requested_bytes);
						const auto free_import = std::find_if(arm64_host_imports.begin(),
							arm64_host_imports.end(), [](const auto& item) {
								return item.symbol == "_free";
							});
						const auto free_target = free_import == arm64_host_imports.end() ?
							&FreeStandardHostAllocation :
							reinterpret_cast<DarlingHostFreeImport>(free_import->target);
						cpu.x[0] = register_guest_malloc(allocation, requested_bytes,
							free_target);
					} else if (name == "_realloc") {
						const auto free_import = std::find_if(arm64_host_imports.begin(),
							arm64_host_imports.end(), [](const auto& item) {
								return item.symbol == "_free";
							});
						const auto free_target = free_import == arm64_host_imports.end() ?
							&FreeStandardHostAllocation :
							reinterpret_cast<DarlingHostFreeImport>(free_import->target);
						cpu.x[0] = resize_guest_malloc(
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
							static_cast<std::size_t>(x[1]),
							reinterpret_cast<DarlingHostReallocImport>(imported->target),
							free_target);
					} else if (name == "_free") {
						release_guest_malloc(
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
							reinterpret_cast<DarlingHostFreeImport>(imported->target));
						cpu.x[0] = 0;
					} else if (name == "__Block_copy" || name == "_Block_copy") {
						cpu.x[0] = reinterpret_cast<std::uintptr_t>(
							reinterpret_cast<DarlingBlockCopyImport>(
							imported->target)(reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[0]))));
					} else if (name == "__Block_release" || name == "_Block_release") {
						reinterpret_cast<DarlingBlockReleaseImport>(imported->target)(
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])));
						cpu.x[0] = 0;
					} else if (name == "__Block_object_assign" ||
						name == "_Block_object_assign") {
						reinterpret_cast<DarlingBlockObjectAssignImport>(imported->target)(
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])),
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[1])),
							static_cast<int>(x[2]));
						cpu.x[0] = 0;
					} else if (name == "__Block_object_dispose" ||
						name == "_Block_object_dispose") {
						reinterpret_cast<DarlingBlockObjectDisposeImport>(imported->target)(
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x[0])),
							static_cast<int>(x[1]));
						cpu.x[0] = 0;
					} else if (name == "_CFRelease") {
						using Call = void (DARLING_CF_GUEST_ABI *)(const void*);
						reinterpret_cast<Call>(imported->target)(reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(x[0])));
						cpu.x[0] = 0;
					} else if (name == "_objc_release") {
						reinterpret_cast<DarlingObjcReleaseImport>(imported->target)(
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0])));
						cpu.x[0] = 0;
					} else if (name == "_objc_retain" ||
						name == "_objc_autoreleaseReturnValue" ||
						name == "_objc_retainAutoreleaseReturnValue") {
						cpu.x[0] = reinterpret_cast<std::uintptr_t>(
							reinterpret_cast<DarlingObjcUnaryImport>(imported->target)(
								reinterpret_cast<void*>(static_cast<std::uintptr_t>(x[0]))));
					} else if (name == "_strlen") {
						if (!backend.ContainsGuestCString(x[0], 1u << 20))
							throw std::runtime_error("ARM64 strlen import received an invalid guest string");
						cpu.x[0] = reinterpret_cast<std::size_t (*)(const char*)>(
							imported->target)(reinterpret_cast<const char*>(
								static_cast<std::uintptr_t>(x[0])));
					} else if (name == "_sel_registerName") {
						if (!backend.ContainsGuestCString(x[0], 4096))
							throw std::runtime_error("ARM64 selector import received an invalid string");
						cpu.x[0] = reinterpret_cast<DarlingObjcNameImport>(
							imported->target)(reinterpret_cast<const char*>(
								static_cast<std::uintptr_t>(x[0])));
					} else if (name == "_objc_getClass") {
						if (!backend.ContainsGuestCString(x[0], 4096))
							throw std::runtime_error("ARM64 class lookup import received an invalid string");
						cpu.x[0] = reinterpret_cast<DarlingObjcNameImport>(
							imported->target)(reinterpret_cast<const char*>(
								static_cast<std::uintptr_t>(x[0])));
					} else if (name == "_memcpy" || name == "_memmove" ||
						name == "_memcmp" || name == "_memset") {
						const auto count = static_cast<std::size_t>(x[2]);
						if (count > (1u << 30) ||
							!backend.ContainsGuestRange(x[0], count,
								name != "_memcmp") ||
							(name != "_memset" && !backend.ContainsGuestRange(
								x[1], count, false)))
							throw std::runtime_error("ARM64 memory import received an invalid guest range");
						if (name == "_memcpy")
							cpu.x[0] = reinterpret_cast<std::uintptr_t>(
								reinterpret_cast<DarlingHostMemcpyImport>(imported->target)(
									reinterpret_cast<void*>(
									static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
									static_cast<std::uintptr_t>(x[1])), count));
						else if (name == "_memmove")
							cpu.x[0] = reinterpret_cast<std::uintptr_t (*)(void*, const void*,
								std::size_t)>(imported->target)(reinterpret_cast<void*>(
								static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(x[1])), count);
						else if (name == "_memcmp")
							cpu.x[0] = static_cast<std::uint64_t>(static_cast<std::int64_t>(
								reinterpret_cast<int (*)(const void*, const void*, std::size_t)>(
									imported->target)(reinterpret_cast<const void*>(
										static_cast<std::uintptr_t>(x[0])), reinterpret_cast<const void*>(
										static_cast<std::uintptr_t>(x[1])), count)));
						else
							cpu.x[0] = reinterpret_cast<std::uintptr_t (*)(void*, int,
								std::size_t)>(imported->target)(reinterpret_cast<void*>(
								static_cast<std::uintptr_t>(x[0])), static_cast<int>(x[1]), count);
					} else {
						throw std::runtime_error("ARM64 host import has no ABI adapter: " + name);
					}
					continue;
				}
				if (*result.svc != 0x80) {
					throw std::runtime_error("ARM64 Mach-O reached unsupported SVC #" +
						std::to_string(*result.svc));
				}

				// Darwin encodes UNIX calls as class 2 in x16's high byte. Accept
				// unclassified low syscall numbers as well for hand-written guests.
				const auto syscall_word = result.state.x[16];
				const auto syscall_class = static_cast<std::uint8_t>(syscall_word >> 24);
				if (syscall_class != 0 && syscall_class != 2) {
					throw std::runtime_error("ARM64 Mach-O issued an unsupported non-UNIX "
						"SVC dispatch class " + std::to_string(syscall_class));
				}
				const auto syscall_number = static_cast<std::uint32_t>(
					syscall_word & 0x00ffffffu);
				const auto& arguments = result.state.x;
				if (syscall_number == 1) { // SYS_exit
					if (finalize_process) {
						initializer_seed = result.state;
						finalize_guest_process();
					}
					return static_cast<int>(arguments[0]);
				}

				std::int64_t syscall_result = -1;
				std::uint64_t syscall_result_x1 = result.state.x[1];
				int error_number = 0;
				DarwinErrno::Clear();
				switch (syscall_number) {
				case 9: // SYS_link
					if (!backend.ContainsGuestCString(arguments[0], 4096) ||
						!backend.ContainsGuestCString(arguments[1], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_link(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[1])));
					break;
				case 7: { // SYS_wait4
					if (arguments[0] > std::numeric_limits<std::uint32_t>::max() ||
						arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					if ((arguments[1] != 0 && !backend.ContainsGuestRange(arguments[1],
							sizeof(int), true)) ||
						(arguments[3] != 0 && !backend.ContainsGuestRange(arguments[3],
							sizeof(darling_rusage), true))) {
						error_number = EFAULT;
						break;
					}
					int status = 0;
					darling_rusage usage{};
					syscall_result = darling_windows_wait4(
						static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[0])),
						arguments[1] == 0 ? nullptr : &status,
						static_cast<int>(arguments[2]),
						arguments[3] == 0 ? nullptr : &usage);
					if (syscall_result > 0) {
						if (arguments[1] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[1])), &status, sizeof(status));
						if (arguments[3] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[3])), &usage, sizeof(usage));
					}
					break;
				}
				case 73: { // SYS_munmap (owned anonymous or placeholder-backed file subranges)
					if (arguments[0] > std::numeric_limits<std::uintptr_t>::max() ||
						arguments[1] > std::numeric_limits<std::size_t>::max() ||
						arguments[1] == 0) {
						error_number = EINVAL;
						break;
					}
					const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
					if (page_size == 0 || arguments[1] >
						(std::numeric_limits<std::size_t>::max)() - (page_size - 1)) {
						error_number = EINVAL;
						break;
					}
					const auto rounded_length = ((static_cast<std::size_t>(arguments[1]) + page_size - 1) /
						page_size) * page_size;
					const auto allocation_base = backend.DynamicGuestMappingBaseForRange(
						arguments[0], rounded_length);
					if (!allocation_base || std::none_of(guest_mmap_allocations.begin(),
						guest_mmap_allocations.end(), [allocation_base](const auto& allocation) {
							return allocation.first == *allocation_base;
						})) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_munmap(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
						rounded_length);
					if (syscall_result == 0) {
						(void)backend.RemoveDynamicGuestMappingRange(arguments[0], rounded_length);
						if (!backend.HasDynamicGuestMapping(*allocation_base))
							std::erase_if(guest_mmap_allocations, [allocation_base](const auto& allocation) {
								return allocation.first == *allocation_base;
							});
					}
					break;
				}
				case 74: { // SYS_mprotect
					const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
					if (arguments[0] > std::numeric_limits<std::uintptr_t>::max() ||
						arguments[1] == 0 || arguments[1] > std::numeric_limits<std::size_t>::max() ||
						arguments[2] > 7 || page_size == 0 ||
						arguments[0] % page_size != 0 ||
						arguments[1] > std::numeric_limits<std::size_t>::max() - (page_size - 1)) {
						error_number = EINVAL;
						break;
					}
					const auto length = static_cast<std::size_t>(arguments[1]);
					const auto rounded_length = ((length + page_size - 1) / page_size) * page_size;
					if (!backend.CanProtectGuestRange(arguments[0], rounded_length)) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_mprotect(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
						length, static_cast<int>(arguments[2]));
					if (syscall_result == 0 && !backend.ProtectGuestRange(arguments[0],
						rounded_length, (arguments[2] & 1) != 0,
						(arguments[2] & 2) != 0, (arguments[2] & 4) != 0)) {
						error_number = EFAULT;
						syscall_result = -1;
					}
					break;
				}
				case 65: // SYS_msync
				case 75: { // SYS_madvise
					if (arguments[0] > std::numeric_limits<std::uintptr_t>::max() ||
						arguments[1] == 0 || arguments[1] > std::numeric_limits<std::size_t>::max() ||
						arguments[2] > std::numeric_limits<int>::max()) {
						error_number = EINVAL;
						break;
					}
					const auto length = static_cast<std::size_t>(arguments[1]);
					if (!backend.ContainsGuestRange(arguments[0], length, false)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = syscall_number == 65 ? darling_windows_msync(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
						length, static_cast<int>(arguments[2])) : darling_windows_madvise(
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
						length, static_cast<int>(arguments[2]));
					break;
				}
				case 197: { // SYS_mmap
					const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
					if (arguments[0] > std::numeric_limits<std::uintptr_t>::max() ||
						arguments[1] == 0 || arguments[1] > std::numeric_limits<std::size_t>::max() ||
						arguments[2] > 7 || arguments[3] > std::numeric_limits<int>::max() ||
						page_size == 0 || arguments[1] >
						std::numeric_limits<std::size_t>::max() - (page_size - 1)) {
						error_number = EINVAL;
						break;
					}
					const auto length = static_cast<std::size_t>(arguments[1]);
					const auto rounded_length = ((length + page_size - 1) / page_size) * page_size;
					void* requested = reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0]));
					constexpr std::uint64_t map_fixed = 0x0010;
					std::optional<std::uint64_t> replaced_allocation_base;
					if ((arguments[3] & map_fixed) != 0 &&
						backend.HasGuestRangeOverlap(arguments[0], rounded_length)) {
						replaced_allocation_base = backend.DynamicGuestMappingBaseForRange(
							arguments[0], rounded_length);
						if (!replaced_allocation_base || std::none_of(guest_mmap_allocations.begin(),
							guest_mmap_allocations.end(), [replaced_allocation_base](const auto& allocation) {
								return allocation.first == *replaced_allocation_base;
							})) {
							error_number = EINVAL;
							break;
						}
					}
					const auto descriptor = static_cast<int>(static_cast<std::int32_t>(arguments[4]));
					const auto offset = static_cast<std::int64_t>(arguments[5]);
					void* mapped = darling_windows_mmap(requested, length,
						static_cast<int>(arguments[2]), static_cast<int>(arguments[3]),
						descriptor, offset);
					if (mapped == reinterpret_cast<void*>(-1))
						break;
					const auto mapped_address = reinterpret_cast<std::uintptr_t>(mapped);
					A64GuestMemoryRegion region{mapped_address, rounded_length,
						(arguments[2] & 1) != 0, (arguments[2] & 2) != 0,
						(arguments[2] & 4) != 0, true, mapped_address, length, true};
					const bool registered = replaced_allocation_base ?
						backend.ReplaceDynamicGuestMappingRange(mapped_address,
							rounded_length, region) : backend.RegisterDynamicGuestMapping(region);
					if (!registered) {
						(void)darling_windows_munmap(mapped, length);
						if (replaced_allocation_base)
							(void)backend.RemoveDynamicGuestMappingRange(mapped_address, rounded_length);
						error_number = EINVAL;
						break;
					}
					if (!replaced_allocation_base) {
						try {
							guest_mmap_allocations.emplace_back(mapped_address, length);
						} catch (...) {
							(void)backend.RemoveDynamicGuestMapping(mapped_address);
							(void)darling_windows_munmap(mapped, length);
							error_number = ENOMEM;
							break;
						}
					}
					syscall_result = static_cast<std::int64_t>(mapped_address);
					break;
				}
				case 202: { // SYS_sysctl
					if (arguments[1] != 2 || !backend.ContainsGuestRange(
						arguments[0], 2 * sizeof(int), false) || arguments[3] == 0 ||
						!backend.ContainsGuestRange(arguments[3], sizeof(std::size_t), true)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[5] > std::numeric_limits<std::size_t>::max() ||
						(arguments[4] != 0 && arguments[5] != 0 &&
						 !backend.ContainsGuestRange(arguments[4],
							static_cast<std::size_t>(arguments[5]), false))) {
						error_number = EFAULT;
						break;
					}
					int mib[2]{};
					std::memcpy(mib, reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(arguments[0])), sizeof(mib));
					std::size_t old_length = 0;
					std::memcpy(&old_length, reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(arguments[3])), sizeof(old_length));
					constexpr std::size_t maximum_sysctl_output = 1024 * 1024;
					if (arguments[2] != 0 && (old_length > maximum_sysctl_output ||
						(old_length != 0 && !backend.ContainsGuestRange(
							arguments[2], old_length, true)))) {
						error_number = old_length > maximum_sysctl_output ? EINVAL : EFAULT;
						break;
					}
					std::vector<std::byte> old_value(arguments[2] == 0 ? 0 :
						(std::max)(old_length, std::size_t{1}));
					syscall_result = darling_windows_sysctl(mib, 2,
						arguments[2] == 0 ? nullptr : old_value.data(), &old_length,
						reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[4])),
						static_cast<std::size_t>(arguments[5]));
					std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[3])),
						&old_length, sizeof(old_length));
					if (syscall_result == 0 && arguments[2] != 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[2])),
							old_value.data(), old_length);
					break;
				}
				case 194: // SYS_getrlimit
				case 195: { // SYS_setrlimit
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					if (!backend.ContainsGuestRange(arguments[1],
						sizeof(darling_rlimit), syscall_number == 194)) {
						error_number = EFAULT;
						break;
					}
					darling_rlimit limit{};
					auto* guest_limit = reinterpret_cast<darling_rlimit*>(
						static_cast<std::uintptr_t>(arguments[1]));
					if (syscall_number == 195)
						std::memcpy(&limit, guest_limit, sizeof(limit));
					syscall_result = syscall_number == 194 ?
						darling_windows_getrlimit(static_cast<int>(arguments[0]), &limit) :
						darling_windows_setrlimit(static_cast<int>(arguments[0]), &limit);
					if (syscall_result == 0 && syscall_number == 194)
						std::memcpy(guest_limit, &limit, sizeof(limit));
					break;
				}
				case 5: // SYS_open
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_open(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						static_cast<int>(arguments[1]), static_cast<int>(arguments[2]));
					break;
				case 3: // SYS_read
				case 4: { // SYS_write
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					const auto bytes = static_cast<std::size_t>(arguments[2]);
					if (bytes != 0 && !backend.ContainsGuestRange(arguments[1], bytes,
						syscall_number == 3)) {
						error_number = EFAULT;
						break;
					}
					if (bytes == 0) {
						syscall_result = 0;
						break;
					}
					const auto descriptor = static_cast<int>(arguments[0]);
					const auto value = syscall_number == 3 ?
						darling_windows_read(descriptor,
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[1])), bytes) :
						darling_windows_write(descriptor,
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])), bytes);
					syscall_result = value;
					break;
				}
				case 6: // SYS_close
					if (arguments[0] > static_cast<std::uint64_t>(
						std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_close(
						static_cast<int>(arguments[0]));
					break;
				case 92: { // SYS_fcntl
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					const auto descriptor = static_cast<int>(arguments[0]);
					const auto command = static_cast<int>(arguments[1]);
					switch (command) {
					case 0:   // F_DUPFD
					case 2:   // F_SETFD
					case 4:   // F_SETFL
					case 67: // F_DUPFD_CLOEXEC
						if (arguments[2] > static_cast<std::uint64_t>(
								std::numeric_limits<int>::max())) {
							error_number = EINVAL;
							break;
						}
						syscall_result = darling_windows_fcntl(descriptor, command,
							static_cast<int>(arguments[2]));
						break;
					case 1: // F_GETFD
					case 3: // F_GETFL
						syscall_result = darling_windows_fcntl(descriptor, command);
						break;
					default:
						error_number = EINVAL;
						break;
					}
					break;
				}
				case 93: { // SYS_select
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					constexpr std::size_t fd_set_size = sizeof(darling_fd_set);
					constexpr std::size_t timeval_size = sizeof(darling_timeval);
					if ((arguments[1] != 0 && !backend.ContainsGuestRange(
							arguments[1], fd_set_size, true)) ||
						(arguments[2] != 0 && !backend.ContainsGuestRange(
							arguments[2], fd_set_size, true)) ||
						(arguments[3] != 0 && !backend.ContainsGuestRange(
							arguments[3], fd_set_size, true)) ||
						(arguments[4] != 0 && !backend.ContainsGuestRange(
							arguments[4], timeval_size, true))) {
						error_number = EFAULT;
						break;
					}
					darling_fd_set read_set{};
					darling_fd_set write_set{};
					darling_fd_set exception_set{};
					darling_timeval timeout{};
					if (arguments[1] != 0)
						std::memcpy(&read_set, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[1])), fd_set_size);
					if (arguments[2] != 0)
						std::memcpy(&write_set, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[2])), fd_set_size);
					if (arguments[3] != 0)
						std::memcpy(&exception_set, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[3])), fd_set_size);
					if (arguments[4] != 0)
						std::memcpy(&timeout, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[4])), timeval_size);
					syscall_result = darling_windows_select(
						static_cast<int>(arguments[0]),
						arguments[1] == 0 ? nullptr : &read_set,
						arguments[2] == 0 ? nullptr : &write_set,
						arguments[3] == 0 ? nullptr : &exception_set,
						arguments[4] == 0 ? nullptr : &timeout);
					if (syscall_result >= 0) {
						if (arguments[1] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[1])), &read_set, fd_set_size);
						if (arguments[2] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[2])), &write_set, fd_set_size);
						if (arguments[3] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[3])), &exception_set, fd_set_size);
						if (arguments[4] != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[4])), &timeout, timeval_size);
					}
					break;
				}
				case 230: { // SYS_poll
					constexpr std::uint64_t maximum_poll_descriptors = 4096;
					if (arguments[1] > maximum_poll_descriptors) {
						error_number = EINVAL;
						break;
					}
					using HostPollDescriptor = darling::windows_host::darling_pollfd;
					static_assert(sizeof(HostPollDescriptor) == 8);
					const auto count = static_cast<std::size_t>(arguments[1]);
					const auto bytes = count * sizeof(HostPollDescriptor);
					if (bytes != 0 && (!backend.ContainsGuestRange(
							arguments[0], bytes, false) || !backend.ContainsGuestRange(
							arguments[0], bytes, true))) {
						error_number = EFAULT;
						break;
					}
					std::vector<HostPollDescriptor> descriptors(count);
					if (bytes != 0)
						std::memcpy(descriptors.data(), reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[0])), bytes);
					const auto timeout = static_cast<std::int32_t>(
						static_cast<std::uint32_t>(arguments[2]));
					syscall_result = darling_windows_poll(
						descriptors.empty() ? nullptr : descriptors.data(), count, timeout);
					if (syscall_result >= 0 && bytes != 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[0])), descriptors.data(), bytes);
					break;
				}
				case 54: { // SYS_ioctl (supported terminal requests only)
					constexpr std::uint64_t tiocgwinsz = 0x40087468ull;
					constexpr std::uint64_t tiocswinsz = 0x80087467ull;
					constexpr std::uint64_t fionread = 0x4004667full;
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > (std::numeric_limits<unsigned long>::max)()) {
						error_number = EINVAL;
						break;
					}
					const auto request = arguments[1];
					if (request == tiocgwinsz || request == tiocswinsz) {
						if (!backend.ContainsGuestRange(arguments[2],
							sizeof(darling_winsize), request == tiocgwinsz)) {
							error_number = EFAULT;
							break;
						}
						darling_winsize size{};
						if (request == tiocswinsz)
							std::memcpy(&size, reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(arguments[2])), sizeof(size));
						syscall_result = darling_windows_ioctl(
							static_cast<int>(arguments[0]),
							static_cast<unsigned long>(request), &size);
						if (syscall_result == 0 && request == tiocgwinsz)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[2])), &size, sizeof(size));
					} else if (request == fionread) {
						if (!backend.ContainsGuestRange(arguments[2],
							sizeof(std::uint32_t), true)) {
							error_number = EFAULT;
							break;
						}
						unsigned long available = 0;
						syscall_result = darling_windows_ioctl(
							static_cast<int>(arguments[0]),
							static_cast<unsigned long>(request), &available);
						if (syscall_result == 0) {
							const auto guest_available = static_cast<std::uint32_t>(available);
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[2])), &guest_available, sizeof(guest_available));
						}
					} else {
						error_number = ENOTTY;
					}
					break;
				}
				case 10: // SYS_unlink
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_unlink(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])));
					break;
				case 12: // SYS_chdir
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_chdir(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])));
					break;
				case 15: // SYS_chmod
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_chmod(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						static_cast<int>(arguments[1]));
					break;
				case 124: // SYS_fchmod
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_fchmod(
						static_cast<int>(arguments[0]), static_cast<int>(arguments[1]));
					break;
				case 20: // SYS_getpid
					syscall_result = darling_windows_getpid();
					syscall_result_x1 = static_cast<std::uint64_t>(
						darling_windows_getppid());
					break;
				case 23: // SYS_setuid
				case 181: // SYS_setgid
				case 182: // SYS_setegid
				case 183: // SYS_seteuid
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					switch (syscall_number) {
					case 23:
						syscall_result = darling_windows_setuid(
							static_cast<int>(arguments[0]));
						break;
					case 181:
						syscall_result = darling_windows_setgid(
							static_cast<int>(arguments[0]));
						break;
					case 182:
						syscall_result = darling_windows_setegid(
							static_cast<int>(arguments[0]));
						break;
					default:
						syscall_result = darling_windows_seteuid(
							static_cast<int>(arguments[0]));
						break;
					}
					break;
				case 79: { // SYS_getgroups
					if (arguments[0] > 1024) {
						error_number = EINVAL;
						break;
					}
					const auto count = static_cast<std::size_t>(arguments[0]);
					if (count != 0 && (arguments[1] == 0 ||
							!backend.ContainsGuestRange(arguments[1],
								count * sizeof(std::uint32_t), true))) {
						error_number = EFAULT;
						break;
					}
					std::vector<std::uint32_t> groups(count);
					syscall_result = darling_windows_getgroups(
						static_cast<int>(count), count == 0 ? nullptr : groups.data());
					if (syscall_result > 0 && count != 0) {
						const auto copied = std::min<std::size_t>(
							static_cast<std::size_t>(syscall_result), count);
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[1])), groups.data(), copied * sizeof(std::uint32_t));
					}
					break;
				}
				case 80: { // SYS_setgroups
					if (arguments[0] > 1024) {
						error_number = EINVAL;
						break;
					}
					const auto count = static_cast<std::size_t>(arguments[0]);
					if (count != 0 && (arguments[1] == 0 ||
							!backend.ContainsGuestRange(arguments[1],
								count * sizeof(std::uint32_t), false))) {
						error_number = EFAULT;
						break;
					}
					std::vector<std::uint32_t> groups(count);
					if (count != 0)
						std::memcpy(groups.data(), reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[1])),
							count * sizeof(std::uint32_t));
					syscall_result = darling_windows_setgroups(
						static_cast<int>(count), count == 0 ? nullptr : groups.data());
					break;
				}
				case 82: // SYS_setpgid
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_setpgid(
						static_cast<int>(arguments[0]), static_cast<int>(arguments[1]));
					break;
				case 89: // SYS_getdtablesize
					syscall_result = darling_windows_getdtablesize();
					break;
				case 147: // SYS_setsid
					syscall_result = darling_windows_setsid();
					break;
				case 151: // SYS_getpgid
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_getpgid(
						static_cast<int>(arguments[0]));
					break;
				case 131: // SYS_flock
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_flock(
						static_cast<int>(arguments[0]), static_cast<int>(arguments[1]));
					break;
				case 37: // SYS_kill
					if (arguments[0] > std::numeric_limits<std::uint32_t>::max() ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_kill(
						static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[0])),
						static_cast<int>(arguments[1]));
					break;
				case 46: { // SYS_sigaction
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						(arguments[1] != 0 && !backend.ContainsGuestRange(arguments[1],
							sizeof(darling_darwin_sigaction_record), false)) ||
						(arguments[2] != 0 && !backend.ContainsGuestRange(arguments[2],
							sizeof(darling_darwin_sigaction_record), true))) {
						error_number = EFAULT;
						break;
					}
					darling_darwin_sigaction_record action{};
					darling_darwin_sigaction_record previous{};
					if (arguments[1] != 0)
						std::memcpy(&action, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[1])), sizeof(action));
					syscall_result = darling_windows_sigaction(
						static_cast<int>(arguments[0]), arguments[1] == 0 ? nullptr : &action,
						arguments[2] == 0 ? nullptr : &previous);
					if (syscall_result != 0) {
						error_number = EINVAL;
					} else if (arguments[2] != 0) {
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[2])), &previous, sizeof(previous));
					}
					break;
				}
				case 48: { // SYS_sigprocmask
					if ((arguments[1] != 0 && !backend.ContainsGuestRange(arguments[1],
							sizeof(darling_darwin_sigset), false)) ||
						(arguments[2] != 0 && !backend.ContainsGuestRange(arguments[2],
							sizeof(darling_darwin_sigset), true))) {
						error_number = EFAULT;
						break;
					}
					darling_darwin_sigset set{};
					darling_darwin_sigset previous{};
					if (arguments[1] != 0)
						std::memcpy(&set, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[1])), sizeof(set));
					syscall_result = darling_windows_sigprocmask(
						static_cast<int>(arguments[0]), arguments[1] == 0 ? nullptr : &set,
						arguments[2] == 0 ? nullptr : &previous);
					if (syscall_result != 0) {
						error_number = EINVAL;
					} else if (arguments[2] != 0) {
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[2])), &previous, sizeof(previous));
					}
					break;
				}
				case 96: // SYS_setpriority
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > std::numeric_limits<std::uint32_t>::max() ||
						arguments[2] > std::numeric_limits<std::uint32_t>::max()) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_setpriority(
						static_cast<int>(arguments[0]), static_cast<std::uint32_t>(arguments[1]),
						static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[2])));
					break;
				case 100: // SYS_getpriority
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > std::numeric_limits<std::uint32_t>::max()) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_getpriority(
						static_cast<int>(arguments[0]), static_cast<std::uint32_t>(arguments[1]));
					break;
				case 117: { // SYS_getrusage
					if (arguments[0] > std::numeric_limits<std::uint32_t>::max()) {
						error_number = EINVAL;
						break;
					}
					if (!backend.ContainsGuestRange(arguments[1],
						sizeof(darling_rusage), true)) {
						error_number = EFAULT;
						break;
					}
					darling_rusage usage{};
					syscall_result = darling_windows_getrusage(
						static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[0])), &usage);
					if (syscall_result == 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[1])), &usage, sizeof(usage));
					break;
				}
				case 24: // SYS_getuid
					syscall_result = darling_windows_getuid();
					syscall_result_x1 = static_cast<std::uint64_t>(
						darling_windows_geteuid());
					break;
				case 25: // SYS_geteuid
					syscall_result = darling_windows_geteuid();
					break;
				case 29: { // SYS_recvfrom
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[3] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					const auto bytes = static_cast<std::size_t>(arguments[2]);
					if (bytes != 0 && !backend.ContainsGuestRange(arguments[1], bytes, true)) {
						error_number = EFAULT;
						break;
					}
					if ((arguments[4] == 0) != (arguments[5] == 0)) {
						error_number = EFAULT;
						break;
					}
					std::array<unsigned char, 512> address_buffer{};
					int address_length = 0;
					int guest_address_capacity = 0;
					int* guest_address_length = nullptr;
					void* guest_address = nullptr;
					if (arguments[4] != 0) {
						if (!backend.ContainsGuestRange(arguments[5], sizeof(int), false) ||
							!backend.ContainsGuestRange(arguments[5], sizeof(int), true)) {
							error_number = EFAULT;
							break;
						}
						std::memcpy(&address_length, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[5])), sizeof(address_length));
						if (address_length < 0) {
							error_number = EINVAL;
							break;
						}
						guest_address_capacity = address_length;
						if (address_length > static_cast<int>(address_buffer.size()))
							address_length = static_cast<int>(address_buffer.size());
						if (address_length > 0 && !backend.ContainsGuestRange(arguments[4],
							static_cast<std::size_t>(address_length), true)) {
							error_number = EFAULT;
							break;
						}
						guest_address = reinterpret_cast<void*>(address_buffer.data());
						guest_address_length = &address_length;
					}
					const auto value = darling_windows_recvfrom(
						static_cast<int>(arguments[0]),
						reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[1])),
						bytes, static_cast<int>(arguments[3]), guest_address,
						guest_address_length);
					if (value >= 0 && arguments[4] != 0) {
						const auto copy_bytes = address_length > 0 ?
							(std::min)(static_cast<std::size_t>(address_length),
								(std::min)(static_cast<std::size_t>(guest_address_capacity),
									address_buffer.size())) : 0;
						if (copy_bytes != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[4])), address_buffer.data(), copy_bytes);
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[5])), &address_length, sizeof(address_length));
					}
					syscall_result = value;
					break;
				}
				case 30: { // SYS_accept
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if ((arguments[1] == 0) != (arguments[2] == 0)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] == 0) {
						syscall_result = darling_windows_accept(
							static_cast<int>(arguments[0]), nullptr, nullptr);
						break;
					}
					if (!backend.ContainsGuestRange(arguments[2], sizeof(std::uint32_t), false) ||
						!backend.ContainsGuestRange(arguments[2], sizeof(std::uint32_t), true)) {
						error_number = EFAULT;
						break;
					}
					std::uint32_t guest_capacity = 0;
					std::memcpy(&guest_capacity, reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(arguments[2])), sizeof(guest_capacity));
					std::array<unsigned char, 512> address_buffer{};
					const auto capacity = (std::min)(static_cast<std::size_t>(guest_capacity),
						address_buffer.size());
					if (capacity != 0 && !backend.ContainsGuestRange(
						arguments[1], capacity, true)) {
						error_number = EFAULT;
						break;
					}
					int address_length = static_cast<int>(capacity);
					syscall_result = darling_windows_accept(static_cast<int>(arguments[0]),
						address_buffer.data(), &address_length);
					if (syscall_result >= 0 && address_length >= 0) {
						const auto copy_bytes = (std::min)(static_cast<std::size_t>(address_length),
							capacity);
						if (copy_bytes != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[1])), address_buffer.data(), copy_bytes);
						const auto result_length = static_cast<std::uint32_t>(address_length);
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[2])), &result_length, sizeof(result_length));
					}
					break;
				}
				case 31: // SYS_getpeername
				case 32: { // SYS_getsockname
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[1] == 0 || arguments[2] == 0 ||
						!backend.ContainsGuestRange(arguments[2], sizeof(std::uint32_t), false) ||
						!backend.ContainsGuestRange(arguments[2], sizeof(std::uint32_t), true)) {
						error_number = EFAULT;
						break;
					}
					std::uint32_t guest_capacity = 0;
					std::memcpy(&guest_capacity, reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(arguments[2])), sizeof(guest_capacity));
					std::array<unsigned char, 512> address_buffer{};
					const auto capacity = (std::min)(static_cast<std::size_t>(guest_capacity),
						address_buffer.size());
					if (capacity != 0 && !backend.ContainsGuestRange(
						arguments[1], capacity, true)) {
						error_number = EFAULT;
						break;
					}
					int address_length = static_cast<int>(capacity);
					syscall_result = syscall_number == 31 ?
						darling_windows_getpeername(static_cast<int>(arguments[0]),
							address_buffer.data(), &address_length) :
						darling_windows_getsockname(static_cast<int>(arguments[0]),
							address_buffer.data(), &address_length);
					if (syscall_result == 0 && address_length >= 0) {
						const auto copy_bytes = (std::min)(static_cast<std::size_t>(address_length),
							capacity);
						if (copy_bytes != 0)
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[1])), address_buffer.data(), copy_bytes);
						const auto result_length = static_cast<std::uint32_t>(address_length);
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[2])), &result_length, sizeof(result_length));
					}
					break;
				}
				case 39: // SYS_getppid
					syscall_result = darling_windows_getppid();
					break;
				case 41: // SYS_dup
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_dup(static_cast<int>(arguments[0]));
					break;
				case 42: { // SYS_pipe returns both descriptors in registers
					int descriptors[2]{};
					if (darling_windows_pipe(descriptors) == 0) {
						syscall_result = descriptors[0];
						syscall_result_x1 = static_cast<std::uint64_t>(descriptors[1]);
					}
					break;
				}
				case 43: // SYS_getegid
					syscall_result = darling_windows_getegid();
					break;
				case 47: // SYS_getgid
					syscall_result = darling_windows_getgid();
					syscall_result_x1 = static_cast<std::uint64_t>(
						darling_windows_getegid());
					break;
				case 49: { // SYS_getlogin
					if (arguments[0] == 0 || arguments[1] == 0) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] > 4096) {
						error_number = EINVAL;
						break;
					}
					const auto size = static_cast<std::size_t>(arguments[1]);
					if (!backend.ContainsGuestRange(arguments[0], size, true)) {
						error_number = EFAULT;
						break;
					}
					std::vector<char> login(size);
					const int result = darling_windows_getlogin_r(login.data(), size);
					if (result != 0) {
						error_number = DarwinErrno::Get();
						if (error_number == 0)
							error_number = result;
						break;
					}
					const auto terminator = std::find(login.begin(), login.end(), '\0');
					if (terminator == login.end()) {
						error_number = EIO;
						break;
					}
					const auto bytes = static_cast<std::size_t>(
						std::distance(login.begin(), terminator)) + 1;
					std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
						arguments[0])), login.data(), bytes);
					syscall_result = 0;
					break;
				}
				case 50: // SYS_setlogin
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_setlogin(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
							arguments[0])));
					break;
				case 81: // SYS_getpgrp
					syscall_result = darling_windows_getpgrp();
					break;
				case 57: // SYS_symlink
					if (!backend.ContainsGuestCString(arguments[0], 4096) ||
						!backend.ContainsGuestCString(arguments[1], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_symlink(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[1])));
					break;
				case 58: // SYS_readlink
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					if (arguments[2] != 0 && !backend.ContainsGuestRange(
						arguments[1], static_cast<std::size_t>(arguments[2]), true)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_readlink(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						reinterpret_cast<char*>(static_cast<std::uintptr_t>(arguments[1])),
						static_cast<std::size_t>(arguments[2]));
					break;
				case 120: // SYS_readv
				case 121: { // SYS_writev
					constexpr std::uint64_t maximum_iov_count = 1024;
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > maximum_iov_count) {
						error_number = EINVAL;
						break;
					}
					const auto count = static_cast<std::size_t>(arguments[2]);
					if (count == 0) {
						syscall_result = 0;
						break;
					}
					const auto vector_bytes = count * sizeof(darling_iovec);
				if (!backend.ContainsGuestRange(arguments[1], vector_bytes, false)) {
					error_number = EFAULT;
					break;
				}
					std::vector<darling_iovec> vectors(count);
					std::memcpy(vectors.data(), reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(arguments[1])), vector_bytes);
					bool invalid_vector = false;
					for (const auto& vector : vectors) {
						if (vector.iov_len > static_cast<std::size_t>(
								std::numeric_limits<int>::max())) {
							error_number = EINVAL;
							invalid_vector = true;
							break;
						}
						if (vector.iov_len != 0 && !backend.ContainsGuestRange(
								reinterpret_cast<std::uintptr_t>(vector.iov_base),
								vector.iov_len, syscall_number == 120)) {
							error_number = EFAULT;
							invalid_vector = true;
							break;
						}
					}
					if (invalid_vector) {
						break;
					}
					syscall_result = syscall_number == 120 ?
						darling_windows_readv(static_cast<int>(arguments[0]),
							vectors.data(), static_cast<int>(count)) :
						darling_windows_writev(static_cast<int>(arguments[0]),
							vectors.data(), static_cast<int>(count));
					break;
				}
				case 153: // SYS_pread
				case 154: { // SYS_pwrite
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > std::numeric_limits<std::uint32_t>::max() ||
						static_cast<std::int64_t>(arguments[3]) < 0) {
						error_number = EINVAL;
						break;
					}
					const auto bytes = static_cast<std::size_t>(arguments[2]);
					if (bytes != 0 && !backend.ContainsGuestRange(arguments[1], bytes,
						syscall_number == 153)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = syscall_number == 153 ?
						darling_windows_pread(static_cast<int>(arguments[0]),
							reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[1])),
							bytes, static_cast<std::int64_t>(arguments[3])) :
						darling_windows_pwrite(static_cast<int>(arguments[0]),
							reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])),
							bytes, static_cast<std::int64_t>(arguments[3]));
					break;
				}
				case 60: // SYS_umask
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_umask(
						static_cast<int>(arguments[0]));
					break;
				case 90: // SYS_dup2
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_dup2(
						static_cast<int>(arguments[0]),
						static_cast<int>(arguments[1]));
					break;
				case 97: // SYS_socket
				case 135: { // SYS_socketpair
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					if (syscall_number == 97) {
						syscall_result = darling_windows_socket(
							static_cast<int>(arguments[0]),
							static_cast<int>(arguments[1]),
							static_cast<int>(arguments[2]));
						break;
					}
					if (!backend.ContainsGuestRange(arguments[3],
						sizeof(int) * 2, true)) {
						error_number = EFAULT;
						break;
					}
					int descriptors[2]{};
					syscall_result = darling_windows_socketpair(
						static_cast<int>(arguments[0]),
						static_cast<int>(arguments[1]),
						static_cast<int>(arguments[2]),
						descriptors);
					if (syscall_result == 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[3])), descriptors, sizeof(descriptors));
					break;
				}
				case 98: // SYS_connect
				case 104: { // SYS_bind
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > 65536) {
						error_number = EINVAL;
						break;
					}
					const auto address_length = static_cast<std::size_t>(arguments[2]);
					if (address_length != 0 && !backend.ContainsGuestRange(
						arguments[1], address_length, false)) {
						error_number = EFAULT;
						break;
					}
					std::vector<unsigned char> address_storage(address_length);
					if (address_length != 0)
						std::memcpy(address_storage.data(), reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[1])), address_length);
					const auto* address = address_length == 0 ? nullptr :
						static_cast<const void*>(address_storage.data());
					syscall_result = syscall_number == 98 ?
						darling_windows_connect(static_cast<int>(arguments[0]), address,
							static_cast<int>(address_length)) :
						darling_windows_bind(static_cast<int>(arguments[0]), address,
							static_cast<int>(address_length));
					break;
				}
				case 105: // SYS_setsockopt
				case 118: { // SYS_getsockopt
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					constexpr std::uint64_t maximum_option_size = 1024 * 1024;
					std::uint32_t option_size = 0;
					if (syscall_number == 105) {
						if (arguments[4] > maximum_option_size) {
							error_number = EINVAL;
							break;
						}
						option_size = static_cast<std::uint32_t>(arguments[4]);
					} else {
						if (!backend.ContainsGuestRange(arguments[4],
								sizeof(option_size), false)) {
							error_number = EFAULT;
							break;
						}
						std::memcpy(&option_size, reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[4])), sizeof(option_size));
						if (option_size > maximum_option_size) {
							error_number = EINVAL;
							break;
						}
					}
					if (option_size != 0 && (arguments[3] == 0 ||
							!backend.ContainsGuestRange(arguments[3], option_size,
								syscall_number == 118))) {
						error_number = EFAULT;
						break;
					}
					std::vector<unsigned char> option_value(option_size);
					if (syscall_number == 105) {
						if (option_size != 0)
							std::memcpy(option_value.data(), reinterpret_cast<const void*>(
								static_cast<std::uintptr_t>(arguments[3])), option_size);
						syscall_result = darling_windows_setsockopt(
							static_cast<int>(arguments[0]), static_cast<int>(arguments[1]),
							static_cast<int>(arguments[2]),
							option_size == 0 ? nullptr : option_value.data(),
							static_cast<int>(option_size));
					} else {
						int actual_size = static_cast<int>(option_size);
						syscall_result = darling_windows_getsockopt(
							static_cast<int>(arguments[0]), static_cast<int>(arguments[1]),
							static_cast<int>(arguments[2]),
							option_size == 0 ? nullptr : option_value.data(), &actual_size);
						if (syscall_result == 0 && actual_size >= 0 &&
							static_cast<std::uint32_t>(actual_size) <= option_size) {
							if (actual_size != 0)
								std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
									arguments[3])), option_value.data(),
									static_cast<std::size_t>(actual_size));
							const auto guest_size = static_cast<std::uint32_t>(actual_size);
							std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
								arguments[4])), &guest_size, sizeof(guest_size));
						} else if (syscall_result == 0) {
							error_number = EIO;
						}
					}
					break;
				}
				case 106: // SYS_listen
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_listen(
						static_cast<int>(arguments[0]),
						static_cast<int>(arguments[1]));
					break;
				case 133: { // SYS_sendto
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[3] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) || arguments[5] > 65536) {
						error_number = EINVAL;
						break;
					}
					const auto bytes = static_cast<std::size_t>(arguments[2]);
					const auto address_length = static_cast<std::size_t>(arguments[5]);
					if ((bytes != 0 && !backend.ContainsGuestRange(arguments[1], bytes, false)) ||
						(address_length != 0 && !backend.ContainsGuestRange(
							arguments[4], address_length, false))) {
						error_number = EFAULT;
						break;
					}
					std::vector<unsigned char> address_storage(address_length);
					if (address_length != 0)
						std::memcpy(address_storage.data(), reinterpret_cast<const void*>(
							static_cast<std::uintptr_t>(arguments[4])), address_length);
					syscall_result = darling_windows_sendto(
						static_cast<int>(arguments[0]),
						reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])),
						bytes, static_cast<int>(arguments[3]),
						address_length == 0 ? nullptr :
							static_cast<const void*>(address_storage.data()),
						static_cast<int>(address_length));
					break;
				}
				case 134: // SYS_shutdown
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_shutdown(
						static_cast<int>(arguments[0]),
						static_cast<int>(arguments[1]));
					break;
				case 95: // SYS_fsync
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_fsync(
						static_cast<int>(arguments[0]));
					break;
				case 116: { // SYS_gettimeofday(tp, tzp, mach_absolute_time)
					if ((arguments[0] != 0 && !backend.ContainsGuestRange(
							arguments[0], sizeof(darling_timeval), true)) ||
						(arguments[2] != 0 && !backend.ContainsGuestRange(
							arguments[2], sizeof(std::uint64_t), true))) {
						error_number = EFAULT;
						break;
					}
					if (arguments[0] != 0) {
						darling_timeval value{};
						syscall_result = darling_windows_gettimeofday(&value, nullptr);
						if (syscall_result != 0)
							break;
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[0])), &value, sizeof(value));
					} else {
						syscall_result = 0;
					}
					if (arguments[2] != 0) {
						const auto absolute_time = darling_windows_mach_absolute_time();
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[2])), &absolute_time, sizeof(absolute_time));
					}
					break;
				}
				case 128: // SYS_rename
					if (!backend.ContainsGuestCString(arguments[0], 4096) ||
						!backend.ContainsGuestCString(arguments[1], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_rename(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[1])));
					break;
				case 136: // SYS_mkdir
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_mkdir(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						static_cast<int>(arguments[1]));
					break;
				case 137: // SYS_rmdir
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_rmdir(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])));
					break;
				case 187: // SYS_fdatasync
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_fdatasync(
						static_cast<int>(arguments[0]));
					break;
				case 157: // SYS_statfs
				case 158: { // SYS_fstatfs
					if (!backend.ContainsGuestRange(arguments[1],
						sizeof(darling_darwin_statfs), true)) {
						error_number = EFAULT;
						break;
					}
					if (syscall_number == 157 && !backend.ContainsGuestCString(
						arguments[0], 32768)) {
						error_number = EFAULT;
						break;
					}
					if (syscall_number == 158 && arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					darling_darwin_statfs information{};
					syscall_result = syscall_number == 157 ?
						darling_windows_statfs(reinterpret_cast<const char*>(
							static_cast<std::uintptr_t>(arguments[0])), &information) :
						darling_windows_fstatfs(static_cast<int>(arguments[0]), &information);
					if (syscall_result == 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[1])), &information, sizeof(information));
					break;
				}
				case 188: // SYS_stat
				case 190: { // SYS_lstat
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (!backend.ContainsGuestRange(arguments[1],
						sizeof(darling_darwin_stat), true)) {
						error_number = EFAULT;
						break;
					}
					const auto* path = reinterpret_cast<const char*>(
						static_cast<std::uintptr_t>(arguments[0]));
					darling_darwin_stat metadata{};
					syscall_result = syscall_number == 188 ?
						darling_windows_stat(path, &metadata) :
						darling_windows_lstat(path, &metadata);
					if (syscall_result == 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[1])), &metadata, sizeof(metadata));
					break;
				}
				case 189: { // SYS_fstat
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					if (!backend.ContainsGuestRange(arguments[1],
						sizeof(darling_darwin_stat), true)) {
						error_number = EFAULT;
						break;
					}
					darling_darwin_stat metadata{};
					syscall_result = darling_windows_fstat(
						static_cast<int>(arguments[0]), &metadata);
					if (syscall_result == 0)
						std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
							arguments[1])), &metadata, sizeof(metadata));
					break;
				}
				case 33: // SYS_access
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					if (arguments[1] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_access(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						static_cast<int>(arguments[1]));
					break;
				case 199: // SYS_lseek
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max()) ||
						arguments[2] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EINVAL;
						break;
					}
					syscall_result = darling_windows_lseek(
						static_cast<int>(arguments[0]),
						static_cast<std::int64_t>(arguments[1]),
						static_cast<int>(arguments[2]));
					break;
				case 201: // SYS_ftruncate
					if (arguments[0] > static_cast<std::uint64_t>(
							std::numeric_limits<int>::max())) {
						error_number = EBADF;
						break;
					}
					syscall_result = darling_windows_ftruncate(
						static_cast<int>(arguments[0]),
						static_cast<std::int64_t>(arguments[1]));
					break;
				case 200: // SYS_truncate
					if (!backend.ContainsGuestCString(arguments[0], 4096)) {
						error_number = EFAULT;
						break;
					}
					syscall_result = darling_windows_truncate(
						reinterpret_cast<const char*>(static_cast<std::uintptr_t>(arguments[0])),
						static_cast<std::int64_t>(arguments[1]));
					break;
				default:
					error_number = ENOSYS;
					break;
				}
				if (syscall_result < 0 && error_number == 0)
					error_number = DarwinErrno::Get();
				if (error_number != 0) {
					if (error_number <= 0)
						error_number = EIO;
					DarwinErrno::Set(error_number);
					cpu = result.state;
					cpu.x[0] = static_cast<std::uint64_t>(error_number);
					cpu.pstate |= (1u << 29); // Darwin syscall error carry flag
				} else {
					cpu = result.state;
					cpu.x[0] = static_cast<std::uint64_t>(syscall_result);
					cpu.x[1] = syscall_result_x1;
					cpu.pstate &= ~(1u << 29);
				}
				continue;
			}
			if (!result.fault.empty()) {
				throw std::runtime_error(result.fault + " at guest address " +
					std::to_string(result.fault_address.value_or(result.state.pc)));
			}
			if (result.exception) {
				throw std::runtime_error("ARM64 Mach-O raised Dynarmic exception " +
					std::to_string(static_cast<unsigned>(*result.exception)) +
					" at guest address " + std::to_string(result.state.pc));
			}
			throw std::runtime_error("ARM64 Mach-O stopped without a guest return or Darwin trap");
		}
	}
#endif
	if (entry_abi == DarwinEntryAbi::DarwinStart) {
	#if (defined(_WIN64) && defined(__clang__)) || defined(_M_IX86)
		// Darwin's crt1 entry receives argc at the initial stack pointer, followed by argv, envp and
		// the Apple parameter vector. Keep all pointed-to storage alive until
		// the non-returning entry transfer.
		// Leave committed scratch space below the initial Darwin stack.  The
		// guest entry may issue a normal call, which pushes its return address
		// below RSP even though the Darwin ABI starts with argc at [RSP].
		// Keep substantial native stack below the Darwin initial words. A guest
		// call into a SysV host bridge still needs return-address space and a
		// bounded prologue below RSP. The buffer stays on the native thread stack:
		// heap-backed alternate stacks faulted while guest code called Windows/SEH.
		constexpr std::size_t startup_stack_guard_words = 512;
		constexpr std::size_t startup_stack_capacity = 2048;
		alignas(16) std::array<std::uintptr_t, startup_stack_capacity> startup_stack{};
		const auto startup_words = startup_stack_guard_words + 1 + argv.size() +
			envp.size() + apple.size();
		if (startup_words > startup_stack.size()) {
			throw std::runtime_error("Darwin startup stack exceeds native stack fixture capacity");
		}
		std::size_t startup_index = startup_stack_guard_words;
		startup_stack[startup_index++] = static_cast<std::uintptr_t>(arguments.size());
		for (const auto value : argv)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		for (const auto value : envp)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		for (const auto value : apple)
			startup_stack[startup_index++] = reinterpret_cast<std::uintptr_t>(value);
		if (std::getenv("WINTOSH_TRACE_DARWIN_START") != nullptr) {
			std::fprintf(stderr,
				"WINTOSH_DARWIN_START_STACK=0x%llx argc=%zu argv=0x%llx envp=0x%llx apple=0x%llx words=%llx,%llx,%llx,%llx,%llx,%llx\n",
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words)),
				arguments.size(),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1)),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1 + argv.size())),
				static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(startup_stack.data() + startup_stack_guard_words + 1 + argv.size() + envp.size())),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 1]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 2]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 3]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 4]),
				static_cast<unsigned long long>(startup_stack[startup_stack_guard_words + 5]));
		}
		darling_windows_enter_darwin_start(
			reinterpret_cast<void*>(mapping.EntryAddress()),
			startup_stack.data() + startup_stack_guard_words);
		throw std::runtime_error("Darwin startup entry returned unexpectedly");
#else
		throw std::runtime_error(
			"Darwin startup transfer requires a supported Windows host architecture");
#endif
	}
	if (entry_abi == DarwinEntryAbi::CompatibleMain) {
#if !defined(__clang__) && !defined(_M_IX86)
		throw std::runtime_error(
			"real Mach-O execution requires a Clang build with System-V ABI support");
#endif
	}
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<void*>(mapping.EntryAddress()), 1);
	using EntryFunction = abi::GuestFn<int, int, char**, char**>;
	auto entry = reinterpret_cast<EntryFunction>(mapping.EntryAddress());
	return entry(static_cast<int>(arguments.size()), argv.data(), envp.data());
}

MachOImage::MachOImage(HANDLE file, HANDLE mapping, const std::uint8_t* view,
	std::size_t file_bytes, std::size_t image_offset, MachHeader64 header,
	bool is_32_bit,
	std::vector<MachOSegment> segments, std::vector<std::string> dependencies,
	std::vector<MachOSection> sections,
	std::vector<std::string> weak_dependencies, std::vector<std::string> rpaths,
	std::vector<MachOSymbol> symbols, std::vector<MachORelocation> relocations,
	std::vector<std::uint32_t> indirect_symbols,
	std::vector<DyldBindAction> bind_actions,
	std::vector<DyldBindAction> threaded_bind_targets,
	std::vector<DyldRebaseAction> rebase_actions,
	std::vector<DyldChainedFixup> chained_fixups, std::vector<DyldChainedImport> chained_imports,
	std::vector<MachOReexport> reexports,
	std::uint64_t entry_offset, std::array<std::uint8_t, 16> uuid,
	std::string install_name, std::string dylinker_path) noexcept :
	m_file(file), m_mapping(mapping), m_view(view), m_file_bytes(file_bytes),
	m_image_offset(image_offset),
	m_header_storage(header), m_header(&m_header_storage), m_is_32_bit(is_32_bit),
	m_segments(std::move(segments)),
	m_sections(std::move(sections)), m_dependencies(std::move(dependencies)),
	m_weak_dependencies(std::move(weak_dependencies)),
	m_rpaths(std::move(rpaths)),
	m_symbols(std::move(symbols)),
	m_relocations(std::move(relocations)),
	m_indirect_symbols(std::move(indirect_symbols)),
	m_bind_actions(std::move(bind_actions)),
	m_threaded_bind_targets(std::move(threaded_bind_targets)),
	m_rebase_actions(std::move(rebase_actions)),
	m_chained_fixups(std::move(chained_fixups)),
	m_chained_imports(std::move(chained_imports)),
	m_reexports(std::move(reexports)),
	m_uuid(uuid),
	m_install_name(std::move(install_name)),
	m_dylinker_path(std::move(dylinker_path)),
	m_entry_offset(entry_offset)
{
}

MachOImage::MachOImage(MachOImage&& other) noexcept :
	m_file(other.m_file), m_mapping(other.m_mapping), m_view(other.m_view),
	m_file_bytes(other.m_file_bytes), m_image_offset(other.m_image_offset),
	m_header_storage(other.m_header_storage), m_header(&m_header_storage),
	m_is_32_bit(other.m_is_32_bit),
	m_segments(std::move(other.m_segments)),
	m_sections(std::move(other.m_sections)),
	m_dependencies(std::move(other.m_dependencies)),
	m_weak_dependencies(std::move(other.m_weak_dependencies)),
	m_rpaths(std::move(other.m_rpaths)),
	m_symbols(std::move(other.m_symbols)),
	m_relocations(std::move(other.m_relocations)),
	m_indirect_symbols(std::move(other.m_indirect_symbols)),
	m_bind_actions(std::move(other.m_bind_actions)),
	m_threaded_bind_targets(std::move(other.m_threaded_bind_targets)),
	m_rebase_actions(std::move(other.m_rebase_actions)),
	m_chained_fixups(std::move(other.m_chained_fixups)),
	m_chained_imports(std::move(other.m_chained_imports)),
	m_reexports(std::move(other.m_reexports)),
	m_uuid(other.m_uuid),
	m_install_name(std::move(other.m_install_name)),
	m_dylinker_path(std::move(other.m_dylinker_path)),
	m_entry_offset(other.m_entry_offset)
{
	other.m_file = INVALID_HANDLE_VALUE;
	other.m_mapping = nullptr;
	other.m_view = nullptr;
	other.m_image_offset = 0;
	other.m_header = &other.m_header_storage;
	other.m_is_32_bit = false;
}

MachOImage& MachOImage::operator=(MachOImage&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_file = other.m_file;
		m_mapping = other.m_mapping;
		m_view = other.m_view;
		m_file_bytes = other.m_file_bytes;
		m_image_offset = other.m_image_offset;
		m_header_storage = other.m_header_storage;
		m_header = &m_header_storage;
		m_is_32_bit = other.m_is_32_bit;
		m_segments = std::move(other.m_segments);
		m_sections = std::move(other.m_sections);
		m_dependencies = std::move(other.m_dependencies);
		m_weak_dependencies = std::move(other.m_weak_dependencies);
		m_rpaths = std::move(other.m_rpaths);
		m_symbols = std::move(other.m_symbols);
		m_relocations = std::move(other.m_relocations);
		m_indirect_symbols = std::move(other.m_indirect_symbols);
		m_bind_actions = std::move(other.m_bind_actions);
		m_threaded_bind_targets = std::move(other.m_threaded_bind_targets);
		m_rebase_actions = std::move(other.m_rebase_actions);
		m_chained_fixups = std::move(other.m_chained_fixups);
		m_chained_imports = std::move(other.m_chained_imports);
		m_reexports = std::move(other.m_reexports);
		m_uuid = other.m_uuid;
		m_install_name = std::move(other.m_install_name);
		m_dylinker_path = std::move(other.m_dylinker_path);
		m_entry_offset = other.m_entry_offset;
		other.m_file = INVALID_HANDLE_VALUE;
		other.m_mapping = nullptr;
		other.m_view = nullptr;
		other.m_image_offset = 0;
		other.m_header = &other.m_header_storage;
		other.m_is_32_bit = false;
	}
	return *this;
}

MachOImage::~MachOImage() noexcept
{
	Reset();
}

void MachOImage::Reset() noexcept
{
	if (m_view != nullptr) {
		UnmapViewOfFile(m_view);
		m_view = nullptr;
	}
	if (m_mapping != nullptr) {
		CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
	if (m_file != INVALID_HANDLE_VALUE) {
		CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
	}
	m_header = &m_header_storage;
	m_is_32_bit = false;
}

MachOImage::Mapping::Mapping(void* reservation, std::vector<MappedMachOSegment> segments,
	std::uintptr_t entry_address, std::intptr_t slide, std::size_t reservation_bytes,
	std::size_t thunk_pool_offset) noexcept :
	m_reservation(reservation), m_segments(std::move(segments)), m_entry_address(entry_address),
	m_slide(slide), m_reservation_bytes(reservation_bytes),
	m_thunk_pool_offset(thunk_pool_offset)
{
}

MachOImage::Mapping::Mapping(Mapping&& other) noexcept :
	m_reservation(other.m_reservation), m_segments(std::move(other.m_segments)),
	m_entry_address(other.m_entry_address), m_slide(other.m_slide),
	m_reservation_bytes(other.m_reservation_bytes),
	m_thunk_pool_offset(other.m_thunk_pool_offset),
	m_thunk_pool_used(other.m_thunk_pool_used),
	m_terminators_executed(other.m_terminators_executed),
	m_branch_thunks(std::move(other.m_branch_thunks)),
	m_arm64_host_imports(std::move(other.m_arm64_host_imports)),
	m_relocation_original_values(std::move(other.m_relocation_original_values)),
	m_relocation_original_sites(std::move(other.m_relocation_original_sites)),
	m_relocation_original_widths(std::move(other.m_relocation_original_widths))
{
	other.m_reservation = nullptr;
	other.m_reservation_bytes = 0;
	other.m_thunk_pool_offset = 0;
	other.m_thunk_pool_used = 0;
	other.m_arm64_host_imports.clear();
}

MachOImage::Mapping& MachOImage::Mapping::operator=(Mapping&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_reservation = other.m_reservation;
		m_segments = std::move(other.m_segments);
		m_entry_address = other.m_entry_address;
		m_slide = other.m_slide;
		m_reservation_bytes = other.m_reservation_bytes;
		m_thunk_pool_offset = other.m_thunk_pool_offset;
		m_thunk_pool_used = other.m_thunk_pool_used;
		m_terminators_executed = other.m_terminators_executed;
		m_branch_thunks = std::move(other.m_branch_thunks);
		m_arm64_host_imports = std::move(other.m_arm64_host_imports);
		m_relocation_original_values = std::move(other.m_relocation_original_values);
		m_relocation_original_sites = std::move(other.m_relocation_original_sites);
		m_relocation_original_widths = std::move(other.m_relocation_original_widths);
		other.m_reservation = nullptr;
		other.m_reservation_bytes = 0;
		other.m_thunk_pool_offset = 0;
		other.m_thunk_pool_used = 0;
		other.m_arm64_host_imports.clear();
	}
	return *this;
}

MachOImage::Mapping::~Mapping() noexcept
{
	Reset();
}

void MachOImage::Mapping::Reset() noexcept
{
	if (m_reservation != nullptr) {
		VirtualFree(m_reservation, 0, MEM_RELEASE);
		m_reservation = nullptr;
	}
	m_reservation_bytes = 0;
	m_thunk_pool_offset = 0;
	m_thunk_pool_used = 0;
	m_terminators_executed = false;
	m_branch_thunks.clear();
	m_arm64_host_imports.clear();
	m_relocation_original_values.clear();
	m_relocation_original_sites.clear();
	m_relocation_original_widths.clear();
}

std::uintptr_t MachOImage::Mapping::CreateArm64HostImportThunk(
	const std::string& symbol, std::uintptr_t target) const
{
	if (target == 0 || m_reservation == nullptr)
		throw std::invalid_argument("ARM64 host import thunk requires a target and mapping");
	MEMORY_BASIC_INFORMATION memory{};
	if (VirtualQuery(reinterpret_cast<const void*>(target), &memory,
		sizeof(memory)) == sizeof(memory) && memory.State == MEM_COMMIT &&
		(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
		(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
			PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
		auto data_region = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
		auto data_region_bytes = static_cast<std::size_t>(memory.RegionSize);
		if (memory.Type == MEM_PRIVATE) {
			// VirtualQuery coalesces private heap pages into large regions. Exposing
			// that whole region as imported guest data makes later HeapAlloc-backed
			// guest malloc registrations collide with unrelated host allocations.
			const auto allocation_bytes = HeapSize(GetProcessHeap(), 0,
				reinterpret_cast<const void*>(target));
			if (allocation_bytes == static_cast<SIZE_T>(-1) || allocation_bytes == 0)
				throw std::runtime_error(
					"ARM64 private host data import is not a process-heap allocation base: " +
					symbol);
			data_region = target;
			data_region_bytes = static_cast<std::size_t>(allocation_bytes);
		}
		const auto existing = std::find_if(m_arm64_host_imports.begin(),
			m_arm64_host_imports.end(), [&symbol, target](const auto& item) {
				return item.identifier == 0 && item.symbol == symbol &&
					item.target == target;
			});
		if (existing == m_arm64_host_imports.end()) {
			const bool writable = (memory.Protect & (PAGE_READWRITE |
				PAGE_WRITECOPY)) != 0;
			m_arm64_host_imports.push_back({0, symbol, target, target,
				data_region, data_region_bytes, writable});
		}
		return target;
	}
	const auto existing = std::find_if(m_arm64_host_imports.begin(),
		m_arm64_host_imports.end(), [&symbol, target](const auto& item) {
			return item.identifier != 0 && item.symbol == symbol && item.target == target;
		});
	if (existing != m_arm64_host_imports.end())
		return existing->thunk;
	constexpr std::size_t thunk_bytes = 16;
	constexpr std::size_t thunk_capacity = 0x10000 / thunk_bytes;
	if (m_thunk_pool_used >= thunk_capacity)
		throw std::runtime_error("Mach-O ARM64 import thunk pool is exhausted");
	const auto address = reinterpret_cast<std::uintptr_t>(m_reservation) +
		m_thunk_pool_offset + m_thunk_pool_used * thunk_bytes;
	const auto page = address & ~std::uintptr_t(0xfff);
	const auto page_offset = address - page;
	if (page_offset == 0 && VirtualAlloc(reinterpret_cast<void*>(page), 0x1000,
		MEM_COMMIT, PAGE_READWRITE) == nullptr)
		ThrowLastError("VirtualAlloc(Mach-O ARM64 import thunk)");
	DWORD old_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page), 0x1000,
		PAGE_READWRITE, &old_protection))
		ThrowLastError("VirtualProtect(Mach-O ARM64 import thunk write)");
	const auto identifier = next_arm64_import_identifier.fetch_add(1,
		std::memory_order_relaxed);
	if (identifier == 0)
		throw std::overflow_error("ARM64 host import identifier space exhausted");
	const std::array<std::uint32_t, 4> code{
		0xd2800010u | ((identifier & 0xffffu) << 5),
		0xf2a00010u | (((identifier >> 16) & 0xffffu) << 5),
		0xd4000001u | (0x82u << 5),
		0xd65f03c0u
	};
	std::memcpy(reinterpret_cast<void*>(address), code.data(), sizeof(code));
	DWORD restored_protection = PAGE_NOACCESS;
	if (!VirtualProtect(reinterpret_cast<void*>(page), 0x1000,
		PAGE_EXECUTE_READ, &restored_protection))
		ThrowLastError("VirtualProtect(Mach-O ARM64 import thunk execute)");
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), sizeof(code));
	m_arm64_host_imports.push_back({identifier, symbol, target, address});
	++m_thunk_pool_used;
	return address;
}

void MachOLazyBindingSession::ApplyOne(std::size_t action_index) const
{
	m_image.ApplyLazyBindAction(action_index, m_mapping, m_bindings);
}

void MachOLazyBindingSession::ApplyAll() const
{
	m_image.ApplyLazyBindActions(m_mapping, m_bindings);
}

bool MachOLazyBindingSession::ApplyForSlot(std::uintptr_t slot_address) const
{
	const auto action = FindActionForSlot(slot_address);
	if (!action.has_value())
		return false;
	ApplyOne(*action);
	return true;
}

std::optional<std::size_t> MachOLazyBindingSession::FindActionForLazyInfo(
	std::uintptr_t lazy_binding_info) const noexcept
{
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (action.lazy && action.stream_offset == lazy_binding_info)
			return index;
	}
	return std::nullopt;
}

std::optional<std::uintptr_t> MachOLazyBindingSession::ResolveForLazyInfo(
	std::uintptr_t lazy_binding_info) const
{
	const auto same_binding_symbol = [](const std::string& left,
		const std::string& right) {
		return left == right || (left.size() > 1 && left.front() == '_' &&
			left.substr(1) == right);
	};
	std::optional<std::uintptr_t> resolved_target;
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (!action.lazy || action.stream_offset != lazy_binding_info)
			continue;
		ApplyOne(index);
		const auto found = std::find_if(m_bindings.begin(), m_bindings.end(),
			[&action, &same_binding_symbol](const DyldResolvedBinding& binding) {
				return same_binding_symbol(action.symbol, binding.symbol) &&
					(!binding.has_dylib_ordinal || binding.dylib_ordinal ==
						action.dylib_ordinal);
			});
		if (found != m_bindings.end())
			resolved_target = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(found->address) + action.addend);
		else if (action.weak)
			resolved_target = 0;
	}
	return resolved_target;
}

std::optional<std::size_t> MachOLazyBindingSession::FindActionForSlot(
	std::uintptr_t slot_address) const noexcept
{
	for (std::size_t index = 0; index < m_image.BindActions().size(); ++index) {
		const auto& action = m_image.BindActions()[index];
		if (!action.lazy || action.segment_index >= m_image.Segments().size())
			continue;
		const auto& segment = m_image.Segments()[action.segment_index];
		const auto candidate = static_cast<std::uintptr_t>(segment.vm_address) +
			static_cast<std::uintptr_t>(action.segment_offset) +
			static_cast<std::intptr_t>(m_mapping.Slide());
		if (candidate == slot_address)
			return index;
	}
	return std::nullopt;
}

MachOLazyBindingSession::Activation::Activation(
	const MachOLazyBindingSession* session) noexcept
	: previous_(active_lazy_binding_session), session_(session), active_(true)
{
	active_lazy_binding_session = session;
	try {
		std::lock_guard lock(active_lazy_binding_sessions_mutex);
		active_lazy_binding_sessions.push_back(session);
	} catch (...) {
		// Activation is noexcept. If bookkeeping allocation fails, the binder
		// fails closed instead of terminating the host process.
	}
}

MachOLazyBindingSession::Registration::Registration(
	const MachOLazyBindingSession* session)
	: session_(session)
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	active_lazy_binding_sessions.push_back(session);
}

MachOLazyBindingSession::Registration::Registration(Registration&& other) noexcept
	: session_(other.session_)
{
	other.session_ = nullptr;
}

MachOLazyBindingSession::Registration& MachOLazyBindingSession::Registration::operator=(
	Registration&& other) noexcept
{
	if (this != &other) {
		if (session_ != nullptr)
			RemoveActiveLazyBindingSession(session_);
		session_ = other.session_;
		other.session_ = nullptr;
	}
	return *this;
}

MachOLazyBindingSession::Registration::~Registration() noexcept
{
	if (session_ != nullptr)
		RemoveActiveLazyBindingSession(session_);
}

MachOLazyBindingSession::Activation::Activation(Activation&& other) noexcept
	: previous_(other.previous_), session_(other.session_), active_(other.active_)
{
	other.active_ = false;
	other.session_ = nullptr;
}

MachOLazyBindingSession::Activation& MachOLazyBindingSession::Activation::operator=(
	Activation&& other) noexcept
{
	if (this != &other) {
		if (active_) {
			active_lazy_binding_session = previous_;
			RemoveActiveLazyBindingSession(session_);
		}
		previous_ = other.previous_;
		session_ = other.session_;
		active_ = other.active_;
		other.active_ = false;
		other.session_ = nullptr;
	}
	return *this;
}

MachOLazyBindingSession::Activation::~Activation() noexcept
{
	if (active_) {
		active_lazy_binding_session = previous_;
		RemoveActiveLazyBindingSession(session_);
	}
}

const MachOLazyBindingSession* MachOLazyBindingSession::Current() noexcept
{
	return active_lazy_binding_session;
}

bool MachOLazyBindingSession::HasActiveSessions() noexcept
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	return !active_lazy_binding_sessions.empty();
}

bool MachOLazyBindingSession::OwnsImageCacheAddress(
	std::uintptr_t image_cache) const noexcept
{
	if (image_cache == 0) return false;
	for (const auto& segment : m_mapping.Segments()) {
		if (segment.size != 0 && segment.address != 0 &&
			image_cache >= segment.address &&
			image_cache - segment.address < segment.size)
			return true;
	}
	return false;
}

std::optional<std::uintptr_t> MachOLazyBindingSession::ResolveActiveForImage(
	std::uintptr_t image_cache, std::uintptr_t lazy_binding_info)
{
	std::lock_guard lock(active_lazy_binding_sessions_mutex);
	const MachOLazyBindingSession* unique_candidate = nullptr;
	for (auto session = active_lazy_binding_sessions.rbegin();
		session != active_lazy_binding_sessions.rend(); ++session) {
		if (!(*session)->FindActionForLazyInfo(lazy_binding_info).has_value())
			continue;
		if ((*session)->OwnsImageCacheAddress(image_cache))
			return (*session)->ResolveForLazyInfo(lazy_binding_info);
		if (unique_candidate != nullptr && unique_candidate != *session)
			return std::nullopt;
		unique_candidate = *session;
	}
	// The cache pointer disambiguates identical lazy-stream offsets across
	// images. If only one active image owns the offset, it is sufficient alone.
	return unique_candidate == nullptr ? std::nullopt :
		unique_candidate->ResolveForLazyInfo(lazy_binding_info);
}

} // namespace darling::windows_host
