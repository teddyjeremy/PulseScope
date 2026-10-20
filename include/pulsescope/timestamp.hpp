#pragma once

#include <cstdint>
#include <string>

namespace pulsescope {

// All analysis timestamps are 100-nanosecond ticks, the same unit as
// FILETIME, measured from a session epoch (not from year 1601).
// A raw Windows QPC value is converted with the session's performance
// frequency before it enters the model. See docs/metrics.md.
inline constexpr std::uint64_t kTicksPerSecond = 10'000'000ULL;
inline constexpr std::uint64_t kTicksPerMillisecond = 10'000ULL;
inline constexpr std::uint64_t kTicksPerMicrosecond = 10ULL;

struct Timestamp {
  std::uint64_t ticks = 0;

  constexpr bool operator==(const Timestamp& o) const { return ticks == o.ticks; }
  constexpr bool operator!=(const Timestamp& o) const { return ticks != o.ticks; }
  constexpr bool operator<(const Timestamp& o) const { return ticks < o.ticks; }
  constexpr bool operator<=(const Timestamp& o) const { return ticks <= o.ticks; }
  constexpr bool operator>(const Timestamp& o) const { return ticks > o.ticks; }
  constexpr bool operator>=(const Timestamp& o) const { return ticks >= o.ticks; }
};

struct TimeRange {
  Timestamp start{};
  Timestamp end{};

  bool valid() const { return end.ticks >= start.ticks; }
  std::uint64_t durationTicks() const { return end.ticks >= start.ticks ? end.ticks - start.ticks : 0; }
  bool contains(Timestamp t) const { return t.ticks >= start.ticks && t.ticks < end.ticks; }
  bool overlaps(Timestamp a, Timestamp b) const { return a.ticks < end.ticks && b.ticks > start.ticks; }
};

// ticks = deltaQpc * kTicksPerSecond / frequency, with floor division.
// Returns false if frequency is 0. Saturates instead of wrapping.
bool QpcDeltaToTicks(std::uint64_t deltaQpc, std::uint64_t frequency, std::uint64_t& outTicks);

// Convert an absolute QPC reading into session-relative 100ns ticks.
// epochQpc is the session start counter. If qpc < epoch, out is 0 and the
// function returns false (clock went backwards relative to the epoch).
bool QpcToTimestamp(std::uint64_t qpc, std::uint64_t epochQpc, std::uint64_t frequency, Timestamp& out);

// FILETIME is 100ns since 1601-01-01 UTC. The session epoch is also a
// FILETIME. Session-relative ticks are fileTime - epoch, when fileTime >= epoch.
bool FileTimeToTimestamp(std::uint64_t fileTime, std::uint64_t epochFileTime, Timestamp& out);

double TicksToSeconds(std::uint64_t ticks);
double TicksToMilliseconds(std::uint64_t ticks);

// Human duration, e.g. "1.250 ms", "3.000 s", "40 us". Not localized.
std::string FormatDuration(std::uint64_t ticks);
std::string FormatTimestamp(Timestamp t);

// Intersection of [a,b) with [c,d). Returns false if empty.
bool IntersectHalfOpen(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
                       std::uint64_t& outStart, std::uint64_t& outEnd);

}  // namespace pulsescope
