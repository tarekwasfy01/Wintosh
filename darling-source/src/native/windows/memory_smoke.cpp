/*
 * Stage 2 Darwin memory syscall boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_memory.h"
#include "darling_windows_stdio.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
	try {
		using darling::windows_host::DarwinMemory;
		using darling::windows_host::DarwinMemoryProtection;
		void* address = DarwinMemory::Mmap(4096,
			DarwinMemoryProtection::Read | DarwinMemoryProtection::Write);
		const char payload[] = "DARLING-MMAP";
		std::memcpy(address, payload, sizeof(payload));
		DarwinMemory::Mprotect(address, 4096, DarwinMemoryProtection::Read);
		DarwinMemory::Munmap(address, 4096);
		void* c_address = darling_windows_mmap(nullptr, 4096, 3, 0x1000 | 2, -1, 0);
		if (c_address == reinterpret_cast<void*>(-1) ||
			darling_windows_madvise(c_address, 4096, 2) != 0 ||
			darling_windows_madvise(c_address, 4096, 999) == 0 ||
			darling_windows_msync(c_address, 4096, 16) != 0 ||
			darling_windows_msync(c_address, 4096, 1 | 16) == 0 ||
			darling_windows_msync(reinterpret_cast<void*>(0x1), 1, 16) == 0 ||
			darling_windows_mprotect(c_address, 4096, 1) != 0 ||
			darling_windows_munmap(c_address, 4096) != 0 ||
			darling_windows_host_symbol("_mmap") == 0 ||
			darling_windows_host_symbol("_mprotect") == 0 ||
			darling_windows_host_symbol("_munmap") == 0 ||
			darling_windows_host_symbol("_madvise") == 0 ||
			darling_windows_host_symbol("_msync") == 0) {
			throw std::runtime_error("Darwin mmap symbol ABI mismatch");
		}
		void* fixed_probe = darling_windows_mmap(nullptr, 65536, 3,
			0x1000 | 2, -1, 0);
		if (fixed_probe == reinterpret_cast<void*>(-1)) {
			throw std::runtime_error("MAP_FIXED probe allocation failed");
		}
		void* fixed_mapping = darling_windows_mmap(fixed_probe, 4096, 3,
			0x1000 | 2 | 0x0010, -1, 0);
		if (fixed_mapping != fixed_probe || darling_windows_munmap(fixed_mapping, 4096) != 0) {
			if (fixed_mapping != reinterpret_cast<void*>(-1)) darling_windows_munmap(fixed_mapping, 4096);
			else darling_windows_munmap(fixed_probe, 65536);
			throw std::runtime_error("MAP_FIXED address placement failed");
		}
		const auto file_path = std::filesystem::temp_directory_path() /
			"darling_windows_mmap_smoke.bin";
		const auto file_name = file_path.string();
		(void)std::filesystem::remove(file_path);
		const int file_descriptor = darling_windows_open(file_name.c_str(), 0x0202, 0600);
		if (file_descriptor < 0) {
			throw std::runtime_error("file-backed mmap open failed");
		}
		const char file_payload[] = "MMAP-FILE";
		if (darling_windows_write(file_descriptor, file_payload, sizeof(file_payload)) !=
			static_cast<int>(sizeof(file_payload))) {
			darling_windows_close(file_descriptor);
			throw std::runtime_error("file-backed mmap seed write failed");
		}
		void* file_mapping = darling_windows_mmap(nullptr, 4096, 3, 0x0001,
			file_descriptor, 0);
		if (file_mapping == reinterpret_cast<void*>(-1) ||
			std::memcmp(file_mapping, file_payload, sizeof(file_payload)) != 0) {
			darling_windows_close(file_descriptor);
			(void)std::filesystem::remove(file_path);
			throw std::runtime_error("file-backed mmap contents failed");
		}
		static_cast<char*>(file_mapping)[0] = 'X';
		if (darling_windows_msync(static_cast<char*>(file_mapping) + 1, 1, 16) != 0 ||
			darling_windows_msync(file_mapping, 4096, 1 | 16) == 0 ||
			darling_windows_munmap(file_mapping, 4096) != 0 ||
			darling_windows_close(file_descriptor) != 0) {
			(void)std::filesystem::remove(file_path);
			throw std::runtime_error("file-backed mmap unmap failed");
		}
		const int verify_descriptor = darling_windows_open(file_name.c_str(), 0, 0);
		char verify[sizeof(file_payload)]{};
		const auto verified = verify_descriptor < 0 ? -1 :
			darling_windows_read(verify_descriptor, verify, sizeof(verify));
		if (verify_descriptor >= 0) darling_windows_close(verify_descriptor);
		if (verified != static_cast<int>(sizeof(verify)) || verify[0] != 'X' ||
			std::memcmp(verify + 1, file_payload + 1, sizeof(verify) - 1) != 0) {
			throw std::runtime_error("file-backed mmap persistence failed");
		}
		const int offset_descriptor = darling_windows_open(file_name.c_str(), 0x0002, 0);
		if (offset_descriptor < 0) {
			throw std::runtime_error("offset mmap reopen failed");
		}
		void* private_mapping = darling_windows_mmap(nullptr, 4, 3, 0x0002,
			offset_descriptor, 5);
		if (private_mapping == reinterpret_cast<void*>(-1) ||
			std::memcmp(private_mapping, "FILE", 4) != 0) {
			if (private_mapping != reinterpret_cast<void*>(-1)) darling_windows_munmap(private_mapping, 4);
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("private offset mmap contents failed");
		}
		static_cast<char*>(private_mapping)[0] = 'P';
		if (darling_windows_munmap(private_mapping, 4) != 0) {
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("private offset mmap unmap failed");
		}
		void* shared_offset_mapping = darling_windows_mmap(nullptr, 4, 3, 0x0001,
			offset_descriptor, 5);
		if (shared_offset_mapping == reinterpret_cast<void*>(-1) ||
			std::memcmp(shared_offset_mapping, "FILE", 4) != 0) {
			if (shared_offset_mapping != reinterpret_cast<void*>(-1)) darling_windows_munmap(shared_offset_mapping, 4);
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("shared offset mmap COW boundary failed");
		}
		static_cast<char*>(shared_offset_mapping)[0] = 'S';
		if (darling_windows_munmap(shared_offset_mapping, 4) != 0 ||
			darling_windows_close(offset_descriptor) != 0) {
			throw std::runtime_error("shared offset mmap unmap failed");
		}
		const int fixed_file_descriptor = darling_windows_open(file_name.c_str(), 0x0002, 0);
		if (fixed_file_descriptor < 0) {
			throw std::runtime_error("fixed file mmap reopen failed");
		}
		void* first_file_mapping = darling_windows_mmap(nullptr, 4096, 3, 0x0001,
			fixed_file_descriptor, 0);
		void* replacement_file_mapping = first_file_mapping == reinterpret_cast<void*>(-1) ?
			reinterpret_cast<void*>(-1) : darling_windows_mmap(first_file_mapping, 4096, 3,
			0x0001 | 0x0010, fixed_file_descriptor, 0);
		if (replacement_file_mapping != first_file_mapping ||
			darling_windows_munmap(replacement_file_mapping, 4096) != 0 ||
			darling_windows_close(fixed_file_descriptor) != 0) {
			if (first_file_mapping != reinterpret_cast<void*>(-1) &&
				replacement_file_mapping == reinterpret_cast<void*>(-1)) {
				darling_windows_munmap(first_file_mapping, 4096);
			}
			if (fixed_file_descriptor >= 0) darling_windows_close(fixed_file_descriptor);
			throw std::runtime_error("fixed file mmap replacement failed");
		}
		const int offset_verify_descriptor = darling_windows_open(file_name.c_str(), 0, 0);
		char offset_verify[sizeof(file_payload)]{};
		const auto offset_verified = offset_verify_descriptor < 0 ? -1 :
			darling_windows_read(offset_verify_descriptor, offset_verify, sizeof(offset_verify));
		if (offset_verify_descriptor >= 0) darling_windows_close(offset_verify_descriptor);
		(void)std::filesystem::remove(file_path);
		if (offset_verified != static_cast<int>(sizeof(offset_verify)) ||
			offset_verify[5] != 'S' || offset_verify[0] != 'X') {
			throw std::runtime_error("offset mmap persistence/COW failed");
		}
		std::cout << "DARWIN_SYSCALL_MEMORY=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "MEMORY_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
