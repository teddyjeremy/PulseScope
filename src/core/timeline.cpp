#include "pulsescope/timeline.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace pulsescope {
namespace {

std::uint32_t PixelOf(std::uint64_t t, const TimeRange& window, std::uint32_t pixels) {
  const std::uint64_t span = window.durationTicks();
  if (span == 0 || pixels == 0) return 0;
  if (t <= window.start.ticks) return 0;
  if (t >= window.end.ticks) return pixels - 1;
  const std::uint64_t rel = t - window.start.ticks;
  auto px = static_cast<std::uint32_t>((rel * pixels) / span);
  if (px >= pixels) px = pixels - 1;
  return px;
}

std::string Fold(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

}  // namespace

Status BuildTimeline(const TraceSession& session, const TimelineOptions& options, TimelineModel& out,
                     const CancellationToken* cancel) {
  out = {};
  SchedOptions sopt;
  sopt.window = options.window;
  SchedModel model;
  const Status st = ReconstructScheduling(session, sopt, model, cancel);
  if (!st) return st;
  out.window = model.window;
  out.pixels = options.pixels == 0 ? 1 : options.pixels;
  if (out.window.durationTicks() == 0) {
    out.note = "timeline window has no duration";
    return Status::Ok();
  }

  struct Acc {
    TimelineLane lane;
  };
  std::unordered_map<std::uint64_t, Acc> lanes;
  const std::string needle = Fold(options.text);

  for (const auto& slice : model.slices) {
    const std::uint64_t id = slice.thread.valid() ? slice.thread.id : (0xA000000000000000ULL | slice.tid);
    Acc& acc = lanes[id];
    auto& lane = acc.lane;
    lane.thread = slice.thread;
    lane.process = slice.process;
    lane.tid = slice.tid;
    lane.pid = slice.pid;
    if (lane.image.empty()) {
      if (const auto* proc = session.Identities().FindProcess(slice.process)) {
        lane.image = session.Strings().Get(proc->imageId);
      }
    }
    if (slice.kind == SliceKind::Running) lane.executingTicks += slice.end.ticks - slice.start.ticks;
    if (lane.columns.empty()) lane.columns.assign(out.pixels, {});
    const std::uint32_t x0 = PixelOf(slice.start.ticks, out.window, out.pixels);
    const std::uint32_t x1 = PixelOf(slice.end.ticks > 0 ? slice.end.ticks - 1 : slice.end.ticks, out.window, out.pixels);
    for (std::uint32_t x = x0; x <= x1 && x < out.pixels; ++x) {
      auto& col = lane.columns[x];
      switch (slice.kind) {
        case SliceKind::Running:
          if (col.running < 0xFFFF) ++col.running;
          break;
        case SliceKind::Ready:
          if (col.ready < 0xFFFF) ++col.ready;
          break;
        case SliceKind::Waiting:
          if (col.waiting < 0xFFFF) ++col.waiting;
          break;
        case SliceKind::OffCpuUnsplit:
          if (col.unsplit < 0xFFFF) ++col.unsplit;
          break;
      }
    }
    if (x1 > x0) lane.spans.push_back(slice);
  }

  std::vector<TimelineLane> all;
  all.reserve(lanes.size());
  for (auto& [_, acc] : lanes) {
    auto& lane = acc.lane;
    if (!needle.empty()) {
      const std::string blob = Fold(lane.image + " " + std::to_string(lane.tid) + " " + std::to_string(lane.pid));
      if (blob.find(needle) == std::string::npos) continue;
    }
    for (auto& col : lane.columns) {
      const int kinds = (col.running > 0) + (col.ready > 0) + (col.waiting > 0) + (col.unsplit > 0);
      if (kinds == 0) col.dominant = ColumnKind::Empty;
      else if (kinds > 1) col.dominant = ColumnKind::Mixed;
      else if (col.running) col.dominant = ColumnKind::Running;
      else if (col.ready) col.dominant = ColumnKind::Ready;
      else if (col.waiting) col.dominant = ColumnKind::Waiting;
      else col.dominant = ColumnKind::OffCpuUnsplit;
    }
    lane.label = lane.image.empty() ? ("tid " + std::to_string(lane.tid)) : (lane.image + " tid " + std::to_string(lane.tid));
    all.push_back(std::move(lane));
  }
  std::sort(all.begin(), all.end(), [](const TimelineLane& a, const TimelineLane& b) {
    if (a.executingTicks != b.executingTicks) return a.executingTicks > b.executingTicks;
    return a.tid < b.tid;
  });
  const std::uint32_t cap = options.maxLanes == 0 ? 1 : options.maxLanes;
  if (all.size() > cap) {
    out.omittedLanes = all.size() - cap;
    all.resize(cap);
  }
  out.lanes = std::move(all);
  out.note =
      "Each lane is one thread identity. Columns aggregate every slice that touches that pixel. "
      "Spans narrower than a pixel are not kept individually; use the column counts. "
      "Zoom by requesting a narrower window.";
  return Status::Ok();
}

const SchedSlice* HitTestLane(const TimelineLane& lane, std::uint32_t x, const TimeRange& window, std::uint32_t pixels) {
  if (pixels == 0 || x >= pixels) return nullptr;
  const std::uint64_t span = window.durationTicks();
  if (span == 0) return nullptr;
  const std::uint64_t t = window.start.ticks + (span * x) / pixels;
  const SchedSlice* best = nullptr;
  for (const auto& slice : lane.spans) {
    if (t < slice.start.ticks || t >= slice.end.ticks) continue;
    if (slice.kind != SliceKind::Running) continue;
    best = &slice;
  }
  return best;
}

}  // namespace pulsescope
