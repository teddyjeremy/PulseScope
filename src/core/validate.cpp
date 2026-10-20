#include "pulsescope/validate.hpp"

namespace pulsescope {

Status ValidateText(std::string_view text, std::size_t maxBytes) {
  if (text.size() > maxBytes) {
    return Status{ErrorCode::LimitExceeded, "text exceeds the allowed length"};
  }
  for (unsigned char c : text) {
    if (c == '\0') return Status{ErrorCode::InvalidArgument, "text contains a NUL"};
  }
  return Status::Ok();
}

Status ValidateAllocation(std::uint64_t bytes, std::uint64_t limit) {
  if (bytes > limit) return Status{ErrorCode::LimitExceeded, "allocation request exceeds the configured limit"};
  return Status::Ok();
}

bool IsSuspectCounter(std::uint64_t bytes) { return bytes > (256ull << 40); }

}  // namespace pulsescope
