#pragma once

#include "pulsescope/schedule.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulsescope {

struct ThreadCpuStats {
  ThreadKey thread{};
  ProcessKey process{};
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  bool pidKnown = false;
  std::uint32_t imageId = 0;
  std::string image;
  std::uint64_t executingTicks = 0;
  std::uint64_t readyTicks = 0;
  std::uint64_t waitingTicks = 0;
  std::uint64_t unsplitOffCpuTicks = 0;
  std::uint32_t contextSwitches = 0;  // switch-ins
  std::uint32_t migrations = 0;
  std::uint16_t lastCpu = 0xFFFF;
  bool idle = false;
};

struct ProcessCpuStats {
  ProcessKey process{};
  std::uint32_t pid = 0;
  bool pidKnown = false;
  std::uint32_t imageId = 0;
  std::string image;
  std::uint64_t executingTicks = 0;
  std::uint64_t readyTicks = 0;
  std::uint64_t waitingTicks = 0;
  std::uint64_t unsplitOffCpuTicks = 0;
  std::uint32_t contextSwitches = 0;
  std::uint32_t migrations = 0;
  std::uint32_t threadCount = 0;
  bool idle = false;
};

struct UtilBucket {
  Timestamp start{};
  Timestamp end{};
  double utilization = 0;  // non-idle executing / (bucket * cpuCount), 0..1
  std::uint64_t nonIdleTicks = 0;
  std::uint64_t idleTicks = 0;
  std::uint64_t unknownPidTicks = 0;
};

struct CpuAnalysis {
  TimeRange window{};
  std::uint32_t cpuCount = 0;
  bool cpuCountFromSession = false;
  bool readyTimeAvailable = false;
  bool idleSeen = false;
  std::uint64_t nonIdleExecutingTicks = 0;
  std::uint64_t idleExecutingTicks = 0;
  std::uint64_t unknownPidExecutingTicks = 0;
  std::uint64_t unknownLeadingTicks = 0;
  std::uint32_t maxReadyQueue = 0;
  std::uint32_t cswitchCount = 0;
  std::vector<ThreadCpuStats> threads;
  std::vector<ProcessCpuStats> processes;
  std::vector<UtilBucket> buckets;
  std::optional<double> averageUtilization;
  bool hasSampledCpu = false;
  struct SampledPoint {
    Timestamp ts{};
    std::uint32_t pid = 0;
    ProcessKey process{};
    double percent = 0;  // 0..100 of the machine or the process, as recorded
  };
  std::vector<SampledPoint> sampled;
  std::string definition;
};

struct CpuAnalyzeOptions {
  TimeRange window{};
  std::uint32_t bucketCount = 60;
};

Status AnalyzeCpu(const TraceSession& session, const CpuAnalyzeOptions& options, CpuAnalysis& out,
                  const CancellationToken* cancel = nullptr);

}  // namespace pulsescope
