/*
 * Stage 2 Darwin signal boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_signals.h"
#include "darling_windows_stdio.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<int> signal_count{0};
std::atomic<int> siginfo_count{0};
std::atomic<int> siginfo_mask_seen{0};
std::atomic<int> deferred_count{0};
std::atomic<int> deferred_mask_seen{0};

void Handler(int signal_number)
{
	if (signal_number == SIGINT) {
		signal_count.fetch_add(1, std::memory_order_relaxed);
	}
	if (signal_number == SIGTERM) {
		deferred_count.fetch_add(1, std::memory_order_relaxed);
		if (darling::windows_host::DarwinSignals::Blocked(SIGINT))
			deferred_mask_seen.fetch_add(1, std::memory_order_relaxed);
	}
}

void SiginfoHandler(int signal_number, void* info, void* context)
{
	if (signal_number == SIGTERM && info == nullptr && context == nullptr &&
		darling::windows_host::DarwinSignals::Blocked(SIGINT))
		siginfo_mask_seen.fetch_add(1, std::memory_order_relaxed);
	if (signal_number == SIGTERM && info == nullptr && context == nullptr)
		siginfo_count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

int main()
{
	darling_darwin_sigset constructed{};
	darling_darwin_sigset filled{};
	const bool sigset_primitives =
		darling_windows_sigemptyset(&constructed) == 0 &&
		darling_windows_sigaddset(&constructed, SIGTERM) == 0 &&
		darling_windows_sigismember(&constructed, SIGTERM) == 1 &&
		darling_windows_sigdelset(&constructed, SIGTERM) == 0 &&
		darling_windows_sigismember(&constructed, SIGTERM) == 0 &&
		darling_windows_sigfillset(&filled) == 0 &&
		darling_windows_sigismember(&filled, SIGTERM) == 1 &&
		darling_windows_sigismember(&filled, 0) == -1 &&
		darling_windows_host_symbol("sigemptyset") != 0 &&
		darling_windows_host_symbol("sigfillset") != 0 &&
		darling_windows_host_symbol("sigaddset") != 0 &&
		darling_windows_host_symbol("sigdelset") != 0 &&
		darling_windows_host_symbol("sigismember") != 0 &&
		darling_windows_host_symbol("__sigemptyset") != 0 &&
		darling_windows_host_symbol("__sigismember") != 0;
	if (!sigset_primitives) {
		std::cerr << "SIGNAL_SMOKE_ERROR=sigset primitives failed\n";
		return 1;
	}
	const auto previous = darling::windows_host::DarwinSignals::Install(SIGINT, Handler);
	if (previous == SIG_ERR || !darling::windows_host::DarwinSignals::Raise(SIGINT) ||
		signal_count.load(std::memory_order_relaxed) != 1) {
		std::cerr << "SIGNAL_SMOKE_ERROR=handler dispatch failed\n";
		return 1;
	}
	const std::uint64_t sigint_bit = std::uint64_t{1} << SIGINT;
	if (darling::windows_host::DarwinSignals::Block(sigint_bit) != 0 ||
		!darling::windows_host::DarwinSignals::Raise(SIGINT) ||
		signal_count.load(std::memory_order_relaxed) != 1 ||
		(darling::windows_host::DarwinSignals::Pending() & sigint_bit) == 0 ||
		darling::windows_host::DarwinSignals::Unblock(sigint_bit) != sigint_bit ||
		signal_count.load(std::memory_order_relaxed) != 2) {
		std::cerr << "SIGNAL_SMOKE_ERROR=mask/pending dispatch failed\n";
		return 2;
	}
	const auto reset_handler = darling::windows_host::DarwinSignals::Reset(SIGINT);
	(void)reset_handler;
	darling_darwin_sigaction_record action{};
	action.handler = Handler;
	action.mask.bits[0] = 1u << (SIGINT - 1);
	darling_darwin_sigaction_record old_action{};
	darling_darwin_sigset old_mask{};
	darling_darwin_sigset pending{};
	const int sigaction_result = darling_windows_sigaction(SIGTERM, &action, &old_action);
	const int sigprocmask_result = darling_windows_sigprocmask(3, &old_mask, nullptr);
	const int sigpending_result = darling_windows_sigpending(&pending);
	const bool signal_symbols = darling_windows_host_symbol("_sigaction") != 0 &&
		darling_windows_host_symbol("_sigprocmask") != 0 &&
		darling_windows_host_symbol("_sigpending") != 0;
	const bool normalized_signal_symbols = darling_windows_host_symbol("sigaction") != 0 &&
		darling_windows_host_symbol("sigprocmask") != 0 &&
		darling_windows_host_symbol("sigpending") != 0 &&
		darling_windows_host_symbol("sigwait") != 0 &&
		darling_windows_host_symbol("sigtimedwait") != 0 &&
		darling_windows_host_symbol("sigwaitinfo") != 0 &&
		darling_windows_host_symbol("sigqueue") != 0 &&
		darling_windows_host_symbol("sigaction_nocancel") != 0 &&
		darling_windows_host_symbol("sigprocmask_nocancel") != 0 &&
		darling_windows_host_symbol("sigpending_nocancel") != 0 &&
		darling_windows_host_symbol("sigwait_nocancel") != 0 &&
		darling_windows_host_symbol("sigwaitinfo_nocancel") != 0 &&
		darling_windows_host_symbol("sigqueue_nocancel") != 0 &&
		darling_windows_host_symbol("pthread_sigmask") != 0 &&
		darling_windows_host_symbol("__pthread_sigmask") != 0 &&
		darling_windows_host_symbol("__sigaction") != 0;
	std::cout << "DARWIN_NORMALIZED_SIGNAL_SYMBOLS="
		<< (normalized_signal_symbols ? "PASS" : "FAIL") << "\n";
	const bool setup_ok = sigaction_result == 0 && sigprocmask_result == 0 &&
		sigpending_result == 0 && signal_symbols && normalized_signal_symbols;
	std::cout << "SIGNAL_SETUP sigaction=" << sigaction_result
		<< " sigprocmask=" << sigprocmask_result
		<< " sigpending=" << sigpending_result
		<< " legacy_symbols=" << (signal_symbols ? 1 : 0)
		<< " normalized_symbols=" << (normalized_signal_symbols ? 1 : 0) << "\n";
	if (!setup_ok) {
		std::cerr << "SIGNAL_SMOKE_ERROR=setup sigaction=" << sigaction_result
			<< " sigprocmask=" << sigprocmask_result
			<< " sigpending=" << sigpending_result
			<< " symbols=" << (signal_symbols ? 1 : 0) << "\n";
		return 3;
	}
	darling_darwin_sigaction_record siginfo_action{};
	siginfo_action.sigaction_handler = &SiginfoHandler;
	siginfo_action.flags = 0x0040; // SA_SIGINFO
	siginfo_action.mask.bits[0] = 1u << (SIGINT - 1);
	if (darling_windows_sigaction(SIGTERM, &siginfo_action, nullptr) != 0 ||
		!darling::windows_host::DarwinSignals::Raise(SIGTERM) ||
		siginfo_count.load(std::memory_order_relaxed) != 1 ||
		siginfo_mask_seen.load(std::memory_order_relaxed) != 1 ||
		darling::windows_host::DarwinSignals::Blocked(SIGINT))
		return 4;
	darling_darwin_sigaction_record deferred_action{};
	deferred_action.handler = &Handler;
	deferred_action.flags = 0x0004; // SA_RESETHAND
	deferred_action.mask.bits[0] = 1u << (SIGINT - 1);
	darling_darwin_sigset blocked_term{};
	darling_windows_sigemptyset(&blocked_term);
	darling_windows_sigaddset(&blocked_term, SIGTERM);
	if (darling_windows_sigaction(SIGTERM, &deferred_action, nullptr) != 0 ||
		darling_windows_sigprocmask(1, &blocked_term, nullptr) != 0 ||
		!darling::windows_host::DarwinSignals::Raise(SIGTERM) ||
		deferred_count.load(std::memory_order_relaxed) != 0 ||
		darling_windows_sigprocmask(2, &blocked_term, nullptr) != 0 ||
		deferred_count.load(std::memory_order_relaxed) != 1 ||
		deferred_mask_seen.load(std::memory_order_relaxed) != 1)
		return 8;
	darling_darwin_sigaction_record reset_action{};
	if (darling_windows_sigaction(SIGTERM, nullptr, &reset_action) != 0 ||
		reset_action.handler != SIG_DFL)
		return 9;
	darling_darwin_sigset wait_set{};
	wait_set.bits[0] = 1u << (SIGINT - 1);
	std::atomic<int> waited_signal{0};
	std::atomic<int> wait_result{-1};
	std::thread waiter([&] {
		int selected = 0;
		wait_result.store(darling_windows_sigwait(&wait_set, &selected),
			std::memory_order_relaxed);
		waited_signal.store(selected, std::memory_order_relaxed);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	if (!darling::windows_host::DarwinSignals::Raise(SIGINT)) {
		waiter.detach();
		return 5;
	}
	waiter.join();
	if (darling_windows_host_symbol("_sigwait") == 0 ||
		wait_result.load(std::memory_order_relaxed) != 0 ||
		waited_signal.load(std::memory_order_relaxed) != SIGINT) {
		std::cerr << "SIGNAL_SMOKE_ERROR=sigwait failed\n";
		return 6;
	}
	darling_timespec zero_timeout{};
	int timed_signal = 0;
	if (darling_windows_host_symbol("_sigtimedwait") == 0 ||
		darling_windows_sigtimedwait(&wait_set, &timed_signal, &zero_timeout) != -1 ||
		*darling_windows_errno() != 35) {
		std::cerr << "SIGNAL_SMOKE_ERROR=sigtimedwait timeout failed\n";
		return 7;
	}
	std::atomic<int> waitinfo_result{-1};
	darling_siginfo waitinfo{};
	darling_signal_value queued_value{};
	queued_value.sival_int = 42;
	std::thread info_waiter([&] {
		waitinfo_result.store(darling_windows_sigwaitinfo(&wait_set, &waitinfo),
			std::memory_order_relaxed);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	if (darling_windows_sigqueue(darling_windows_getpid(), SIGINT, &queued_value) != 0) {
		info_waiter.detach();
		return 8;
	}
	info_waiter.join();
	if (darling_windows_host_symbol("_sigwaitinfo") == 0 ||
		darling_windows_host_symbol("_sigqueue") == 0 ||
		waitinfo_result.load(std::memory_order_relaxed) != SIGINT ||
		waitinfo.si_signo != SIGINT || waitinfo.si_code != 0 ||
		waitinfo.si_pid != 0 || waitinfo.si_value.si_value_int != 42) {
		std::cerr << "SIGNAL_SMOKE_ERROR=sigwaitinfo failed\n";
		return 9;
	}
	std::cout << "DARWIN_SYSCALL_SIGNAL=PASS\n";
	return 0;
}
