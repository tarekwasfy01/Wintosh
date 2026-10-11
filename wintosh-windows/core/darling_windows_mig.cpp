/* This file is part of the Darling Windows port. Licensed under GPL-3.0. */
#include "darling_windows_mig.h"
namespace darling::windows_host {
bool MigRoutineRegistry::Register(Routine routine) {
	if (routine.routine_id == 0 || !routine.handler || routine.minimum_request_size > routine.maximum_request_size || routine.maximum_reply_size == 0) return false;
	return m_routines.emplace(routine.routine_id, std::move(routine)).second;
}
bool MigRoutineRegistry::Unregister(std::uint32_t routine_id) { return m_routines.erase(routine_id) != 0; }
bool MigRoutineRegistry::Contains(std::uint32_t routine_id) const noexcept { return m_routines.find(routine_id) != m_routines.end(); }
std::vector<std::uint8_t> MigRoutineRegistry::Dispatch(std::uint32_t routine_id, std::span<const std::uint8_t> request) const {
	const auto found = m_routines.find(routine_id);
	if (found == m_routines.end() || request.size() < found->second.minimum_request_size || request.size() > found->second.maximum_request_size) return {};
	auto reply = found->second.handler(request);
	if (reply.size() > found->second.maximum_reply_size) return {};
	return reply;
}
}
