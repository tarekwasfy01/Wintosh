/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_terminal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace darling::windows_host {

namespace {

using CreatePseudoConsoleProc = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
using ClosePseudoConsoleProc = VOID(WINAPI*)(HANDLE);
using ResizePseudoConsoleProc = HRESULT(WINAPI*)(HANDLE, COORD);

[[noreturn]] void ThrowLastError(const char* operation)
{
	throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

template <typename Function>
Function TerminalFunction(const char* name)
{
	const auto module = GetModuleHandleW(L"kernel32.dll");
	const auto address = module == nullptr ? nullptr : GetProcAddress(module, name);
	if (address == nullptr) {
		throw std::runtime_error(std::string("ConPTY API is unavailable: ") + name);
	}
	return reinterpret_cast<Function>(address);
}

void CloseHandleIfValid(HANDLE handle) noexcept
{
	if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
}

} // namespace

std::unique_ptr<DarwinPseudoTerminal> DarwinPseudoTerminal::Start(
	const std::wstring& command_line, const std::wstring& working_directory,
	std::uint16_t columns, std::uint16_t rows)
{
	if (command_line.empty() || columns == 0 || rows == 0) {
		throw std::invalid_argument("ConPTY command line or dimensions are invalid");
	}
	const auto create = TerminalFunction<CreatePseudoConsoleProc>("CreatePseudoConsole");
	const auto close = TerminalFunction<ClosePseudoConsoleProc>("ClosePseudoConsole");
	SECURITY_ATTRIBUTES attributes{};
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;
	HANDLE input_read = nullptr;
	HANDLE input_write = nullptr;
	HANDLE output_read = nullptr;
	HANDLE output_write = nullptr;
	if (!CreatePipe(&input_read, &input_write, &attributes, 0) ||
		!CreatePipe(&output_read, &output_write, &attributes, 0)) {
		CloseHandleIfValid(input_read);
		CloseHandleIfValid(input_write);
		CloseHandleIfValid(output_read);
		CloseHandleIfValid(output_write);
		ThrowLastError("CreatePipe(ConPTY)");
	}
	SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
	HANDLE pseudo_console = nullptr;
	const HRESULT created = create(COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)},
		input_read, output_write, 0, &pseudo_console);
	if (FAILED(created)) {
		CloseHandleIfValid(input_read);
		CloseHandleIfValid(output_write);
		CloseHandle(input_write);
		CloseHandle(output_read);
		throw std::system_error(static_cast<int>(created), std::system_category(),
			"CreatePseudoConsole");
	}
	std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
	mutable_command.push_back(L'\0');
	SIZE_T attribute_bytes = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
	if (attribute_bytes == 0) {
		close(pseudo_console);
		CloseHandle(input_write);
		CloseHandle(output_read);
		ThrowLastError("InitializeProcThreadAttributeList(size)");
	}
	std::vector<std::byte> attribute_storage(attribute_bytes);
	auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
	if (!InitializeProcThreadAttributeList(list, 1, 0, &attribute_bytes) ||
		!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
			pseudo_console, sizeof(pseudo_console), nullptr, nullptr)) {
		const DWORD error = GetLastError();
		DeleteProcThreadAttributeList(list);
		close(pseudo_console);
		CloseHandle(input_write);
		CloseHandle(output_read);
		SetLastError(error);
		ThrowLastError("UpdateProcThreadAttribute(ConPTY)");
	}
	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = input_read;
	startup.StartupInfo.hStdOutput = output_write;
	startup.StartupInfo.hStdError = output_write;
	startup.lpAttributeList = list;
	PROCESS_INFORMATION process{};
	const BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
		TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
		working_directory.empty() ? nullptr : working_directory.c_str(),
		&startup.StartupInfo, &process);
	DeleteProcThreadAttributeList(list);
	CloseHandle(input_read);
	CloseHandle(output_write);
	if (!started) {
		const DWORD error = GetLastError();
		close(pseudo_console);
		CloseHandle(input_write);
		CloseHandle(output_read);
		SetLastError(error);
		ThrowLastError("CreateProcessW(ConPTY)");
	}
	CloseHandle(process.hThread);
	process.hThread = nullptr;
	return std::unique_ptr<DarwinPseudoTerminal>(new DarwinPseudoTerminal(
		pseudo_console, input_write, output_read, process));
}

