/* Native OOL ownership bookkeeping; GPL-3.0-only. */
#include "darling_windows_mach_ool.h"

#include <mutex>
#include <unordered_map>

namespace darling::windows_host {

bool MachOolOwnershipTable::Register(void* address, std::size_t bytes, Release release)
{
	if (address == nullptr || bytes == 0 || !release) return false;
	std::lock_guard lock(mutex_);
	return entries_.emplace(address, Entry{bytes, 0, 0, std::move(release)}).second;
}

bool MachOolOwnershipTable::RetainQueue(void* address)
{
	std::lock_guard lock(mutex_);
	const auto found = entries_.find(address);
	if (found == entries_.end()) return false;
	++found->second.queue_refs;
	return true;
}

bool MachOolOwnershipTable::RetainReceiver(void* address)
{
	std::lock_guard lock(mutex_);
	const auto found = entries_.find(address);
	if (found == entries_.end()) return false;
	++found->second.receiver_refs;
	return true;
}

bool MachOolOwnershipTable::ReleaseQueue(void* address)
{
	Release release;
	{
		std::lock_guard lock(mutex_);
		const auto found = entries_.find(address);
		if (found == entries_.end() || found->second.queue_refs == 0) return false;
		--found->second.queue_refs;
		if (found->second.queue_refs == 0 && found->second.receiver_refs == 0) {
			release = std::move(found->second.release);
			entries_.erase(found);
		}
	}
	if (release) release(address);
	return true;
}

bool MachOolOwnershipTable::ReleaseReceiver(void* address)
{
	Release release;
	{
		std::lock_guard lock(mutex_);
		const auto found = entries_.find(address);
		if (found == entries_.end() || found->second.receiver_refs == 0) return false;
		--found->second.receiver_refs;
		if (found->second.queue_refs == 0 && found->second.receiver_refs == 0) {
			release = std::move(found->second.release);
			entries_.erase(found);
		}
	}
	if (release) release(address);
	return true;
}

bool MachOolOwnershipTable::Contains(void* address) const
{
	std::lock_guard lock(mutex_);
	return entries_.find(address) != entries_.end();
}

std::size_t MachOolOwnershipTable::Bytes(void* address) const
{
	std::lock_guard lock(mutex_);
	const auto found = entries_.find(address);
	return found == entries_.end() ? 0 : found->second.bytes;
}

} // namespace darling::windows_host
