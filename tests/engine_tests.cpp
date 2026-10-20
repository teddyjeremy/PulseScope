#include "harness.hpp"

#include "pulsescope/bookmarks.hpp"
#include "pulsescope/compare.hpp"
#include "pulsescope/cpu.hpp"
#include "pulsescope/export.hpp"
#include "pulsescope/filter.hpp"
#include "pulsescope/fixture.hpp"
#include "pulsescope/json.hpp"
#include "pulsescope/memory.hpp"
#include "pulsescope/settings.hpp"
#include "pulsescope/timeline.hpp"
#include "pulsescope/timestamp.hpp"
#include "pulsescope/validate.hpp"

#include <fstream>
#include <string>

namespace {

using namespace pulsescope;

const char kReady[] = R"({
  "schema": "pulsescope.session.v1",
  "synthetic": true,
  "label": "ready-wait",
  "cpuCount": 1,
  "events": [
    {"us": 0, "kind": "processStart", "pid": 0, "image": "Idle"},
    {"us": 0, "kind": "processStart", "pid": 42, "image": "worker.exe"},
    {"us": 0, "kind": "threadStart", "pid": 0, "tid": 1},
    {"us": 0, "kind": "threadStart", "pid": 42, "tid": 7},
    {"us": 0, "kind": "cswitch", "cpu": 0, "newTid": 7, "oldTid": 1},
    {"us": 1000, "kind": "cswitch", "cpu": 0, "newTid": 1, "oldTid": 7},
    {"us": 1500, "kind": "readyThread", "readyTid": 7, "pid": 42},
    {"us": 1800, "kind": "cswitch", "cpu": 0, "newTid": 7, "oldTid": 1},
    {"us": 2000, "kind": "cswitch", "cpu": 0, "newTid": 1, "oldTid": 7}
  ]
})";

const char kMigrate[] = R"({
  "schema": "pulsescope.session.v1",
  "synthetic": true,
  "label": "migrate",
  "cpuCount": 2,
  "events": [
    {"us": 0, "kind": "processStart", "pid": 9, "image": "mover.exe"},
    {"us": 0, "kind": "threadStart", "pid": 9, "tid": 4},
    {"us": 0, "kind": "cswitch", "cpu": 0, "newTid": 4, "oldTid": 1},
    {"us": 100, "kind": "cswitch", "cpu": 0, "newTid": 1, "oldTid": 4},
    {"us": 200, "kind": "cswitch", "cpu": 1, "newTid": 4, "oldTid": 2},
    {"us": 300, "kind": "cswitch", "cpu": 1, "newTid": 2, "oldTid": 4}
  ]
})";

const char kReuse[] = R"({
  "schema": "pulsescope.session.v1",
  "synthetic": true,
  "label": "pid-reuse",
  "cpuCount": 1,
  "events": [
    {"us": 0, "kind": "processStart", "pid": 10, "image": "alpha.exe"},
    {"us": 0, "kind": "threadStart", "pid": 10, "tid": 3},
    {"us": 0, "kind": "cswitch", "cpu": 0, "newTid": 3, "oldTid": 1},
    {"us": 1000, "kind": "cswitch", "cpu": 0, "newTid": 1, "oldTid": 3},
    {"us": 1000, "kind": "processEnd", "pid": 10},
    {"us": 1000, "kind": "processStart", "pid": 10, "image": "beta.exe"},
    {"us": 1000, "kind": "threadStart", "pid": 10, "tid": 3},
    {"us": 1000, "kind": "cswitch", "cpu": 0, "newTid": 3, "oldTid": 1},
    {"us": 2000, "kind": "cswitch", "cpu": 0, "newTid": 1, "oldTid": 3},
    {"us": 2000, "kind": "processEnd", "pid": 10}
  ]
})";

const ThreadCpuStats* FindTid(const CpuAnalysis& cpu, std::uint32_t tid) {
  for (const auto& row : cpu.threads) {
    if (row.tid == tid && !row.idle) return &row;
  }
  return nullptr;
}

}  // namespace

