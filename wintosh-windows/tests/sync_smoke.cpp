/*
 * Stage 1 Windows semaphore and shared-memory proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_sync.h"

#include <cstring>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

int wmain()
{
	try {
		darling::windows_host::Mutex mutex;
		if (!mutex.TryLock())
			return 1;
		mutex.Unlock();
		darling::windows_host::ConditionVariable condition;
		mutex.Lock();
		if (condition.Wait(mutex, 20))
			return 2;
		mutex.Unlock();
		std::cout << "WINDOWS_CONDITION_TIMEOUT=PASS\n";
		std::atomic<bool> signalled{false};
		std::thread condition_waiter([&] {
			mutex.Lock();
			const bool woke = condition.Wait(mutex, 2000);
			signalled.store(woke, std::memory_order_release);
			mutex.Unlock();
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		condition.NotifyOne();
		condition_waiter.join();
		if (!signalled.load(std::memory_order_acquire))
			return 3;
		std::cout << "WINDOWS_MUTEX_CONDITION=PASS\n";

		const auto suffix = std::to_wstring(GetCurrentProcessId());
		auto semaphore = darling::windows_host::Semaphore::Create(L"darling-stage1-sem-" + suffix, 0, 1);
		if (semaphore.TryWait()) {
			return 4;
		}
		semaphore.Post();
		if (!semaphore.TryWait(1000)) {
			return 5;
		}
		std::atomic<bool> acquired{false};
		std::thread waiter([&] {
			acquired.store(semaphore.TryWait(2000), std::memory_order_release);
		});
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		semaphore.Post();
		waiter.join();
		if (!acquired.load(std::memory_order_acquire))
			return 6;
		auto moved_semaphore = std::move(semaphore);
		moved_semaphore.Post();
		if (!moved_semaphore.TryWait(1000))
			return 7;

		auto memory = darling::windows_host::SharedMemory::Create(L"darling-stage1-shm-" + suffix, 4096);
		if (memory.Size() != 4096 || memory.Data() == nullptr)
			return 8;
		std::memcpy(memory.Data(), "DARLING-WINDOWS", 16);
		const auto* text = static_cast<const char*>(memory.Data());
		std::cout << "WINDOWS_SEMAPHORE=PASS\n";
		std::cout << "WINDOWS_SHARED_MEMORY=" << text << "\n";
		auto moved_memory = std::move(memory);
		if (moved_memory.Size() != 4096 || moved_memory.Data() == nullptr)
			return 9;
		return std::strncmp(text, "DARLING-WINDOWS", 15) == 0 ? 0 : 10;
	} catch (const std::exception& error) {
		std::cerr << "SYNC_SMOKE_ERROR=" << error.what() << "\n";
		return 5;
	}
}
