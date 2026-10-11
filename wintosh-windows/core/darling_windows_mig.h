/* This file is part of the Darling Windows port. Licensed under GPL-3.0. */
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace darling::windows_host {
class MigRoutineRegistry final {
public:
	using Handler = std::function<std::vector<std::uint8_t>(std::span<const std::uint8_t>)>;
	struct Routine { std::uint32_t routine_id = 0; std::size_t minimum_request_size = 0; std::size_t maximum_request_size = 0; std::size_t maximum_reply_size = 0; Handler handler; };
	bool Register(Routine routine);
	bool Unregister(std::uint32_t routine_id);
	[[nodiscard]] bool Contains(std::uint32_t routine_id) const noexcept;
	[[nodiscard]] std::vector<std::uint8_t> Dispatch(std::uint32_t routine_id, std::span<const std::uint8_t> request) const;
private:
	std::unordered_map<std::uint32_t, Routine> m_routines;
};
}
