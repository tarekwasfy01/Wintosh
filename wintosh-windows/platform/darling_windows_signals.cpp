/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_signals.h"
#include "darling_windows_errno.h"
#include "darling_windows_stdio.h"

#include <array>
#include <chrono>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace darling::windows_host {

namespace {

thread_local std::uint64_t blocked_signals = 0;
thread_local std::uint64_t pending_signals = 0;
std::array<DarwinSignals::Handler, 64> installed_handlers{};
std::array<darling_darwin_sigaction_record, 64> installed_actions{};
bool actions_initialized = false;
std::mutex sigwait_mutex;
std::condition_variable sigwait_condition;
std::uint64_t sigwait_mask = 0;
std::uint64_t sigwait_pending = 0;
std::array<darling_signal_value, 64> sigwait_values{};

void InitializeActions() noexcept
{
	if (actions_initialized)
		return;
	for (auto& action : installed_actions)
		action.handler = SIG_DFL;
	actions_initialized = true;
}

std::uint64_t SetToBits(const darling_darwin_sigset& set) noexcept
{
	const auto darwin_bits = static_cast<std::uint64_t>(set.bits[0]) |
		(static_cast<std::uint64_t>(set.bits[1]) << 32);
	return darwin_bits << 1;
}

void BitsToSet(std::uint64_t bits, darling_darwin_sigset& set) noexcept
{
	set = {};
	const auto darwin_bits = bits >> 1;
	set.bits[0] = static_cast<std::uint32_t>(darwin_bits);
	set.bits[1] = static_cast<std::uint32_t>(darwin_bits >> 32);
}

bool ValidSetSignal(int signal_number) noexcept
{
	return signal_number > 0 && signal_number < 64;
}

std::uint32_t* SetWord(darling_darwin_sigset& set, int signal_number) noexcept
{
	return &set.bits[static_cast<std::size_t>(signal_number - 1) / 32];
}

std::uint32_t SetMask(int signal_number) noexcept
{
	return std::uint32_t{1} << ((signal_number - 1) % 32);
}

std::uint64_t SignalBit(int signal_number) noexcept
{
	return signal_number > 0 && signal_number < 64 ?
		(std::uint64_t{1} << signal_number) : 0;
}

void DeliverUnblocked() noexcept
{
	const auto deliverable = pending_signals & ~blocked_signals;
	for (int signal_number = 1; signal_number < 64; ++signal_number) {
		const auto bit = SignalBit(signal_number);
		if ((deliverable & bit) == 0)
			continue;
		pending_signals &= ~bit;
		InitializeActions();
		const auto& action = installed_actions[static_cast<std::size_t>(signal_number)];
		const auto handler = installed_handlers[static_cast<std::size_t>(signal_number)];
		if (handler != nullptr && handler != SIG_DFL && handler != SIG_IGN) {
			const auto previous_mask = blocked_signals;
			blocked_signals |= SetToBits(action.mask);
			if ((action.flags & 0x0010) == 0) // SA_NODEFER
				blocked_signals |= bit;
			handler(signal_number);
			if ((action.flags & 0x0004) != 0) // SA_RESETHAND
				installed_actions[static_cast<std::size_t>(signal_number)].handler = SIG_DFL;
			blocked_signals = previous_mask;
			DeliverUnblocked();
		} else {
			(void)std::raise(signal_number);
		}
	}
}

} // namespace

DarwinSignals::Handler DarwinSignals::Install(int signal_number,
	Handler handler) noexcept
{
	InitializeActions();
	DarwinSignals::Handler previous = SIG_DFL;
	if (signal_number > 0 && signal_number < 64)
		previous = installed_handlers[static_cast<std::size_t>(signal_number)];
	// SIGINT is the only signal in this bridge that needs to be registered with
	// the MSVC CRT; the remaining Darwin dispositions are delivered by Raise.
	if (signal_number == SIGINT)
		previous = std::signal(signal_number, handler);
	if (signal_number > 0 && signal_number < 64 && previous != SIG_ERR) {
		installed_handlers[static_cast<std::size_t>(signal_number)] = handler;
		installed_actions[static_cast<std::size_t>(signal_number)].handler = handler;
	}
	return previous;
}

