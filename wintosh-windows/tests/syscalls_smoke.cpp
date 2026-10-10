/*
 * Stage 2 Darwin file-descriptor boundary proof.
 * GPL-3.0-only; see the bundled license and source manifests.
 */

#include "darling_windows_syscalls.h"
#include "darling_windows_errno.h"
#include "darling_windows_stdio.h"

#include <windows.h>
#include <ws2tcpip.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int wmain()
{
	try {
		const bool normalized_fd_symbols =
			darling_windows_host_symbol("open") != 0 &&
			darling_windows_host_symbol("read") != 0 &&
			darling_windows_host_symbol("write") != 0 &&
			darling_windows_host_symbol("close") != 0 &&
			darling_windows_host_symbol("dup") != 0 &&
			darling_windows_host_symbol("dup2") != 0 &&
			darling_windows_host_symbol("pipe") != 0 &&
			darling_windows_host_symbol("pipe2") != 0;
		std::cout << "DARWIN_NORMALIZED_FD_SYMBOLS="
			<< (normalized_fd_symbols ? "PASS" : "FAIL") << "\n";
		if (!normalized_fd_symbols)
			return 3;
		wchar_t temp_path[MAX_PATH]{};
		const DWORD length = GetTempPathW(MAX_PATH, temp_path);
		if (length == 0 || length >= MAX_PATH) {
			return 2;
		}
		const auto path = std::filesystem::path(temp_path) / L"darling-syscall-smoke.txt";
		const std::string payload = "DARLING-SYSCALL";
		{
			darling::windows_host::DarwinSyscalls syscalls;
			const int descriptor = syscalls.OpenWrite(path);
			if (syscalls.Write(descriptor, payload.data(), payload.size()) != payload.size()) {
				throw std::runtime_error("short syscall write");
			}
			BY_HANDLE_FILE_INFORMATION file_information{};
			syscalls.GetFileInformation(descriptor, &file_information);
			if (file_information.nFileSizeHigh != 0 ||
				file_information.nFileSizeLow != payload.size()) {
				throw std::runtime_error("syscall file metadata size mismatch");
			}
			darling_darwin_stat darwin_stat{};
			const int abi_descriptor = darling_windows_open(path.string().c_str(), 0x0000, 0);
			if (abi_descriptor < 0 || darling_windows_fstat(abi_descriptor, &darwin_stat) != 0 ||
				darwin_stat.st_size != static_cast<std::int64_t>(payload.size()) ||
				darwin_stat.st_blksize <= 0) {
				if (abi_descriptor >= 0) darling_windows_close(abi_descriptor);
				throw std::runtime_error("Darwin fstat metadata mismatch");
			}
			darling_windows_close(abi_descriptor);
			if (darling_windows_fstat(abi_descriptor, nullptr) != -1 ||
				darling::windows_host::DarwinErrno::Get() != 22) {
				throw std::runtime_error("Darwin fstat null-output validation mismatch");
			}
			darling_darwin_stat at_stat{};
			if (darling_windows_fstatat(-2, path.string().c_str(), &at_stat, 0) != 0 ||
				at_stat.st_size != darwin_stat.st_size) {
				throw std::runtime_error("Darwin fstatat metadata mismatch");
			}
			const int directory_descriptor = darling_windows_open(
				path.parent_path().string().c_str(), 0x00100000, 0);
			darling_darwin_stat relative_stat{};
			if (directory_descriptor < 0 ||
				darling_windows_fstatat(directory_descriptor, path.filename().string().c_str(),
					&relative_stat, 0) != 0 || relative_stat.st_size != darwin_stat.st_size) {
				if (directory_descriptor >= 0) darling_windows_close(directory_descriptor);
				throw std::runtime_error("Darwin relative fstatat metadata mismatch");
			}
			darling_windows_close(directory_descriptor);
			if (darling_windows_fstatat(-2, path.string().c_str(), &at_stat, 0x4000) != -1 ||
				darling::windows_host::DarwinErrno::Get() != 22) {
				throw std::runtime_error("Darwin fstatat invalid-flag validation mismatch");
			}
			darling_darwin_statfs filesystem_stat{};
			if (darling_windows_statfs(path.string().c_str(), &filesystem_stat) != 0 ||
				filesystem_stat.f_bsize <= 0 || filesystem_stat.f_blocks == 0 ||
				filesystem_stat.f_bfree > filesystem_stat.f_blocks) {
				throw std::runtime_error("Darwin statfs metadata mismatch");
			}
			syscalls.Flush(descriptor);
			syscalls.Close(descriptor);
			const int read_descriptor = syscalls.OpenRead(path);
			std::string result(payload.size(), '\0');
			if (syscalls.Read(read_descriptor, result.data(), result.size()) != result.size() ||
				result != payload) {
				throw std::runtime_error("syscall readback mismatch");
			}
			std::string offset_result(payload.size(), '\0');
			if (syscalls.ReadAt(read_descriptor, offset_result.data(),
				offset_result.size(), 0) != offset_result.size() ||
				offset_result != payload) {
				throw std::runtime_error("syscall offset read mismatch");
			}
			if (syscalls.Seek(read_descriptor, 0, FILE_BEGIN) != 0) {
				throw std::runtime_error("syscall seek rewind failed");
			}
			const int duplicate_descriptor = syscalls.Duplicate(read_descriptor);
			std::string duplicate_result(payload.size(), '\0');
			if (syscalls.Read(duplicate_descriptor, duplicate_result.data(),
				duplicate_result.size()) != duplicate_result.size() ||
				duplicate_result != payload) {
				syscalls.Close(duplicate_descriptor);
				throw std::runtime_error("syscall duplicate readback mismatch");
			}
			syscalls.Close(duplicate_descriptor);
			syscalls.Close(read_descriptor);
			const std::string updated_payload = "DARLING-UPDATED";
			const int read_write_descriptor = syscalls.OpenReadWrite(path);
			if (syscalls.WriteAt(read_write_descriptor, updated_payload.data(),
				updated_payload.size(), 0) != updated_payload.size()) {
				syscalls.Close(read_write_descriptor);
				throw std::runtime_error("syscall offset write mismatch");
			}
			std::string updated_result(updated_payload.size(), '\0');
			if (syscalls.ReadAt(read_write_descriptor, updated_result.data(),
				updated_result.size(), 0) != updated_result.size() ||
				updated_result != updated_payload) {
				syscalls.Close(read_write_descriptor);
				throw std::runtime_error("syscall offset write readback mismatch");
			}
			syscalls.Truncate(read_write_descriptor, updated_payload.size());
			syscalls.Lock(read_write_descriptor, 2);
			syscalls.Lock(read_write_descriptor, 8);
			syscalls.Close(read_write_descriptor);
			const int lock_owner = syscalls.OpenReadWrite(path);
			const int lock_contender = syscalls.OpenReadWrite(path);
			bool invalid_lock_rejected = false;
			try {
				syscalls.Lock(lock_owner, 0);
			} catch (const std::exception&) {
				invalid_lock_rejected = darling::windows_host::DarwinErrno::Get() == 22;
			}
			if (!invalid_lock_rejected)
				throw std::runtime_error("invalid lock operation did not return EINVAL");
			syscalls.Lock(lock_owner, 2);
			bool contention_rejected = false;
			try {
				syscalls.Lock(lock_contender, 2 | 4);
			} catch (const std::exception&) {
				contention_rejected = true;
			}
			syscalls.Lock(lock_owner, 8);
			syscalls.Close(lock_contender);
			syscalls.Close(lock_owner);
			if (!contention_rejected)
				throw std::runtime_error("nonblocking lock contention was accepted");
			const int shared_owner = syscalls.OpenReadWrite(path);
			const int shared_peer = syscalls.OpenReadWrite(path);
			syscalls.Lock(shared_owner, 1);
			syscalls.Lock(shared_peer, 1);
			syscalls.Lock(shared_peer, 8);
			syscalls.Lock(shared_owner, 8);
			syscalls.Close(shared_peer);
			syscalls.Close(shared_owner);
			int pipe_descriptors[2]{};
			syscalls.CreatePipe(pipe_descriptors);
			syscalls.SetDescriptorFlags(pipe_descriptors[0], 0x0004);
		syscalls.SetDescriptorFdFlags(pipe_descriptors[0], 1);
		if ((syscalls.GetDescriptorFdFlags(pipe_descriptors[0]) & 1) == 0)
			throw std::runtime_error("close-on-exec descriptor flag was not retained");
			char pipe_byte = 0;
			bool would_block = false;
			try {
				(void)syscalls.Read(pipe_descriptors[0], &pipe_byte, 1);
			} catch (const std::exception&) {
				would_block = darling::windows_host::DarwinErrno::Get() == 35;
			}
			if (!would_block || (syscalls.GetDescriptorFlags(pipe_descriptors[0]) & 0x0004) == 0) {
				throw std::runtime_error("nonblocking pipe read did not report EAGAIN");
			}
			const int duplicated_read = syscalls.DuplicateAtLeast(pipe_descriptors[0], 40);
			if ((syscalls.GetDescriptorFlags(duplicated_read) & 0x0004) == 0 ||
				(syscalls.GetDescriptorFdFlags(duplicated_read) & 1) != 0 ||
				(syscalls.GetDescriptorFdFlags(pipe_descriptors[0]) & 1) == 0) {
				throw std::runtime_error("dup did not share pipe status flags");
			}
			syscalls.Close(duplicated_read);
			const char pipe_payload = 'P';
			if (syscalls.Write(pipe_descriptors[1], &pipe_payload, 1) != 1) {
				throw std::runtime_error("nonblocking pipe write failed");
			}
			int select_pipe[2]{-1, -1};
			const char select_payload = 'S';
			if (darling_windows_pipe(select_pipe) != 0 ||
				darling_windows_write(select_pipe[1], &select_payload, 1) != 1) {
				if (select_pipe[0] >= 0) darling_windows_close(select_pipe[0]);
				if (select_pipe[1] >= 0) darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin select pipe setup failed");
			}
			darling_fd_set select_read_set{};
			select_read_set.fds_bits[select_pipe[0] / 32] |=
				static_cast<std::int32_t>(1) << (select_pipe[0] % 32);
			darling_timeval select_timeout{0, 0};
			if (darling_windows_select(select_pipe[0] + 1, &select_read_set,
				nullptr, nullptr, &select_timeout) != 1 ||
				(select_read_set.fds_bits[select_pipe[0] / 32] &
					(static_cast<std::int32_t>(1) << (select_pipe[0] % 32))) == 0) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin select pipe readiness failed");
			}
			darling_timeval invalid_select_timeout{0, 1'000'000};
			if (darling_windows_select(0, nullptr, nullptr, nullptr,
				&invalid_select_timeout) != -1 ||
				darling::windows_host::DarwinErrno::Get() != 22) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin select timeout validation failed");
			}
			darling_fd_set pselect_write_set{};
			pselect_write_set.fds_bits[select_pipe[1] / 32] |=
				static_cast<std::int32_t>(1) << (select_pipe[1] % 32);
			const darling_timespec pselect_timeout{0, 0};
			if (darling_windows_pselect(select_pipe[1] + 1, nullptr,
				&pselect_write_set, nullptr, &pselect_timeout, nullptr) != 1 ||
				(pselect_write_set.fds_bits[select_pipe[1] / 32] &
					(static_cast<std::int32_t>(1) << (select_pipe[1] % 32))) == 0) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin pselect pipe readiness failed");
			}
			darling_fd_set masked_pselect_write_set{};
			masked_pselect_write_set.fds_bits[select_pipe[1] / 32] |=
				static_cast<std::int32_t>(1) << (select_pipe[1] % 32);
			const darling_darwin_sigset empty_signal_mask{};
			if (darling_windows_pselect(select_pipe[1] + 1, nullptr,
				&masked_pselect_write_set, nullptr, &pselect_timeout,
				&empty_signal_mask) != 1 ||
				(masked_pselect_write_set.fds_bits[select_pipe[1] / 32] &
					(static_cast<std::int32_t>(1) << (select_pipe[1] % 32))) == 0) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin pselect signal-mask path failed");
			}
			const darling_timespec invalid_pselect_timeout{0, 1'000'000'000};
			if (darling_windows_pselect(0, nullptr, nullptr, nullptr,
				&invalid_pselect_timeout, nullptr) != -1 ||
				darling::windows_host::DarwinErrno::Get() != 22) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin pselect timeout validation failed");
			}
			darling::windows_host::darling_pollfd invalid_poll_descriptor{9999, 0x0001, 0};
			if (darling_windows_poll(&invalid_poll_descriptor, 1, 0) != 1 ||
				(invalid_poll_descriptor.revents & 0x0020) == 0) {
				darling_windows_close(select_pipe[0]);
				darling_windows_close(select_pipe[1]);
				throw std::runtime_error("Darwin invalid poll descriptor handling failed");
			}
			darling_windows_close(select_pipe[0]);
			darling_windows_close(select_pipe[1]);
			darling::windows_host::darling_pollfd poll_descriptor{};
			poll_descriptor.fd = pipe_descriptors[0];
			poll_descriptor.events = 0x0001;
			if (syscalls.Poll(&poll_descriptor, 1, 1000) != 1 ||
				(poll_descriptor.revents & 0x0001) == 0 ||
				syscalls.Read(pipe_descriptors[0], &pipe_byte, 1) != 1 || pipe_byte != pipe_payload) {
				throw std::runtime_error("nonblocking pipe poll/data path failed");
			}
			syscalls.Close(pipe_descriptors[0]);
			syscalls.Close(pipe_descriptors[1]);
			int socket_pair[2]{};
			constexpr int darwin_sock_cloexec = 0x10000000;
			constexpr int darwin_sock_nonblock = 0x20000000;
			if (darling_windows_socketpair(1, 1 | darwin_sock_cloexec | darwin_sock_nonblock,
				0, socket_pair) != 0 ||
				(darling_windows_fcntl(socket_pair[0], 3) & 0x0004) == 0 ||
				(darling_windows_fcntl(socket_pair[1], 3) & 0x0004) == 0 ||
				(darling_windows_fcntl(socket_pair[0], 1) & 1) == 0 ||
				(darling_windows_fcntl(socket_pair[1], 1) & 1) == 0) {
				throw std::runtime_error("Darwin socketpair creation failed");
			}
			const char socket_payload = 'S';
			char socket_result = 0;
			int socket_address_length = 0;
			if (darling_windows_sendto(socket_pair[0], &socket_payload, 1, 0,
				nullptr, 0) != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socketpair send path failed");
			}
			darling_fd_set socket_read_set{};
			socket_read_set.fds_bits[socket_pair[1] / 32] |=
				static_cast<std::int32_t>(1) << (socket_pair[1] % 32);
			darling_timeval socket_select_timeout{0, 0};
			if (darling_windows_select(socket_pair[1] + 1, &socket_read_set,
				nullptr, nullptr, &socket_select_timeout) != 1 ||
				(socket_read_set.fds_bits[socket_pair[1] / 32] &
					(static_cast<std::int32_t>(1) << (socket_pair[1] % 32))) == 0 ||
				darling_windows_recvfrom(socket_pair[1], &socket_result, 1, 0,
					nullptr, &socket_address_length) != 1 || socket_result != socket_payload) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socketpair data path failed");
			}
			darling_fd_set socket_write_set{};
			socket_write_set.fds_bits[socket_pair[0] / 32] |=
				static_cast<std::int32_t>(1) << (socket_pair[0] % 32);
			darling_timeval socket_write_timeout{0, 0};
			if (darling_windows_select(socket_pair[0] + 1, nullptr,
				&socket_write_set, nullptr, &socket_write_timeout) != 1 ||
				(socket_write_set.fds_bits[socket_pair[0] / 32] &
					(static_cast<std::int32_t>(1) << (socket_pair[0] % 32))) == 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socketpair write readiness failed");
			}
			int socket_buffer = 0;
			int socket_buffer_length = sizeof(socket_buffer);
			if (darling_windows_getsockopt(socket_pair[0], SOL_SOCKET, SO_SNDBUF,
				&socket_buffer, &socket_buffer_length) != 0 || socket_buffer <= 0 ||
				socket_buffer_length != static_cast<int>(sizeof(socket_buffer))) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket option read failed");
			}
			const int requested_socket_buffer = socket_buffer;
			if (darling_windows_setsockopt(socket_pair[0], SOL_SOCKET, SO_SNDBUF,
				&requested_socket_buffer, sizeof(requested_socket_buffer)) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket option write failed");
			}
			constexpr int darwin_so_nosigpipe = 0x1022;
			constexpr int darwin_so_reuseport = 0x0200;
			const int enabled_socket_option = 1;
			int darwin_option_result = 0;
			int darwin_option_length = sizeof(darwin_option_result);
			if (darling_windows_setsockopt(socket_pair[0], SOL_SOCKET, darwin_so_nosigpipe,
				&enabled_socket_option, sizeof(enabled_socket_option)) != 0 ||
				darling_windows_getsockopt(socket_pair[0], SOL_SOCKET, darwin_so_nosigpipe,
					&darwin_option_result, &darwin_option_length) != 0 ||
				darwin_option_result != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin SO_NOSIGPIPE option failed");
			}
			darwin_option_result = 0;
			darwin_option_length = sizeof(darwin_option_result);
			if (darling_windows_setsockopt(socket_pair[0], SOL_SOCKET, darwin_so_reuseport,
				&enabled_socket_option, sizeof(enabled_socket_option)) != 0 ||
				darling_windows_getsockopt(socket_pair[0], SOL_SOCKET, darwin_so_reuseport,
					&darwin_option_result, &darwin_option_length) != 0 ||
				darwin_option_result != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin SO_REUSEPORT option failed");
			}
			in_addr converted_address{};
			char address_text[16]{};
			if (darling_windows_inet_pton(AF_INET, "192.0.2.17", &converted_address) != 1 ||
				darling_windows_inet_ntop(AF_INET, &converted_address, address_text,
					sizeof(address_text)) == nullptr ||
				std::string(address_text) != "192.0.2.17") {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin IPv4 conversion failed");
			}
			unsigned char converted_ipv6[16]{};
			char ipv6_text[46]{};
			if (darling_windows_inet_pton(AF_INET6, "2001:db8::17", converted_ipv6) != 1 ||
				darling_windows_inet_ntop(AF_INET6, converted_ipv6, ipv6_text,
					sizeof(ipv6_text)) == nullptr ||
				std::string(ipv6_text) != "2001:db8::17") {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin IPv6 conversion failed");
			}
			if (darling_windows_inet_pton(AF_INET, "192.0.2.999", &converted_address) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin invalid IPv4 conversion accepted");
			}
			if (darling_windows_inet_pton(AF_INET, nullptr, &converted_address) == 0 ||
				darling_windows_inet_pton(AF_INET, "127.0.0.1", nullptr) == 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin inet_pton null validation failed");
			}
			darling_addrinfo resolver_hints{};
			resolver_hints.ai_family = AF_INET;
			resolver_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* resolver_result = nullptr;
			if (darling_windows_getaddrinfo("127.0.0.1", "80", &resolver_hints,
				&resolver_result) != 0 || resolver_result == nullptr ||
				resolver_result->ai_family != AF_INET ||
				resolver_result->ai_socktype != SOCK_STREAM ||
				resolver_result->ai_addr == nullptr || resolver_result->ai_addrlen == 0) {
				darling_windows_freeaddrinfo(resolver_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin getaddrinfo failed");
			}
			darling_windows_freeaddrinfo(resolver_result);
			sockaddr_in numeric_address{};
			numeric_address.sin_family = AF_INET;
			numeric_address.sin_port = htons(80);
			if (darling_windows_inet_pton(AF_INET, "127.0.0.1",
				&numeric_address.sin_addr) != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin numeric address setup failed");
			}
			char numeric_host[64]{};
			char numeric_service[16]{};
			if (darling_windows_getnameinfo(&numeric_address, sizeof(numeric_address),
				numeric_host, sizeof(numeric_host), numeric_service, sizeof(numeric_service),
				0x02 | 0x08) != 0 || std::string(numeric_host) != "127.0.0.1" ||
				std::string(numeric_service) != "80") {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin getnameinfo failed");
			}
			sockaddr_in6 numeric_ipv6_address{};
			numeric_ipv6_address.sin6_family = AF_INET6;
			numeric_ipv6_address.sin6_port = htons(443);
			if (darling_windows_inet_pton(AF_INET6, "2001:db8::17",
				&numeric_ipv6_address.sin6_addr) != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin IPv6 address setup failed");
			}
			char numeric_ipv6_host[64]{};
			char numeric_ipv6_service[16]{};
			if (darling_windows_getnameinfo(&numeric_ipv6_address, sizeof(numeric_ipv6_address),
				numeric_ipv6_host, sizeof(numeric_ipv6_host), numeric_ipv6_service,
				sizeof(numeric_ipv6_service), 0x02 | 0x08) != 0 ||
				std::string(numeric_ipv6_host) != "2001:db8::17" ||
				std::string(numeric_ipv6_service) != "443") {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin IPv6 getnameinfo failed");
			}
			if (darling_windows_getnameinfo(&numeric_address, sizeof(numeric_address),
				nullptr, sizeof(numeric_host), numeric_service, sizeof(numeric_service),
				0) == 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin getnameinfo invalid buffer accepted");
			}
			const char message_payload[] = "MSG";
			darling_iovec send_vector{const_cast<char*>(message_payload), 3};
			darling_msghdr send_message{};
			send_message.msg_iov = &send_vector;
			send_message.msg_iovlen = 1;
			char message_result[4]{};
			darling_iovec receive_vector{message_result, 3};
			darling_msghdr receive_message{};
			receive_message.msg_iov = &receive_vector;
			receive_message.msg_iovlen = 1;
			if (darling_windows_sendmsg(socket_pair[0], &send_message, 0) != 3 ||
				darling_windows_recvmsg(socket_pair[1], &receive_message, 0) != 3 ||
				std::string(message_result, 3) != "MSG") {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket message path failed");
			}
			darling_msghdr invalid_control_message{};
			invalid_control_message.msg_iov = &send_vector;
			invalid_control_message.msg_iovlen = 1;
			invalid_control_message.msg_control = message_result;
			invalid_control_message.msg_controllen = 1;
			if (darling_windows_sendmsg(socket_pair[0], &invalid_control_message, 0) == 3) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin invalid control message accepted");
			}
			int rights_pipe[2]{-1, -1};
			if (darling_windows_pipe(rights_pipe) != 0 ||
				darling_windows_write(rights_pipe[1], "RIGHT", 5) != 5) {
				if (rights_pipe[0] >= 0) darling_windows_close(rights_pipe[0]);
				if (rights_pipe[1] >= 0) darling_windows_close(rights_pipe[1]);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin rights pipe setup failed");
			}
			unsigned char rights_control[64]{};
			const int rights_values[1]{rights_pipe[0]};
			const darling_cmsghdr rights_header{
				static_cast<std::uint32_t>(sizeof(darling_cmsghdr) + sizeof(rights_values)), 0xffff, 1};
			std::memcpy(rights_control, &rights_header, sizeof(rights_header));
			std::memcpy(rights_control + sizeof(rights_header), rights_values, sizeof(rights_values));
			char rights_value = 'R';
			darling_iovec rights_send_vector{&rights_value, 1};
			darling_msghdr rights_send_message{};
			rights_send_message.msg_iov = &rights_send_vector;
			rights_send_message.msg_iovlen = 1;
			rights_send_message.msg_control = rights_control;
			rights_send_message.msg_controllen = sizeof(rights_header) + sizeof(int);
		unsigned char received_control[64]{};
			char received_value = 0;
			darling_iovec rights_receive_vector{&received_value, 1};
			darling_msghdr rights_receive_message{};
			rights_receive_message.msg_iov = &rights_receive_vector;
			rights_receive_message.msg_iovlen = 1;
			rights_receive_message.msg_control = received_control;
			rights_receive_message.msg_controllen = sizeof(received_control);
			const auto* received_header = reinterpret_cast<const darling_cmsghdr*>(received_control);
			const auto rights_sent = darling_windows_sendmsg(socket_pair[0], &rights_send_message, 0);
			const auto rights_received = rights_sent == 1 ?
				darling_windows_recvmsg(socket_pair[1], &rights_receive_message, 0) : -1;
			if (rights_sent != 1 || rights_received != 1 || received_value != 'R' ||
				received_header->cmsg_len <
					static_cast<std::uint32_t>(sizeof(darling_cmsghdr) + sizeof(rights_values))) {
				darling_windows_close(rights_pipe[0]);
				darling_windows_close(rights_pipe[1]);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin rights message transport failed");
			}
			int received_rights[1]{-1};
			std::memcpy(received_rights, received_control + sizeof(darling_cmsghdr), sizeof(received_rights));
			char rights_readback[6]{};
			const bool rights_ok = received_rights[0] >= 0 &&
				darling_windows_read(received_rights[0], rights_readback, 5) == 5 &&
				std::string(rights_readback, 5) == "RIGHT";
			if (received_rights[0] >= 0) darling_windows_close(received_rights[0]);
			if (!rights_ok) {
				darling_windows_close(rights_pipe[0]);
				darling_windows_close(rights_pipe[1]);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin rights descriptor transfer failed");
			}
			darling_windows_close(rights_pipe[0]);
			darling_windows_close(rights_pipe[1]);
			if (darling_windows_sendmsg(socket_pair[0], nullptr, 0) != -1 ||
				darling_windows_recvmsg(socket_pair[1], nullptr, 0) != -1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket message null validation failed");
			}
			char dont_wait_value = 0;
			darling_iovec dont_wait_vector{&dont_wait_value, 1};
			darling_msghdr dont_wait_message{};
			dont_wait_message.msg_iov = &dont_wait_vector;
			dont_wait_message.msg_iovlen = 1;
			if (darling_windows_recvmsg(socket_pair[1], &dont_wait_message, 0x0080) != -1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_DONTWAIT was ignored");
			}
			const char invalid_flag_value = 'F';
			darling_iovec invalid_flag_vector{const_cast<char*>(&invalid_flag_value), 1};
			darling_msghdr invalid_flag_message{};
			invalid_flag_message.msg_iov = &invalid_flag_vector;
			invalid_flag_message.msg_iovlen = 1;
			if (darling_windows_sendmsg(socket_pair[0], &invalid_flag_message, 0x4000) != -1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin unsupported message flag accepted");
			}
			const char peek_value = 'K';
			if (darling_windows_sendto(socket_pair[0], &peek_value, 1, 0, nullptr, 0) != 1) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_PEEK setup failed");
			}
			char peek_buffer = 0;
			darling_iovec peek_vector{&peek_buffer, 1};
			darling_msghdr peek_message{};
			peek_message.msg_iov = &peek_vector;
			peek_message.msg_iovlen = 1;
			if (darling_windows_recvmsg(socket_pair[1], &peek_message, 0x0002) != 1 ||
				peek_buffer != peek_value) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_PEEK read failed");
			}
			peek_buffer = 0;
			if (darling_windows_recvmsg(socket_pair[1], &peek_message, 0) != 1 ||
				peek_buffer != peek_value) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_PEEK consumed data");
			}
			const char waitall_value[] = "ALL";
			if (darling_windows_sendto(socket_pair[0], waitall_value, 3, 0, nullptr, 0) != 3) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_WAITALL setup failed");
			}
			char waitall_buffer[3]{};
			darling_iovec waitall_vector{waitall_buffer, sizeof(waitall_buffer)};
			darling_msghdr waitall_message{};
			waitall_message.msg_iov = &waitall_vector;
			waitall_message.msg_iovlen = 1;
			if (darling_windows_recvmsg(socket_pair[1], &waitall_message, 0x0040) != 3 ||
				std::memcmp(waitall_buffer, waitall_value, 3) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin MSG_WAITALL read failed");
			}
			darling_addrinfo* hostname_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "80", nullptr,
				&hostname_result) != 0 || hostname_result == nullptr ||
				hostname_result->ai_addr == nullptr || hostname_result->ai_addrlen == 0) {
				darling_windows_freeaddrinfo(hostname_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin hostname resolution failed");
			}
			darling_windows_freeaddrinfo(hostname_result);
			darling_addrinfo service_result_hints{};
			service_result_hints.ai_family = AF_INET;
			service_result_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* service_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "http", &service_result_hints,
				&service_result) != 0 || service_result == nullptr ||
				service_result->ai_addr == nullptr || service_result->ai_addrlen < sizeof(sockaddr_in) ||
				reinterpret_cast<const sockaddr_in*>(service_result->ai_addr)->sin_port != htons(80)) {
				darling_windows_freeaddrinfo(service_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin service resolution failed");
			}
			darling_windows_freeaddrinfo(service_result);
			darling_addrinfo numeric_host_hints{};
			numeric_host_hints.ai_flags = 0x04;
			numeric_host_hints.ai_family = AF_INET;
			numeric_host_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* numeric_host_result = nullptr;
			if (darling_windows_getaddrinfo("127.0.0.1", "80", &numeric_host_hints,
				&numeric_host_result) != 0 || numeric_host_result == nullptr) {
				darling_windows_freeaddrinfo(numeric_host_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_NUMERICHOST numeric path failed");
			}
			darling_windows_freeaddrinfo(numeric_host_result);
			numeric_host_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "80", &numeric_host_hints,
				&numeric_host_result) == 0 || numeric_host_result != nullptr) {
				darling_windows_freeaddrinfo(numeric_host_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_NUMERICHOST hostname path failed");
			}
			darling_addrinfo numeric_service_hints{};
			numeric_service_hints.ai_flags = 0x1000;
			numeric_service_hints.ai_family = AF_INET;
			numeric_service_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* numeric_service_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "80", &numeric_service_hints,
				&numeric_service_result) != 0 || numeric_service_result == nullptr) {
				darling_windows_freeaddrinfo(numeric_service_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_NUMERICSERV numeric path failed");
			}
			darling_windows_freeaddrinfo(numeric_service_result);
			numeric_service_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "http", &numeric_service_hints,
				&numeric_service_result) == 0 || numeric_service_result != nullptr) {
				darling_windows_freeaddrinfo(numeric_service_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_NUMERICSERV service path failed");
			}
			darling_addrinfo canonname_hints{};
			canonname_hints.ai_flags = 0x02;
			canonname_hints.ai_family = AF_INET;
			canonname_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* canonname_result = nullptr;
			if (darling_windows_getaddrinfo("localhost", "80", &canonname_hints,
				&canonname_result) != 0 || canonname_result == nullptr ||
				canonname_result->ai_canonname == nullptr ||
				canonname_result->ai_canonname[0] == '\0') {
				darling_windows_freeaddrinfo(canonname_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_CANONNAME path failed");
			}
			darling_windows_freeaddrinfo(canonname_result);
			darling_addrinfo passive_hints{};
			passive_hints.ai_flags = 0x01;
			passive_hints.ai_family = AF_INET;
			passive_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* passive_result = nullptr;
			if (darling_windows_getaddrinfo(nullptr, "80", &passive_hints,
				&passive_result) != 0 || passive_result == nullptr ||
				passive_result->ai_addr == nullptr || passive_result->ai_addrlen < sizeof(sockaddr_in)) {
				darling_windows_freeaddrinfo(passive_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin AI_PASSIVE path failed");
			}
			darling_windows_freeaddrinfo(passive_result);
			darling_addrinfo passive_ipv6_hints{};
			passive_ipv6_hints.ai_flags = 0x01;
			passive_ipv6_hints.ai_family = AF_INET6;
			passive_ipv6_hints.ai_socktype = SOCK_STREAM;
			darling_addrinfo* passive_ipv6_result = nullptr;
			if (darling_windows_getaddrinfo(nullptr, "443", &passive_ipv6_hints,
				&passive_ipv6_result) != 0 || passive_ipv6_result == nullptr ||
				passive_ipv6_result->ai_addr == nullptr ||
				passive_ipv6_result->ai_addrlen < sizeof(sockaddr_in6)) {
				darling_windows_freeaddrinfo(passive_ipv6_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin IPv6 AI_PASSIVE path failed");
			}
			darling_windows_freeaddrinfo(passive_ipv6_result);
			darling_addrinfo* invalid_hostname_result = nullptr;
			if (darling_windows_getaddrinfo("darling-invalid-host.invalid", "80", nullptr,
				&invalid_hostname_result) == 0 || invalid_hostname_result != nullptr) {
				darling_windows_freeaddrinfo(invalid_hostname_result);
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin invalid hostname handling failed");
			}
			darling_fd_set empty_socket_read_set{};
			empty_socket_read_set.fds_bits[socket_pair[1] / 32] |=
				static_cast<std::int32_t>(1) << (socket_pair[1] % 32);
			darling_timeval empty_socket_timeout{0, 0};
			if (darling_windows_select(socket_pair[1] + 1, &empty_socket_read_set,
				nullptr, nullptr, &empty_socket_timeout) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin empty socket readiness mismatch");
			}
			darling_fd_set empty_socket_exception_set{};
			empty_socket_exception_set.fds_bits[socket_pair[1] / 32] |=
				static_cast<std::int32_t>(1) << (socket_pair[1] % 32);
			darling_timeval empty_exception_timeout{0, 0};
			if (darling_windows_select(socket_pair[1] + 1, nullptr, nullptr,
				&empty_socket_exception_set, &empty_exception_timeout) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin empty socket exception mismatch");
			}
			if (darling_windows_shutdown(socket_pair[0], 1) != 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket shutdown failed");
			}
			darling_fd_set socket_hangup_read_set{};
			socket_hangup_read_set.fds_bits[socket_pair[1] / 32] |=
				static_cast<std::int32_t>(1) << (socket_pair[1] % 32);
			darling_timeval socket_hangup_timeout{0, 0};
			if (darling_windows_select(socket_pair[1] + 1, &socket_hangup_read_set,
				nullptr, nullptr, &socket_hangup_timeout) != 1 ||
				(socket_hangup_read_set.fds_bits[socket_pair[1] / 32] &
					(static_cast<std::int32_t>(1) << (socket_pair[1] % 32))) == 0) {
				darling_windows_close(socket_pair[0]);
				darling_windows_close(socket_pair[1]);
				throw std::runtime_error("Darwin socket shutdown readiness failed");
			}
			darling_windows_close(socket_pair[0]);
			darling_windows_close(socket_pair[1]);
		}
		std::filesystem::remove(path);
		std::cout << "DARWIN_SYSCALL_FILE_IO=PASS\n";
		std::cout << "DARWIN_SYSCALL_PIPE_NONBLOCK=PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "SYSCALL_SMOKE_ERROR=" << error.what() << "\n";
		return 1;
	}
}
