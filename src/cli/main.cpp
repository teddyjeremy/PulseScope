#include "pulsescope/compare.hpp"
#include "pulsescope/cpu.hpp"
#include "pulsescope/export.hpp"
#include "pulsescope/filter.hpp"
#include "pulsescope/fixture.hpp"
#include "pulsescope/memory.hpp"
#include "pulsescope/timeline.hpp"
#include "pulsescope/version.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

using namespace pulsescope;

void Usage() {
  std::cerr
      << "PulseScope " << kVersionString << "\n"
      << "  pulsescope-cli analyze <fixture.json> [--out-json f] [--out-html f] [--out-csv f]\n"
      << "  pulsescope-cli compare <baseline.json> <candidate.json> [--out-json f] [--out-html f]\n"
      << "  pulsescope-cli bench [--events N]\n"
      << "  pulsescope-cli version\n"
      << "Fixtures are not ETL files and are labeled synthetic when they say so.\n"
      << "Real ETW collection is the Windows application, not this portable tool.\n";
}

Status ReadAll(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status{ErrorCode::IoFailure, "could not open " + path};
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  if (out.size() > 64u * 1024u * 1024u) return Status{ErrorCode::LimitExceeded, "input exceeds 64 MiB"};
  return Status::Ok();
}

Status WriteAll(const std::string& path, const std::string& body) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return Status{ErrorCode::IoFailure, "could not write " + path};
  out << body;
  if (!out) return Status{ErrorCode::IoFailure, "write failed for " + path};
  return Status::Ok();
}

int Fail(const Status& st) {
  std::cerr << ToString(st.code) << ": " << st.message << "\n";
  if (st.code == ErrorCode::InvalidArgument) return 2;
  if (st.code == ErrorCode::MalformedInput || st.code == ErrorCode::IoFailure) return 3;
  if (st.code == ErrorCode::Cancelled) return 4;
  return 1;
}

