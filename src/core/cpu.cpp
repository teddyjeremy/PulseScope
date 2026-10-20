#include "pulsescope/cpu.hpp"

#include <algorithm>
#include <unordered_map>

namespace pulsescope {
namespace {

struct Acc {
  ThreadCpuStats stats;
  std::uint16_t prevCpu = 0xFFFF;
  bool hadRun = false;
};

void AddTicks(UtilBucket& bucket, std::uint64_t ticks, bool pidKnown, std::uint32_t pid) {
  if (!pidKnown) bucket.unknownPidTicks += ticks;
  else if (pid == 0) bucket.idleTicks += ticks;
  else bucket.nonIdleTicks += ticks;
}

}  // namespace

Status AnalyzeCpu(const TraceSession& session, const CpuAnalyzeOptions& options, CpuAnalysis& out,
                  const CancellationToken* cancel) {
  out = {};
  SchedOptions sopt;
  sopt.window = options.window;
  SchedModel model;
  const Status st = ReconstructScheduling(session, sopt, model, cancel);
  if (!st) return st;

  out.window = model.window;
  out.readyTimeAvailable = model.readyEventsPresent;
  out.idleSeen = model.idleSeen;
  out.unknownLeadingTicks = model.unknownLeadingTicks;
  out.cswitchCount = model.cswitchCount;
  out.cpuCountFromSession = session.Info().cpuCount > 0;
  out.cpuCount = out.cpuCountFromSession ? session.Info().cpuCount : model.cpuCountObserved;

  std::unordered_map<std::uint64_t, Acc> byThread;
  std::uint32_t runningReady = 0;  // simultaneous ready slices are counted from events below

  // Max ready queue: sweep ready-slice start/end. Only meaningful with ready events.
  struct Mark {
    std::uint64_t t;
    int delta;
    std::uint64_t seq;
  };
  std::vector<Mark> marks;
  marks.reserve(model.slices.size());

  std::uint64_t step = 0;
  for (const auto& slice : model.slices) {
    if (!ShouldContinue(cancel, step++)) return Status{ErrorCode::Cancelled, "cpu analysis cancelled"};
    if (slice.kind == SliceKind::Ready) {
      marks.push_back({slice.start.ticks, +1, step});
      marks.push_back({slice.end.ticks, -1, step});
    }
    if (slice.kind != SliceKind::Running && slice.kind != SliceKind::Ready && slice.kind != SliceKind::Waiting &&
        slice.kind != SliceKind::OffCpuUnsplit) {
      continue;
    }
    const std::uint64_t id = slice.thread.valid() ? slice.thread.id : (0xC000000000000000ULL | slice.tid);
    Acc& acc = byThread[id];
    auto& stats = acc.stats;
    stats.thread = slice.thread;
    stats.tid = slice.tid;
    stats.pid = slice.pid;
    stats.pidKnown = slice.pidKnown;
    stats.process = slice.process;
    stats.idle = slice.pidKnown && slice.pid == 0;
    if (const auto* proc = session.Identities().FindProcess(slice.process)) {
      stats.imageId = proc->imageId;
      stats.image = session.Strings().Get(proc->imageId);
    }

    const std::uint64_t dur = slice.end.ticks - slice.start.ticks;
    if (slice.kind == SliceKind::Running) {
      stats.executingTicks += dur;
      stats.contextSwitches += 1;
      if (acc.hadRun && acc.prevCpu != 0xFFFF && slice.cpu != 0xFFFF && acc.prevCpu != slice.cpu) {
        stats.migrations += 1;
      }
      if (slice.cpu != 0xFFFF) {
        acc.prevCpu = slice.cpu;
        stats.lastCpu = slice.cpu;
      }
      acc.hadRun = true;
      if (!slice.pidKnown) out.unknownPidExecutingTicks += dur;
      else if (slice.pid == 0) out.idleExecutingTicks += dur;
      else out.nonIdleExecutingTicks += dur;
    } else if (slice.kind == SliceKind::Ready) {
      stats.readyTicks += dur;
    } else if (slice.kind == SliceKind::Waiting) {
      stats.waitingTicks += dur;
    } else {
      stats.unsplitOffCpuTicks += dur;
    }
  }

  std::sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) {
    if (a.t != b.t) return a.t < b.t;
    return a.delta < b.delta;  // close before open at the same timestamp
  });
  int depth = 0;
  for (const auto& mark : marks) {
    depth += mark.delta;
    if (depth < 0) depth = 0;
    if (static_cast<std::uint32_t>(depth) > out.maxReadyQueue) out.maxReadyQueue = static_cast<std::uint32_t>(depth);
  }
  (void)runningReady;

  out.threads.reserve(byThread.size());
  std::unordered_map<std::uint64_t, ProcessCpuStats> byProc;
  for (auto& [_, acc] : byThread) {
    out.threads.push_back(acc.stats);
    const std::uint64_t pk = acc.stats.process.valid() ? acc.stats.process.id : (0xD000000000000000ULL | acc.stats.pid);
    auto& proc = byProc[pk];
    proc.process = acc.stats.process;
    proc.pid = acc.stats.pid;
    proc.pidKnown = acc.stats.pidKnown;
    proc.imageId = acc.stats.imageId;
    if (proc.image.empty()) proc.image = acc.stats.image;
    proc.idle = acc.stats.idle;
    proc.executingTicks += acc.stats.executingTicks;
    proc.readyTicks += acc.stats.readyTicks;
    proc.waitingTicks += acc.stats.waitingTicks;
    proc.unsplitOffCpuTicks += acc.stats.unsplitOffCpuTicks;
    proc.contextSwitches += acc.stats.contextSwitches;
    proc.migrations += acc.stats.migrations;
    proc.threadCount += 1;
  }
  std::sort(out.threads.begin(), out.threads.end(), [](const ThreadCpuStats& a, const ThreadCpuStats& b) {
    if (a.executingTicks != b.executingTicks) return a.executingTicks > b.executingTicks;
    return a.tid < b.tid;
  });
  out.processes.reserve(byProc.size());
  for (auto& [_, proc] : byProc) out.processes.push_back(proc);
  std::sort(out.processes.begin(), out.processes.end(), [](const ProcessCpuStats& a, const ProcessCpuStats& b) {
    if (a.executingTicks != b.executingTicks) return a.executingTicks > b.executingTicks;
    return a.pid < b.pid;
  });

  const std::uint32_t buckets = options.bucketCount == 0 ? 1 : options.bucketCount;
  const std::uint64_t span = out.window.durationTicks();
  if (span > 0 && out.cpuCount > 0) {
    out.buckets.resize(buckets);
    for (std::uint32_t i = 0; i < buckets; ++i) {
      const std::uint64_t a = out.window.start.ticks + (span * i) / buckets;
      const std::uint64_t b = out.window.start.ticks + (span * (i + 1)) / buckets;
      out.buckets[i].start.ticks = a;
      out.buckets[i].end.ticks = b;
    }
    for (const auto& slice : model.slices) {
      if (slice.kind != SliceKind::Running) continue;
      for (auto& bucket : out.buckets) {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        if (!IntersectHalfOpen(slice.start.ticks, slice.end.ticks, bucket.start.ticks, bucket.end.ticks, a, b)) continue;
        AddTicks(bucket, b - a, slice.pidKnown, slice.pid);
      }
    }
    double utilSum = 0;
    std::uint32_t utilN = 0;
    for (auto& bucket : out.buckets) {
      const std::uint64_t width = bucket.end.ticks - bucket.start.ticks;
      const double denom = static_cast<double>(width) * static_cast<double>(out.cpuCount);
      if (denom > 0) {
        bucket.utilization = static_cast<double>(bucket.nonIdleTicks) / denom;
        if (bucket.utilization > 1.0) bucket.utilization = 1.0;
        utilSum += bucket.utilization;
        ++utilN;
      }
    }
    if (utilN > 0) out.averageUtilization = utilSum / static_cast<double>(utilN);
  }

  for (const auto& event : session.Events()) {
    if (event.kind != EventKind::SampledCpu) continue;
    if (event.ts < out.window.start || event.ts >= out.window.end) continue;
    CpuAnalysis::SampledPoint point;
    point.ts = event.ts;
    point.pid = event.pid;
    point.process = event.process;
    point.percent = event.sampledCpuPercent;
    out.sampled.push_back(point);
  }
  out.hasSampledCpu = !out.sampled.empty();

  out.definition =
      "executingTicks: time the thread was the incoming thread of a context switch until the next switch "
      "on that CPU, clipped to the window. Source: cswitch events. "
      "readyTicks / waitingTicks: only when at least one ReadyThread event is present in the trace. "
      "Waiting is switch-out until ReadyThread; ready is ReadyThread until the next switch-in. "
      "Gaps that cannot be split are unsplitOffCpuTicks and are not called waiting time. "
      "averageUtilization: non-idle executing ticks / (bucket width * cpuCount). Idle is pid 0 with a known pid. "
      "Threads whose pid is unknown are excluded from utilization. "
      "contextSwitches counts switch-ins. migrations counts a CPU change between consecutive running slices. "
      "maxReadyQueue is the peak number of simultaneous ready slices, a contention indicator, not a cause. "
      "This is not sampled CPU percent. SampledCpu events are a separate metric.";
  return Status::Ok();
}

}  // namespace pulsescope