DarwinPseudoTerminal::DarwinPseudoTerminal(HANDLE pseudo_console, HANDLE input_write,
	HANDLE output_read, PROCESS_INFORMATION process) noexcept :
	m_pseudo_console(pseudo_console), m_input_write(input_write),
	m_output_read(output_read), m_process(process)
{
	m_output_thread = std::thread(&DarwinPseudoTerminal::ReadOutputLoop, this);
}

DarwinPseudoTerminal::~DarwinPseudoTerminal() noexcept
{
	CloseHandleIfValid(m_process.hProcess);
	CloseHandleIfValid(m_input_write);
	if (m_output_thread.joinable()) {
		CancelSynchronousIo(m_output_thread.native_handle());
	}
	CloseHandleIfValid(m_output_read);
	if (m_output_thread.joinable()) m_output_thread.join();
	if (m_pseudo_console != nullptr) {
		try {
			TerminalFunction<ClosePseudoConsoleProc>("ClosePseudoConsole")(m_pseudo_console);
		} catch (...) {
		}
	}
}

void DarwinPseudoTerminal::ReadOutputLoop() noexcept
{
	char buffer[4096];
	for (;;) {
		DWORD read = 0;
		if (!ReadFile(m_output_read, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr)) {
			break;
		}
		if (read == 0) break;
		try {
			std::lock_guard<std::mutex> lock(m_output_mutex);
			m_pending_output.insert(m_pending_output.end(), buffer, buffer + read);
		} catch (...) {
			break;
		}
	}
}

std::size_t DarwinPseudoTerminal::ReadAvailable(void* buffer, std::size_t bytes)
{
	if (buffer == nullptr || bytes == 0) return 0;
	std::lock_guard<std::mutex> lock(m_output_mutex);
	const auto count = (std::min)(bytes, m_pending_output.size());
	if (count == 0) return 0;
	std::memcpy(buffer, m_pending_output.data(), count);
	m_pending_output.erase(m_pending_output.begin(), m_pending_output.begin() + count);
	return count;
}

std::size_t DarwinPseudoTerminal::Write(const void* buffer, std::size_t bytes)
{
	if (buffer == nullptr || bytes == 0) return 0;
	DWORD written = 0;
	if (!WriteFile(m_input_write, buffer,
		static_cast<DWORD>((std::min)(bytes, static_cast<std::size_t>(MAXDWORD))), &written, nullptr)) {
		ThrowLastError("WriteFile(ConPTY)");
	}
	return written;
}

void DarwinPseudoTerminal::Resize(std::uint16_t columns, std::uint16_t rows)
{
	if (columns == 0 || rows == 0) throw std::invalid_argument("invalid ConPTY dimensions");
	const auto resize = TerminalFunction<ResizePseudoConsoleProc>("ResizePseudoConsole");
	const HRESULT result = resize(m_pseudo_console,
		COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)});
	if (FAILED(result)) {
		throw std::system_error(static_cast<int>(result), std::system_category(),
			"ResizePseudoConsole");
	}
}

bool DarwinPseudoTerminal::Wait(std::uint32_t timeout_ms, std::uint32_t* exit_code)
{
	const DWORD result = WaitForSingleObject(m_process.hProcess, timeout_ms);
	if (result == WAIT_TIMEOUT) return false;
	if (result != WAIT_OBJECT_0) ThrowLastError("WaitForSingleObject(ConPTY)");
	DWORD code = 0;
	if (!GetExitCodeProcess(m_process.hProcess, &code)) ThrowLastError("GetExitCodeProcess(ConPTY)");
	if (exit_code != nullptr) *exit_code = code;
	return true;
}

} // namespace darling::windows_host