std::string BenchFixture(std::uint64_t slices) {
  std::ostringstream os;
  os << "{\n  \"schema\": \"pulsescope.session.v1\",\n  \"synthetic\": true,\n"
     << "  \"label\": \"bench\",\n  \"cpuCount\": 4,\n  \"events\": [\n"
     << "    {\"us\": 0, \"kind\": \"processStart\", \"pid\": 10, \"image\": \"bench.exe\"},\n"
     << "    {\"us\": 0, \"kind\": \"threadStart\", \"pid\": 10, \"tid\": 10}\n";
  for (std::uint64_t i = 0; i < slices; ++i) {
    const std::uint64_t t0 = i * 20;
    const std::uint64_t t1 = t0 + 10;
    const unsigned cpu = static_cast<unsigned>(i % 4);
    os << "    ,{\"us\": " << t0 << ", \"kind\": \"cswitch\", \"cpu\": " << cpu << ", \"newTid\": 10, \"oldTid\": 1}\n";
    os << "    ,{\"us\": " << t1 << ", \"kind\": \"cswitch\", \"cpu\": " << cpu << ", \"newTid\": 1, \"oldTid\": 10}\n";
  }
  os << "  ]\n}\n";
  return os.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    Usage();
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "version") {
    std::cout << kProductName << " " << kVersionString << "\n";
    return 0;
  }
  if (cmd == "analyze" && argc >= 3) {
    std::string body;
    Status st = ReadAll(argv[2], body);
    if (!st) return Fail(st);
    TraceSession session;
    st = LoadFixtureJson(body, session);
    if (!st) return Fail(st);
    CpuAnalysis cpu;
    MemoryAnalysis mem;
    st = AnalyzeCpu(session, {}, cpu);
    if (!st) return Fail(st);
    st = AnalyzeMemory(session, {}, mem);
    if (!st) return Fail(st);
    EventFilter filter;
    std::vector<FilterHit> hits;
    st = FilterEvents(session, filter, hits);
    if (!st) return Fail(st);
    ReportInput report;
    report.session = &session;
    report.cpu = &cpu;
    report.memory = &mem;
    report.title = session.Info().label.empty() ? "PulseScope analysis" : session.Info().label;
    std::string jsonOut;
    std::string htmlOut;
    std::string csvOut;
    for (int i = 3; i + 1 < argc; i += 2) {
      const std::string flag = argv[i];
      if (flag == "--out-json") jsonOut = argv[i + 1];
      else if (flag == "--out-html") htmlOut = argv[i + 1];
      else if (flag == "--out-csv") csvOut = argv[i + 1];
      else {
        Usage();
        return 2;
      }
    }
    if (!jsonOut.empty()) {
      st = WriteAll(jsonOut, ExportAnalysisJson(report));
      if (!st) return Fail(st);
    }
    if (!htmlOut.empty()) {
      st = WriteAll(htmlOut, ExportReportHtml(report));
      if (!st) return Fail(st);
    }
    if (!csvOut.empty()) {
      st = WriteAll(csvOut, ExportEventsCsv(session, hits));
      if (!st) return Fail(st);
    }
    std::cout << "source=" << ToString(session.Info().source) << " synthetic=" << (session.Info().synthetic ? "yes" : "no")
              << " events=" << session.Events().size()
              << " nonIdleExecuting=" << FormatDuration(cpu.nonIdleExecutingTicks) << "\n";
    return 0;
  }
  if (cmd == "compare" && argc >= 4) {
    std::string a;
    std::string b;
    Status st = ReadAll(argv[2], a);
    if (!st) return Fail(st);
    st = ReadAll(argv[3], b);
    if (!st) return Fail(st);
    TraceSession base;
    TraceSession cand;
    st = LoadFixtureJson(a, base);
    if (!st) return Fail(st);
    st = LoadFixtureJson(b, cand);
    if (!st) return Fail(st);
    CpuAnalysis baseCpu;
    CpuAnalysis candCpu;
    MemoryAnalysis baseMem;
    MemoryAnalysis candMem;
    st = AnalyzeCpu(base, {}, baseCpu);
    if (!st) return Fail(st);
    st = AnalyzeCpu(cand, {}, candCpu);
    if (!st) return Fail(st);
    st = AnalyzeMemory(base, {}, baseMem);
    if (!st) return Fail(st);
    st = AnalyzeMemory(cand, {}, candMem);
    if (!st) return Fail(st);
    const ComparisonResult cmp = CompareAnalyses(baseCpu, baseMem, candCpu, candMem, base.Info(), cand.Info(), {});
    ReportInput report;
    report.session = &cand;
    report.cpu = &candCpu;
    report.memory = &candMem;
    report.comparison = &cmp;
    report.title = "PulseScope comparison";
    for (int i = 4; i + 1 < argc; i += 2) {
      const std::string flag = argv[i];
      if (flag == "--out-json") {
        st = WriteAll(argv[i + 1], ExportAnalysisJson(report));
        if (!st) return Fail(st);
      } else if (flag == "--out-html") {
        st = WriteAll(argv[i + 1], ExportReportHtml(report));
        if (!st) return Fail(st);
      } else {
        Usage();
        return 2;
      }
    }
    std::cout << cmp.summary << "\n";
    for (const auto& delta : cmp.deltas) {
      if (!delta.comparable || !delta.regression) continue;
      std::cout << "indicator " << delta.id << " baseline=" << delta.baseline << " candidate=" << delta.candidate << "\n";
    }
    return 0;
  }
  if (cmd == "bench") {
    std::uint64_t slices = 20000;
    for (int i = 2; i + 1 < argc; i += 2) {
      if (std::string(argv[i]) == "--events") slices = std::strtoull(argv[i + 1], nullptr, 10);
    }
    const auto body = BenchFixture(slices);
    const auto t0 = std::chrono::steady_clock::now();
    TraceSession session;
    Status st = LoadFixtureJson(body, session);
    if (!st) return Fail(st);
    const auto t1 = std::chrono::steady_clock::now();
    CpuAnalysis cpu;
    st = AnalyzeCpu(session, {}, cpu);
    if (!st) return Fail(st);
    const auto t2 = std::chrono::steady_clock::now();
    const auto loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const auto cpuMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    const double events = static_cast<double>(session.Events().size());
    std::cout << "synthetic=yes events=" << session.Events().size() << " load_ms=" << loadMs
              << " analyze_ms=" << cpuMs << " load_events_per_s=" << (events / (loadMs / 1000.0))
              << " nonIdleTicks=" << cpu.nonIdleExecutingTicks << "\n";
    return 0;
  }
  Usage();
  return 2;
}
