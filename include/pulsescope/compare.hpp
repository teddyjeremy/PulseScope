#pragma once

#include "pulsescope/cpu.hpp"
#include "pulsescope/memory.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pulsescope {

enum class MetricDirection { LowerIsBetter, HigherIsBetter, Neutral };

struct MetricDelta {
  std::string id;
  std::string description;
  std::string unit;
  double baseline = 0;
  double candidate = 0;
  double absoluteDelta = 0;
  std::optional<double> percentDelta;
  MetricDirection direction = MetricDirection::Neutral;
  bool regression = false;
  bool comparable = true;
  std::string reason;
};

struct ComparisonResult {
  bool compatible = false;
  std::string summary;
  std::vector<std::string> caveats;
  std::vector<MetricDelta> deltas;
};

struct CompareOptions {
  // Fractional threshold. 0.05 means a 5% move in the worse direction.
  double regressionFraction = 0.05;
};

// Compares two analyses. Metrics that do not share a definition are skipped
// and explained. A regression flag is not a causal claim.
ComparisonResult CompareAnalyses(const CpuAnalysis& baseCpu, const MemoryAnalysis& baseMem,
                                 const CpuAnalysis& candCpu, const MemoryAnalysis& candMem,
                                 const SessionInfo& baseInfo, const SessionInfo& candInfo,
                                 const CompareOptions& options = {});

}  // namespace pulsescope
