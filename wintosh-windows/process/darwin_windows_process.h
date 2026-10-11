/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace darling::windows_host {

struct DarwinX86ThreadState64 final {
	std::uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;
	std::uint64_t rdi = 0, rsi = 0, rbp = 0, rsp = 0;
	std::uint64_t r8 = 0, r9 = 0, r10 = 0, r11 = 0;
	std::uint64_t r12 = 0, r13 = 0, r14 = 0, r15 = 0;
	std::uint64_t rip = 0, rflags = 0;
	std::uint64_t cs = 0, fs = 0, gs = 0;
};

struct ProcessMemoryRegion final {
	std::uintptr_t base = 0;
	std::size_t size = 0;
	DWORD state = 0;
	DWORD protect = 0;
	DWORD type = 0;
};

class ProcessGroup;

class Process final {
public:
	Process() noexcept = default;
	Process(const Process&) = delete;
	Process& operator=(const Process&) = delete;
	Process(Process&& other) noexcept;
	Process& operator=(Process&& other) noexcept;
	~Process() noexcept;

	static Process Launch(const std::wstring& executable,
		const std::wstring& command_line,
		const std::wstring& working_directory = {});
	static Process Launch(const std::wstring& executable,
		const std::wstring& command_line,
		const std::wstring& working_directory,
		const std::vector<std::wstring>& environment);
	static Process LaunchSuspended(const std::wstring& executable,
		const std::wstring& command_line,
		const std::wstring& working_directory = {},
		const std::vector<std::wstring>& environment = {});
	static Process LaunchInGroup(ProcessGroup& group,
		const std::wstring& executable,
		const std::wstring& command_line,
		const std::wstring& working_directory = {},
		const std::vector<std::wstring>& environment = {});

	[[nodiscard]] DWORD Wait(DWORD timeout_ms = INFINITE) const noexcept;
	[[nodiscard]] DWORD ExitCode() const noexcept;
	[[nodiscard]] DWORD ProcessId() const noexcept;
	[[nodiscard]] DWORD ThreadId() const noexcept { return m_thread_id; }
	[[nodiscard]] std::vector<DWORD> ThreadIds() const;
	[[nodiscard]] std::vector<ProcessMemoryRegion> MemoryRegions() const;
	[[nodiscard]] bool GetPrimaryThreadContext(CONTEXT& context) const noexcept;
	[[nodiscard]] bool SetPrimaryThreadContext(const CONTEXT& context) const noexcept;
	[[nodiscard]] bool GetThreadContext(DWORD thread_id, CONTEXT& context) const noexcept;
	[[nodiscard]] bool SetThreadContext(DWORD thread_id, const CONTEXT& context) const noexcept;
	[[nodiscard]] bool GetDarwinX86ThreadState64(DarwinX86ThreadState64& state) const noexcept;
	[[nodiscard]] bool SetDarwinX86ThreadState64(const DarwinX86ThreadState64& state) const noexcept;
	[[nodiscard]] DWORD Suspend() noexcept;
	[[nodiscard]] DWORD Resume() noexcept;
	void Terminate(UINT exit_code = 1) noexcept;
	[[nodiscard]] bool Valid() const noexcept { return m_process != nullptr; }

private:
	Process(HANDLE process, HANDLE thread, HANDLE job, bool owns_job) noexcept;
	void Reset() noexcept;

	HANDLE m_process = nullptr;
	HANDLE m_thread = nullptr;
	DWORD m_thread_id = 0;
	HANDLE m_job = nullptr;
	bool m_owns_job = false;
};

class ProcessGroup final {
public:
	ProcessGroup();
	ProcessGroup(const ProcessGroup&) = delete;
	ProcessGroup& operator=(const ProcessGroup&) = delete;
	ProcessGroup(ProcessGroup&& other) noexcept;
	ProcessGroup& operator=(ProcessGroup&& other) noexcept;
	~ProcessGroup() noexcept;

	[[nodiscard]] bool Valid() const noexcept { return m_job != nullptr; }
	void Terminate(UINT exit_code = 1) noexcept;

private:
	friend class Process;
	[[nodiscard]] HANDLE NativeHandle() const noexcept { return m_job; }
	void Reset() noexcept;
	HANDLE m_job = nullptr;
};

} // namespace darling::windows_host
