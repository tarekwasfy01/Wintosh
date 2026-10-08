/*
 * Stage 1 Windows thread and TLS boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_thread.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
	try {
		darling::windows_host::TlsSlot slot;
		std::atomic<bool> tls_is_thread_local{false};
		auto thread = darling::windows_host::Thread::Start([&] {
			int thread_value = 42;
			slot.Set(&thread_value);
			tls_is_thread_local.store(slot.Get() == &thread_value);
		});
		if (thread.Join(10000) != WAIT_OBJECT_0 || thread.ExitCode() != 0 ||
			!tls_is_thread_local.load() || slot.Get() != nullptr) {
			return 2;
		}
		std::cout << "WINDOWS_THREAD=PASS\n";
		std::cout << "WINDOWS_THREAD_EXIT=PASS\n";
		std::cout << "WINDOWS_TLS=PASS\n";
		auto delayed = darling::windows_host::Thread::Start([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(25));
		});
		if (delayed.Join(0) != WAIT_TIMEOUT ||
			delayed.Join(10000) != WAIT_OBJECT_0 || delayed.ExitCode() != 0) {
			std::cerr << "THREAD_SMOKE_ERROR=join timeout semantics\n";
			return 4;
		}
		std::cout << "WINDOWS_THREAD_JOIN_TIMEOUT=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "THREAD_SMOKE_ERROR=" << error.what() << "\n";
		return 3;
	}
}
