#include "pulsescope/memory.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace pulsescope {
namespace {

constexpr std::uint64_t kSuspectBytes = 256ull << 40;  // 256 TiB, larger than any real counter we accept quietly

struct Point {
  double seconds = 0;
  double working = 0;
  double priv = 0;
};

bool Fit(const std::vector<Point>& points, double& wsSlope, double& privSlope) {
  if (points.size() < 2) return false;
  const double n = static_cast<double>(points.size());
  double sx = 0, sy = 0, sz = 0, sxx = 0, sxy = 0, sxz = 0;
  for (const auto& p : points) {
    sx += p.seconds;
    sy += p.working;
    sz += p.priv;
    sxx += p.seconds * p.seconds;
    sxy += p.seconds * p.working;
    sxz += p.seconds * p.priv;
  }
  const double den = n * sxx - sx * sx;
  if (!(std::fabs(den) > 1e-12)) return false;
  wsSlope = (n * sxy - sx * sy) / den;
  privSlope = (n * sxz - sx * sz) / den;
  return true;
}

}  // namespace

Status AnalyzeMemory(const TraceSession& session, const MemoryAnalyzeOptions& options, MemoryAnalysis& out,
                     const CancellationToken* cancel) {
  out = {};
  if (!session.finalized()) return Status{ErrorCode::InvalidArgument, "session must be finalized"};

  TimeRange window = options.window;
  if (window.durationTicks() == 0 && !session.empty()) {
    window.start = session.TraceStart();
    window.end = session.TraceEnd();
    if (window.end.ticks < UINT64_MAX) window.end.ticks += 1;
  }
  out.window = window;

  struct Acc {
    MemorySeriesStats stats;
    std::vector<Point> points;
    bool sawSample = false;
  };
  std::unordered_map<std::uint64_t, Acc> byProc;

  std::uint64_t step = 0;
  for (const auto& event : session.Events()) {
    if (!ShouldContinue(cancel, step++)) return Status{ErrorCode::Cancelled, "memory analysis cancelled"};
    if (window.durationTicks() > 0 && (event.ts < window.start || event.ts >= window.end)) continue;

    const bool sample = event.kind == EventKind::MemorySample;
    const bool alloc = event.kind == EventKind::VirtualAlloc || event.kind == EventKind::VirtualFree;
    if (!sample && !alloc) continue;

    const std::uint64_t id = event.process.valid() ? event.process.id : (0xE000000000000000ULL | event.pid);
    Acc& acc = byProc[id];
    acc.stats.process = event.process;
    acc.stats.pid = event.pid;
    acc.stats.pidKnown = event.pidKnown || event.pid != 0;
    if (const auto* proc = session.Identities().FindProcess(event.process)) {
      acc.stats.imageId = proc->imageId;
      acc.stats.image = session.Strings().Get(proc->imageId);
    }

    if (sample) {
      out.hasSamples = true;
      auto& s = acc.stats;
      const bool suspect = event.workingSet > kSuspectBytes || event.privateBytes > kSuspectBytes ||
                           event.commitBytes > kSuspectBytes;
      if (suspect) {
        ++s.suspectSamples;
        continue;
      }
      if (!acc.sawSample) {
        s.workingSetFirst = event.workingSet;
        s.privateFirst = event.privateBytes;
        s.commitFirst = event.commitBytes;
        acc.sawSample = true;
      }
      s.workingSetLast = event.workingSet;
      s.privateLast = event.privateBytes;
      s.commitLast = event.commitBytes;
      s.workingSetPeak = std::max(s.workingSetPeak, event.workingSet);
      s.privatePeak = std::max(s.privatePeak, event.privateBytes);
      s.commitPeak = std::max(s.commitPeak, event.commitBytes);
      ++s.samples;
      Point p;
      p.seconds = TicksToSeconds(event.ts.ticks - window.start.ticks);
      p.working = static_cast<double>(event.workingSet);
      p.priv = static_cast<double>(event.privateBytes);
      acc.points.push_back(p);
    } else {
      out.hasAllocationEvents = true;
      if (event.kind == EventKind::VirtualAlloc) {
        acc.stats.virtualAllocBytes += event.byteCount;
        ++acc.stats.virtualAllocEvents;
      } else {
        acc.stats.virtualFreeBytes += event.byteCount;
        ++acc.stats.virtualFreeEvents;
      }
    }
  }

  for (auto& [_, acc] : byProc) {
    double ws = 0;
    double priv = 0;
    if (Fit(acc.points, ws, priv)) {
      acc.stats.slopeValid = true;
      acc.stats.workingSetBytesPerSecond = ws;
      acc.stats.privateBytesPerSecond = priv;
    }
    out.processes.push_back(acc.stats);
  }
  std::sort(out.processes.begin(), out.processes.end(), [](const MemorySeriesStats& a, const MemorySeriesStats& b) {
    if (a.workingSetPeak != b.workingSetPeak) return a.workingSetPeak > b.workingSetPeak;
    return a.pid < b.pid;
  });

  out.definition =
      "workingSet, privateBytes, and commitBytes come from memory samples (live GetProcessMemoryInfo "
      "PROCESS_MEMORY_COUNTERS_EX PrivateUsage / WorkingSetSize / PagefileUsage, or equivalent trace samples). "
      "A sample is a point in time, not an allocation history. "
      "workingSetBytesPerSecond is the least-squares slope of samples against seconds in the window. "
      "virtualAllocBytes sums sizes on VirtualAlloc events only when those events were collected. "
      "Absence of allocation events means allocation data was not collected, not that allocations did not happen. "
      "Samples above 256 TiB are counted as suspect and excluded.";
  return Status::Ok();
}

}  // namespace pulsescope
