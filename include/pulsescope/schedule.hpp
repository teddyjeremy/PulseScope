#pragma once

#include "pulsescope/cancel.hpp"
#include "pulsescope/session.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pulsescope {

enum class SliceKind { Running, Ready, Waiting, OffCpuUnsplit };

struct SchedSlice {
  ThreadKey thread{};
  ProcessKey process{};
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  bool pidKnown = false;
  std::uint16_t cpu = 0xFFFF;
  Timestamp start{};
  Timestamp end{};
  SliceKind kind = SliceKind::Running;
  bool closedAtWindowEnd = false;
  std::int8_t waitReason = 0;
};

struct SchedModel {
  TimeRange window{};
  std::vector<SchedSlice> slices;
  std::uint32_t cpuCountObserved = 0;
  bool readyEventsPresent = false;
  bool idleSeen = false;
  std::uint64_t unknownLeadingTicks = 0;
  std::uint32_t cswitchCount = 0;
  std::uint32_t readyCount = 0;
  std::uint32_t openTails = 0;
  std::string note;
};

struct SchedOptions {
  TimeRange window{};
  bool useFullTraceIfEmpty = true;
};

// Rebuilds execution, ready, and waiting slices from context switches and
// ReadyThread events. Does not invent a running slice before the first
// observed switch on a CPU; that gap is reported as unknownLeadingTicks.
Status ReconstructScheduling(const TraceSession& session, const SchedOptions& options, SchedModel& out,
                             const CancellationToken* cancel = nullptr);

}  // namespace pulsescope
