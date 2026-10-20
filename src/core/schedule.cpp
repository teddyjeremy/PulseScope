#include "pulsescope/schedule.hpp"

#include <unordered_map>

namespace pulsescope {
namespace {

struct ThreadState {
  bool known = false;
  bool off = false;
  bool ready = false;
  Timestamp offSince{};
  Timestamp readySince{};
  std::uint16_t lastCpu = 0xFFFF;
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  bool pidKnown = false;
  ProcessKey process{};
  ThreadKey key{};
  std::int8_t waitReason = 0;
};

struct CpuState {
  bool occupied = false;
  bool sawSwitch = false;
  ThreadKey thread{};
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  bool pidKnown = false;
  ProcessKey process{};
  Timestamp since{};
};

void ClipAdd(SchedModel& model, SchedSlice slice, const TimeRange& window) {
  if (slice.end.ticks <= slice.start.ticks) return;
  std::uint64_t a = 0;
  std::uint64_t b = 0;
  if (!IntersectHalfOpen(slice.start.ticks, slice.end.ticks, window.start.ticks, window.end.ticks, a, b)) {
    return;
  }
  slice.start.ticks = a;
  slice.end.ticks = b;
  if (slice.kind == SliceKind::Running && slice.pidKnown && slice.pid == 0) model.idleSeen = true;
  model.slices.push_back(std::move(slice));
}

std::uint64_t StateKey(ThreadKey key, std::uint32_t tid) {
  if (key.valid()) return key.id;
  return 0x8000000000000000ULL | static_cast<std::uint64_t>(tid);
}

}  // namespace

Status ReconstructScheduling(const TraceSession& session, const SchedOptions& options, SchedModel& out,
                             const CancellationToken* cancel) {
  out = {};
  if (!session.finalized()) {
    return Status{ErrorCode::InvalidArgument, "session must be finalized before scheduling analysis"};
  }

  TimeRange window = options.window;
  bool autoFit = false;
  if (window.durationTicks() == 0) {
    if (!options.useFullTraceIfEmpty || session.empty()) {
      out.note = "empty analysis window";
      return Status::Ok();
    }
    autoFit = true;
    window.start = session.TraceStart();
    window.end = session.TraceEnd();
    // Exclusive bound one tick past the last event so that event is visited.
    // Open slices are closed at the last event, not at this extra tick.
    if (window.end.ticks < UINT64_MAX) window.end.ticks += 1;
  }
  out.window = window;
  const Timestamp closeTime = autoFit && window.end.ticks > 0 ? Timestamp{window.end.ticks - 1} : window.end;

  for (const auto& event : session.Events()) {
    if (event.kind == EventKind::ReadyThread) {
      out.readyEventsPresent = true;
      break;
    }
  }

  std::unordered_map<std::uint64_t, ThreadState> threads;
  std::unordered_map<std::uint16_t, CpuState> cpus;

  auto touch = [&](ThreadKey key, std::uint32_t tid, std::uint32_t pid, bool pidKnown, ProcessKey process) -> ThreadState& {
    ThreadState& st = threads[StateKey(key, tid)];
    st.known = true;
    st.key = key;
    if (st.tid == 0) st.tid = tid;
    if (pidKnown) {
      st.pid = pid;
      st.pidKnown = true;
      st.process = process;
    }
    return st;
  };

  std::uint64_t step = 0;
  for (const auto& event : session.Events()) {
    if (!ShouldContinue(cancel, step++)) {
      return Status{ErrorCode::Cancelled, "scheduling reconstruction cancelled"};
    }
    if (event.ts >= window.end) break;  // events are time-sorted

    if (event.kind == EventKind::ReadyThread) {
      ++out.readyCount;
      ThreadState& st = touch(event.thread, event.tid, event.pid, event.pidKnown, event.process);
      if (st.off && !st.ready && event.ts.ticks >= st.offSince.ticks) {
        SchedSlice wait;
        wait.thread = st.key;
        wait.process = st.process;
        wait.tid = st.tid;
        wait.pid = st.pid;
        wait.pidKnown = st.pidKnown;
        wait.start = st.offSince;
        wait.end = event.ts;
        wait.kind = SliceKind::Waiting;
        wait.waitReason = st.waitReason;
        ClipAdd(out, wait, window);
        st.ready = true;
        st.readySince = event.ts;
      }
      continue;
    }

    if (event.kind != EventKind::ContextSwitch || event.cpu == 0xFFFF) continue;
    if (static_cast<std::uint32_t>(event.cpu) + 1 > out.cpuCountObserved) {
      out.cpuCountObserved = static_cast<std::uint32_t>(event.cpu) + 1;
    }

    ++out.cswitchCount;
    CpuState& cpu = cpus[event.cpu];
    if (!cpu.sawSwitch && event.ts > window.start) {
      const std::uint64_t gap = event.ts.ticks - window.start.ticks;
      // One CPU's unknown prefix. Summed across CPUs this is "CPU-ticks",
      // not wall time, which matches the utilization denominator.
      out.unknownLeadingTicks += gap;
    }

    if (cpu.occupied && event.ts.ticks > cpu.since.ticks) {
      SchedSlice run;
      run.thread = cpu.thread;
      run.process = cpu.process;
      run.tid = cpu.tid;
      run.pid = cpu.pid;
      run.pidKnown = cpu.pidKnown;
      run.cpu = event.cpu;
      run.start = cpu.since;
      run.end = event.ts;
      run.kind = SliceKind::Running;
      ClipAdd(out, run, window);

      ThreadState& oldSt = touch(cpu.thread, cpu.tid, cpu.pid, cpu.pidKnown, cpu.process);
      oldSt.off = true;
      oldSt.ready = false;
      oldSt.offSince = event.ts;
      oldSt.waitReason = event.oldWaitReason;
      oldSt.lastCpu = event.cpu;
    }
    cpu.sawSwitch = true;
    cpu.occupied = false;

    const std::uint32_t newTid = event.newTid != 0 ? event.newTid : event.tid;
    ThreadState& neu = touch(event.thread, newTid, event.pid, event.pidKnown, event.process);
    if (neu.off) {
      if (neu.ready) {
        SchedSlice ready;
        ready.thread = neu.key;
        ready.process = neu.process;
        ready.tid = neu.tid;
        ready.pid = neu.pid;
        ready.pidKnown = neu.pidKnown;
        ready.cpu = event.cpu;
        ready.start = neu.readySince;
        ready.end = event.ts;
        ready.kind = SliceKind::Ready;
        ClipAdd(out, ready, window);
      } else {
        SchedSlice gap;
        gap.thread = neu.key;
        gap.process = neu.process;
        gap.tid = neu.tid;
        gap.pid = neu.pid;
        gap.pidKnown = neu.pidKnown;
        gap.start = neu.offSince;
        gap.end = event.ts;
        gap.kind = SliceKind::OffCpuUnsplit;
        gap.waitReason = neu.waitReason;
        ClipAdd(out, gap, window);
      }
      neu.off = false;
      neu.ready = false;
    }
    neu.lastCpu = event.cpu;

    cpu.occupied = true;
    cpu.thread = neu.key;
    cpu.tid = neu.tid;
    cpu.pid = neu.pid;
    cpu.pidKnown = neu.pidKnown;
    cpu.process = neu.process;
    cpu.since = event.ts;
  }

  for (auto& [cpuIndex, cpu] : cpus) {
    if (!cpu.occupied) continue;
    if (closeTime.ticks <= cpu.since.ticks) {
      ++out.openTails;
      continue;
    }
    SchedSlice run;
    run.thread = cpu.thread;
    run.process = cpu.process;
    run.tid = cpu.tid;
    run.pid = cpu.pid;
    run.pidKnown = cpu.pidKnown;
    run.cpu = cpuIndex;
    run.start = cpu.since;
    run.end = closeTime;
    run.kind = SliceKind::Running;
    run.closedAtWindowEnd = true;
    ClipAdd(out, run, window);
  }

  for (auto& [_, st] : threads) {
    if (!st.off) continue;
    const Timestamp end = closeTime;
    if (end.ticks <= st.offSince.ticks) continue;
    SchedSlice gap;
    gap.thread = st.key;
    gap.process = st.process;
    gap.tid = st.tid;
    gap.pid = st.pid;
    gap.pidKnown = st.pidKnown;
    gap.start = st.ready ? st.readySince : st.offSince;
    gap.end = end;
    gap.closedAtWindowEnd = true;
    gap.waitReason = st.waitReason;
    if (st.ready) {
      gap.kind = SliceKind::Ready;
    } else if (out.readyEventsPresent) {
      // Still waiting: no ReadyThread arrived before the window closed.
      gap.kind = SliceKind::Waiting;
    } else {
      gap.kind = SliceKind::OffCpuUnsplit;
    }
    // If the thread became ready and is still ready, the waiting slice was
    // already emitted at the ready event. Only the remaining ready tail is new.
    if (!st.ready && out.readyEventsPresent) {
      gap.start = st.offSince;
      gap.kind = SliceKind::Waiting;
    }
    ClipAdd(out, gap, window);
  }

  out.note =
      "Running time is the sum of context-switch slices. "
      "Ready and waiting are split only when ReadyThread events exist. "
      "Time before the first context switch on a CPU is unmeasured, not attributed. "
      "Idle is pid 0 only when the process id is known.";
  return Status::Ok();
}

}  // namespace pulsescope
