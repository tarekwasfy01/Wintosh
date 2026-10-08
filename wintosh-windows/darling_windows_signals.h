/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#pragma once

#include <csignal>
#include <cstdint>

struct darling_darwin_sigset final {
	std::uint32_t bits[4]{};
};

struct darling_signal_value final {
	union {
		int sival_int;
		std::uintptr_t sival_ptr;
	};
	darling_signal_value() : sival_int(0) {}
};

struct darling_timespec;
struct darling_siginfo;

struct darling_darwin_sigaction_record final {
	union {
		void (*handler)(int);
		void (*sigaction_handler)(int, void*, void*);
	};
	darling_darwin_sigset mask{};
	int flags = 0;
	darling_darwin_sigaction_record() : handler(nullptr) {}
};

extern "C" int darling_windows_sigaction(int signal_number,
	const darling_darwin_sigaction_record* action,
	darling_darwin_sigaction_record* old_action);
extern "C" int darling_windows_sigprocmask(int how,
	const darling_darwin_sigset* set, darling_darwin_sigset* old_set);
extern "C" int darling_windows_sigpending(darling_darwin_sigset* set);
extern "C" int darling_windows_sigwait(const darling_darwin_sigset* set,
	int* signal_number);
extern "C" int darling_windows_sigtimedwait(const darling_darwin_sigset* set,
	int* signal_number, const darling_timespec* timeout);
extern "C" int darling_windows_sigwaitinfo(const darling_darwin_sigset* set,
	darling_siginfo* info);
extern "C" int darling_windows_sigqueue(int process_id, int signal_number,
	const darling_signal_value* value);

namespace darling::windows_host {

class DarwinSignals final {
public:
	using Handler = void (*)(int);

	[[nodiscard]] static Handler Install(int signal_number, Handler handler) noexcept;
	static bool Raise(int signal_number) noexcept;
	[[nodiscard]] static Handler Reset(int signal_number) noexcept;
	static bool Blocked(int signal_number) noexcept;
	static std::uint64_t Mask() noexcept;
	static std::uint64_t SetMask(std::uint64_t mask) noexcept;
	static std::uint64_t Block(std::uint64_t mask) noexcept;
	static std::uint64_t Unblock(std::uint64_t mask) noexcept;
	static std::uint64_t Pending() noexcept;
};

} // namespace darling::windows_host