PS_TEST(qpc_converts_without_overflow) {
  std::uint64_t ticks = 0;
  PS_CHECK(QpcDeltaToTicks(10'000'000, 10'000'000, ticks));
  PS_EQ(ticks, kTicksPerSecond);
  Timestamp ts;
  PS_CHECK(QpcToTimestamp(50'000'000, 40'000'000, 10'000'000, ts));
  PS_EQ(ts.ticks, kTicksPerSecond);
  PS_CHECK(!QpcToTimestamp(1, 2, 10'000'000, ts));
  PS_CHECK(!QpcDeltaToTicks(5, 0, ticks));
  PS_CHECK(FileTimeToTimestamp(2'000, 1'000, ts));
  PS_EQ(ts.ticks, 1000u);
}

PS_TEST(ready_and_waiting_are_split_only_with_ready_events) {
  TraceSession session;
  PS_CHECK(LoadFixtureJson(kReady, session));
  PS_CHECK(session.Info().synthetic);
  CpuAnalysis cpu;
  PS_CHECK(AnalyzeCpu(session, {}, cpu));
  const auto* worker = FindTid(cpu, 7);
  PS_CHECK(worker != nullptr);
  PS_EQ(worker->executingTicks, 1200ull * kTicksPerMicrosecond);
  PS_EQ(worker->waitingTicks, 500ull * kTicksPerMicrosecond);
  PS_EQ(worker->readyTicks, 300ull * kTicksPerMicrosecond);
  PS_EQ(worker->unsplitOffCpuTicks, 0u);
  PS_CHECK(cpu.readyTimeAvailable);
  PS_CHECK(cpu.idleSeen);
  PS_EQ(worker->image, std::string("worker.exe"));
}

PS_TEST(migration_counts_a_cpu_change) {
  TraceSession session;
  PS_CHECK(LoadFixtureJson(kMigrate, session));
  CpuAnalysis cpu;
  PS_CHECK(AnalyzeCpu(session, {}, cpu));
  const auto* mover = FindTid(cpu, 4);
  PS_CHECK(mover != nullptr);
  PS_EQ(mover->migrations, 1u);
  PS_EQ(mover->executingTicks, 200ull * kTicksPerMicrosecond);
  PS_CHECK(mover->unsplitOffCpuTicks >= 100ull * kTicksPerMicrosecond);
  PS_CHECK(!cpu.readyTimeAvailable);
  PS_EQ(mover->waitingTicks, 0u);
}

PS_TEST(pid_reuse_does_not_merge_lifetimes) {
  TraceSession session;
  PS_CHECK(LoadFixtureJson(kReuse, session));
  PS_EQ(session.Identities().Processes().size(), 2u);
  PS_CHECK(session.Identities().Processes()[0].reuseClosed || session.Identities().Processes()[0].hasEnd);
  CpuAnalysis cpu;
  PS_CHECK(AnalyzeCpu(session, {}, cpu));
  std::uint64_t alpha = 0;
  std::uint64_t beta = 0;
  for (const auto& proc : cpu.processes) {
    if (proc.image == "alpha.exe") alpha = proc.executingTicks;
    if (proc.image == "beta.exe") beta = proc.executingTicks;
  }
  PS_EQ(alpha, 1000ull * kTicksPerMicrosecond);
  PS_EQ(beta, 1000ull * kTicksPerMicrosecond);
  Timestamp mid;
  mid.ticks = 1500ull * kTicksPerMicrosecond;
  const auto key = session.Identities().ProcessAt(10, mid);
  PS_CHECK(key.has_value());
  const auto* rec = session.Identities().FindProcess(*key);
  PS_CHECK(rec != nullptr);
  PS_EQ(session.Strings().Get(rec->imageId), std::string("beta.exe"));
}

