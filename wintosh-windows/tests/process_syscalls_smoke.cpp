/*
 * Stage 2 Darwin process syscall boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_process_syscalls.h"
#include "darling_windows_stdio.h"

#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstring>

namespace {

constexpr std::size_t SpawnPathMax = 1024;

bool ResumeSpawnedMainThread(int process_id)
{
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;
	THREADENTRY32 entry{};
	entry.dwSize = sizeof(entry);
	bool resumed = false;
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID != static_cast<DWORD>(process_id))
				continue;
			const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE,
				entry.th32ThreadID);
			if (thread == nullptr)
				continue;
			const DWORD previous = ResumeThread(thread);
			CloseHandle(thread);
			if (previous != static_cast<DWORD>(-1)) {
				resumed = true;
				break;
			}
		} while (Thread32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return resumed;
}

#pragma pack(push, 4)
struct SpawnAction final {
	int type = 0;
	union {
		int file_descriptor = 0;
		std::uint32_t fileport;
	};
	union {
		struct {
			int flags;
			int mode;
			char path[SpawnPathMax];
		} open;
		struct { int new_file_descriptor; } dup2;
		struct { char path[SpawnPathMax]; } chdir;
	};
};

struct SpawnActions final {
	int allocated = 6;
	int count = 6;
	SpawnAction action[6];
};
#pragma pack(pop)

} // namespace

int wmain()
{
	try {
		auto process = darling::windows_host::DarwinProcessSyscalls::Spawn(
			L"C:\\Windows\\System32\\cmd.exe",
			{L"/d", L"/c", L"exit 7"});
		if (!process.Valid() || process.Wait(10000) != WAIT_OBJECT_0 ||
			process.ExitCode() != 7) {
			throw std::runtime_error("Darwin process syscall result mismatch");
		}
		auto environment_process = darling::windows_host::DarwinProcessSyscalls::Spawn(
			L"C:\\Windows\\System32\\cmd.exe",
			{L"/d", L"/c", L"if \"%DARLING_WSL_BRIDGE_TEST%\"==\"present\" (exit 0) else (exit 9)"},
			{}, {L"DARLING_WSL_BRIDGE_TEST=present"});
		if (!environment_process.Valid() || environment_process.Wait(10000) != WAIT_OBJECT_0 ||
			environment_process.ExitCode() != 0) {
			throw std::runtime_error("Darwin process environment was not isolated or propagated");
		}
		const char* spawn_arguments[] = {"cmd.exe", "/d", "/c", "exit 11", nullptr};
		const char* any_child_arguments[] = {"cmd.exe", "/d", "/c", "exit 13", nullptr};
		const char* any_wait4_arguments[] = {"cmd.exe", "/d", "/c", "exit 17", nullptr};
		const char* waitid_arguments[] = {"cmd.exe", "/d", "/c", "exit 29", nullptr};
		const char* spawn_environment[] = {"DARWIN_POSIX_SPAWN_TEST=present", nullptr};
		int spawned_pid = 0;
		int spawned_status = -1;
		int any_child_pid = 0;
		int any_child_status = -1;
		int any_wait4_pid = 0;
		int any_wait4_status = -1;
		int waitid_pid = 0;
		darling_rusage spawned_usage{};
		darling_rusage children_usage{};
		darling_rusage any_wait4_usage{};
		darling_siginfo waitid_info{};
		const bool normalized_process_symbols =
			darling_windows_host_symbol("getppid") != 0 &&
			darling_windows_host_symbol("posix_spawn") != 0 &&
			darling_windows_host_symbol("posix_spawnp") != 0 &&
			darling_windows_host_symbol("waitpid") != 0 &&
			darling_windows_host_symbol("wait4") != 0 &&
			darling_windows_host_symbol("waitid") != 0 &&
			darling_windows_host_symbol("kill") != 0;
		std::cout << "DARWIN_NORMALIZED_PROCESS_SYMBOLS="
			<< (normalized_process_symbols ? "PASS" : "FAIL") << "\n";
		const bool posix_spawn_ok = darling_windows_host_symbol("_posix_spawn") != 0 &&
			darling_windows_host_symbol("_posix_spawnp") != 0 &&
			darling_windows_host_symbol("_wait4") != 0 &&
			darling_windows_host_symbol("_waitpid") != 0 &&
			darling_windows_posix_spawn(&spawned_pid, "C:\\Windows\\System32\\cmd.exe",
			nullptr, nullptr, spawn_arguments, spawn_environment) == 0 && spawned_pid > 0 &&
			darling_windows_wait4(spawned_pid, &spawned_status, 0, &spawned_usage) == spawned_pid &&
			spawned_status == (11 << 8) && spawned_usage.ru_utime.tv_sec >= 0 &&
			spawned_usage.ru_stime.tv_sec >= 0 && spawned_usage.ru_maxrss >= 0 &&
			spawned_usage.ru_minflt >= 0 &&
			darling_windows_getrusage(-1, &children_usage) == 0 &&
			children_usage.ru_maxrss >= spawned_usage.ru_maxrss &&
			children_usage.ru_minflt >= spawned_usage.ru_minflt &&
			darling_windows_posix_spawn(&any_child_pid, "C:\\Windows\\System32\\cmd.exe",
			nullptr, nullptr, any_child_arguments, spawn_environment) == 0 &&
			any_child_pid > 0 && darling_windows_waitpid(0, &any_child_status, 0) == any_child_pid &&
			any_child_status == (13 << 8) &&
			darling_windows_posix_spawn(&any_wait4_pid, "C:\\Windows\\System32\\cmd.exe",
			nullptr, nullptr, any_wait4_arguments, spawn_environment) == 0 &&
			any_wait4_pid > 0 && darling_windows_wait4(-1, &any_wait4_status, 0,
			&any_wait4_usage) == any_wait4_pid && any_wait4_status == (17 << 8) &&
			any_wait4_usage.ru_maxrss >= 0 && any_wait4_usage.ru_minflt >= 0 &&
			 darling_windows_host_symbol("_waitid") != 0 && normalized_process_symbols &&
			darling_windows_posix_spawn(&waitid_pid, "C:\\Windows\\System32\\cmd.exe",
			nullptr, nullptr, waitid_arguments, spawn_environment) == 0 && waitid_pid > 0 &&
			darling_windows_waitid(1, waitid_pid, &waitid_info, 4 | 0x20) == 0 &&
			waitid_info.si_signo == 20 && waitid_info.si_errno == 0 &&
			waitid_info.si_uid == static_cast<std::uint32_t>(darling_windows_getuid()) &&
			waitid_info.si_code == 1 &&
			waitid_info.si_pid == waitid_pid && waitid_info.si_status == 29 &&
			darling_windows_waitid(1, waitid_pid, &waitid_info, 4) == 0 &&
			waitid_info.si_pid == waitid_pid && waitid_info.si_status == 29;
		if (!posix_spawn_ok) {
			throw std::runtime_error("posix_spawn process result mismatch");
		}
		wchar_t temp_path[MAX_PATH]{};
		const DWORD temp_length = GetTempPathW(MAX_PATH, temp_path);
		if (temp_length == 0 || temp_length >= MAX_PATH) {
			throw std::runtime_error("temporary path unavailable");
		}
		const auto redirected_path = std::filesystem::path(temp_path) /
			L"darling-posix-spawn-actions.txt";
		SpawnActions actions{};
		actions.action[0].type = 0; // PSFA_OPEN from XNU spawn_internal.h
		actions.action[0].file_descriptor = 1;
		actions.action[0].open.flags = 0x0601; // O_WRONLY|O_CREAT|O_TRUNC
		actions.action[0].open.mode = 0600;
		const auto redirected_utf8 = redirected_path.u8string();
		if (redirected_utf8.size() >= SpawnPathMax) {
			throw std::runtime_error("temporary path is too long");
		}
		std::memcpy(actions.action[0].open.path, redirected_utf8.data(),
			redirected_utf8.size());
		actions.action[1].type = 2; // PSFA_DUP2
		actions.action[1].file_descriptor = 1;
		actions.action[1].dup2.new_file_descriptor = 2;
		actions.action[2].type = 1; // PSFA_CLOSE
		actions.action[2].file_descriptor = 0;
		actions.action[3].type = 5; // PSFA_CHDIR
	const auto directory_utf8 = std::filesystem::path(temp_path).u8string();
	if (directory_utf8.size() >= SpawnPathMax) {
		throw std::runtime_error("temporary directory path is too long");
	}
	std::memcpy(actions.action[3].chdir.path, directory_utf8.data(),
		directory_utf8.size());
	actions.action[4].type = 0; // PSFA_OPEN directory for fchdir
	actions.action[4].file_descriptor = 0;
	actions.action[4].open.flags = 0x00100000; // O_DIRECTORY
	actions.action[4].open.mode = 0;
	std::memcpy(actions.action[4].open.path, directory_utf8.data(),
		directory_utf8.size());
	actions.action[5].type = 6; // PSFA_FCHDIR
	actions.action[5].file_descriptor = 0;
	const char* action_arguments[] = {"cmd.exe", "/d", "/c",
		"echo DARWIN-SPAWN-ACTION & cd", nullptr};
		int action_pid = 0;
		int action_status = -1;
		const int action_spawn_result = darling_windows_posix_spawn(&action_pid,
			"C:\\Windows\\System32\\cmd.exe", &actions, nullptr,
			action_arguments, nullptr);
		const int action_wait_result = action_pid > 0 ?
			darling_windows_waitpid(action_pid, &action_status, 0) : -1;
		if (action_spawn_result != 0 || action_pid <= 0 ||
			action_wait_result != action_pid || action_status != 0) {
			std::cerr << "SPAWN_ACTION_RESULTS=" << action_spawn_result << ","
				<< action_pid << "," << action_wait_result << "," << action_status << "\n";
			throw std::runtime_error("posix_spawn file action failed");
		}
		std::string redirected_contents;
		{
			std::ifstream redirected_file(redirected_path, std::ios::binary);
			redirected_contents.assign(std::istreambuf_iterator<char>(redirected_file), {});
		}
		std::filesystem::remove(redirected_path);
		if (redirected_contents.find("DARWIN-SPAWN-ACTION") == std::string::npos ||
			redirected_contents.find("Temp") == std::string::npos) {
			throw std::runtime_error("posix_spawn open action did not redirect stdout");
		}
		const short cloexec_default = 0x4000; // POSIX_SPAWN_CLOEXEC_DEFAULT from XNU
		const char* attribute_arguments[] = {"cmd.exe", "/d", "/c", "exit 13", nullptr};
		int attribute_pid = 0;
		int attribute_status = -1;
		if (darling_windows_posix_spawn(&attribute_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &cloexec_default,
			attribute_arguments, nullptr) != 0 || attribute_pid <= 0 ||
			darling_windows_waitpid(attribute_pid, &attribute_status, 0) != attribute_pid ||
			attribute_status != (13 << 8)) {
			throw std::runtime_error("posix_spawn CLOEXEC_DEFAULT attribute failed");
		}
		const short resetids = 0x0001; // POSIX_SPAWN_RESETIDS; same-token Windows emulation
		const char* resetids_arguments[] = {"cmd.exe", "/d", "/c", "exit 14", nullptr};
		int resetids_pid = 0;
		int resetids_status = -1;
		if (darling_windows_posix_spawn(&resetids_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &resetids,
			resetids_arguments, nullptr) != 0 || resetids_pid <= 0 ||
			darling_windows_waitpid(resetids_pid, &resetids_status, 0) != resetids_pid ||
			resetids_status != (14 << 8)) {
			throw std::runtime_error("posix_spawn RESETIDS attribute failed");
		}
		const short start_suspended = 0x0080; // POSIX_SPAWN_START_SUSPENDED from XNU
		const char* suspended_arguments[] = {"cmd.exe", "/d", "/c", "exit 17", nullptr};
		int suspended_pid = 0;
		int suspended_status = -1;
		if (darling_windows_posix_spawn(&suspended_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &start_suspended,
			suspended_arguments, nullptr) != 0 || suspended_pid <= 0 ||
			!ResumeSpawnedMainThread(suspended_pid) ||
			darling_windows_waitpid(suspended_pid, &suspended_status, 0) != suspended_pid ||
			suspended_status != (17 << 8)) {
			throw std::runtime_error("posix_spawn START_SUSPENDED attribute failed");
		}
		const short setsid = 0x0400; // POSIX_SPAWN_SETSID from XNU
		const char* session_arguments[] = {"cmd.exe", "/d", "/c", "exit 19", nullptr};
		int session_pid = 0;
		int session_status = -1;
		if (darling_windows_posix_spawn(&session_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &setsid,
			session_arguments, nullptr) != 0 || session_pid <= 0 ||
			darling_windows_waitpid(session_pid, &session_status, 0) != session_pid ||
			session_status != (19 << 8)) {
			throw std::runtime_error("posix_spawn SETSID attribute failed");
		}
		struct SpawnPgroupAttributes final {
			short flags;
			short padding;
			int pgroup;
		};
		const SpawnPgroupAttributes setpgroup{0x0002, 0, 0};
		const char* pgroup_arguments[] = {"cmd.exe", "/d", "/c", "exit 23", nullptr};
		int pgroup_pid = 0;
		int pgroup_status = -1;
		const int pgroup_spawn_result = darling_windows_posix_spawn(&pgroup_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &setpgroup,
			pgroup_arguments, nullptr);
		const int pgroup_wait_result = pgroup_spawn_result == 0 && pgroup_pid > 0 ?
			darling_windows_waitpid(-pgroup_pid, &pgroup_status, 0) : -1;
		if (pgroup_spawn_result != 0 || pgroup_pid <= 0 ||
			pgroup_wait_result != pgroup_pid || pgroup_status != (23 << 8)) {
			throw std::runtime_error("posix_spawn SETPGROUP attribute failed");
		}
		const char* group_kill_arguments[] = {"cmd.exe", "/d", "/c",
			"C:\\Windows\\System32\\ping.exe -n 30 127.0.0.1 > nul", nullptr};
		int group_kill_first = 0;
		int group_kill_second = 0;
		if (darling_windows_posix_spawn(&group_kill_first,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &setpgroup,
			group_kill_arguments, nullptr) != 0 || group_kill_first <= 0) {
			throw std::runtime_error("posix_spawn group-kill first child failed");
		}
		const SpawnPgroupAttributes setpgroup_existing{0x0002, 0, group_kill_first};
		if (darling_windows_posix_spawn(&group_kill_second,
			"C:\\Windows\\System32\\cmd.exe", nullptr, &setpgroup_existing,
			group_kill_arguments, nullptr) != 0 || group_kill_second <= 0) {
			throw std::runtime_error("posix_spawn group-kill second child failed");
		}
		if (darling_windows_getpgid(group_kill_first) != group_kill_first ||
			darling_windows_getpgid(group_kill_second) != group_kill_first ||
			darling_windows_getsid(group_kill_second) != group_kill_first) {
			throw std::runtime_error("child process-group identity mismatch");
		}
		if (darling_windows_setpgid(group_kill_second, group_kill_first) != 0 ||
			darling_windows_getpgid(group_kill_second) != group_kill_first) {
			throw std::runtime_error("child setpgid result mismatch");
		}
		if (darling_windows_kill(-group_kill_first, 0) != 0) {
			throw std::runtime_error("group existence probe failed");
		}
		if (darling_windows_kill(-group_kill_first, 9) != 0) {
			throw std::runtime_error("group SIGKILL failed");
		}
		int group_kill_status = -1;
		const int group_wait_first = darling_windows_waitpid(-group_kill_first,
			&group_kill_status, 0);
		const int group_status_first = group_kill_status;
		const int group_wait_second = darling_windows_waitpid(-group_kill_first,
			&group_kill_status, 0);
		const bool first_member_ok = (group_wait_first == group_kill_first ||
			group_wait_first == group_kill_second) && group_status_first == (137 << 8);
		const bool second_member_ok = (group_wait_second == group_kill_first ||
			group_wait_second == group_kill_second) && group_kill_status == (137 << 8) &&
			group_wait_second != group_wait_first;
		if (!first_member_ok || !second_member_ok) {
			std::cerr << "GROUP_KILL_RESULTS=" << group_wait_first << "," <<
				group_status_first << "," << group_wait_second << "," << group_kill_status <<
				" expected=" << group_kill_first << "," << group_kill_second << "\n";
			throw std::runtime_error("group SIGKILL wait result mismatch");
		}
		int term_pid = 0;
		int term_status = -1;
		const char* term_arguments[] = {"cmd.exe", "/d", "/c",
			"C:\\Windows\\System32\\ping.exe -n 30 127.0.0.1 > nul", nullptr};
		if (darling_windows_posix_spawn(&term_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, nullptr,
			term_arguments, nullptr) != 0 || term_pid <= 0 ||
			darling_windows_kill(term_pid, 15) != 0 ||
			darling_windows_waitpid(term_pid, &term_status, 0) != term_pid ||
			term_status != (143 << 8)) {
			throw std::runtime_error("SIGTERM emulation result mismatch");
		}
		int term_info_pid = 0;
		darling_siginfo term_info{};
		if (darling_windows_posix_spawn(&term_info_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, nullptr,
			term_arguments, nullptr) != 0 || term_info_pid <= 0 ||
			darling_windows_kill(term_info_pid, 15) != 0 ||
			darling_windows_waitid(1, term_info_pid, &term_info, 4) != 0 ||
			term_info.si_code != 2 || term_info.si_status != 15) {
			throw std::runtime_error("SIGTERM waitid detail mismatch");
		}
		const char* kill_arguments[] = {"cmd.exe", "/d", "/c",
			"ping -n 30 127.0.0.1 > nul", nullptr};
		int kill_pid = 0;
		darling_siginfo killed_info{};
		if (darling_windows_posix_spawn(&kill_pid,
			"C:\\Windows\\System32\\cmd.exe", nullptr, nullptr,
			kill_arguments, nullptr) != 0 || kill_pid <= 0 ||
			darling_windows_kill(kill_pid, 9) != 0 ||
			darling_windows_waitid(1, kill_pid, &killed_info, 4) != 0 ||
			killed_info.si_errno != 0 || killed_info.si_uid !=
			static_cast<std::uint32_t>(darling_windows_getuid()) || killed_info.si_code != 2 ||
			killed_info.si_status != 9) {
			throw std::runtime_error("cross-process SIGKILL emulation failed");
		}
		std::cout << "DARWIN_POSIX_SPAWN_FILE_ACTIONS=PASS\n";
		std::cout << "DARWIN_POSIX_SPAWN_ATTRIBUTES=PASS\n";
		std::cout << "DARWIN_POSIX_SPAWN_RESETIDS=EMULATED\n";
		std::cout << "DARWIN_POSIX_SPAWN_START_SUSPENDED=PASS\n";
		std::cout << "DARWIN_POSIX_SPAWN_SETSID=EMULATED\n";
		std::cout << "DARWIN_POSIX_SPAWN_SETPGROUP=EMULATED\n";
		std::cout << "DARWIN_KILL_SIGKILL=GROUP_NATIVE\n";
		std::cout << "DARWIN_KILL_SIGTERM=EMULATED\n";
		std::cout << "DARWIN_SYSCALL_PROCESS=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "PROCESS_SYSCALL_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
