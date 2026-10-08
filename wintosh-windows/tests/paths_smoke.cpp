/*
 * Stage 2 Darwin path boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_paths.h"

#include <windows.h>

#include <filesystem>
#include <iostream>

int wmain()
{
	try {
		const auto original = darling::windows_host::DarwinPaths::CurrentWorkingDirectory();
		const auto absolute = darling::windows_host::DarwinPaths::AbsolutePath(L".");
		if (absolute != original) {
			std::cerr << "PATH_SMOKE_ERROR=absolute path mismatch\n";
			return 1;
		}
		std::cout << "DARWIN_ABSOLUTE_PATH=PASS\n";
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			throw std::runtime_error("temporary path unavailable");
		}
		std::wstring target_text(temp_path, temp_length);
		while (!target_text.empty() &&
			(target_text.back() == L'\\' || target_text.back() == L'/'))
			target_text.pop_back();
		const auto target = std::filesystem::path(target_text).lexically_normal();
		const auto canonical = darling::windows_host::DarwinPaths::CanonicalPath(target);
		if (canonical != target) {
			std::cerr << "PATH_SMOKE_ERROR=canonical path mismatch\n";
			return 1;
		}
		std::cout << "DARWIN_CANONICAL_PATH=PASS\n";
		darling::windows_host::DarwinPaths::ChangeWorkingDirectory(target);
		const auto changed = darling::windows_host::DarwinPaths::CurrentWorkingDirectory();
		darling::windows_host::DarwinPaths::ChangeWorkingDirectory(original);
		if (changed.filename() != target.filename()) {
			std::cerr << "PATH_SMOKE_ERROR=working directory mismatch changed="
				<< changed.string() << " target=" << target.string() << "\n";
			return 1;
		}
		std::cout << "DARWIN_SYSCALL_PATHS=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "PATH_SMOKE_ERROR=" << error.what() << "\n";
		return 2;
	}
}
