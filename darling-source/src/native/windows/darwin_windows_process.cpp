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
	m_process(process), m_thread(thread), m_job(job), m_owns_job(owns_job)
{
}

Process::Process(Process&& other) noexcept :
	m_process(other.m_process), m_thread(other.m_thread)
	, m_job(other.m_job), m_owns_job(other.m_owns_job)
{
	other.m_process = nullptr;
	other.m_thread = nullptr;
	other.m_job = nullptr;
	other.m_owns_job = false;
}

Process& Process::operator=(Process&& other) noexcept
{
	if (this != &other) {
		Reset();
		m_process = other.m_process;
		m_thread = other.m_thread;
		m_job = other.m_job;
		m_owns_job = other.m_owns_job;
		other.m_process = nullptr;
		other.m_thread = nullptr;
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
		mutable_command_line.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
		environment.empty() ? nullptr : environment_block.data(),
		working_directory.empty() ? nullptr : working_directory.c_str(),
		&startup, &process_info))
		ThrowLastError("CreateProcessW");
	CloseHandle(process_info.hThread);
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
	return Process(process_info.hProcess, nullptr, job, job != nullptr);
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
