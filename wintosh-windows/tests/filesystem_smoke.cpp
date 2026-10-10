/*
 * Stage 2 Darwin filesystem boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_filesystem.h"
#include "darling_windows_stdio.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool SameWindowsPath(const std::filesystem::path& left,
	const std::filesystem::path& right)
{
	std::error_code error;
	return std::filesystem::equivalent(left, right, error) && !error;
}

}

int wmain()
{
	try {
		wchar_t temp_path[MAX_PATH]{};
		const DWORD length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) {
			return 2;
		}
		const auto directory = std::filesystem::path(temp_path) / L"darling-directory-smoke";
		std::filesystem::remove_all(directory);
		darling::windows_host::DarwinFilesystem::MakeDirectory(directory);
		const auto nested = directory / L"entry.txt";
		{
			std::ofstream output(nested, std::ios::binary);
			output << "STAT";
		}
		const auto info = darling::windows_host::DarwinFilesystem::Stat(nested);
		if (info.directory || info.size != 4 || info.modification_time_ns == 0) {
			throw std::runtime_error("unexpected stat result");
		}
		const auto native_path = nested.string();
		const auto native_directory = directory.string();
		darling_darwin_stat path_stat{};
		if (darling_windows_stat(native_path.c_str(), &path_stat) != 0 ||
			path_stat.st_size != 4 || path_stat.st_mtimespec.tv_sec == 0) {
			throw std::runtime_error("stat ABI mismatch");
		}
		darling_darwin_stat lpath_stat{};
		if (darling_windows_lstat(native_path.c_str(), &lpath_stat) != 0 ||
			lpath_stat.st_size != path_stat.st_size) {
			throw std::runtime_error("lstat ABI mismatch");
		}
		const darling_timespec requested_times[2]{{1700000000, 123456789},
			{1700000001, 987654321}};
		if (darling_windows_utimensat(-2, native_path.c_str(), requested_times, 0) != 0 ||
			darling_windows_stat(native_path.c_str(), &lpath_stat) != 0 ||
			lpath_stat.st_atimespec.tv_sec != 1700000000 ||
			lpath_stat.st_atimespec.tv_nsec != 123456700 ||
			lpath_stat.st_mtimespec.tv_sec != 1700000001 ||
			lpath_stat.st_mtimespec.tv_nsec != 987654300) {
			throw std::runtime_error("utimensat timestamp mapping mismatch");
		}
		const int futimens_descriptor = darling_windows_open(native_path.c_str(), 0x0002, 0);
		const darling_timespec relative_times[2]{{0x3fffffff, 0}, {0x3ffffffe, 0}};
		if (futimens_descriptor < 0 || darling_windows_futimens(futimens_descriptor,
			relative_times) != 0 || darling_windows_close(futimens_descriptor) != 0 ||
			darling_windows_host_symbol("_utimensat") == 0 ||
			darling_windows_host_symbol("_futimens") == 0) {
			if (futimens_descriptor >= 0) darling_windows_close(futimens_descriptor);
			throw std::runtime_error("futimens ABI mismatch");
		}
		const int chmod_descriptor = darling_windows_open(native_path.c_str(), 0x0000, 0);
		const int chmod_result = darling_windows_chmod(native_path.c_str(), 0400);
		const bool readonly_result = darling_windows_stat(native_path.c_str(), &lpath_stat) == 0 &&
			(lpath_stat.st_mode & 0222) == 0;
		const int fchmod_result = darling_windows_fchmod(chmod_descriptor, 0600);
		darling_windows_close(chmod_descriptor);
		if (chmod_descriptor < 0 || chmod_result != 0 || !readonly_result ||
			fchmod_result != 0 || darling_windows_chmod(native_path.c_str(), 0600) != 0 ||
			darling_windows_host_symbol("_chmod") == 0 ||
			darling_windows_host_symbol("_fchmod") == 0) {
			throw std::runtime_error("chmod readonly mapping mismatch");
		}
		const auto append_path = directory / L"append.txt";
		const auto native_append_path = append_path.string();
		const int create_descriptor = darling_windows_open(
			native_append_path.c_str(), 0x0601, 0600);
		if (create_descriptor < 0 || darling_windows_write(create_descriptor, "A", 1) != 1 ||
			darling_windows_close(create_descriptor) != 0) {
			throw std::runtime_error("open create/truncate failed");
		}
		const int append_descriptor = darling_windows_open(
			native_append_path.c_str(), 0x0209, 0600);
		if (append_descriptor < 0 || darling_windows_write(append_descriptor, "B", 1) != 1 ||
			darling_windows_close(append_descriptor) != 0) {
			throw std::runtime_error("open append failed");
		}
		if (darling_windows_open(native_append_path.c_str(), 0x0A01, 0600) >= 0) {
			throw std::runtime_error("open exclusive unexpectedly succeeded");
		}
		const int verify_descriptor = darling_windows_open(native_append_path.c_str(), 0x0002, 0);
		char append_contents[3]{};
		if (verify_descriptor < 0 || darling_windows_read(verify_descriptor,
			append_contents, 2) != 2 || std::string(append_contents, 2) != "AB") {
			throw std::runtime_error("open append verification failed");
		}
	if (darling_windows_lseek(verify_descriptor, 0, 1) != 2) {
		throw std::runtime_error("initial file offset mismatch");
	}
	char positioned_byte = 0;
	if (darling_windows_pread(verify_descriptor, &positioned_byte, 1, 1) != 1 ||
		positioned_byte != 'B' || darling_windows_lseek(verify_descriptor, 0, 1) != 2) {
		throw std::runtime_error("pread did not preserve file offset");
	}
	const char replacement = 'Z';
	if (darling_windows_pwrite(verify_descriptor, &replacement, 1, 0) != 1 ||
		darling_windows_lseek(verify_descriptor, 0, 1) != 2 ||
		darling_windows_host_symbol("_pread") == 0 ||
		darling_windows_host_symbol("_pwrite") == 0) {
		throw std::runtime_error("pwrite did not preserve file offset");
	}
	char positioned_vectors[2]{};
	darling_iovec read_vectors[2]{{positioned_vectors, 1},
		{positioned_vectors + 1, 1}};
	if (darling_windows_preadv(verify_descriptor, read_vectors, 2, 0) != 2 ||
		std::string(positioned_vectors, 2) != "ZB" ||
		darling_windows_lseek(verify_descriptor, 0, 1) != 2 ||
		darling_windows_host_symbol("_preadv") == 0 ||
		darling_windows_host_symbol("_pwritev") == 0) {
		throw std::runtime_error("preadv did not preserve file offset");
	}
	char vector_left = 'Y';
	char vector_right = 'C';
	darling_iovec write_vectors[2]{{&vector_left, 1}, {&vector_right, 1}};
		if (darling_windows_pwritev(verify_descriptor, write_vectors, 2, 0) != 2 ||
			darling_windows_lseek(verify_descriptor, 0, 1) != 2) {
		throw std::runtime_error("pwritev did not preserve file offset");
	}
	if (darling_windows_fsync(verify_descriptor) != 0 ||
		darling_windows_fdatasync(verify_descriptor) != 0 ||
		darling_windows_host_symbol("_fsync") == 0 ||
		darling_windows_host_symbol("_fdatasync") == 0) {
		throw std::runtime_error("file synchronization ABI mismatch");
	}
	const int lock_descriptor = darling_windows_open(native_append_path.c_str(), 0x0002, 0);
	if (lock_descriptor < 0 || darling_windows_flock(verify_descriptor, 2) != 0 ||
		darling_windows_flock(lock_descriptor, 2 | 4) == 0 ||
		darling_windows_host_symbol("_flock") == 0 ||
		darling_windows_flock(verify_descriptor, 8) != 0 ||
		darling_windows_flock(lock_descriptor, 1 | 4) != 0 ||
		darling_windows_flock(lock_descriptor, 8) != 0 ||
		darling_windows_close(lock_descriptor) != 0) {
		if (lock_descriptor >= 0) darling_windows_close(lock_descriptor);
		throw std::runtime_error("flock semantics mismatch");
	}
		if (darling_windows_fcntl(verify_descriptor, 2, 1) != 0 ||
			darling_windows_fcntl(verify_descriptor, 1) != 1 ||
			darling_windows_fcntl(verify_descriptor, 2, 0) != 0 ||
			darling_windows_fcntl(verify_descriptor, 4, 0x0008) != 0 ||
			(darling_windows_fcntl(verify_descriptor, 3) & 0x0008) == 0 ||
			darling_windows_fcntl(verify_descriptor, 4, 0) != 0) {
			throw std::runtime_error("fcntl descriptor state mismatch");
		}
		const int duplicate = darling_windows_fcntl(verify_descriptor, 0, 30);
		if (duplicate < 30 || darling_windows_fcntl(duplicate, 1) != 0 ||
			darling_windows_close(duplicate) != 0) {
			throw std::runtime_error("F_DUPFD descriptor semantics mismatch");
		}
		const int cloexec_duplicate = darling_windows_fcntl(
			verify_descriptor, 1030, 30);
		if (cloexec_duplicate < 30 || darling_windows_fcntl(cloexec_duplicate, 1) != 1 ||
			darling_windows_close(cloexec_duplicate) != 0) {
			throw std::runtime_error("F_DUPFD_CLOEXEC descriptor semantics mismatch");
		}
		const int darwin_cloexec_duplicate = darling_windows_fcntl(
			verify_descriptor, 67, 30);
		if (darwin_cloexec_duplicate < 30 || darling_windows_fcntl(
			darwin_cloexec_duplicate, 1) != 1 || darling_windows_close(
			darwin_cloexec_duplicate) != 0) {
			throw std::runtime_error("Darwin F_DUPFD_CLOEXEC descriptor semantics mismatch");
		}
	if (darling_windows_lseek(verify_descriptor, 0, 0) != 0 ||
		darling_windows_read(verify_descriptor, append_contents, 2) != 2 ||
		std::string(append_contents, 2) != "YC" ||
		darling_windows_close(verify_descriptor) != 0) {
			throw std::runtime_error("verify descriptor cleanup failed");
		}
		darling_darwin_stat truncated_stat{};
		if (darling_windows_truncate(native_append_path.c_str(), 1) != 0 ||
			darling_windows_stat(native_append_path.c_str(), &truncated_stat) != 0 ||
			truncated_stat.st_size != 1 ||
			darling_windows_host_symbol("_truncate") == 0) {
			throw std::runtime_error("path truncate ABI mismatch");
		}
		if (!DeleteFileW(append_path.c_str())) {
			throw std::runtime_error("append-file cleanup failed");
		}
		const int directory_only_descriptor = darling_windows_open(
			native_directory.c_str(), 0x00100000, 0);
		if (directory_only_descriptor < 0 ||
			darling_windows_close(directory_only_descriptor) != 0) {
			throw std::runtime_error("O_DIRECTORY failed on directory");
		}
		if (darling_windows_open(native_path.c_str(), 0x00100000, 0) >= 0) {
			throw std::runtime_error("O_DIRECTORY accepted regular file");
		}
		const int descriptor = darling_windows_open(
			native_path.c_str(), 0, 0);
		if (descriptor < 0) {
			throw std::runtime_error("open for fstat failed");
		}
		darling_darwin_stat descriptor_stat{};
		const int fstat_result = darling_windows_fstat(descriptor, &descriptor_stat);
		darling_darwin_statfs path_statfs{};
		darling_darwin_statfs descriptor_statfs{};
		const int statfs_result = darling_windows_statfs(native_path.c_str(), &path_statfs);
		const int fstatfs_result = darling_windows_fstatfs(descriptor, &descriptor_statfs);
		darling_windows_close(descriptor);
		if (fstat_result != 0 || descriptor_stat.st_size != 4 || statfs_result != 0 ||
			fstatfs_result != 0 || path_statfs.f_bsize != 4096 ||
			path_statfs.f_blocks == 0 || path_statfs.f_fstypename[0] == '\0' ||
			descriptor_statfs.f_fsid != path_statfs.f_fsid) {
			throw std::runtime_error("fstat ABI mismatch");
		}
		darling_darwin_stat at_stat{};
		if (darling_windows_fstatat(-2, native_path.c_str(), &at_stat, 0) != 0 ||
			at_stat.st_size != 4) {
			throw std::runtime_error("fstatat ABI mismatch");
		}
		const int directory_descriptor = darling_windows_open(
			native_directory.c_str(), 0, 0);
		if (directory_descriptor < 0) {
			throw std::runtime_error("directory open for fstatat failed");
		}
		darling_darwin_stat relative_stat{};
		const int relative_result = darling_windows_fstatat(
			directory_descriptor, "entry.txt", &relative_stat, 0);
		darling_windows_close(directory_descriptor);
		if (relative_result != 0 || relative_stat.st_size != 4) {
			throw std::runtime_error("directory-relative fstatat ABI mismatch");
		}
		const auto symlink = directory / L"entry-link";
		const bool symlink_created = darling_windows_symlink(
			nested.string().c_str(), symlink.string().c_str()) == 0;
		if (symlink_created) {
			darling_darwin_stat followed_link{};
			darling_darwin_stat raw_link{};
			const int stat_result = darling_windows_stat(symlink.string().c_str(), &followed_link);
			const int lstat_result = darling_windows_lstat(symlink.string().c_str(), &raw_link);
			if (stat_result != 0 || followed_link.st_size != 4) {
				std::cerr << "STAT_LINK_RESULT=" << stat_result << " SIZE=" << followed_link.st_size << "\n";
				throw std::runtime_error("stat did not follow symbolic link");
			}
			if (lstat_result != 0 || (raw_link.st_mode & 0170000) != 0120000) {
				std::cerr << "LSTAT_LINK_RESULT=" << lstat_result << " MODE=" << std::oct <<
					(raw_link.st_mode & 0170000) << std::dec << "\n";
				throw std::runtime_error("lstat did not report symbolic link");
			}
			char link_target[1024]{};
			const auto target_length = darling_windows_readlink(
				symlink.string().c_str(), link_target, sizeof(link_target));
			if (target_length <= 0 || target_length >= static_cast<std::int64_t>(sizeof(link_target)) ||
				std::string(link_target, static_cast<std::size_t>(target_length)) != nested.string()) {
				throw std::runtime_error("readlink target mismatch");
			}
			char resolved_link[1024]{};
			if (darling_windows_realpath(symlink.string().c_str(), resolved_link) == nullptr ||
				!SameWindowsPath(std::filesystem::path(resolved_link), nested)) {
				throw std::runtime_error("realpath did not resolve symbolic link");
			}
			if (darling_windows_host_symbol("_symlink") == 0 ||
				darling_windows_host_symbol("_readlink") == 0) {
				throw std::runtime_error("symlink resolver mismatch");
			}
			darling_darwin_stat raw_at_link{};
			if (darling_windows_fstatat(-2, symlink.string().c_str(),
				&raw_at_link, 0x20) != 0 ||
				(raw_at_link.st_mode & 0170000) != 0120000) {
				throw std::runtime_error("fstatat no-follow mismatch");
			}
			if (!DeleteFileW(symlink.c_str())) {
				throw std::runtime_error("symbolic-link cleanup failed");
			}
			std::cout << "DARWIN_LSTAT_REPARSE=PASS\n";
		} else {
			std::cout << "DARWIN_LSTAT_REPARSE=SKIP\n";
		}
		if (darling_windows_host_symbol("_stat64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_stat) ||
			darling_windows_host_symbol("_lstat64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_lstat) ||
			darling_windows_host_symbol("_fstat64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_fstat)) {
			throw std::runtime_error("stat64 symbol aliases mismatch");
		}
		if (darling_windows_host_symbol("_fstatat64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_fstatat)) {
			throw std::runtime_error("fstatat64 symbol alias mismatch");
		}
		if (darling_windows_host_symbol("_statfs64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_statfs) ||
			darling_windows_host_symbol("_fstatfs64") !=
			reinterpret_cast<std::uintptr_t>(&darling_windows_fstatfs)) {
			throw std::runtime_error("statfs64 symbol aliases mismatch");
		}
		const auto hardlink = directory / L"entry-hardlink";
		darling_darwin_stat original_stat{};
		darling_darwin_stat hardlink_stat{};
		const int link_result = darling_windows_link(native_path.c_str(), hardlink.string().c_str());
		const DWORD link_error = GetLastError();
		const int original_result = darling_windows_stat(native_path.c_str(), &original_stat);
		const int hardlink_result = darling_windows_stat(hardlink.string().c_str(), &hardlink_stat);
		const bool hardlink_ok = link_result == 0 && original_result == 0 &&
			hardlink_result == 0 && hardlink_stat.st_ino == original_stat.st_ino &&
			hardlink_stat.st_size == 4 && darling_windows_host_symbol("_link") != 0;
		if (!hardlink_ok && (link_error == ERROR_ACCESS_DENIED ||
			link_error == ERROR_PRIVILEGE_NOT_HELD)) {
			std::cerr << "DARWIN_HARDLINK=SKIP ACCESS_DENIED\n";
		} else if (!hardlink_ok || darling_windows_unlink(hardlink.string().c_str()) != 0) {
			std::cerr << "HARDLINK_RESULTS=" << link_result << "," << original_result << ","
				<< hardlink_result << " INO=" << original_stat.st_ino << "/"
				<< hardlink_stat.st_ino << " SIZE=" << hardlink_stat.st_size <<
				" WIN32_ERROR=" << link_error << "\n";
			throw std::runtime_error("hard-link ABI mismatch");
		}
		const auto empty_directory = directory / L"empty-directory";
		if (!CreateDirectoryW(empty_directory.c_str(), nullptr) ||
			darling_windows_rmdir(empty_directory.string().c_str()) != 0 ||
			darling_windows_host_symbol("_rmdir") == 0) {
			throw std::runtime_error("rmdir ABI mismatch");
		}
		darling_darwin_statfs filesystem_list[26]{};
		const int filesystem_count = darling_windows_getfsstat(
			filesystem_list, sizeof(filesystem_list), 0);
		bool current_volume_found = false;
		for (int index = 0; index < filesystem_count; ++index) {
			current_volume_found = current_volume_found ||
				filesystem_list[index].f_fsid == path_statfs.f_fsid;
		}
		if (filesystem_count < 1 || !current_volume_found ||
			darling_windows_getfsstat(nullptr, 0, 0) < 1 ||
			darling_windows_host_symbol("_getfsstat64") !=
				reinterpret_cast<std::uintptr_t>(&darling_windows_getfsstat)) {
			throw std::runtime_error("getfsstat ABI mismatch");
		}
		const auto entries = darling::windows_host::DarwinFilesystem::ListDirectory(directory);
		if (entries.size() != 1 || entries.front() != L"entry.txt") {
			throw std::runtime_error("directory listing mismatch");
		}
		darling::windows_host::DarwinFilesystem::Unlink(nested);
		if (std::filesystem::exists(nested)) {
			throw std::runtime_error("unlink did not remove file");
		}
		if (darling_windows_rmdir(directory.string().c_str()) != 0) {
			throw std::runtime_error("temporary directory cleanup failed");
		}
		std::cout << "DARWIN_SYSCALL_FILESYSTEM=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FILESYSTEM_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
