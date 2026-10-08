/*
 * This file is part of the Darling Windows port.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_errno.h"

#include <winsock2.h>
#include <windows.h>

namespace darling::windows_host {

namespace {
thread_local int current_errno = 0;
}

int DarwinErrno::Get() noexcept
{
	return current_errno;
}

int* DarwinErrno::Address() noexcept
{
	return &current_errno;
}

void DarwinErrno::Clear() noexcept
{
	current_errno = 0;
}

void DarwinErrno::Set(int value) noexcept
{
	if (value >= WSABASEERR) {
		SetFromWinsock(static_cast<std::uint32_t>(value));
		return;
	}
	current_errno = value;
}

void DarwinErrno::SetFromWin32(std::uint32_t value) noexcept
{
	switch (value) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		current_errno = 2;
		break;
	case ERROR_NOT_FOUND:
		current_errno = 2;
		break;
	case ERROR_BAD_EXE_FORMAT:
	case ERROR_EXE_MACHINE_TYPE_MISMATCH:
		current_errno = 8;
		break;
	case ERROR_MOD_NOT_FOUND:
	case ERROR_PROC_NOT_FOUND:
	case ERROR_DLL_NOT_FOUND:
		current_errno = 2;
		break;
	case ERROR_DLL_INIT_FAILED:
		current_errno = 80;
		break;
	case ERROR_ACCESS_DENIED:
		current_errno = 13;
		break;
	case ERROR_INVALID_HANDLE:
		current_errno = 9;
		break;
	case ERROR_INVALID_PARAMETER:
	case ERROR_INVALID_NAME:
	case ERROR_FILE_INVALID:
		current_errno = 22;
		break;
	case ERROR_INVALID_FUNCTION:
		current_errno = 78;
		break;
	case ERROR_NOT_SUPPORTED:
		current_errno = 45;
		break;
	case ERROR_BUSY:
	case ERROR_DEVICE_IN_USE:
		current_errno = 16;
		break;
	case ERROR_OPERATION_ABORTED:
		current_errno = 4;
		break;
	case ERROR_IO_PENDING:
		current_errno = 35;
		break;
	case ERROR_BROKEN_PIPE:
	case ERROR_NO_DATA:
		current_errno = 32;
		break;
	case ERROR_DIRECTORY:
		current_errno = 20;
		break;
	case ERROR_DIR_NOT_EMPTY:
		current_errno = 66;
		break;
	case ERROR_FILE_EXISTS:
		current_errno = 17;
		break;
	case ERROR_DISK_FULL:
		current_errno = 28;
		break;
	case ERROR_WRITE_PROTECT:
		current_errno = 30;
		break;
	case ERROR_TOO_MANY_OPEN_FILES:
		current_errno = 24;
		break;
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
		current_errno = 13;
		break;
	case ERROR_BAD_NETPATH:
	case ERROR_BAD_NET_NAME:
		current_errno = 2;
		break;
	case ERROR_NETWORK_UNREACHABLE:
		current_errno = 51;
		break;
	case ERROR_HOST_UNREACHABLE:
		current_errno = 65;
		break;
	case ERROR_NETWORK_BUSY:
		current_errno = 50;
		break;
	case ERROR_NETWORK_ACCESS_DENIED:
		current_errno = 13;
		break;
	case ERROR_CONNECTION_ABORTED:
		current_errno = 53;
		break;
	case ERROR_CONNECTION_REFUSED:
		current_errno = 61;
		break;
	case ERROR_CANCELLED:
		current_errno = 89;
		break;
	case ERROR_TIMEOUT:
	case ERROR_SEM_TIMEOUT:
		current_errno = 60;
		break;
	case ERROR_TOO_MANY_LINKS:
		current_errno = 62;
		break;
	case ERROR_INVALID_DRIVE:
		current_errno = 19;
		break;
	case ERROR_NOT_READY:
		current_errno = 6;
		break;
	case ERROR_FILE_TOO_LARGE:
		current_errno = 27;
		break;
	case ERROR_BUFFER_OVERFLOW:
		current_errno = 63;
		break;
	case ERROR_NOT_SAME_DEVICE:
		current_errno = 18;
		break;
	case ERROR_CANNOT_MAKE:
		current_errno = 13;
		break;
	case ERROR_PRIVILEGE_NOT_HELD:
		current_errno = 1;
		break;
	case ERROR_ALREADY_EXISTS:
		current_errno = 17;
		break;
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
		current_errno = 12;
		break;
	default:
		current_errno = 5;
		break;
	}
}

void DarwinErrno::SetFromWinsock(std::uint32_t value) noexcept
{
	switch (value) {
	case WSAEINTR: current_errno = 4; break;
	case WSAEBADF: current_errno = 9; break;
	case WSAEACCES: current_errno = 13; break;
	case WSAEFAULT: current_errno = 14; break;
	case WSAEINVAL: current_errno = 22; break;
	case WSAEMFILE: current_errno = 24; break;
	case WSAEWOULDBLOCK: current_errno = 35; break;
	case WSAEINPROGRESS: current_errno = 36; break;
	case WSAEALREADY: current_errno = 37; break;
	case WSAENOTSOCK: current_errno = 38; break;
	case WSAEDESTADDRREQ: current_errno = 39; break;
	case WSAEMSGSIZE: current_errno = 40; break;
	case WSAEPROTOTYPE: current_errno = 41; break;
	case WSAENOPROTOOPT: current_errno = 42; break;
	case WSAEPROTONOSUPPORT: current_errno = 43; break;
	case WSAESOCKTNOSUPPORT: current_errno = 44; break;
	case WSAEOPNOTSUPP: current_errno = 45; break;
	case WSAEPFNOSUPPORT: current_errno = 46; break;
	case WSAEAFNOSUPPORT: current_errno = 47; break;
	case WSAEADDRINUSE: current_errno = 48; break;
	case WSAEADDRNOTAVAIL: current_errno = 49; break;
	case WSAENETDOWN: current_errno = 50; break;
	case WSAENETUNREACH: current_errno = 51; break;
	case WSAENETRESET: current_errno = 52; break;
	case WSAECONNABORTED: current_errno = 53; break;
	case WSAECONNRESET: current_errno = 54; break;
	case WSAENOBUFS: current_errno = 55; break;
	case WSAEISCONN: current_errno = 56; break;
	case WSAENOTCONN: current_errno = 57; break;
	case WSAESHUTDOWN: current_errno = 58; break;
	case WSAETIMEDOUT: current_errno = 60; break;
	case WSAECONNREFUSED: current_errno = 61; break;
	case WSAEHOSTDOWN: current_errno = 64; break;
	case WSAEHOSTUNREACH: current_errno = 65; break;
	default: current_errno = 5; break;
	}
}

} // namespace darling::windows_host

extern "C" int* darling_windows_errno()
{
	return darling::windows_host::DarwinErrno::Address();
}
