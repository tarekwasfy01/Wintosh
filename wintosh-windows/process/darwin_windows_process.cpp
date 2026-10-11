/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "darwin_windows_process.h"

#include <system_error>

#include <algorithm>
#include <tlhelp32.h>
#include <vector>

namespace darling::windows_host {

namespace {

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()),
		std::system_category(), operation);
}

} // namespace

Process::Process(HANDLE process, HANDLE thread, HANDLE job, bool owns_job) noexcept :
	m_process(process), m_thread(thread), m_thread_id(thread == nullptr ? 0 : GetThreadId(thread)),
	m_job(job), m_owns_job(owns_job)
{
}

Process::Process(Process&& other) noexcept :
	m_process(other.m_process), m_thread(other.m_thread)
	, m_thread_id(other.m_thread_id), m_job(other.m_job), m_owns_job(other.m_owns_job)
{
	other.m_process = nullptr;
	other.m_thread = nullptr;
	other.m_thread_id = 0;
	other.m_job = nullptr;
	other.m_owns_job = false;
}

Process& Process::operator=(Process&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_process = other.m_process;
		m_thread = other.m_thread;
		m_thread_id = other.m_thread_id;
		m_job = other.m_job;
		m_owns_job = other.m_owns_job;
		other.m_process = nullptr;
		other.m_thread = nullptr;
		other.m_thread_id = 0;
		other.m_job = nullptr;
		other.m_owns_job = false;
	}
	return *this;
}

Process::~Process() noexcept
{
	Reset();
}

Process Process::Launch(const std::wstring& executable,
	const std::wstring& command_line,
	const std::wstring& working_directory)

{
	return Launch(executable, command_line, working_directory, {});
}

Process Process::Launch(const std::wstring& executable,
	const std::wstring& command_line,
	const std::wstring& working_directory,
	const std::vector<std::wstring>& environment)
{
	auto process = LaunchSuspended(executable, command_line, working_directory, environment);
	if (process.Resume() == static_cast<DWORD>(-1)) {
		return Process{};
	}
	return process;
}

Process Process::LaunchSuspended(const std::wstring& executable,
	const std::wstring& command_line,
	const std::wstring& working_directory,
	const std::vector<std::wstring>& environment)
{
	std::wstring mutable_command_line = command_line;
	if (mutable_command_line.empty())
		mutable_command_line = L"\"" + executable + L"\"";
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process_info{};
	std::vector<wchar_t> environment_block;
	if (!environment.empty()) {
		for (const auto& entry : environment) {
			if (entry.find(L'\0') != std::wstring::npos)
				throw std::invalid_argument("environment entry contains NUL");
			environment_block.insert(environment_block.end(), entry.begin(), entry.end());
			environment_block.push_back(L'\0');
		}
		environment_block.push_back(L'\0');
	}
	if (!CreateProcessW(executable.empty() ? nullptr : executable.c_str(),
		mutable_command_line.data(), nullptr, nullptr, FALSE,
		CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
		environment.empty() ? nullptr : environment_block.data(),
		working_directory.empty() ? nullptr : working_directory.c_str(),
		&startup, &process_info))
		ThrowLastError("CreateProcessW");
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (job != nullptr) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
			&limits, sizeof(limits)) || !AssignProcessToJobObject(job, process_info.hProcess)) {
			CloseHandle(job);
			job = nullptr;
		}
	}
	return Process(process_info.hProcess, process_info.hThread, job, job != nullptr);
}

Process Process::LaunchInGroup(ProcessGroup& group,
	const std::wstring& executable,
	const std::wstring& command_line,
	const std::wstring& working_directory,
	const std::vector<std::wstring>& environment)
{
	std::wstring mutable_command_line = command_line;
	if (mutable_command_line.empty()) {
		mutable_command_line = L"\"" + executable + L"\"";
	}

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process_info{};
	std::vector<wchar_t> environment_block;
	if (!environment.empty()) {
		for (const auto& entry : environment) {
			if (entry.find(L'\0') != std::wstring::npos) {
				throw std::invalid_argument("environment entry contains NUL");
			}
			environment_block.insert(environment_block.end(), entry.begin(), entry.end());
			environment_block.push_back(L'\0');
		}
		environment_block.push_back(L'\0');
	}

	const BOOL started = CreateProcessW(
		executable.empty() ? nullptr : executable.c_str(),
		mutable_command_line.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_UNICODE_ENVIRONMENT,
		environment.empty() ? nullptr : environment_block.data(),
		working_directory.empty() ? nullptr : working_directory.c_str(),
		&startup,
		&process_info);
	if (!started) {
		ThrowLastError("CreateProcessW");
	}

	CloseHandle(process_info.hThread);
	if (!group.Valid() || !AssignProcessToJobObject(group.NativeHandle(), process_info.hProcess)) {
		// A process may already belong to a host-managed job. Preserve the
		// process boundary and fall back to direct termination in that case.
		return Process(process_info.hProcess, nullptr, nullptr, false);
	}
	return Process(process_info.hProcess, nullptr, group.NativeHandle(), false);
}

DWORD Process::Wait(DWORD timeout_ms) const noexcept
{
	return m_process == nullptr ? WAIT_FAILED : WaitForSingleObject(m_process, timeout_ms);
}

DWORD Process::ExitCode() const noexcept
{
	DWORD exit_code = STILL_ACTIVE;
	if (m_process == nullptr || !GetExitCodeProcess(m_process, &exit_code)) {
		return STILL_ACTIVE;
	}
	return exit_code;
}

DWORD Process::ProcessId() const noexcept
{
	return m_process == nullptr ? 0 : GetProcessId(m_process);
}

