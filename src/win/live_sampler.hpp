#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pulsescope::win {

struct LiveProcess {
  std::uint32_t pid = 0;
  std::string image;
  std::uint64_t workingSet = 0;
  std::uint64_t privateBytes = 0;
  std::uint64_t commitBytes = 0;
  double cpuPercentOfMachine = 0;
  bool cpuKnown = false;
  bool accessLimited = false;
};

// Unelevated sampler. CPU percent comes from GetProcessTimes deltas between
// samples, so the first sample has no percent. These numbers are sampled
// counters, not context-switch execution time.
class LiveSampler {
 public:
  LiveSampler();
  ~LiveSampler();
  void Start(std::uint32_t intervalMs);
  void Stop();
  std::vector<LiveProcess> Snapshot() const;
  std::uint32_t processorCount() const { return processors_; }

 private:
  void ThreadMain();
  std::atomic<bool> stop_{false};
  std::uint32_t intervalMs_ = 500;
  std::uint32_t processors_ = 1;
  mutable std::mutex mu_;
  std::vector<LiveProcess> rows_;
  std::thread thread_;
};

}  // namespace pulsescope::win
