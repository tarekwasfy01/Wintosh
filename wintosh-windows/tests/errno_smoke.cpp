/*
 * Stage 2 Darwin errno boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_errno.h"
#include "darling_windows_syscalls.h"

#include <iostream>

int main()
{
	darling::windows_host::DarwinErrno::Clear();
	try {
		darling::windows_host::DarwinSyscalls syscalls;
		(void)syscalls.OpenRead(L"C:\\darling-path-that-does-not-exist\\missing");
		std::cerr << "ERRNO_SMOKE_ERROR=missing file unexpectedly opened\n";
		return 1;
	} catch (const std::exception&) {
		if (darling::windows_host::DarwinErrno::Get() != 2) {
			std::cerr << "ERRNO_SMOKE_ERROR=unexpected errno\n";
			return 2;
		}
	}
	std::cout << "DARWIN_ERRNO=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NOT_FOUND);
	if (darling::windows_host::DarwinErrno::Get() != 2) {
		std::cerr << "DARWIN_ERRNO_NOT_FOUND=FAIL\n";
		return 36;
	}
	std::cout << "DARWIN_ERRNO_NOT_FOUND=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_BAD_EXE_FORMAT);
	if (darling::windows_host::DarwinErrno::Get() != 8) return 37;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_EXE_MACHINE_TYPE_MISMATCH);
	if (darling::windows_host::DarwinErrno::Get() != 8) return 38;
	std::cout << "DARWIN_ERRNO_EXEC_FORMAT=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_MOD_NOT_FOUND);
	if (darling::windows_host::DarwinErrno::Get() != 2) return 39;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_PROC_NOT_FOUND);
	if (darling::windows_host::DarwinErrno::Get() != 2) return 40;
	std::cout << "DARWIN_ERRNO_LOADER=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_DLL_NOT_FOUND);
	if (darling::windows_host::DarwinErrno::Get() != 2) return 43;
	std::cout << "DARWIN_ERRNO_DLL_NOT_FOUND=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_DLL_INIT_FAILED);
	if (darling::windows_host::DarwinErrno::Get() != 80) return 41;
	std::cout << "DARWIN_ERRNO_LOADER_INIT=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_INVALID_PARAMETER);
	if (darling::windows_host::DarwinErrno::Get() != 22) {
		std::cerr << "DARWIN_ERRNO_INVALID_PARAMETER=FAIL\n";
		return 3;
	}
	std::cout << "DARWIN_ERRNO_INVALID_PARAMETER=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_OPERATION_ABORTED);
	if (darling::windows_host::DarwinErrno::Get() != 4) {
		std::cerr << "DARWIN_ERRNO_ABORTED=FAIL\n";
		return 4;
	}
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_IO_PENDING);
	if (darling::windows_host::DarwinErrno::Get() != 35) {
		std::cerr << "DARWIN_ERRNO_PENDING=FAIL\n";
		return 5;
	}
	std::cout << "DARWIN_ERRNO_ABORTED=PASS\n";
	std::cout << "DARWIN_ERRNO_PENDING=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_BROKEN_PIPE);
	if (darling::windows_host::DarwinErrno::Get() != 32) {
		std::cerr << "DARWIN_ERRNO_BROKEN_PIPE=FAIL\n";
		return 6;
	}
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NO_DATA);
	if (darling::windows_host::DarwinErrno::Get() != 32) {
		std::cerr << "DARWIN_ERRNO_NO_DATA=FAIL\n";
		return 7;
	}
	std::cout << "DARWIN_ERRNO_BROKEN_PIPE=PASS\n";
	std::cout << "DARWIN_ERRNO_NO_DATA=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_DIRECTORY);
	if (darling::windows_host::DarwinErrno::Get() != 20) return 8;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_DIR_NOT_EMPTY);
	if (darling::windows_host::DarwinErrno::Get() != 66) return 9;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_FILE_EXISTS);
	if (darling::windows_host::DarwinErrno::Get() != 17) return 10;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_DISK_FULL);
	if (darling::windows_host::DarwinErrno::Get() != 28) return 11;
	std::cout << "DARWIN_ERRNO_FILESYSTEM=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_WRITE_PROTECT);
	if (darling::windows_host::DarwinErrno::Get() != 30) return 12;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_TOO_MANY_OPEN_FILES);
	if (darling::windows_host::DarwinErrno::Get() != 24) return 13;
	std::cout << "DARWIN_ERRNO_PROTECTION_LIMITS=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_SHARING_VIOLATION);
	if (darling::windows_host::DarwinErrno::Get() != 13) return 14;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_LOCK_VIOLATION);
	if (darling::windows_host::DarwinErrno::Get() != 13) return 15;
	std::cout << "DARWIN_ERRNO_LOCKING=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_BAD_NETPATH);
	if (darling::windows_host::DarwinErrno::Get() != 2) return 16;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_BAD_NET_NAME);
	if (darling::windows_host::DarwinErrno::Get() != 2) return 17;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NETWORK_UNREACHABLE);
	if (darling::windows_host::DarwinErrno::Get() != 51) return 18;
	std::cout << "DARWIN_ERRNO_NETWORK_PATH=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_HOST_UNREACHABLE);
	if (darling::windows_host::DarwinErrno::Get() != 65) return 19;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NETWORK_BUSY);
	if (darling::windows_host::DarwinErrno::Get() != 50) return 20;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NETWORK_ACCESS_DENIED);
	if (darling::windows_host::DarwinErrno::Get() != 13) return 21;
	std::cout << "DARWIN_ERRNO_NETWORK_STATE=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_CONNECTION_ABORTED);
	if (darling::windows_host::DarwinErrno::Get() != 53) return 22;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_CONNECTION_REFUSED);
	if (darling::windows_host::DarwinErrno::Get() != 61) return 23;
	std::cout << "DARWIN_ERRNO_CONNECTION=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_CANCELLED);
	if (darling::windows_host::DarwinErrno::Get() != 89) return 25;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_TIMEOUT);
	if (darling::windows_host::DarwinErrno::Get() != 60) return 26;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_SEM_TIMEOUT);
	if (darling::windows_host::DarwinErrno::Get() != 60) return 27;
	std::cout << "DARWIN_ERRNO_CANCEL_TIMEOUT=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_TOO_MANY_LINKS);
	if (darling::windows_host::DarwinErrno::Get() != 62) return 28;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_INVALID_DRIVE);
	if (darling::windows_host::DarwinErrno::Get() != 19) return 29;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NOT_READY);
	if (darling::windows_host::DarwinErrno::Get() != 6) return 30;
	std::cout << "DARWIN_ERRNO_DEVICE_PATH=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_FILE_TOO_LARGE);
	if (darling::windows_host::DarwinErrno::Get() != 27) return 31;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_BUFFER_OVERFLOW);
	if (darling::windows_host::DarwinErrno::Get() != 63) return 32;
	std::cout << "DARWIN_ERRNO_SIZE_LIMITS=PASS\n";
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_NOT_SAME_DEVICE);
	if (darling::windows_host::DarwinErrno::Get() != 18) return 33;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_CANNOT_MAKE);
	if (darling::windows_host::DarwinErrno::Get() != 13) return 34;
	darling::windows_host::DarwinErrno::SetFromWin32(ERROR_PRIVILEGE_NOT_HELD);
	if (darling::windows_host::DarwinErrno::Get() != 1) return 35;
	std::cout << "DARWIN_ERRNO_CROSS_DEVICE_PRIVILEGE=PASS\n";
	return 0;
}
