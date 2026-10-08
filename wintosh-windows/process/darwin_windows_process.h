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

#include <string>
#include <vector>

namespace darling::windows_host {

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
	static Process LaunchInGroup(ProcessGroup& group,
		const std::wstring& executable,
		const std::wstring& command_line,
		const std::wstring& working_directory = {},
		const std::vector<std::wstring>& environment = {});

	[[nodiscard]] DWORD Wait(DWORD timeout_ms = INFINITE) const noexcept;
	[[nodiscard]] DWORD ExitCode() const noexcept;
	void Terminate(UINT exit_code = 1) noexcept;
	[[nodiscard]] bool Valid() const noexcept { return m_process != nullptr; }

private:
	Process(HANDLE process, HANDLE thread, HANDLE job, bool owns_job) noexcept;
	void Reset() noexcept;

	HANDLE m_process = nullptr;
	HANDLE m_thread = nullptr;
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
