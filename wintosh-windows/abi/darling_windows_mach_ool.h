/* Native OOL ownership bookkeeping contract; GPL-3.0-only. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace darling::windows_host {

class MachOolOwnershipTable final {
public:
	using Release = std::function<void(void*)>;

	bool Register(void* address, std::size_t bytes, Release release);
	bool RetainQueue(void* address);
	bool RetainReceiver(void* address);
	bool ReleaseQueue(void* address);
	bool ReleaseReceiver(void* address);
	bool Contains(void* address) const;
	std::size_t Bytes(void* address) const;

private:
	struct Entry final {
		std::size_t bytes = 0;
		std::uint32_t queue_refs = 0;
		std::uint32_t receiver_refs = 0;
		Release release;
	};

	mutable std::mutex mutex_;
	std::unordered_map<void*, Entry> entries_;
};

} // namespace darling::windows_host
