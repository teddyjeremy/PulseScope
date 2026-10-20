#pragma once

#include "pulsescope/cancel.hpp"
#include "pulsescope/identity.hpp"
#include "pulsescope/model.hpp"
#include "pulsescope/status.hpp"

#include <string>

namespace pulsescope {

struct SessionInfo {
  std::string schema = "pulsescope.session.v1";
  DataSource source = DataSource::ImportedFixture;
  bool synthetic = false;
  std::string label;
  std::string sourceName;
  std::uint32_t cpuCount = 0;
  std::uint32_t schemaVersion = 1;
  std::string appVersion;
  std::string collectionNote;
  std::string clockNote;
};

struct SessionLimits {
  std::uint64_t maxEvents = 500'000;
  std::uint32_t maxStringBytes = 4096;
};

// Owns normalized events, the string table, and process/thread identities.
// Ingest is append-only. Finalize sorts by time, rebuilds identities, and
// attaches keys. Analysis APIs require a finalized session.
class TraceSession {
 public:
  TraceSession() = default;
  TraceSession(SessionInfo info, SessionLimits limits);

  Status Ingest(Event event, const CancellationToken* cancel = nullptr);
  Status Finalize(const CancellationToken* cancel = nullptr);

  bool finalized() const { return finalized_; }
  bool truncated() const { return truncated_; }
  std::uint64_t droppedEvents() const { return dropped_; }
  std::uint64_t disorderCount() const { return disorder_; }
  std::uint64_t undecodedCount() const { return undecoded_; }
  std::uint64_t suspectCount() const { return suspect_; }
  std::uint64_t lostEventMarkers() const { return lostMarkers_; }

  const SessionInfo& Info() const { return info_; }
  SessionInfo& Info() { return info_; }
  const SessionLimits& Limits() const { return limits_; }
  const std::vector<Event>& Events() const { return events_; }
  const IdentityTracker& Identities() const { return ids_; }
  const StringTable& Strings() const { return strings_; }
  StringTable& Strings() { return strings_; }

  void NoteUndecoded(std::uint64_t n = 1) { undecoded_ += n; }
  void NoteSuspect(std::uint64_t n = 1) { suspect_ += n; }
  void AddLostEvents(std::uint64_t n) { lostMarkers_ += n; }

  Timestamp TraceStart() const;
  Timestamp TraceEnd() const;
  bool empty() const { return events_.empty(); }

 private:
  void Resolve(Event& event);

  SessionInfo info_{};
  SessionLimits limits_{};
  std::vector<Event> events_;
  StringTable strings_{};
  IdentityTracker ids_{};
  bool finalized_ = false;
  bool truncated_ = false;
  std::uint64_t dropped_ = 0;
  std::uint64_t disorder_ = 0;
  std::uint64_t undecoded_ = 0;
  std::uint64_t suspect_ = 0;
  std::uint64_t lostMarkers_ = 0;
  Timestamp lastIngest_{};
  bool hasLast_ = false;
  std::uint64_t nextSeq_ = 1;
};

}  // namespace pulsescope
