/* Wintosh Mach receive-trailer ABI layout smoke test. GPL-3.0-only. */
#include "darling_windows_mach.h"

#include <cstddef>
#include <iostream>

int main()
{
	if (sizeof(darling_mach_msg_trailer) != 8 ||
		offsetof(darling_mach_msg_seqno_trailer, msgh_seqno) != 8 ||
		sizeof(darling_mach_msg_seqno_trailer) != 12 ||
		sizeof(darling_mach_security_token) != 8 ||
		sizeof(darling_mach_audit_token) != 32 ||
		sizeof(darling_mach_msg_security_trailer) != 20 ||
		sizeof(darling_mach_msg_audit_trailer) != 52) {
		std::cerr << "MACH_TRAILER_ABI=FAIL\n";
		return 1;
	}
	std::cout << "MACH_TRAILER_ABI=PASS\n";
	return 0;
}
