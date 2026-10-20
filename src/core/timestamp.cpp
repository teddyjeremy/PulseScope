#include "pulsescope/timestamp.hpp"

#include <cmath>
#include <sstream>
#include <iomanip>

namespace pulsescope {

bool QpcDeltaToTicks(std::uint64_t deltaQpc, std::uint64_t frequency, std::uint64_t& outTicks) {
  outTicks = 0;
  if (frequency == 0) {
    return false;
  }
  // (delta / freq) * scale + (delta % freq) * scale / freq
  // The second term fits in 64 bits when scale < 2^32, which it does
  // (1e7), as long as (delta % freq) <= freq-1 and freq itself fits.
  constexpr std::uint64_t kScale = kTicksPerSecond;
  const std::uint64_t whole = deltaQpc / frequency;
  const std::uint64_t rem = deltaQpc % frequency;
  if (whole > UINT64_MAX / kScale) {
    outTicks = UINT64_MAX;
    return true;
  }
  const std::uint64_t hi = whole * kScale;
  const std::uint64_t lo = (rem * kScale) / frequency;
  if (hi > UINT64_MAX - lo) {
    outTicks = UINT64_MAX;
    return true;
  }
  outTicks = hi + lo;
  return true;
}

bool QpcToTimestamp(std::uint64_t qpc, std::uint64_t epochQpc, std::uint64_t frequency, Timestamp& out) {
  out = {};
  if (qpc < epochQpc) {
    return false;
  }
  return QpcDeltaToTicks(qpc - epochQpc, frequency, out.ticks);
}

bool FileTimeToTimestamp(std::uint64_t fileTime, std::uint64_t epochFileTime, Timestamp& out) {
  out = {};
  if (fileTime < epochFileTime) {
    return false;
  }
  out.ticks = fileTime - epochFileTime;
  return true;
}

double TicksToSeconds(std::uint64_t ticks) {
  return static_cast<double>(ticks) / static_cast<double>(kTicksPerSecond);
}

double TicksToMilliseconds(std::uint64_t ticks) {
  return static_cast<double>(ticks) / static_cast<double>(kTicksPerMillisecond);
}

std::string FormatDuration(std::uint64_t ticks) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(3);
  if (ticks >= kTicksPerSecond) {
    os << TicksToSeconds(ticks) << " s";
  } else if (ticks >= kTicksPerMillisecond) {
    os << TicksToMilliseconds(ticks) << " ms";
  } else if (ticks >= kTicksPerMicrosecond) {
    os << (static_cast<double>(ticks) / static_cast<double>(kTicksPerMicrosecond)) << " us";
  } else {
    os << std::setprecision(0) << ticks << " x100ns";
  }
  return os.str();
}

std::string FormatTimestamp(Timestamp t) { return FormatDuration(t.ticks); }

bool IntersectHalfOpen(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
                       std::uint64_t& outStart, std::uint64_t& outEnd) {
  outStart = a > c ? a : c;
  outEnd = b < d ? b : d;
  return outStart < outEnd;
}

}  // namespace pulsescope
