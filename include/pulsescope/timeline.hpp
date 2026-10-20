#pragma once

#include "pulsescope/schedule.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pulsescope {

enum class ColumnKind { Empty, Running, Ready, Waiting, OffCpuUnsplit, Mixed };

struct TimelineColumn {
  std::uint16_t running = 0;
  std::uint16_t ready = 0;
  std::uint16_t waiting = 0;
  std::uint16_t unsplit = 0;
  ColumnKind dominant = ColumnKind::Empty;
};

struct TimelineLane {
  ThreadKey thread{};
  ProcessKey process{};
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  std::string image;
  std::string label;
  std::uint64_t executingTicks = 0;
  std::vector<SchedSlice> spans;      // spans that cover at least one pixel, for hit testing
  std::vector<TimelineColumn> columns;
};

struct TimelineModel {
  TimeRange window{};
  std::uint32_t pixels = 0;
  std::vector<TimelineLane> lanes;
  std::uint64_t omittedLanes = 0;
  std::string note;
};

struct TimelineOptions {
  TimeRange window{};
  std::uint32_t pixels = 800;
  std::uint32_t maxLanes = 40;
  std::string text;  // optional image/tid filter
};

Status BuildTimeline(const TraceSession& session, const TimelineOptions& options, TimelineModel& out,
                     const CancellationToken* cancel = nullptr);

// x is a pixel column. Returns the span under that column if one was retained.
const SchedSlice* HitTestLane(const TimelineLane& lane, std::uint32_t x, const TimeRange& window, std::uint32_t pixels);

}  // namespace pulsescope