PS_TEST(memory_slope_and_suspect_samples) {
  const char* json = R"({
    "schema": "pulsescope.session.v1",
    "synthetic": true,
    "label": "growth",
    "cpuCount": 1,
    "events": [
      {"us": 0, "kind": "processStart", "pid": 5, "image": "grow.exe"},
      {"us": 0, "kind": "memorySample", "pid": 5, "workingSet": 1000, "privateBytes": 800, "commitBytes": 900},
      {"us": 10000000, "kind": "memorySample", "pid": 5, "workingSet": 2000, "privateBytes": 800, "commitBytes": 900},
      {"us": 10000000, "kind": "memorySample", "pid": 5, "workingSet": 999999999999999999, "privateBytes": 1, "commitBytes": 1},
      {"us": 1, "kind": "virtualAlloc", "pid": 5, "bytes": 4096}
    ]
  })";
  TraceSession session;
  PS_CHECK(LoadFixtureJson(json, session));
  PS_CHECK(session.suspectCount() >= 1);
  MemoryAnalysis mem;
  PS_CHECK(AnalyzeMemory(session, {}, mem));
  PS_CHECK(mem.hasSamples);
  PS_CHECK(mem.hasAllocationEvents);
  PS_EQ(mem.processes.size(), 1u);
  PS_CHECK(mem.processes[0].slopeValid);
  PS_CHECK(std::fabs(mem.processes[0].workingSetBytesPerSecond - 100.0) < 0.01);
  PS_EQ(mem.processes[0].suspectSamples, 1u);
  PS_EQ(mem.processes[0].virtualAllocBytes, 4096u);
  PS_EQ(mem.processes[0].samples, 2u);
}

PS_TEST(comparison_flags_regression_and_refuses_mixed_metrics) {
  TraceSession base;
  TraceSession cand;
  PS_CHECK(LoadFixtureJson(kReady, base));
  std::string heavier = kReady;
  const auto pos = heavier.find("\"label\": \"ready-wait\"");
  PS_CHECK(pos != std::string::npos);
  CpuAnalysis a;
  CpuAnalysis b;
  PS_CHECK(AnalyzeCpu(base, {}, a));
  // Candidate runs the same pattern twice as long by editing the closing switch.
  std::string json = kReady;
  const auto at = json.rfind("\"us\": 2000");
  PS_CHECK(at != std::string::npos);
  json.replace(at, std::string("\"us\": 2000").size(), "\"us\": 5000");
  PS_CHECK(LoadFixtureJson(json, cand));
  PS_CHECK(AnalyzeCpu(cand, {}, b));
  MemoryAnalysis emptyA;
  MemoryAnalysis emptyB;
  emptyA.window = a.window;
  emptyB.window = b.window;
  const auto cmp = CompareAnalyses(a, emptyA, b, emptyB, base.Info(), cand.Info(), {});
  PS_CHECK(cmp.compatible);
  bool saw = false;
  for (const auto& delta : cmp.deltas) {
    if (delta.id == "cpu.nonIdleExecutingSeconds") {
      saw = true;
      PS_CHECK(delta.comparable);
      PS_CHECK(delta.regression);
      PS_CHECK(delta.percentDelta.has_value());
    }
  }
  PS_CHECK(saw);

  CpuAnalysis sampledOnly;
  sampledOnly.window = a.window;
  sampledOnly.hasSampledCpu = true;
  const auto mixed = CompareAnalyses(a, emptyA, sampledOnly, emptyB, base.Info(), cand.Info(), {});
  bool skipped = false;
  for (const auto& delta : mixed.deltas) {
    if (delta.id == "cpu.nonIdleExecutingSeconds") {
      skipped = !delta.comparable;
    }
  }
  PS_CHECK(skipped);
}

