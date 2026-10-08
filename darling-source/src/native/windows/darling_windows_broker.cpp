/*
 * Stage 1 Darling Windows host broker.
 *
 * Copyright (C) 2026 Darling Windows port contributors
 *
 * Licensed under the GNU General Public License, version 3.
 */

#include "darling_windows_runtime.h"

#include <iostream>

int wmain(int argc, wchar_t** argv)
{
	try {
		if (argc != 3) {
			std::wcerr << L"usage: darling_windows_broker <prefix> <pipe-name>\n";
			return 2;
		}

		auto server = darling::windows_host::NamedPipeRpcServer::Create(argv[2]);
		// Publish the IPC endpoint before filesystem bootstrap so the parent can
		// synchronize with a live broker even when prefix creation is slow.
		const auto prefix = darling::windows_host::Prefix::Create(argv[1]);
		server.WaitForClient();

		for (;;) {
			const auto request = server.Read();
			if (request == "PING") {
				server.Write("PONG");
				continue;
			}
			if (request == "SHUTDOWN") {
				server.Write("BYE");
				if (server.Read() != "ACK") {
					return 3;
				}
				return 0;
			}
			server.Write("UNKNOWN_REQUEST");
		}
	} catch (const std::exception& error) {
		std::cerr << "darling_windows_broker: " << error.what() << "\n";
		return 1;
	}
}
