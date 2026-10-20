#pragma once

#include "pulsescope/timestamp.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pulsescope {

enum class DataSource {
  SyntheticFixture,
  ImportedFixture,
  EtwRealtime,
  EtlFile,
  LiveSampled,
};

enum class EventKind : std::uint16_t {
  Unknown = 0,
  ProcessStart,
  ProcessEnd,
  ThreadStart,
  ThreadEnd,
  ContextSwitch,
  ReadyThread,
  SampledCpu,
  MemorySample,
  VirtualAlloc,
  VirtualFree,
  LostEvents,
  Marker,
};

inline const char* ToString(DataSource s) {
  switch (s) {
    case DataSource::SyntheticFixture:
      return "synthetic-fixture";
    case DataSource::ImportedFixture:
      return "imported-fixture";
    case DataSource::EtwRealtime:
      return "etw-realtime";
    case DataSource::EtlFile:
      return "etl-file";
    case DataSource::LiveSampled:
      return "live-sampled";
  }
  return "unknown";
}

inline const char* ToString(EventKind k) {
  switch (k) {
    case EventKind::Unknown:
      return "unknown";
    case EventKind::ProcessStart:
      return "processStart";
    case EventKind::ProcessEnd:
      return "processEnd";
    case EventKind::ThreadStart:
      return "threadStart";
    case EventKind::ThreadEnd:
      return "threadEnd";
    case EventKind::ContextSwitch:
      return "cswitch";
    case EventKind::ReadyThread:
      return "readyThread";
    case EventKind::SampledCpu:
      return "sampledCpu";
    case EventKind::MemorySample:
      return "memorySample";
    case EventKind::VirtualAlloc:
      return "virtualAlloc";
    case EventKind::VirtualFree:
      return "virtualFree";
    case EventKind::LostEvents:
      return "lostEvents";
    case EventKind::Marker:
      return "marker";
  }
  return "unknown";
}

inline bool TryParseEventKind(std::string_view s, EventKind& out) {
  struct Item {
    const char* n;
    EventKind k;
  };
  static constexpr Item kItems[] = {
      {"unknown", EventKind::Unknown},
      {"processStart", EventKind::ProcessStart},
      {"processEnd", EventKind::ProcessEnd},
      {"threadStart", EventKind::ThreadStart},
      {"threadEnd", EventKind::ThreadEnd},
      {"cswitch", EventKind::ContextSwitch},
      {"readyThread", EventKind::ReadyThread},
      {"sampledCpu", EventKind::SampledCpu},
      {"memorySample", EventKind::MemorySample},
      {"virtualAlloc", EventKind::VirtualAlloc},
      {"virtualFree", EventKind::VirtualFree},
      {"lostEvents", EventKind::LostEvents},
      {"marker", EventKind::Marker},
  };
  for (const auto& item : kItems) {
    if (s == item.n) {
      out = item.k;
      return true;
    }
  }
  return false;
}

struct ProcessKey {
  std::uint64_t id = 0;
  constexpr bool valid() const { return id != 0; }
  constexpr bool operator==(const ProcessKey& o) const { return id == o.id; }
  constexpr bool operator<(const ProcessKey& o) const { return id < o.id; }
};

struct ThreadKey {
  std::uint64_t id = 0;
  constexpr bool valid() const { return id != 0; }
  constexpr bool operator==(const ThreadKey& o) const { return id == o.id; }
  constexpr bool operator<(const ThreadKey& o) const { return id < o.id; }
};

class StringTable {
 public:
  StringTable() { strings_.emplace_back(); }

  std::uint32_t Intern(std::string_view text) {
    if (text.empty()) return 0;
    const std::string key(text);
    const auto it = index_.find(key);
    if (it != index_.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(strings_.size());
    strings_.push_back(key);
    index_.emplace(strings_.back(), id);
    return id;
  }

  const std::string& Get(std::uint32_t id) const {
    if (id >= strings_.size()) return strings_[0];
    return strings_[id];
  }

  std::uint32_t size() const { return static_cast<std::uint32_t>(strings_.size()); }

 private:
  std::vector<std::string> strings_;
  std::unordered_map<std::string, std::uint32_t> index_;
};

// One normalized record. Strings live in the session string table.
// Identity keys are filled in by TraceSession::Finalize and are not
// implied by pid/tid alone.
struct Event {
  std::uint64_t sequence = 0;
  Timestamp ts{};
  EventKind kind = EventKind::Unknown;
  std::uint32_t pid = 0;
  std::uint32_t tid = 0;
  std::uint16_t cpu = 0xFFFF;
  std::uint32_t providerId = 0;
  std::uint32_t nameId = 0;
  std::uint32_t detailId = 0;
  std::uint32_t imageId = 0;
  std::uint32_t parentPid = 0;
  ProcessKey process{};
  ThreadKey thread{};
  bool pidKnown = false;
  bool synthetic = false;
  int opcode = -1;

  std::uint32_t oldTid = 0;
  std::uint32_t newTid = 0;
  std::uint32_t oldPid = 0;
  std::uint32_t newPid = 0;
  bool newPidKnown = false;
  std::int8_t oldWaitReason = 0;
  std::int8_t oldThreadState = 0;
  std::int8_t newPriority = 0;
  std::int8_t oldPriority = 0;

  std::uint32_t readyTid = 0;
  bool readyPidKnown = false;
  std::uint32_t readyPid = 0;

  double sampledCpuPercent = 0;  // 0..100, process-wide. Not execution time.

  std::uint64_t workingSet = 0;
  std::uint64_t privateBytes = 0;
  std::uint64_t commitBytes = 0;

  std::uint64_t byteCount = 0;  // virtual alloc/free size when known
  std::uint64_t lostCount = 0;
};

struct ProcessRecord {
  ProcessKey key{};
  std::uint32_t pid = 0;
  std::uint32_t parentPid = 0;
  Timestamp start{};
  Timestamp end{};
  bool hasEnd = false;
  std::uint32_t imageId = 0;
  bool reuseClosed = false;
};

struct ThreadRecord {
  ThreadKey key{};
  std::uint32_t tid = 0;
  std::uint32_t pid = 0;
  bool pidKnown = false;
  ProcessKey process{};
  Timestamp start{};
  Timestamp end{};
  bool hasEnd = false;
  bool reuseClosed = false;
};

}  // namespace pulsescope
