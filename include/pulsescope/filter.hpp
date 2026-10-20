#pragma once

#include "pulsescope/cpu.hpp"
#include "pulsescope/memory.hpp"
#include "pulsescope/session.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulsescope {

struct EventFilter {
  std::optional<TimeRange> range;
  std::optional<std::uint32_t> pid;
  std::optional<std::uint32_t> tid;
  std::optional<ProcessKey> process;
  std::optional<ThreadKey> thread;
  std::optional<EventKind> kind;
  std::string text;  // case-insensitive ASCII substring of name, provider, detail, image
};

struct FilterHit {
  std::uint64_t index = 0;
};

// Returns indices into session.Events(), in time order (the session order).
Status FilterEvents(const TraceSession& session, const EventFilter& filter, std::vector<FilterHit>& out,
                    const CancellationToken* cancel = nullptr);

enum class ProcessSort { ExecutingTime, Name, Pid, WorkingSet };

struct ProcessRow {
  ProcessKey key{};
  std::uint32_t pid = 0;
  std::string image;
  Timestamp start{};
  Timestamp end{};
  bool hasEnd = false;
  bool historical = true;
  std::uint64_t executingTicks = 0;
  std::uint64_t readyTicks = 0;
  std::uint64_t waitingTicks = 0;
  std::uint32_t contextSwitches = 0;
  std::uint32_t migrations = 0;
  std::uint32_t threadCount = 0;
  std::uint64_t eventCount = 0;
  std::uint64_t workingSetLast = 0;
  bool idle = false;
};

Status BuildProcessRows(const TraceSession& session, const CpuAnalysis& cpu, const MemoryAnalysis& memory,
                        ProcessSort sort, bool descending, std::vector<ProcessRow>& out);

}  // namespace pulsescope
