/*
 * Stage 1 Windows host dynamic-library boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_library.h"

#include <iostream>
#include <utility>

int wmain()
{
	try {
		auto kernel = darling::windows_host::Library::Open(L"kernel32.dll");
		auto moved = std::move(kernel);
		if (kernel.Handle() != nullptr || moved.Handle() == nullptr)
			return 2;
		darling::windows_host::Library assigned =
			darling::windows_host::Library::Open(L"kernel32.dll");
		assigned = std::move(moved);
		if (moved.Handle() != nullptr || assigned.Handle() == nullptr)
			return 3;
		const auto symbol = assigned.Symbol("GetCurrentProcessId");
		const auto get_process_id = reinterpret_cast<DWORD(WINAPI*)()>(symbol);
		const auto pid = get_process_id();
		bool missing_symbol_rejected = false;
		try {
			(void)assigned.Symbol("DarlingDefinitelyMissingSymbol");
		} catch (const std::exception&) {
			missing_symbol_rejected = true;
		}
		if (!missing_symbol_rejected)
			return 4;
		std::cout << "WINDOWS_LIBRARY=PASS\n";
		std::cout << "WINDOWS_SYMBOL=GetCurrentProcessId\n";
		std::cout << "WINDOWS_PID=" << pid << "\n";
		return pid == 0 ? 5 : 0;
	} catch (const std::exception& error) {
		std::cerr << "LIBRARY_SMOKE_ERROR=" << error.what() << "\n";
		return 3;
	}
}
