#include "darling_windows_stdio.h"

#include <iostream>
#include <atomic>
#include <thread>

int main()
{
	void* mutex = nullptr;
	void* spin = nullptr;
	void* rwlock = nullptr;
	void* barrier = nullptr;
	const bool mutex_ok = darling_windows_pthread_mutex_init(&mutex, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&mutex) == 0 &&
		darling_windows_pthread_mutex_trylock(&mutex) == 16 &&
		darling_windows_pthread_mutex_unlock(&mutex) == 0 &&
		darling_windows_pthread_mutex_destroy(&mutex) == 0;
	const bool spin_ok = darling_windows_pthread_spin_init(&spin, 0) == 0 &&
		darling_windows_pthread_spin_lock(&spin) == 0 &&
		darling_windows_pthread_spin_trylock(&spin) == 16 &&
		darling_windows_pthread_spin_unlock(&spin) == 0 &&
		darling_windows_pthread_spin_destroy(&spin) == 0;
	const bool rwlock_ok = darling_windows_pthread_rwlock_init(&rwlock, nullptr) == 0 &&
		darling_windows_pthread_rwlock_rdlock(&rwlock) == 0 &&
		darling_windows_pthread_rwlock_unlock(&rwlock) == 0 &&
		darling_windows_pthread_rwlock_wrlock(&rwlock) == 0 &&
		darling_windows_pthread_rwlock_unlock(&rwlock) == 0 &&
		darling_windows_pthread_rwlock_destroy(&rwlock) == 0;
	const bool barrier_init_ok = darling_windows_pthread_barrier_init(&barrier, nullptr, 2) == 0;
	int barrier_results[2]{0, 0};
	std::thread first([&]() { barrier_results[0] = darling_windows_pthread_barrier_wait(&barrier); });
	std::thread second([&]() { barrier_results[1] = darling_windows_pthread_barrier_wait(&barrier); });
	first.join();
	second.join();
	const bool barrier_ok = barrier_init_ok &&
		((barrier_results[0] == -1 && barrier_results[1] == 0) ||
		 (barrier_results[0] == 0 && barrier_results[1] == -1)) &&
		darling_windows_pthread_barrier_destroy(&barrier) == 0;
	void* condition = nullptr;
	void* condition_mutex = nullptr;
	std::atomic_bool condition_ready{false};
	const bool condition_initialized =
		darling_windows_pthread_cond_init(&condition, nullptr) == 0 &&
		darling_windows_pthread_mutex_init(&condition_mutex, nullptr) == 0 &&
		darling_windows_pthread_mutex_lock(&condition_mutex) == 0;
	std::thread condition_worker([&]() {
		darling_windows_pthread_mutex_lock(&condition_mutex);
	condition_ready.store(true, std::memory_order_release);
		darling_windows_pthread_cond_signal(&condition);
		darling_windows_pthread_mutex_unlock(&condition_mutex);
	});
	const bool condition_ok = condition_initialized &&
		darling_windows_pthread_cond_wait(&condition, &condition_mutex) == 0 &&
		condition_ready.load(std::memory_order_acquire) &&
		darling_windows_pthread_mutex_unlock(&condition_mutex) == 0;
	condition_worker.join();
	const bool condition_cleanup_ok =
		darling_windows_pthread_mutex_destroy(&condition_mutex) == 0 &&
		darling_windows_pthread_cond_destroy(&condition) == 0;
	if (!mutex_ok || !spin_ok || !rwlock_ok || !barrier_ok || !condition_ok ||
		!condition_cleanup_ok) return 1;
	std::cout << "DARWIN_PTHREAD_SYNC_SMOKE=PASS\n";
	return 0;
}
