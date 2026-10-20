#include "pulsescope/identity.hpp"

namespace pulsescope {

void IdentityTracker::Clear() {
  processes_.clear();
  threads_.clear();
  processByPid_.clear();
  threadByTid_.clear();
  nextProcess_ = 1;
  nextThread_ = 1;
  anomalies_ = 0;
}

ProcessKey IdentityTracker::OnProcessStart(std::uint32_t pid, Timestamp ts, std::uint32_t imageId,
                                          std::uint32_t parentPid) {
  auto& bucket = processByPid_[pid];
  for (auto index : bucket) {
    auto& rec = processes_[index];
    if (!rec.hasEnd) {
      // A new start while one is still open means the id was reused, or the
      // end event was lost. Close the previous lifetime at this timestamp.
      if (ts.ticks >= rec.start.ticks) {
        rec.end = ts;
        rec.hasEnd = true;
        rec.reuseClosed = true;
      } else {
        ++anomalies_;
      }
    }
  }
  ProcessRecord rec;
  rec.key.id = nextProcess_++;
  rec.pid = pid;
  rec.parentPid = parentPid;
  rec.start = ts;
  rec.imageId = imageId;
  const auto index = static_cast<std::uint32_t>(processes_.size());
  processes_.push_back(rec);
  bucket.push_back(index);
  return rec.key;
}

ProcessKey IdentityTracker::OnProcessEnd(std::uint32_t pid, Timestamp ts) {
  auto& bucket = processByPid_[pid];
  for (auto it = bucket.rbegin(); it != bucket.rend(); ++it) {
    auto& rec = processes_[*it];
    if (!rec.hasEnd && ts.ticks >= rec.start.ticks) {
      rec.end = ts;
      rec.hasEnd = true;
      return rec.key;
    }
  }
  ++anomalies_;
  return {};
}

ThreadKey IdentityTracker::OnThreadStart(std::uint32_t tid, std::uint32_t pid, bool pidKnown, Timestamp ts,
                                        ProcessKey process) {
  auto& bucket = threadByTid_[tid];
  for (auto index : bucket) {
    auto& rec = threads_[index];
    if (!rec.hasEnd) {
      if (ts.ticks >= rec.start.ticks) {
        rec.end = ts;
        rec.hasEnd = true;
        rec.reuseClosed = true;
      } else {
        ++anomalies_;
      }
    }
  }
  ThreadRecord rec;
  rec.key.id = nextThread_++;
  rec.tid = tid;
  rec.pid = pid;
  rec.pidKnown = pidKnown;
  rec.process = process;
  rec.start = ts;
  const auto index = static_cast<std::uint32_t>(threads_.size());
  threads_.push_back(rec);
  bucket.push_back(index);
  return rec.key;
}

ThreadKey IdentityTracker::OnThreadEnd(std::uint32_t tid, Timestamp ts) {
  auto& bucket = threadByTid_[tid];
  for (auto it = bucket.rbegin(); it != bucket.rend(); ++it) {
    auto& rec = threads_[*it];
    if (!rec.hasEnd && ts.ticks >= rec.start.ticks) {
      rec.end = ts;
      rec.hasEnd = true;
      return rec.key;
    }
  }
  ++anomalies_;
  return {};
}

std::optional<ProcessKey> IdentityTracker::ProcessAt(std::uint32_t pid, Timestamp ts) const {
  const auto it = processByPid_.find(pid);
  if (it == processByPid_.end()) return std::nullopt;
  const ProcessRecord* best = nullptr;
  for (auto index : it->second) {
    const auto& rec = processes_[index];
    if (ts.ticks < rec.start.ticks) continue;
    if (rec.hasEnd && ts.ticks >= rec.end.ticks) continue;
    if (best == nullptr || rec.start.ticks >= best->start.ticks) best = &rec;
  }
  if (best == nullptr) return std::nullopt;
  return best->key;
}

std::optional<ThreadKey> IdentityTracker::ThreadAt(std::uint32_t tid, Timestamp ts) const {
  const auto it = threadByTid_.find(tid);
  if (it == threadByTid_.end()) return std::nullopt;
  const ThreadRecord* best = nullptr;
  for (auto index : it->second) {
    const auto& rec = threads_[index];
    if (ts.ticks < rec.start.ticks) continue;
    if (rec.hasEnd && ts.ticks >= rec.end.ticks) continue;
    if (best == nullptr || rec.start.ticks >= best->start.ticks) best = &rec;
  }
  if (best == nullptr) return std::nullopt;
  return best->key;
}

const ProcessRecord* IdentityTracker::FindProcess(ProcessKey key) const {
  if (!key.valid() || key.id > processes_.size()) return nullptr;
  const auto& rec = processes_[static_cast<std::size_t>(key.id - 1)];
  return rec.key.id == key.id ? &rec : nullptr;
}

const ThreadRecord* IdentityTracker::FindThread(ThreadKey key) const {
  if (!key.valid() || key.id > threads_.size()) return nullptr;
  const auto& rec = threads_[static_cast<std::size_t>(key.id - 1)];
  return rec.key.id == key.id ? &rec : nullptr;
}

}  // namespace pulsescope
