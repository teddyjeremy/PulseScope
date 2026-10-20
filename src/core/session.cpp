#include "pulsescope/session.hpp"

#include "pulsescope/version.hpp"

#include <algorithm>

namespace pulsescope {

TraceSession::TraceSession(SessionInfo info, SessionLimits limits)
    : info_(std::move(info)), limits_(limits) {
  if (info_.appVersion.empty()) info_.appVersion = std::string(kVersionString);
}

Status TraceSession::Ingest(Event event, const CancellationToken* cancel) {
  if (cancel != nullptr && cancel->IsCancelled()) {
    return Status{ErrorCode::Cancelled, "ingest cancelled"};
  }
  if (truncated_) {
    ++dropped_;
    return Status{ErrorCode::LimitExceeded, "event cap reached; further events are counted and dropped"};
  }
  if (events_.size() >= limits_.maxEvents) {
    truncated_ = true;
    ++dropped_;
    return Status{ErrorCode::LimitExceeded, "event cap reached; further events are counted and dropped"};
  }
  if (event.sequence == 0) event.sequence = nextSeq_++;
  else if (event.sequence >= nextSeq_) nextSeq_ = event.sequence + 1;

  if (hasLast_ && event.ts < lastIngest_) ++disorder_;
  lastIngest_ = event.ts;
  hasLast_ = true;
  finalized_ = false;
  if (event.kind == EventKind::LostEvents) lostMarkers_ += event.lostCount == 0 ? 1 : event.lostCount;
  events_.push_back(std::move(event));
  return Status::Ok();
}

void TraceSession::Resolve(Event& event) {
  switch (event.kind) {
    case EventKind::ProcessStart: {
      event.pidKnown = true;
      event.process = ids_.OnProcessStart(event.pid, event.ts, event.imageId, event.parentPid);
      break;
    }
    case EventKind::ProcessEnd: {
      event.process = ids_.OnProcessEnd(event.pid, event.ts);
      event.pidKnown = event.process.valid();
      break;
    }
    case EventKind::ThreadStart: {
      if (!event.pidKnown && event.pid != 0) event.pidKnown = true;
      if (!event.process.valid() && event.pidKnown) {
        if (auto pk = ids_.ProcessAt(event.pid, event.ts)) event.process = *pk;
      }
      event.thread = ids_.OnThreadStart(event.tid, event.pid, event.pidKnown, event.ts, event.process);
      break;
    }
    case EventKind::ThreadEnd: {
      event.thread = ids_.OnThreadEnd(event.tid, event.ts);
      if (const auto* rec = ids_.FindThread(event.thread)) {
        event.pid = rec->pid;
        event.pidKnown = rec->pidKnown;
        event.process = rec->process;
      }
      break;
    }
    case EventKind::ContextSwitch: {
      if (event.newTid == 0) event.newTid = event.tid;
      event.tid = event.newTid;
      if (event.newPidKnown) {
        event.pid = event.newPid;
        event.pidKnown = true;
      }
      if (auto tk = ids_.ThreadAt(event.newTid, event.ts)) {
        event.thread = *tk;
        if (const auto* rec = ids_.FindThread(*tk)) {
          if (!event.pidKnown && rec->pidKnown) {
            event.pid = rec->pid;
            event.pidKnown = true;
          }
          if (!event.process.valid()) event.process = rec->process;
        }
      }
      if (!event.process.valid() && event.pidKnown) {
        if (auto pk = ids_.ProcessAt(event.pid, event.ts)) event.process = *pk;
      }
      break;
    }
    case EventKind::ReadyThread: {
      if (event.readyTid == 0) event.readyTid = event.tid;
      event.tid = event.readyTid;
      if (event.readyPidKnown) {
        event.pid = event.readyPid;
        event.pidKnown = true;
      }
      if (auto tk = ids_.ThreadAt(event.readyTid, event.ts)) {
        event.thread = *tk;
        if (const auto* rec = ids_.FindThread(*tk)) {
          if (!event.pidKnown && rec->pidKnown) {
            event.pid = rec->pid;
            event.pidKnown = true;
          }
          if (!event.process.valid()) event.process = rec->process;
        }
      }
      break;
    }
    default: {
      if (event.tid != 0) {
        if (auto tk = ids_.ThreadAt(event.tid, event.ts)) {
          event.thread = *tk;
          if (const auto* rec = ids_.FindThread(*tk)) {
            if (!event.pidKnown && rec->pidKnown) {
              event.pid = rec->pid;
              event.pidKnown = true;
            }
            if (!event.process.valid()) event.process = rec->process;
          }
        }
      }
      if (!event.process.valid() && event.pidKnown) {
        if (auto pk = ids_.ProcessAt(event.pid, event.ts)) event.process = *pk;
      } else if (!event.pidKnown && event.pid != 0) {
        // A non-zero pid on a sample is treated as known. Pid 0 is only
        // known when the producer set pidKnown (the Idle process).
        event.pidKnown = true;
        if (auto pk = ids_.ProcessAt(event.pid, event.ts)) event.process = *pk;
      }
      break;
    }
  }
}

Status TraceSession::Finalize(const CancellationToken* cancel) {
  std::sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) {
    if (a.ts != b.ts) return a.ts < b.ts;
    return a.sequence < b.sequence;
  });
  ids_.Clear();
  for (std::uint64_t i = 0; i < events_.size(); ++i) {
    if (!ShouldContinue(cancel, i)) {
      finalized_ = false;
      return Status{ErrorCode::Cancelled, "finalize cancelled"};
    }
    Resolve(events_[i]);
  }
  finalized_ = true;
  return Status::Ok();
}

Timestamp TraceSession::TraceStart() const {
  if (events_.empty()) return {};
  return events_.front().ts;
}

Timestamp TraceSession::TraceEnd() const {
  if (events_.empty()) return {};
  return events_.back().ts;
}

}  // namespace pulsescope
