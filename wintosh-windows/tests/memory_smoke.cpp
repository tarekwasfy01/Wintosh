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
		const auto page_size = static_cast<std::size_t>(darling_windows_getpagesize());
		void* partial_mapping = darling_windows_mmap(nullptr, page_size * 3, 3,
			0x1000 | 2, -1, 0);
		if (partial_mapping == reinterpret_cast<void*>(-1) ||
			darling_windows_mprotect(partial_mapping, page_size * 3, 0) != 0) {
			if (partial_mapping != reinterpret_cast<void*>(-1))
				darling_windows_munmap(partial_mapping, page_size * 3);
			throw std::runtime_error("partial MAP_FIXED preparation failed");
		}
		void* fixed_replacement = darling_windows_mmap(
			static_cast<char*>(partial_mapping) + page_size, page_size, 3,
			0x1000 | 2 | 0x0010, -1, 0);
		const auto middle_unmap = fixed_replacement ==
			static_cast<char*>(partial_mapping) + page_size ? darling_windows_munmap(
				static_cast<char*>(partial_mapping) + page_size, page_size) : -1;
		const auto repeated_unmap = darling_windows_munmap(
			static_cast<char*>(partial_mapping) + page_size, page_size);
		void* hole_replacement = darling_windows_mmap(
			static_cast<char*>(partial_mapping) + page_size, page_size, 3,
			0x1000 | 2 | 0x0010, -1, 0);
		if (hole_replacement != reinterpret_cast<void*>(-1))
			static_cast<char*>(hole_replacement)[0] = 'H';
		const auto hole_unmap = hole_replacement == reinterpret_cast<void*>(-1) ? -1 :
			darling_windows_munmap(hole_replacement, page_size);
		if (partial_mapping == reinterpret_cast<void*>(-1) ||
			fixed_replacement != static_cast<char*>(partial_mapping) + page_size ||
			middle_unmap != 0 || repeated_unmap == 0 || hole_replacement !=
			static_cast<char*>(partial_mapping) + page_size || hole_unmap != 0 ||
			darling_windows_munmap(partial_mapping, page_size) != 0 ||
			darling_windows_munmap(static_cast<char*>(partial_mapping) + page_size * 2, page_size) != 0) {
			if (partial_mapping != reinterpret_cast<void*>(-1))
				darling_windows_munmap(partial_mapping, page_size * 3);
			throw std::runtime_error("partial anonymous munmap interval handling failed");
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
		constexpr std::int64_t offset_page = 4096;
		constexpr char offset_payload[] = "FILE";
		if (darling_windows_lseek(offset_descriptor, offset_page, 0) != offset_page ||
			darling_windows_write(offset_descriptor, offset_payload,
				sizeof(offset_payload) - 1) != static_cast<int>(sizeof(offset_payload) - 1)) {
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("offset mmap seed write failed");
		}
		void* private_mapping = darling_windows_mmap(nullptr, 4, 3, 0x0002,
			offset_descriptor, offset_page);
		if (private_mapping == reinterpret_cast<void*>(-1) ||
			std::memcmp(private_mapping, "FILE", 4) != 0) {
			if (private_mapping != reinterpret_cast<void*>(-1)) darling_windows_munmap(private_mapping, 4);
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("private offset mmap contents failed");
		}
		static_cast<char*>(private_mapping)[0] = 'P';
		const auto private_unmap_result = darling_windows_munmap(private_mapping, 4);
		if (private_unmap_result != 0) {
			std::cerr << "private unmap errno=" << *darling_windows_errno() << "\n";
			darling_windows_close(offset_descriptor);
			throw std::runtime_error("private offset mmap unmap failed");
		}
		void* shared_offset_mapping = darling_windows_mmap(nullptr, 4, 3, 0x0001,
			offset_descriptor, offset_page);
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
		const int subgranular_fixed_descriptor = darling_windows_open(file_name.c_str(), 0x0002, 0);
		void* fixed_seed_view = subgranular_fixed_descriptor < 0 ?
			reinterpret_cast<void*>(-1) : darling_windows_mmap(nullptr, 4096, 3,
			0x0001, subgranular_fixed_descriptor, offset_page);
		void* fixed_subgranular_target = fixed_seed_view == reinterpret_cast<void*>(-1) ?
			reinterpret_cast<void*>(-1) : static_cast<char*>(fixed_seed_view) + 4096;
		const auto fixed_seed_unmap = fixed_seed_view == reinterpret_cast<void*>(-1) ? -1 :
			darling_windows_munmap(fixed_seed_view, 4096);
		void* subgranular_fixed_view = fixed_seed_unmap != 0 ?
			reinterpret_cast<void*>(-1) : darling_windows_mmap(fixed_subgranular_target,
			4096, 3, 0x0001 | 0x0010, subgranular_fixed_descriptor, offset_page);
		if (subgranular_fixed_view != fixed_subgranular_target ||
			static_cast<char*>(subgranular_fixed_view)[0] != 'S' ||
			darling_windows_munmap(subgranular_fixed_view, 4096) != 0 ||
			darling_windows_close(subgranular_fixed_descriptor) != 0) {
			if (subgranular_fixed_descriptor >= 0)
				darling_windows_close(subgranular_fixed_descriptor);
			throw std::runtime_error("sub-granularity MAP_FIXED file mapping failed");
		}
		const int offset_verify_descriptor = darling_windows_open(file_name.c_str(), 0, 0);
		char offset_verify[4]{};
		const auto offset_seek = offset_verify_descriptor < 0 ? -1 :
			darling_windows_lseek(offset_verify_descriptor, offset_page, 0);
		const auto offset_verified = offset_seek != offset_page ? -1 :
			darling_windows_read(offset_verify_descriptor, offset_verify, sizeof(offset_verify));
		if (offset_verify_descriptor >= 0) darling_windows_close(offset_verify_descriptor);
		if (offset_verified != static_cast<int>(sizeof(offset_verify)) ||
			offset_verify[0] != 'S' || offset_verify[1] != 'I' ||
			offset_verify[2] != 'L' || offset_verify[3] != 'E') {
			throw std::runtime_error("offset mmap persistence/COW failed");
		}
		const int split_descriptor = darling_windows_open(file_name.c_str(), 0x0002, 0);
		if (split_descriptor < 0 ||
			darling_windows_lseek(split_descriptor, offset_page * 2, 0) != offset_page * 2 ||
			darling_windows_write(split_descriptor, "Z", 1) != 1) {
			if (split_descriptor >= 0) darling_windows_close(split_descriptor);
			throw std::runtime_error("partial file mmap seed failed");
		}
		constexpr std::size_t split_view_bytes = 4096 * 3;
		void* shared_split = darling_windows_mmap(nullptr, split_view_bytes, 3,
			0x0001, split_descriptor, 0);
		if (shared_split == reinterpret_cast<void*>(-1)) {
			darling_windows_close(split_descriptor);
			throw std::runtime_error("partial shared file mmap failed");
		}
		auto* shared_split_bytes = static_cast<char*>(shared_split);
		shared_split_bytes[0] = 'A';
		shared_split_bytes[4096] = 'B';
		shared_split_bytes[8192] = 'C';
		void* fixed_file_middle = darling_windows_mmap(shared_split_bytes + 4096,
			4096, 3, 0x0001 | 0x0010, split_descriptor, 0);
		if (fixed_file_middle != shared_split_bytes + 4096 ||
			shared_split_bytes[0] != 'A' || shared_split_bytes[4096] != 'A' ||
			shared_split_bytes[8192] != 'C' ||
			darling_windows_msync(shared_split, split_view_bytes, 16) != 0 ||
			darling_windows_munmap(shared_split_bytes + 4096, 4096) != 0 ||
			shared_split_bytes[0] != 'A' || shared_split_bytes[8192] != 'C' ||
			darling_windows_munmap(shared_split_bytes + 4096, 4096) == 0 ||
			darling_windows_munmap(shared_split_bytes, 4096) != 0 ||
			darling_windows_munmap(shared_split_bytes + 8192, 4096) != 0) {
			darling_windows_close(split_descriptor);
			throw std::runtime_error("partial shared file munmap interval handling failed");
		}
		void* private_split = darling_windows_mmap(nullptr, split_view_bytes, 3,
			0x0002, split_descriptor, 0);
		if (private_split == reinterpret_cast<void*>(-1)) {
			darling_windows_close(split_descriptor);
			throw std::runtime_error("partial private file mmap failed");
		}
		auto* private_split_bytes = static_cast<char*>(private_split);
		private_split_bytes[0] = 'P';
		private_split_bytes[8192] = 'Q';
		const auto private_middle_unmap = darling_windows_munmap(private_split_bytes + 4096, 4096);
		const char private_left_value = private_split_bytes[0];
		const char private_right_value = private_split_bytes[8192];
		const auto private_left_unmap = darling_windows_munmap(private_split_bytes, 4096);
		const auto private_right_unmap = darling_windows_munmap(private_split_bytes + 8192, 4096);
		if (private_middle_unmap != 0 || private_left_value != 'P' ||
			private_right_value != 'Q' || private_left_unmap != 0 ||
			private_right_unmap != 0) {
			darling_windows_close(split_descriptor);
			throw std::runtime_error("partial private file munmap did not preserve retained COW bytes");
		}
		void* split_verify = darling_windows_mmap(nullptr, split_view_bytes, 1,
			0x0001, split_descriptor, 0);
		if (split_verify == reinterpret_cast<void*>(-1) ||
			static_cast<char*>(split_verify)[0] != 'A' ||
			static_cast<char*>(split_verify)[4096] != 'B' ||
			static_cast<char*>(split_verify)[8192] != 'C' ||
			darling_windows_munmap(split_verify, split_view_bytes) != 0 ||
			darling_windows_close(split_descriptor) != 0) {
			throw std::runtime_error("partial private file munmap changed shared file bytes");
		}
		(void)std::filesystem::remove(file_path);
		std::cout << "DARWIN_SYSCALL_MEMORY=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "MEMORY_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