std::vector<DWORD> Process::ThreadIds() const
{
	std::vector<DWORD> result;
	if (m_process == nullptr)
		return result;
	const DWORD process_id = GetProcessId(m_process);
	if (m_thread_id != 0)
		result.push_back(m_thread_id);
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return result;
	THREADENTRY32 entry{};
	entry.dwSize = sizeof(entry);
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID == process_id &&
				std::find(result.begin(), result.end(), entry.th32ThreadID) == result.end())
				result.push_back(entry.th32ThreadID);
		} while (Thread32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return result;
}

bool Process::GetPrimaryThreadContext(CONTEXT& context) const noexcept
{
	if (m_thread_id == 0)
		return false;
	const HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
		FALSE, m_thread_id);
	if (thread == nullptr)
		return false;
	context.ContextFlags = CONTEXT_FULL;
	const BOOL result = GetThreadContext(thread, &context);
	CloseHandle(thread);
	return result != FALSE;
}

bool Process::SetPrimaryThreadContext(const CONTEXT& context) const noexcept
{
	if (m_thread_id == 0)
		return false;
	const HANDLE thread = OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
		FALSE, m_thread_id);
	if (thread == nullptr)
		return false;
	CONTEXT copy = context;
	const BOOL result = SetThreadContext(thread, &copy);
	CloseHandle(thread);
	return result != FALSE;
}

bool Process::GetDarwinX86ThreadState64(DarwinX86ThreadState64& state) const noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
	CONTEXT context{};
	if (!GetPrimaryThreadContext(context)) return false;
	state = {context.Rax, context.Rbx, context.Rcx, context.Rdx, context.Rdi, context.Rsi,
		context.Rbp, context.Rsp, context.R8, context.R9, context.R10, context.R11,
		context.R12, context.R13, context.R14, context.R15, context.Rip, context.EFlags,
		context.SegCs, context.SegFs, context.SegGs};
	return true;
#else
	(void)state;
	return false;
#endif
}

bool Process::SetDarwinX86ThreadState64(const DarwinX86ThreadState64& state) const noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
	CONTEXT context{};
	if (!GetPrimaryThreadContext(context)) return false;
	context.Rax = state.rax; context.Rbx = state.rbx; context.Rcx = state.rcx; context.Rdx = state.rdx;
	context.Rdi = state.rdi; context.Rsi = state.rsi; context.Rbp = state.rbp; context.Rsp = state.rsp;
	context.R8 = state.r8; context.R9 = state.r9; context.R10 = state.r10; context.R11 = state.r11;
	context.R12 = state.r12; context.R13 = state.r13; context.R14 = state.r14; context.R15 = state.r15;
	context.Rip = state.rip; context.EFlags = static_cast<DWORD>(state.rflags);
	// Windows user-mode SetThreadContext does not permit arbitrary segment
	// selector changes; retain the validated host selectors for this bridge.
	return SetPrimaryThreadContext(context);
#else
	(void)state;
	return false;
#endif
}

DWORD Process::Suspend() noexcept
{
	if (m_process == nullptr)
		return static_cast<DWORD>(-1);
	using SuspendProcessFn = DWORD (WINAPI*)(HANDLE);
	const auto suspend_process = reinterpret_cast<SuspendProcessFn>(
		GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess"));
	if (suspend_process == nullptr)
		return static_cast<DWORD>(-1);
	return suspend_process(m_process);
}

DWORD Process::Resume() noexcept
{
	if (m_thread == nullptr) {
		if (m_process == nullptr)
			return static_cast<DWORD>(-1);
		using ResumeProcessFn = DWORD (WINAPI*)(HANDLE);
		const auto resume_process = reinterpret_cast<ResumeProcessFn>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess"));
		return resume_process == nullptr ? static_cast<DWORD>(-1) : resume_process(m_process);
	}
	const DWORD result = ResumeThread(m_thread);
	if (result != static_cast<DWORD>(-1)) {
		CloseHandle(m_thread);
		m_thread = nullptr;
	}
	return result;
}

void Process::Terminate(UINT exit_code) noexcept
{
	if (m_process != nullptr) {
		if (m_job != nullptr)
			TerminateJobObject(m_job, exit_code);
		else
			TerminateProcess(m_process, exit_code);
	}
}

void Process::Reset() noexcept
{
	if (m_thread != nullptr) {
		CloseHandle(m_thread);
		m_thread = nullptr;
	}
	m_thread_id = 0;
	if (m_process != nullptr) {
		CloseHandle(m_process);
		m_process = nullptr;
	}
	if (m_job != nullptr && m_owns_job) {
		CloseHandle(m_job);
	}
	m_job = nullptr;
	m_owns_job = false;
}

ProcessGroup::ProcessGroup()
{
	m_job = CreateJobObjectW(nullptr, nullptr);
	if (m_job == nullptr) {
		ThrowLastError("CreateJobObjectW");
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(m_job, JobObjectExtendedLimitInformation,
		&limits, sizeof(limits))) {
		Reset();
		ThrowLastError("SetInformationJobObject");
	}
}

ProcessGroup::ProcessGroup(ProcessGroup&& other) noexcept : m_job(other.m_job)
{
	other.m_job = nullptr;
}

ProcessGroup& ProcessGroup::operator=(ProcessGroup&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_job = other.m_job;
		other.m_job = nullptr;
	}
	return *this;
}

ProcessGroup::~ProcessGroup() noexcept
{
	Reset();
}

void ProcessGroup::Terminate(UINT exit_code) noexcept
{
	if (m_job != nullptr) {
		TerminateJobObject(m_job, exit_code);
	}
}

void ProcessGroup::Reset() noexcept
{
	if (m_job != nullptr) {
		CloseHandle(m_job);
		m_job = nullptr;
	}
}

} // namespace darling::windows_host
