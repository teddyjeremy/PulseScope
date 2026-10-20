#pragma once

#include "pulsescope/status.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pulsescope {

Status ValidateText(std::string_view text, std::size_t maxBytes);
Status ValidateAllocation(std::uint64_t bytes, std::uint64_t limit);
bool IsSuspectCounter(std::uint64_t bytes);

}  // namespace pulsescope
