#pragma once

#include "pulsescope/model.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace pulsescope {

// Tracks process and thread lifetimes so a recycled pid or tid does not
// collapse two different entities. A numeric id is only unique inside a
// half-open lifetime [start, end).
class IdentityTracker {
 public:
  void Clear();

  ProcessKey OnProcessStart(std::uint32_t pid, Timestamp ts, std::uint32_t imageId, std::uint32_t parentPid);
  ProcessKey OnProcessEnd(std::uint32_t pid, Timestamp ts);
  ThreadKey OnThreadStart(std::uint32_t tid, std::uint32_t pid, bool pidKnown, Timestamp ts, ProcessKey process);
  ThreadKey OnThreadEnd(std::uint32_t tid, Timestamp ts);

  std::optional<ProcessKey> ProcessAt(std::uint32_t pid, Timestamp ts) const;
  std::optional<ThreadKey> ThreadAt(std::uint32_t tid, Timestamp ts) const;

  const ProcessRecord* FindProcess(ProcessKey key) const;
  const ThreadRecord* FindThread(ThreadKey key) const;

  const std::vector<ProcessRecord>& Processes() const { return processes_; }
  const std::vector<ThreadRecord>& Threads() const { return threads_; }

  std::uint64_t anomalyCount() const { return anomalies_; }

 private:
  std::vector<ProcessRecord> processes_;
  std::vector<ThreadRecord> threads_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> processByPid_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> threadByTid_;
  std::uint64_t nextProcess_ = 1;
  std::uint64_t nextThread_ = 1;
  std::uint64_t anomalies_ = 0;
};

}  // namespace pulsescope
