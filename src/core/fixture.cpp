#include "pulsescope/fixture.hpp"

#include "pulsescope/json.hpp"
#include "pulsescope/validate.hpp"
#include "pulsescope/version.hpp"

namespace pulsescope {

Status LoadFixtureJson(std::string_view json, TraceSession& out, const FixtureLoadOptions& options,
                       const CancellationToken* cancel) {
  Json root;
  JsonLimits limits;
  limits.maxBytes = options.maxBytes;
  Status st = ParseJson(json, root, limits);
  if (!st) return st;
  if (root.type != JsonType::Object) return Status{ErrorCode::MalformedInput, "fixture root must be an object"};

  const Json* schema = root.Find("schema");
  if (schema == nullptr || schema->AsString() == nullptr || *schema->AsString() != "pulsescope.session.v1") {
    return Status{ErrorCode::MalformedInput, "schema must be pulsescope.session.v1"};
  }
  const Json* synthetic = root.Find("synthetic");
  if (synthetic == nullptr || !synthetic->AsBool()) {
    return Status{ErrorCode::MalformedInput, "fixture must declare synthetic true or false"};
  }
  const bool isSynthetic = *synthetic->AsBool();

  SessionInfo info;
  info.source = isSynthetic ? DataSource::SyntheticFixture : DataSource::ImportedFixture;
  info.synthetic = isSynthetic;
  info.appVersion = std::string(kVersionString);
  info.clockNote = "Fixture timestamps are converted from integer microseconds (us) or 100 ns ticks (t100ns).";
  if (const Json* label = root.Find("label"); label && label->AsString()) {
    st = ValidateText(*label->AsString(), 512);
    if (!st) return st;
    info.label = *label->AsString();
  }
  if (const Json* note = root.Find("note"); note && note->AsString()) {
    st = ValidateText(*note->AsString(), 2048);
    if (!st) return st;
    info.collectionNote = *note->AsString();
  }
  if (const Json* cpus = root.Find("cpuCount"); cpus && cpus->AsUint()) {
    if (*cpus->AsUint() > 1024) return Status{ErrorCode::InvalidArgument, "cpuCount is not plausible"};
    info.cpuCount = static_cast<std::uint32_t>(*cpus->AsUint());
  }
  const Json* events = root.Find("events");
  if (events == nullptr || events->type != JsonType::Array) {
    return Status{ErrorCode::MalformedInput, "events array is required"};
  }
  if (events->a.size() > options.maxEvents) {
    return Status{ErrorCode::LimitExceeded, "fixture event count exceeds the configured cap"};
  }

  SessionLimits slim;
  slim.maxEvents = options.maxEvents;
  TraceSession session(std::move(info), slim);

  std::uint64_t index = 0;
  for (const Json& item : events->a) {
    if (cancel != nullptr && cancel->IsCancelled()) return Status{ErrorCode::Cancelled, "fixture load cancelled"};
    if (item.type != JsonType::Object) return Status{ErrorCode::MalformedInput, "event must be an object"};
    const Json* kind = item.Find("kind");
    if (kind == nullptr || kind->AsString() == nullptr) {
      return Status{ErrorCode::MalformedInput, "event kind is required"};
    }
    Event event;
    if (!TryParseEventKind(*kind->AsString(), event.kind)) {
      return Status{ErrorCode::MalformedInput, "unknown event kind: " + *kind->AsString()};
    }
    if (const Json* ticks = item.Find("t100ns"); ticks && ticks->AsUint()) {
      event.ts.ticks = *ticks->AsUint();
    } else if (const Json* us = item.Find("us"); us && us->AsUint()) {
      if (*us->AsUint() > UINT64_MAX / kTicksPerMicrosecond) {
        return Status{ErrorCode::InvalidArgument, "timestamp overflow"};
      }
      event.ts.ticks = *us->AsUint() * kTicksPerMicrosecond;
    } else {
      return Status{ErrorCode::MalformedInput, "event needs us or t100ns"};
    }
    auto u32 = [&](const char* key, std::uint32_t& dest, bool& present) {
      if (const Json* v = item.Find(key); v && v->AsUint()) {
        if (*v->AsUint() > 0xFFFFFFFFULL) return false;
        dest = static_cast<std::uint32_t>(*v->AsUint());
        present = true;
      }
      return true;
    };
    bool present = false;
    if (!u32("pid", event.pid, present)) return Status{ErrorCode::InvalidArgument, "pid out of range"};
    if (present) event.pidKnown = true;
    present = false;
    if (!u32("tid", event.tid, present)) return Status{ErrorCode::InvalidArgument, "tid out of range"};
    present = false;
    std::uint32_t cpu = 0;
    if (!u32("cpu", cpu, present)) return Status{ErrorCode::InvalidArgument, "cpu out of range"};
    if (present) {
      if (cpu > 1023) return Status{ErrorCode::InvalidArgument, "cpu index is not plausible"};
      event.cpu = static_cast<std::uint16_t>(cpu);
    }
    present = false;
    if (!u32("parentPid", event.parentPid, present)) return Status{ErrorCode::InvalidArgument, "parentPid out of range"};
    present = false;
    if (!u32("oldTid", event.oldTid, present)) return Status{ErrorCode::InvalidArgument, "oldTid out of range"};
    present = false;
    if (!u32("newTid", event.newTid, present)) return Status{ErrorCode::InvalidArgument, "newTid out of range"};
    if (event.kind == EventKind::ContextSwitch && event.newTid == 0) event.newTid = event.tid;
    present = false;
    if (!u32("readyTid", event.readyTid, present)) return Status{ErrorCode::InvalidArgument, "readyTid out of range"};
    if (event.kind == EventKind::ReadyThread && event.readyTid == 0) event.readyTid = event.tid;

    if (const Json* image = item.Find("image"); image && image->AsString()) {
      st = ValidateText(*image->AsString(), slim.maxStringBytes);
      if (!st) return st;
      event.imageId = session.Strings().Intern(*image->AsString());
    }
    if (const Json* name = item.Find("name"); name && name->AsString()) {
      st = ValidateText(*name->AsString(), slim.maxStringBytes);
      if (!st) return st;
      event.nameId = session.Strings().Intern(*name->AsString());
    }
    if (const Json* provider = item.Find("provider"); provider && provider->AsString()) {
      st = ValidateText(*provider->AsString(), slim.maxStringBytes);
      if (!st) return st;
      event.providerId = session.Strings().Intern(*provider->AsString());
    }
    if (const Json* detail = item.Find("detail"); detail && detail->AsString()) {
      st = ValidateText(*detail->AsString(), slim.maxStringBytes);
      if (!st) return st;
      event.detailId = session.Strings().Intern(*detail->AsString());
    }
    if (const Json* ws = item.Find("workingSet"); ws && ws->AsUint()) event.workingSet = *ws->AsUint();
    if (const Json* priv = item.Find("privateBytes"); priv && priv->AsUint()) event.privateBytes = *priv->AsUint();
    if (const Json* commit = item.Find("commitBytes"); commit && commit->AsUint()) event.commitBytes = *commit->AsUint();
    if (const Json* bytes = item.Find("bytes"); bytes && bytes->AsUint()) event.byteCount = *bytes->AsUint();
    if (const Json* lost = item.Find("lost"); lost && lost->AsUint()) event.lostCount = *lost->AsUint();
    if (const Json* pct = item.Find("cpuPercent"); pct && pct->AsNumber()) event.sampledCpuPercent = *pct->AsNumber();
    if (event.workingSet > (256ull << 40) || event.privateBytes > (256ull << 40)) {
      session.NoteSuspect();
    }
    event.synthetic = isSynthetic;
    event.sequence = ++index;
    st = session.Ingest(std::move(event), cancel);
    if (!st) return st;
  }
  st = session.Finalize(cancel);
  if (!st) return st;
  out = std::move(session);
  return Status::Ok();
}

}  // namespace pulsescope
