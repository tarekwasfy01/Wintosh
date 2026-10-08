/*
 * Stage 2 Darwin clock boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_time.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
	const auto before = darling::windows_host::DarwinClock::MonotonicNanoseconds();
	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	const auto after = darling::windows_host::DarwinClock::MonotonicNanoseconds();
	const auto wall = darling::windows_host::DarwinClock::WallClockNanoseconds();
	if (before == 0 || after <= before || wall == 0) {
		std::cerr << "TIME_SMOKE_ERROR=clock values invalid\n";
		return 1;
	}
	std::cout << "DARWIN_SYSCALL_TIME=PASS\n";
	return 0;
}
