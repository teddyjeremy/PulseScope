#pragma once

#include "pulsescope/bookmarks.hpp"
#include "pulsescope/compare.hpp"
#include "pulsescope/filter.hpp"

#include <string>
#include <vector>

namespace pulsescope {

struct ReportInput {
  const TraceSession* session = nullptr;
  const CpuAnalysis* cpu = nullptr;
  const MemoryAnalysis* memory = nullptr;
  const ComparisonResult* comparison = nullptr;
  const std::vector<Bookmark>* bookmarks = nullptr;
  std::string title = "PulseScope report";
};

std::string ExportEventsCsv(const TraceSession& session, const std::vector<FilterHit>& hits);
std::string ExportAnalysisJson(const ReportInput& input);
std::string ExportReportHtml(const ReportInput& input);

}  // namespace pulsescope
