/*
 * Stage 1 Windows Darwin-TLV descriptor and per-thread storage proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_thread.h"
#include "darling_windows_tlv.h"

#include <atomic>
#include <cstdint>
#include <iostream>

using namespace darling::windows_host;

void CountDestructor(void* argument)
{
	static_cast<std::atomic<int>*>(argument)->fetch_add(1, std::memory_order_relaxed);
}

int wmain()
{
	DarwinTLVDescriptor descriptor{};
	const std::uint8_t initial = 0x42u;
	InitializeDarwinTLV(&descriptor, 1, &initial, sizeof(initial));
	void* first = descriptor.thunk(&descriptor);
	if (first == nullptr || *static_cast<std::uint8_t*>(first) != initial)
		return 2;
	*static_cast<std::uint8_t*>(first) = 0x78u;
	std::atomic<bool> isolated{false};
	std::atomic<int> destructor_count{0};
	std::atomic<bool> nonnull{false};
	std::atomic<bool> distinct{false};
	std::atomic<std::uint32_t> other_value{0xffffffffu};
	auto thread = Thread::Start([&] {
		darling_windows_tlv_atexit(&CountDestructor, &destructor_count);
		darling_windows_cxa_thread_atexit(&CountDestructor, &destructor_count);
		void* other = descriptor.thunk(&descriptor);
		nonnull.store(other != nullptr, std::memory_order_relaxed);
		distinct.store(other != first, std::memory_order_relaxed);
		if (other != nullptr)
			other_value.store(*static_cast<std::uint8_t*>(other), std::memory_order_relaxed);
		isolated.store(other != nullptr && other != first &&
			(other_value.load(std::memory_order_relaxed) == initial), std::memory_order_release);
		if (other != nullptr)
			*static_cast<std::uint32_t*>(other) = 0xabcdef01u;
	});
	const auto wait_result = thread.Join();
	const auto exit_code = thread.ExitCode();
	if (wait_result != WAIT_OBJECT_0 || exit_code != 0 ||
		!isolated.load(std::memory_order_acquire) ||
		*static_cast<std::uint8_t*>(first) != 0x78u ||
		destructor_count.load(std::memory_order_relaxed) != 2) {
		std::cerr << "TLV_WAIT=" << wait_result << " TLV_EXIT=" << exit_code
			<< " TLV_ISOLATED=" << isolated.load(std::memory_order_acquire)
			<< " TLV_NONNULL=" << nonnull.load(std::memory_order_relaxed)
			<< " TLV_DISTINCT=" << distinct.load(std::memory_order_relaxed)
			<< " TLV_OTHER=" << other_value.load(std::memory_order_relaxed)
			<< " TLV_DESTRUCTORS=" << destructor_count.load(std::memory_order_relaxed)
			<< " TLV_MAIN=" << static_cast<unsigned>(*static_cast<std::uint8_t*>(first)) << "\n";
		return 3;
	}
	DestroyDarwinTLV(&descriptor, 1);
	if (descriptor.thunk != nullptr || descriptor.key != 0 ||
		descriptor.offset != 0 || ResolveDarwinTLV(&descriptor) != nullptr) {
		std::cerr << "TLV_DESTROY=FAIL\n";
		return 4;
	}
	std::cout << "DARWIN_TLV_DESCRIPTOR=PASS\n";
	std::cout << "DARWIN_TLV_THREAD_ISOLATION=PASS\n";
	std::cout << "DARWIN_TLV_DESTRUCTOR=PASS\n";
	std::cout << "DARWIN_TLV_DESTROY=PASS\n";
	return 0;
}