PS_TEST(filter_sort_and_csv_round_trip_shape) {
  TraceSession session;
  PS_CHECK(LoadFixtureJson(kReady, session));
  EventFilter filter;
  filter.text = "WORKER";
  std::vector<FilterHit> hits;
  PS_CHECK(FilterEvents(session, filter, hits));
  PS_CHECK(!hits.empty());
  CpuAnalysis cpu;
  MemoryAnalysis mem;
  PS_CHECK(AnalyzeCpu(session, {}, cpu));
  PS_CHECK(AnalyzeMemory(session, {}, mem));
  std::vector<ProcessRow> rows;
  PS_CHECK(BuildProcessRows(session, cpu, mem, ProcessSort::ExecutingTime, true, rows));
  PS_CHECK(rows.size() >= 2);
  PS_CHECK(rows[0].executingTicks >= rows[1].executingTicks);
  const std::string csv = ExportEventsCsv(session, hits);
  PS_CHECK(csv.find("worker") != std::string::npos || csv.find("cswitch") != std::string::npos);
  ReportInput report;
  report.session = &session;
  report.cpu = &cpu;
  report.memory = &mem;
  const std::string dumped = ExportAnalysisJson(report);
  Json parsed;
  PS_CHECK(ParseJson(dumped, parsed));
  PS_CHECK(parsed.Find("cpu") != nullptr);
  PS_EQ(*parsed.Find("session")->Find("synthetic")->AsBool(), true);
  const std::string html = ExportReportHtml(report);
  PS_CHECK(html.find("not a cause") != std::string::npos || html.find("does not infer") != std::string::npos ||
           html.find("Idle") != std::string::npos);
  PS_CHECK(html.find("<script") == std::string::npos);
}

PS_TEST(timeline_aggregates_subpixel_slices) {
  TraceSession session;
  PS_CHECK(LoadFixtureJson(kReady, session));
  TimelineOptions opt;
  opt.pixels = 4;
  opt.maxLanes = 8;
  TimelineModel model;
  PS_CHECK(BuildTimeline(session, opt, model));
  PS_CHECK(!model.lanes.empty());
  PS_EQ(model.lanes[0].columns.size(), 4u);
  std::uint32_t painted = 0;
  for (const auto& col : model.lanes[0].columns) {
    if (col.dominant != ColumnKind::Empty) ++painted;
  }
  PS_CHECK(painted > 0);
}

PS_TEST(malformed_fixture_and_cap_and_cancel) {
  TraceSession session;
  Status st = LoadFixtureJson("{", session);
  PS_CHECK(!st);
  PS_EQ(st.code, ErrorCode::MalformedInput);
  st = LoadFixtureJson(R"({"schema":"pulsescope.session.v1","synthetic":true,"events":[]})", session);
  PS_CHECK(st);
  FixtureLoadOptions lim;
  lim.maxEvents = 1;
  st = LoadFixtureJson(kReady, session, lim);
  PS_CHECK(!st);
  PS_EQ(st.code, ErrorCode::LimitExceeded);
  CancellationSource source;
  source.Cancel();
  const CancellationToken token = source.token();
  st = LoadFixtureJson(kReady, session, {}, &token);
  PS_CHECK(!st);
  PS_EQ(st.code, ErrorCode::Cancelled);
}

PS_TEST(bookmarks_settings_and_validation) {
  BookmarkStore store;
  Bookmark mark;
  mark.id = "b1";
  mark.title = "hot spot";
  mark.start.ticks = 10;
  mark.end.ticks = 50;
  mark.note = "look here";
  PS_CHECK(store.Add(mark));
  BookmarkStore loaded;
  PS_CHECK(BookmarkStore::FromJson(store.ToJson(), loaded));
  PS_EQ(loaded.All().size(), 1u);
  PS_EQ(loaded.All()[0].title, std::string("hot spot"));
  Settings settings;
  PS_CHECK(SettingsFromJson(SettingsToJson(settings), settings));
  PS_CHECK(!SettingsFromJson(R"({"theme":"neon"})", settings));
  PS_CHECK(!ValidateText(std::string(5000, 'a'), 100));
  PS_CHECK(!ValidateAllocation(100, 10));
  PS_CHECK(IsSuspectCounter(256ull << 40) == false);
  PS_CHECK(IsSuspectCounter((256ull << 40) + 1));
}

PS_TEST(json_preserves_large_integers_and_rejects_limits) {
  Json value;
  PS_CHECK(ParseJson("{\"n\":9007199254740993}", value));
  PS_EQ(*value.Find("n")->AsUint(), 9007199254740993ull);
  Status st = ParseJson("{\"a\":1} trailing", value);
  PS_CHECK(!st);
  JsonLimits limits;
  limits.maxDepth = 2;
  st = ParseJson("{\"a\":{\"b\":{\"c\":1}}}", value, limits);
  PS_CHECK(!st);
}
