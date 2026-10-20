#pragma once

#include "pulsescope/cancel.hpp"
#include "pulsescope/status.hpp"

#include "decoded_event.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace pulsescope::win {

struct EtwConfig {
  std::wstring sessionName = L"PulseScope";
  std::wstring etlPath;
  bool realTime = true;
  bool processesAndThreads = true;
  bool contextSwitches = true;
  bool readyThread = true;
  bool virtualAlloc = false;
  std::uint64_t maxQueuedEvents = 8192;
};

// One ETW session. The ProcessTrace callback decodes into a bounded queue and
// returns. A decoder thread delivers DecodedEvent values to the callback.
// Stop() control-stops the session, closes the trace, and joins both threads.
class EtwSession {
 public:
  using Callback = std::function<void(DecodedEvent)>;

  EtwSession();
  ~EtwSession();
  EtwSession(const EtwSession&) = delete;
  EtwSession& operator=(const EtwSession&) = delete;

  Status Start(const EtwConfig& config, Callback callback);
  Status Stop();

  bool running() const { return running_.load(std::memory_order_acquire); }
  double elapsedSeconds() const;
  std::uint64_t droppedInCallback() const { return dropped_.load(std::memory_order_relaxed); }
  std::uint64_t deliveredEvents() const { return delivered_.load(std::memory_order_relaxed); }
  std::uint64_t undecodedEvents() const { return undecoded_.load(std::memory_order_relaxed); }
  std::uint64_t eventsLostFromSession() const { return eventsLost_.load(std::memory_order_relaxed); }
  const std::string& note() const { return note_; }

  void OnRecord(const void* eventRecord);

 private:
  void ConsumerMain();
  void DecodeMain();
  Status ControlStop();

  EtwConfig config_{};
  Callback callback_;
  std::string note_;
  std::uint64_t sessionHandle_ = 0;
  std::uint64_t traceHandle_ = 0;
  std::uint64_t qpcFrequency_ = 0;
  std::uint64_t epochQpc_ = 0;
  std::uint64_t startQpc_ = 0;

  std::mutex queueMu_;
  std::condition_variable queueCv_;
  std::deque<DecodedEvent> queue_;
  bool stopDecode_ = false;

  std::thread consumer_;
  std::thread decoder_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> delivered_{0};
  std::atomic<std::uint64_t> undecoded_{0};
  std::atomic<std::uint64_t> eventsLost_{0};
};

// File import uses OpenTrace/ProcessTrace. maxFileBytes is checked before the
// file is parsed. Cancellation closes the trace handle from another thread.
Status ImportEtlFile(const std::wstring& path, const EtwSession::Callback& callback, const CancellationToken* cancel,
                     std::uint64_t maxFileBytes, std::string& note);

}  // namespace pulsescope::win