bool DarwinSignals::Raise(int signal_number) noexcept
{
	const auto bit = SignalBit(signal_number);
	if (bit == 0)
		return false;
	{
		std::lock_guard lock(sigwait_mutex);
		if ((sigwait_mask & bit) != 0) {
			sigwait_pending |= bit;
			sigwait_condition.notify_one();
			return true;
		}
	}
	if ((blocked_signals & bit) != 0) {
		pending_signals |= bit;
		return true;
	}
	InitializeActions();
	const auto& action = installed_actions[static_cast<std::size_t>(signal_number)];
	const auto handler = action.handler;
	if ((action.flags & 0x0040) != 0 && action.sigaction_handler != nullptr) {
		const auto previous_mask = blocked_signals;
		blocked_signals |= SetToBits(action.mask);
		if ((action.flags & 0x0010) == 0) // SA_NODEFER
			blocked_signals |= bit;
		if ((action.flags & 0x0004) != 0) // SA_RESETHAND
			installed_actions[static_cast<std::size_t>(signal_number)].sigaction_handler = nullptr;
		action.sigaction_handler(signal_number, nullptr, nullptr);
		blocked_signals = previous_mask;
		DeliverUnblocked();
		return true;
	}
	if (handler != nullptr && handler != SIG_DFL && handler != SIG_IGN) {
		const auto previous_mask = blocked_signals;
		blocked_signals |= SetToBits(action.mask);
		if ((action.flags & 0x0010) == 0) // SA_NODEFER
			blocked_signals |= bit;
		if ((action.flags & 0x0004) != 0) // SA_RESETHAND
			installed_actions[static_cast<std::size_t>(signal_number)].handler = SIG_DFL;
		handler(signal_number);
		blocked_signals = previous_mask;
		DeliverUnblocked();
		return true;
	}
	return std::raise(signal_number) == 0;
}

DarwinSignals::Handler DarwinSignals::Reset(int signal_number) noexcept
{
	const auto previous = std::signal(signal_number, SIG_DFL);
	if (signal_number > 0 && signal_number < 64 && previous != SIG_ERR) {
		InitializeActions();
		installed_handlers[static_cast<std::size_t>(signal_number)] = SIG_DFL;
		installed_actions[static_cast<std::size_t>(signal_number)].handler = SIG_DFL;
	}
	return previous;
}

bool DarwinSignals::Blocked(int signal_number) noexcept
{
	const auto bit = SignalBit(signal_number);
	return bit != 0 && (blocked_signals & bit) != 0;
}

std::uint64_t DarwinSignals::Mask() noexcept
{
	return blocked_signals;
}

std::uint64_t DarwinSignals::SetMask(std::uint64_t mask) noexcept
{
	const auto previous = blocked_signals;
	blocked_signals = mask;
	DeliverUnblocked();
	return previous;
}

std::uint64_t DarwinSignals::Block(std::uint64_t mask) noexcept
{
	const auto previous = blocked_signals;
	blocked_signals |= mask;
	return previous;
}

std::uint64_t DarwinSignals::Unblock(std::uint64_t mask) noexcept
{
	const auto previous = blocked_signals;
	blocked_signals &= ~mask;
	DeliverUnblocked();
	return previous;
}

std::uint64_t DarwinSignals::Pending() noexcept
{
	return pending_signals;
}

extern "C" int darling_windows_sigaction(int signal_number,
	const darling_darwin_sigaction_record* action,
	darling_darwin_sigaction_record* old_action)
{
	constexpr int darwin_sigkill = 9;
	constexpr int darwin_sigstop = 19;
	if (signal_number <= 0 || signal_number >= 64 || signal_number == darwin_sigkill ||
		signal_number == darwin_sigstop ||
		(action != nullptr && (action->flags & ~0x007f) != 0)) {
		return -1;
	}
	InitializeActions();
	auto& current = installed_actions[static_cast<std::size_t>(signal_number)];
	if (old_action != nullptr)
		*old_action = current;
	if (action != nullptr) {
		if ((action->mask.bits[2] | action->mask.bits[3]) != 0)
			return -1;
		const auto mask = SetToBits(action->mask);
		current = *action;
		if ((action->flags & 0x0040) != 0) {
			(void)std::signal(signal_number, SIG_IGN);
		} else {
			(void)DarwinSignals::Install(signal_number, action->handler);
		}
		(void)mask;
	}
	return 0;
}

extern "C" int darling_windows_sigemptyset(darling_darwin_sigset* set)
{
	if (set == nullptr)
		return 22;
	*set = {};
	return 0;
}

extern "C" int darling_windows_sigfillset(darling_darwin_sigset* set)
{
	if (set == nullptr)
		return 22;
	*set = {};
	set->bits[0] = 0xffff'fffeu;
	set->bits[1] = 0x7fffffffu;
	return 0;
}

extern "C" int darling_windows_sigaddset(darling_darwin_sigset* set, int signal_number)
{
	if (set == nullptr || !ValidSetSignal(signal_number))
		return 22;
	*SetWord(*set, signal_number) |= SetMask(signal_number);
	return 0;
}

extern "C" int darling_windows_sigdelset(darling_darwin_sigset* set, int signal_number)
{
	if (set == nullptr || !ValidSetSignal(signal_number))
		return 22;
	*SetWord(*set, signal_number) &= ~SetMask(signal_number);
	return 0;
}

extern "C" int darling_windows_sigismember(const darling_darwin_sigset* set,
	int signal_number)
{
	if (set == nullptr || !ValidSetSignal(signal_number))
		return -1;
	return (*SetWord(*const_cast<darling_darwin_sigset*>(set), signal_number) &
		SetMask(signal_number)) != 0 ? 1 : 0;
}

