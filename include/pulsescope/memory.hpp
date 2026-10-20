#pragma once

#include "pulsescope/session.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulsescope {

struct MemorySeriesStats {
  ProcessKey process{};
  std::uint32_t pid = 0;
  bool pidKnown = false;
  std::uint32_t imageId = 0;
  std::string image;
  std::uint32_t samples = 0;
  std::uint64_t workingSetFirst = 0;
  std::uint64_t workingSetLast = 0;
  std::uint64_t workingSetPeak = 0;
  std::uint64_t privateFirst = 0;
  std::uint64_t privateLast = 0;
  std::uint64_t privatePeak = 0;
  std::uint64_t commitFirst = 0;
  std::uint64_t commitLast = 0;
  std::uint64_t commitPeak = 0;
  bool slopeValid = false;
  double workingSetBytesPerSecond = 0;
  double privateBytesPerSecond = 0;
  std::uint64_t virtualAllocBytes = 0;
  std::uint64_t virtualFreeBytes = 0;
  std::uint32_t virtualAllocEvents = 0;
  std::uint32_t virtualFreeEvents = 0;
  std::uint32_t suspectSamples = 0;
};

struct MemoryAnalysis {
  TimeRange window{};
  bool hasSamples = false;
  bool hasAllocationEvents = false;
  std::vector<MemorySeriesStats> processes;
  std::string definition;
};

struct MemoryAnalyzeOptions {
  TimeRange window{};
};

// Working set, private bytes, and commit are sample counters (live API or
// sampled events). They are not a heap trace. Allocation bytes are summed
// only from VirtualAlloc / VirtualFree events actually present.
Status AnalyzeMemory(const TraceSession& session, const MemoryAnalyzeOptions& options, MemoryAnalysis& out,
                     const CancellationToken* cancel = nullptr);

}  // namespace pulsescope
