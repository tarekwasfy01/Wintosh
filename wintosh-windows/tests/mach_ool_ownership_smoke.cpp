/* OOL ownership table smoke; GPL-3.0-only. */
#include "darling_windows_mach_ool.h"

#include <iostream>

int main()
{
	darling::windows_host::MachOolOwnershipTable table;
	int allocation = 0;
	int releases = 0;
	if (!table.Register(&allocation, sizeof(allocation), [&](void* value) {
		if (value == &allocation) ++releases;
	}) || table.Bytes(&allocation) != sizeof(allocation) ||
		!table.RetainQueue(&allocation) || !table.RetainReceiver(&allocation) ||
		!table.ReleaseQueue(&allocation) || table.Contains(&allocation) == false ||
		!table.ReleaseReceiver(&allocation) || table.Contains(&allocation) || releases != 1 ||
		table.ReleaseReceiver(&allocation)) {
		std::cerr << "MACH_OOL_OWNERSHIP=FAIL\n";
		return 1;
	}
	std::cout << "MACH_OOL_OWNERSHIP=PASS\n";
	return 0;
}