extern "C" int darling_windows_sigprocmask(int how,
	const darling_darwin_sigset* set, darling_darwin_sigset* old_set)
{
	if (old_set != nullptr)
		BitsToSet(DarwinSignals::Mask(), *old_set);
	if (set == nullptr)
		return 0;
	if ((set->bits[2] | set->bits[3]) != 0)
		return -1;
	const auto mask = SetToBits(*set);
	if (how == 1) { // SIG_BLOCK
		(void)DarwinSignals::Block(mask);
	} else if (how == 2) { // SIG_UNBLOCK
		(void)DarwinSignals::Unblock(mask);
	} else if (how == 3) { // SIG_SETMASK
		(void)DarwinSignals::SetMask(mask);
	} else {
		return -1;
	}
	return 0;
}

extern "C" int darling_windows_sigpending(darling_darwin_sigset* set)
{
	if (set == nullptr)
		return -1;
	BitsToSet(DarwinSignals::Pending(), *set);
	return 0;
}

extern "C" int darling_windows_sigwait(const darling_darwin_sigset* set,
	int* signal_number)
{
	if (set == nullptr || signal_number == nullptr ||
		(set->bits[2] | set->bits[3]) != 0) {
		return 22;
	}
	const auto mask = SetToBits(*set);
	if (mask == 0)
		return 22;
	std::unique_lock lock(sigwait_mutex);
	sigwait_mask |= mask;
	sigwait_condition.wait(lock, [&] {
		return (sigwait_pending & mask) != 0;
	});
	const auto available = sigwait_pending & mask;
	int selected = 0;
	for (int candidate = 1; candidate < 64; ++candidate) {
		if ((available & SignalBit(candidate)) != 0) {
			selected = candidate;
			break;
		}
	}
	sigwait_pending &= ~SignalBit(selected);
	sigwait_values[static_cast<std::size_t>(selected)] = {};
	sigwait_mask &= ~SignalBit(selected);
	*signal_number = selected;
	return 0;
}

extern "C" int darling_windows_sigtimedwait(const darling_darwin_sigset* set,
	int* signal_number, const darling_timespec* timeout)
{
	if (set == nullptr || signal_number == nullptr || timeout == nullptr ||
		(set->bits[2] | set->bits[3]) != 0 || timeout->tv_sec < 0 ||
		timeout->tv_nsec < 0 || timeout->tv_nsec >= 1'000'000'000) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto mask = SetToBits(*set);
	if (mask == 0) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	std::unique_lock lock(sigwait_mutex);
	sigwait_mask |= mask;
	const auto ready = [&] { return (sigwait_pending & mask) != 0; };
	const auto duration = std::chrono::seconds(timeout->tv_sec) +
		std::chrono::nanoseconds(timeout->tv_nsec);
	if (!ready() && !sigwait_condition.wait_for(lock, duration, ready)) {
		sigwait_mask &= ~mask;
		darling::windows_host::DarwinErrno::Set(35);
		return -1;
	}
	const auto available = sigwait_pending & mask;
	int selected = 0;
	for (int candidate = 1; candidate < 64; ++candidate) {
		if ((available & SignalBit(candidate)) != 0) {
			selected = candidate;
			break;
		}
	}
	sigwait_pending &= ~SignalBit(selected);
	sigwait_values[static_cast<std::size_t>(selected)] = {};
	sigwait_mask &= ~SignalBit(selected);
	*signal_number = selected;
	return selected;
}

extern "C" int darling_windows_sigwaitinfo(const darling_darwin_sigset* set,
	darling_siginfo* info)
{
	if (set == nullptr || (set->bits[2] | set->bits[3]) != 0)
		return -1;
	const auto mask = SetToBits(*set);
	if (mask == 0)
		return -1;
	std::unique_lock lock(sigwait_mutex);
	sigwait_mask |= mask;
	sigwait_condition.wait(lock, [&] { return (sigwait_pending & mask) != 0; });
	const auto available = sigwait_pending & mask;
	int signal_number = 0;
	for (int candidate = 1; candidate < 64; ++candidate) {
		if ((available & SignalBit(candidate)) != 0) {
			signal_number = candidate;
			break;
		}
	}
	const auto value = sigwait_values[static_cast<std::size_t>(signal_number)];
	sigwait_pending &= ~SignalBit(signal_number);
	sigwait_values[static_cast<std::size_t>(signal_number)] = {};
	sigwait_mask &= ~SignalBit(signal_number);
	if (info != nullptr) {
		std::memset(info, 0, sizeof(*info));
		info->si_signo = signal_number;
		info->si_value.si_value_int = value.sival_int;
	}
	return signal_number;
}

extern "C" int darling_windows_sigqueue(int process_id, int signal_number,
	const darling_signal_value* value)
{
	if (process_id != darling_windows_getpid() || signal_number <= 0 ||
		signal_number >= 64 || value == nullptr) {
		darling::windows_host::DarwinErrno::Set(22);
		return -1;
	}
	const auto bit = SignalBit(signal_number);
	{
		std::lock_guard lock(sigwait_mutex);
		if ((sigwait_mask & bit) != 0) {
			sigwait_values[static_cast<std::size_t>(signal_number)] = *value;
			sigwait_pending |= bit;
			sigwait_condition.notify_one();
			return 0;
		}
	}
	return DarwinSignals::Raise(signal_number) ? 0 : -1;
}

} // namespace darling::windows_host
