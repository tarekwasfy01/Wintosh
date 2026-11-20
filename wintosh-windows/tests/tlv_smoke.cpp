/*
 * Stage 1 Windows Darwin-TLV descriptor and per-thread storage proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_thread.h"
#include "darling_windows_tlv.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace darling::windows_host;

void CountDestructor(void* argument)
{
	static_cast<std::atomic<int>*>(argument)->fetch_add(1, std::memory_order_relaxed);
}

int wmain()
{
	DarwinTLVDescriptor variables[3]{};
	variables[1].offset = 4;
	variables[2].offset = 4; // Two descriptors may name the same variable.
	const std::uint8_t template_data[8] = {0x42, 0, 0, 0, 0x24, 0, 0, 0};
	InitializeDarwinTLV(variables, 3, template_data, sizeof(template_data));
	auto* base = static_cast<std::uint8_t*>(variables[0].thunk(&variables[0]));
	auto* member = static_cast<std::uint8_t*>(variables[1].thunk(&variables[1]));
	if (!base || member != base + 4 || *member != 0x24 ||
		variables[2].thunk(&variables[2]) != member)
		return 5;
	*member = 0x99;
	std::atomic<bool> shared_isolated{false};
	auto shared_thread = Thread::Start([&] {
		auto* other_base = static_cast<std::uint8_t*>(variables[0].thunk(&variables[0]));
		auto* other_member = static_cast<std::uint8_t*>(variables[1].thunk(&variables[1]));
		shared_isolated = other_base && other_base != base &&
			other_member == other_base + 4 && *other_member == 0x24;
	});
	if (shared_thread.Join() != WAIT_OBJECT_0 || !shared_isolated ||
		variables[0].thunk(&variables[0]) != base || *member != 0x99)
		return 6;
	// Corrupted offsets must not resize the block and invalidate base/member.
	variables[2].offset = 8;
	if (ResolveDarwinTLV(&variables[2]) != nullptr || *member != 0x99)
		return 7;
	DestroyDarwinTLV(variables, 3);
	DarwinTLVDescriptor invalid{};
	invalid.offset = 1024 * 1024;
	try {
		InitializeDarwinTLV(&invalid, 1);
		return 8;
	} catch (const std::invalid_argument&) {
		if (invalid.thunk || invalid.key)
			return 9;
	}
	std::cout << "DARWIN_TLV_SHARED_BLOCK=PASS\n";
	std::cout << "DARWIN_TLV_STABLE_POINTERS=PASS\n";
	DarwinTLVDescriptor split_a{}, split_b{};
	split_a.offset = 1;
	split_b.offset = 5;
	const DarwinTLVDescriptorRange split_ranges[] = {{&split_a, 1}, {&split_b, 1}};
	InitializeDarwinTLV(split_ranges, 2, template_data, sizeof(template_data));
	auto* split_a_ptr = static_cast<std::uint8_t*>(split_a.thunk(&split_a));
	auto* split_b_ptr = static_cast<std::uint8_t*>(split_b.thunk(&split_b));
	if (!split_a_ptr || split_b.key != split_a.key || split_b_ptr != split_a_ptr + 4)
		return 10;
	DestroyDarwinTLV(&split_a, 1);
	DestroyDarwinTLV(&split_b, 1);
	std::cout << "DARWIN_TLV_MULTI_RANGE=PASS\n";
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
			*static_cast<std::uint8_t*>(other) = 0x01u;
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
