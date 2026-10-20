#include "pulsescope/filter.hpp"

#include <algorithm>
#include <cctype>

namespace pulsescope {
namespace {

std::string Fold(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

bool Contains(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  return hay.find(needle) != std::string_view::npos;
}

}  // namespace

Status FilterEvents(const TraceSession& session, const EventFilter& filter, std::vector<FilterHit>& out,
                    const CancellationToken* cancel) {
  out.clear();
  if (!session.finalized()) return Status{ErrorCode::InvalidArgument, "session must be finalized"};
  const std::string needle = Fold(filter.text);
  std::uint64_t step = 0;
  for (std::uint64_t i = 0; i < session.Events().size(); ++i) {
    if (!ShouldContinue(cancel, step++)) return Status{ErrorCode::Cancelled, "filter cancelled"};
    const Event& event = session.Events()[i];
    if (filter.range && !filter.range->contains(event.ts)) continue;
    if (filter.pid && event.pid != *filter.pid) continue;
    if (filter.tid && event.tid != *filter.tid) continue;
    if (filter.process && event.process != *filter.process) continue;
    if (filter.thread && event.thread != *filter.thread) continue;
    if (filter.kind && event.kind != *filter.kind) continue;
    if (!needle.empty()) {
      std::string image;
      if (const auto* proc = session.Identities().FindProcess(event.process)) {
        image = session.Strings().Get(proc->imageId);
      }
      const std::string blob = Fold(session.Strings().Get(event.nameId) + " " + session.Strings().Get(event.providerId) +
                                    " " + session.Strings().Get(event.detailId) + " " + image);
      if (!Contains(blob, needle)) continue;
    }
    out.push_back(FilterHit{i});
  }
  return Status::Ok();
}

Status BuildProcessRows(const TraceSession& session, const CpuAnalysis& cpu, const MemoryAnalysis& memory,
                        ProcessSort sort, bool descending, std::vector<ProcessRow>& out) {
  out.clear();
  if (!session.finalized()) return Status{ErrorCode::InvalidArgument, "session must be finalized"};
  for (const auto& proc : session.Identities().Processes()) {
    ProcessRow row;
    row.key = proc.key;
    row.pid = proc.pid;
    row.image = session.Strings().Get(proc.imageId);
    row.start = proc.start;
    row.end = proc.end;
    row.hasEnd = proc.hasEnd;
    row.historical = true;
    row.idle = proc.pid == 0;
    for (const auto& stats : cpu.processes) {
      if (stats.process == proc.key) {
        row.executingTicks = stats.executingTicks;
        row.readyTicks = stats.readyTicks;
        row.waitingTicks = stats.waitingTicks;
        row.contextSwitches = stats.contextSwitches;
        row.migrations = stats.migrations;
        row.threadCount = stats.threadCount;
      }
    }
    for (const auto& stats : memory.processes) {
      if (stats.process == proc.key) row.workingSetLast = stats.workingSetLast;
    }
    for (const auto& event : session.Events()) {
      if (event.process == proc.key) ++row.eventCount;
    }
    out.push_back(std::move(row));
  }
  std::sort(out.begin(), out.end(), [&](const ProcessRow& a, const ProcessRow& b) {
    int cmp = 0;
    switch (sort) {
      case ProcessSort::ExecutingTime:
        cmp = (a.executingTicks > b.executingTicks) - (a.executingTicks < b.executingTicks);
        break;
      case ProcessSort::Name:
        cmp = a.image.compare(b.image);
        break;
      case ProcessSort::Pid:
        cmp = (a.pid > b.pid) - (a.pid < b.pid);
        break;
      case ProcessSort::WorkingSet:
        cmp = (a.workingSetLast > b.workingSetLast) - (a.workingSetLast < b.workingSetLast);
        break;
    }
    if (!descending) cmp = -cmp;
    if (cmp == 0) return a.key.id < b.key.id;
    return cmp > 0;
  });
  return Status::Ok();
}

}  // namespace pulsescope
