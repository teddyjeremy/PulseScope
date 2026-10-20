#include "pulsescope/compare.hpp"

#include <cmath>
#include <unordered_map>

namespace pulsescope {
namespace {

MetricDelta Make(std::string id, std::string description, std::string unit, double baseline, double candidate,
                 MetricDirection direction, double fraction, double minAbsolute) {
  MetricDelta d;
  d.id = std::move(id);
  d.description = std::move(description);
  d.unit = std::move(unit);
  d.baseline = baseline;
  d.candidate = candidate;
  d.absoluteDelta = candidate - baseline;
  d.direction = direction;
  if (baseline != 0.0 && std::isfinite(baseline) && std::isfinite(candidate)) {
    d.percentDelta = (candidate - baseline) / std::fabs(baseline) * 100.0;
  }
  if (direction == MetricDirection::Neutral || !d.percentDelta) return d;
  const double absMove = std::fabs(d.absoluteDelta);
  if (direction == MetricDirection::LowerIsBetter) {
    d.regression = *d.percentDelta >= fraction * 100.0 && absMove >= minAbsolute;
  } else if (direction == MetricDirection::HigherIsBetter) {
    d.regression = *d.percentDelta <= -fraction * 100.0 && absMove >= minAbsolute;
  }
  return d;
}

MetricDelta Skipped(std::string id, std::string reason) {
  MetricDelta d;
  d.id = std::move(id);
  d.comparable = false;
  d.reason = std::move(reason);
  return d;
}

template <typename T, typename Read>
std::unordered_map<std::string, double> ByImage(const std::vector<T>& rows, Read read) {
  std::unordered_map<std::string, double> out;
  for (const auto& row : rows) {
    if (row.image.empty() || row.idle) continue;
    out[row.image] += read(row);
  }
  return out;
}

}  // namespace

ComparisonResult CompareAnalyses(const CpuAnalysis& baseCpu, const MemoryAnalysis& baseMem, const CpuAnalysis& candCpu,
                                 const MemoryAnalysis& candMem, const SessionInfo& baseInfo, const SessionInfo& candInfo,
                                 const CompareOptions& options) {
  ComparisonResult result;
  result.caveats.push_back(
      "Differences are descriptive. PulseScope does not infer why a metric moved. "
      "Processes are matched by image name, and rows that share a name are summed. "
      "A pid is not an identity across runs.");
  if (baseInfo.synthetic || candInfo.synthetic) {
    result.caveats.push_back("At least one side is synthetic fixture data, not a recording of this machine.");
  }
  if (baseInfo.source != candInfo.source) {
    result.caveats.push_back("The two sessions do not share a data source. Only metrics with the same definition are compared.");
  }
  if (baseCpu.cpuCount != 0 && candCpu.cpuCount != 0 && baseCpu.cpuCount != candCpu.cpuCount) {
    result.caveats.push_back("CPU counts differ. Utilization is normalized by each session's cpuCount.");
  }
  const double baseDur = TicksToSeconds(baseCpu.window.durationTicks());
  const double candDur = TicksToSeconds(candCpu.window.durationTicks());
  if (baseDur > 0 && candDur > 0 && std::fabs(candDur - baseDur) / baseDur > 0.05) {
    result.caveats.push_back("Analysis windows differ by more than 5%. Totals that scale with duration are easy to misread.");
  }

  const bool bothPrecise = baseCpu.cswitchCount > 0 && candCpu.cswitchCount > 0;
  if (bothPrecise) {
    result.deltas.push_back(Make("cpu.nonIdleExecutingSeconds",
                                 "Non-idle time spent actually running, from context switches", "seconds",
                                 TicksToSeconds(baseCpu.nonIdleExecutingTicks),
                                 TicksToSeconds(candCpu.nonIdleExecutingTicks), MetricDirection::LowerIsBetter,
                                 options.regressionFraction, 0.001));
    const auto baseBy = ByImage(baseCpu.processes, [](const ProcessCpuStats& p) { return TicksToSeconds(p.executingTicks); });
    const auto candBy = ByImage(candCpu.processes, [](const ProcessCpuStats& p) { return TicksToSeconds(p.executingTicks); });
    for (const auto& [image, baseValue] : baseBy) {
      const auto it = candBy.find(image);
      if (it == candBy.end()) continue;
      result.deltas.push_back(Make("cpu.executingSeconds:" + image,
                                   "Non-idle executing time for image " + image, "seconds", baseValue, it->second,
                                   MetricDirection::LowerIsBetter, options.regressionFraction, 0.001));
    }
  } else {
    result.deltas.push_back(Skipped("cpu.nonIdleExecutingSeconds",
                                    "Both sessions need context-switch data. Sampled CPU percent is not the same metric."));
  }

  if (baseCpu.averageUtilization && candCpu.averageUtilization && bothPrecise) {
    result.deltas.push_back(Make("cpu.averageUtilization", "Mean of per-bucket non-idle utilization (0..1)", "fraction",
                                 *baseCpu.averageUtilization, *candCpu.averageUtilization, MetricDirection::LowerIsBetter,
                                 options.regressionFraction, 0.01));
  }

  if (baseCpu.readyTimeAvailable && candCpu.readyTimeAvailable) {
    result.deltas.push_back(Make("cpu.maxReadyQueue", "Peak number of threads in the ready state at once", "threads",
                                 static_cast<double>(baseCpu.maxReadyQueue), static_cast<double>(candCpu.maxReadyQueue),
                                 MetricDirection::LowerIsBetter, options.regressionFraction, 1.0));
  } else {
    result.deltas.push_back(Skipped("cpu.maxReadyQueue", "Ready-queue depth requires ReadyThread events on both sides."));
  }

  result.deltas.push_back(Make("window.seconds", "Analysis window length", "seconds", baseDur, candDur,
                               MetricDirection::Neutral, options.regressionFraction, 0));

  if (baseMem.hasSamples && candMem.hasSamples) {
    std::unordered_map<std::string, const MemorySeriesStats*> baseRows;
    std::unordered_map<std::string, int> baseCount;
    for (const auto& row : baseMem.processes) {
      if (row.image.empty() || row.samples == 0) continue;
      baseCount[row.image] += 1;
      baseRows[row.image] = &row;
    }
    std::unordered_map<std::string, const MemorySeriesStats*> candRows;
    std::unordered_map<std::string, int> candCount;
    for (const auto& row : candMem.processes) {
      if (row.image.empty() || row.samples == 0) continue;
      candCount[row.image] += 1;
      candRows[row.image] = &row;
    }
    for (const auto& [image, baseRow] : baseRows) {
      if (baseCount[image] != 1 || candCount[image] != 1) {
        result.deltas.push_back(Skipped("memory.workingSetLast:" + image,
                                        "Image matched more than once; refusing to guess which process is which."));
        continue;
      }
      const auto it = candRows.find(image);
      if (it == candRows.end()) continue;
      result.deltas.push_back(Make(
          "memory.workingSetLast:" + image, "Last working-set sample for " + image, "bytes",
          static_cast<double>(baseRow->workingSetLast), static_cast<double>(it->second->workingSetLast),
          MetricDirection::LowerIsBetter, options.regressionFraction, 65536));
      result.deltas.push_back(Make(
          "memory.privatePeak:" + image, "Peak private bytes for " + image, "bytes",
          static_cast<double>(baseRow->privatePeak), static_cast<double>(it->second->privatePeak),
          MetricDirection::LowerIsBetter, options.regressionFraction, 65536));
      if (baseRow->slopeValid && it->second->slopeValid) {
        result.deltas.push_back(Make("memory.workingSetSlope:" + image,
                                     "Working-set least-squares slope for " + image, "bytes/second",
                                     baseRow->workingSetBytesPerSecond, it->second->workingSetBytesPerSecond,
                                     MetricDirection::LowerIsBetter, options.regressionFraction, 4096));
      }
    }
  } else {
    result.deltas.push_back(Skipped("memory.samples", "Both sessions need memory samples. Allocation events are not implied."));
  }

  if (baseMem.hasAllocationEvents != candMem.hasAllocationEvents) {
    result.caveats.push_back("Only one session contains virtual allocation events. Those bytes were not compared.");
  }

  result.compatible = false;
  for (const auto& d : result.deltas) {
    if (d.comparable) result.compatible = true;
  }
  result.summary = result.compatible ? "Compared shared metrics. Unshared metrics are listed as not comparable."
                                     : "No shared metric could be compared.";
  return result;
}

}  // namespace pulsescope
