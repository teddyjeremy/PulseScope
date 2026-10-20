#include "pulsescope/export.hpp"

#include "pulsescope/version.hpp"

#include <sstream>

namespace pulsescope {
namespace {

std::string CsvEscape(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += '"';
  return out;
}

const char* DirectionName(MetricDirection d) {
  switch (d) {
    case MetricDirection::LowerIsBetter:
      return "lower-is-better";
    case MetricDirection::HigherIsBetter:
      return "higher-is-better";
    case MetricDirection::Neutral:
      return "neutral";
  }
  return "neutral";
}

void AppendHtmlEscaped(std::string& out, std::string_view text) {
  for (unsigned char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(static_cast<char>(c));
    }
  }
}

}  // namespace

std::string ExportEventsCsv(const TraceSession& session, const std::vector<FilterHit>& hits) {
  std::ostringstream os;
  os << "sequence,timestamp_100ns,kind,pid,tid,pid_known,process_key,thread_key,cpu,provider,name,detail,synthetic\n";
  for (const auto& hit : hits) {
    if (hit.index >= session.Events().size()) continue;
    const Event& e = session.Events()[hit.index];
    os << e.sequence << ',' << e.ts.ticks << ',' << ToString(e.kind) << ',' << e.pid << ',' << e.tid << ','
       << (e.pidKnown ? "true" : "false") << ',' << e.process.id << ',' << e.thread.id << ',';
    if (e.cpu == 0xFFFF) os << "";
    else os << e.cpu;
    os << ',' << CsvEscape(session.Strings().Get(e.providerId)) << ',' << CsvEscape(session.Strings().Get(e.nameId))
       << ',' << CsvEscape(session.Strings().Get(e.detailId)) << ',' << (e.synthetic ? "true" : "false") << '\n';
  }
  return os.str();
}

std::string ExportAnalysisJson(const ReportInput& input) {
  Json root = Json::Object();
  root.Set("product", Json::String(std::string(kProductName)));
  root.Set("version", Json::String(std::string(kVersionString)));
  root.Set("title", Json::String(input.title));
  if (input.session != nullptr) {
    const auto& info = input.session->Info();
    Json s = Json::Object();
    s.Set("schema", Json::String(info.schema));
    s.Set("source", Json::String(ToString(info.source)));
    s.Set("synthetic", Json::Bool(info.synthetic));
    s.Set("label", Json::String(info.label));
    s.Set("sourceName", Json::String(info.sourceName));
    s.Set("cpuCount", Json::Uint(info.cpuCount));
    s.Set("truncated", Json::Bool(input.session->truncated()));
    s.Set("droppedEvents", Json::Uint(input.session->droppedEvents()));
    s.Set("disorderCount", Json::Uint(input.session->disorderCount()));
    s.Set("undecodedCount", Json::Uint(input.session->undecodedCount()));
    s.Set("lostEventMarkers", Json::Uint(input.session->lostEventMarkers()));
    s.Set("identityAnomalies", Json::Uint(input.session->Identities().anomalyCount()));
    s.Set("eventCount", Json::Uint(input.session->Events().size()));
    root.Set("session", std::move(s));
  }
  if (input.cpu != nullptr) {
    Json c = Json::Object();
    c.Set("definition", Json::String(input.cpu->definition));
    c.Set("cpuCount", Json::Uint(input.cpu->cpuCount));
    c.Set("readyTimeAvailable", Json::Bool(input.cpu->readyTimeAvailable));
    c.Set("nonIdleExecutingTicks", Json::Uint(input.cpu->nonIdleExecutingTicks));
    c.Set("idleExecutingTicks", Json::Uint(input.cpu->idleExecutingTicks));
    c.Set("unknownPidExecutingTicks", Json::Uint(input.cpu->unknownPidExecutingTicks));
    c.Set("unknownLeadingTicks", Json::Uint(input.cpu->unknownLeadingTicks));
    c.Set("maxReadyQueue", Json::Uint(input.cpu->maxReadyQueue));
    c.Set("cswitchCount", Json::Uint(input.cpu->cswitchCount));
    if (input.cpu->averageUtilization) c.Set("averageUtilization", Json::Number(*input.cpu->averageUtilization));
    std::vector<Json> procs;
    for (const auto& proc : input.cpu->processes) {
      Json p = Json::Object();
      p.Set("pid", Json::Uint(proc.pid));
      p.Set("image", Json::String(proc.image));
      p.Set("processKey", Json::Uint(proc.process.id));
      p.Set("executingTicks", Json::Uint(proc.executingTicks));
      p.Set("readyTicks", Json::Uint(proc.readyTicks));
      p.Set("waitingTicks", Json::Uint(proc.waitingTicks));
      p.Set("unsplitOffCpuTicks", Json::Uint(proc.unsplitOffCpuTicks));
      p.Set("contextSwitches", Json::Uint(proc.contextSwitches));
      p.Set("migrations", Json::Uint(proc.migrations));
      p.Set("idle", Json::Bool(proc.idle));
      procs.push_back(std::move(p));
    }
    c.Set("processes", Json::Array(std::move(procs)));
    root.Set("cpu", std::move(c));
  }
  if (input.memory != nullptr) {
    Json m = Json::Object();
    m.Set("definition", Json::String(input.memory->definition));
    m.Set("hasSamples", Json::Bool(input.memory->hasSamples));
    m.Set("hasAllocationEvents", Json::Bool(input.memory->hasAllocationEvents));
    std::vector<Json> procs;
    for (const auto& proc : input.memory->processes) {
      Json p = Json::Object();
      p.Set("pid", Json::Uint(proc.pid));
      p.Set("image", Json::String(proc.image));
      p.Set("samples", Json::Uint(proc.samples));
      p.Set("workingSetLast", Json::Uint(proc.workingSetLast));
      p.Set("workingSetPeak", Json::Uint(proc.workingSetPeak));
      p.Set("privatePeak", Json::Uint(proc.privatePeak));
      p.Set("commitPeak", Json::Uint(proc.commitPeak));
      p.Set("slopeValid", Json::Bool(proc.slopeValid));
      if (proc.slopeValid) p.Set("workingSetBytesPerSecond", Json::Number(proc.workingSetBytesPerSecond));
      p.Set("virtualAllocBytes", Json::Uint(proc.virtualAllocBytes));
      p.Set("virtualFreeBytes", Json::Uint(proc.virtualFreeBytes));
      p.Set("suspectSamples", Json::Uint(proc.suspectSamples));
      procs.push_back(std::move(p));
    }
    m.Set("processes", Json::Array(std::move(procs)));
    root.Set("memory", std::move(m));
  }
  if (input.comparison != nullptr) {
    Json c = Json::Object();
    c.Set("compatible", Json::Bool(input.comparison->compatible));
    c.Set("summary", Json::String(input.comparison->summary));
    std::vector<Json> caveats;
    for (const auto& caveat : input.comparison->caveats) caveats.push_back(Json::String(caveat));
    c.Set("caveats", Json::Array(std::move(caveats)));
    std::vector<Json> deltas;
    for (const auto& delta : input.comparison->deltas) {
      Json d = Json::Object();
      d.Set("id", Json::String(delta.id));
      d.Set("description", Json::String(delta.description));
      d.Set("unit", Json::String(delta.unit));
      d.Set("comparable", Json::Bool(delta.comparable));
      d.Set("baseline", Json::Number(delta.baseline));
      d.Set("candidate", Json::Number(delta.candidate));
      d.Set("absoluteDelta", Json::Number(delta.absoluteDelta));
      if (delta.percentDelta) d.Set("percentDelta", Json::Number(*delta.percentDelta));
      d.Set("direction", Json::String(DirectionName(delta.direction)));
      d.Set("regression", Json::Bool(delta.regression));
      d.Set("reason", Json::String(delta.reason));
      deltas.push_back(std::move(d));
    }
    c.Set("deltas", Json::Array(std::move(deltas)));
    root.Set("comparison", std::move(c));
  }
  root.Set("limitations", Json::String(
      "Synthetic fixtures are not ETW recordings. Sampled CPU and precise execution time are different metrics. "
      "Memory samples are not heap allocation traces. A regression flag is not a cause."));
  return StringifyJson(root, 2);
}

std::string ExportReportHtml(const ReportInput& input) {
  std::string html;
  html.reserve(8192);
  html += "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>";
  AppendHtmlEscaped(html, input.title);
  html += "</title><style>"
          "body{font-family:Segoe UI,sans-serif;background:#0e141b;color:#e7eef6;margin:32px;}"
          "h1{font-weight:600;letter-spacing:-.03em} table{border-collapse:collapse;width:100%;margin:12px 0 28px}"
          "td,th{border-bottom:1px solid #243140;text-align:left;padding:8px;vertical-align:top;font-size:14px}"
          "th{color:#8b9bb0;font-weight:600} .note{color:#8b9bb0} .bad{color:#e7b549} code{color:#7ee0c0}"
          "</style></head><body><h1>";
  AppendHtmlEscaped(html, input.title);
  html += "</h1><p class=\"note\">PulseScope ";
  html += kVersionString;
  html += " — ";
  html += kTagline;
  html += "</p>";
  if (input.session != nullptr) {
    const auto& info = input.session->Info();
    html += "<h2>Source</h2><table><tr><th>Field</th><th>Value</th></tr>";
    auto row = [&](const char* k, const std::string& v) {
      html += "<tr><td>";
      html += k;
      html += "</td><td>";
      AppendHtmlEscaped(html, v);
      html += "</td></tr>";
    };
    row("Data source", ToString(info.source));
    row("Synthetic", info.synthetic ? "yes" : "no");
    row("Label", info.label);
    row("File", info.sourceName);
    row("CPU count", std::to_string(info.cpuCount));
    row("Events retained", std::to_string(input.session->Events().size()));
    row("Truncated", input.session->truncated() ? "yes" : "no");
    row("Dropped after cap", std::to_string(input.session->droppedEvents()));
    row("Undecoded", std::to_string(input.session->undecodedCount()));
    row("Lost-event markers", std::to_string(input.session->lostEventMarkers()));
    row("Collection", info.collectionNote.empty() ? "(not recorded)" : info.collectionNote);
    row("Clock", info.clockNote.empty() ? "session-relative 100 ns ticks" : info.clockNote);
    html += "</table>";
  }
  if (input.cpu != nullptr) {
    html += "<h2>CPU</h2><p class=\"note\">";
    AppendHtmlEscaped(html, input.cpu->definition);
    html += "</p><table><tr><th>Image</th><th>PID</th><th>Executing</th><th>Ready</th><th>Waiting</th><th>Unsplit off-CPU</th><th>Migrations</th></tr>";
    for (const auto& proc : input.cpu->processes) {
      html += "<tr><td>";
      AppendHtmlEscaped(html, proc.image.empty() ? "(unknown image)" : proc.image);
      html += "</td><td>";
      html += std::to_string(proc.pid);
      html += "</td><td>";
      html += FormatDuration(proc.executingTicks);
      html += "</td><td>";
      html += input.cpu->readyTimeAvailable ? FormatDuration(proc.readyTicks) : "n/a";
      html += "</td><td>";
      html += input.cpu->readyTimeAvailable ? FormatDuration(proc.waitingTicks) : "n/a";
      html += "</td><td>";
      html += FormatDuration(proc.unsplitOffCpuTicks);
      html += "</td><td>";
      html += std::to_string(proc.migrations);
      html += "</td></tr>";
    }
    html += "</table>";
  }
  if (input.memory != nullptr) {
    html += "<h2>Memory</h2><p class=\"note\">";
    AppendHtmlEscaped(html, input.memory->definition);
    html += "</p>";
    if (!input.memory->hasAllocationEvents) {
      html += "<p class=\"note\">Allocation events were not in this session. No heap or stack data is implied.</p>";
    }
    html += "<table><tr><th>Image</th><th>PID</th><th>Working set last</th><th>Working set peak</th><th>Private peak</th><th>Slope</th></tr>";
    for (const auto& proc : input.memory->processes) {
      html += "<tr><td>";
      AppendHtmlEscaped(html, proc.image.empty() ? "(unknown image)" : proc.image);
      html += "</td><td>" + std::to_string(proc.pid) + "</td><td>" + std::to_string(proc.workingSetLast) +
              "</td><td>" + std::to_string(proc.workingSetPeak) + "</td><td>" + std::to_string(proc.privatePeak) +
              "</td><td>";
      html += proc.slopeValid ? std::to_string(proc.workingSetBytesPerSecond) + " B/s" : "n/a";
      html += "</td></tr>";
    }
    html += "</table>";
  }
  if (input.comparison != nullptr) {
    html += "<h2>Comparison</h2><p>";
    AppendHtmlEscaped(html, input.comparison->summary);
    html += "</p><ul>";
    for (const auto& caveat : input.comparison->caveats) {
      html += "<li>";
      AppendHtmlEscaped(html, caveat);
      html += "</li>";
    }
    html += "</ul><table><tr><th>Metric</th><th>Baseline</th><th>Candidate</th><th>Delta</th><th>Flag</th></tr>";
    for (const auto& delta : input.comparison->deltas) {
      html += "<tr><td>";
      AppendHtmlEscaped(html, delta.id);
      html += "<div class=\"note\">";
      AppendHtmlEscaped(html, delta.comparable ? delta.description : delta.reason);
      html += "</div></td>";
      if (!delta.comparable) {
        html += "<td colspan=\"4\">not comparable</td></tr>";
        continue;
      }
      html += "<td>" + std::to_string(delta.baseline) + "</td><td>" + std::to_string(delta.candidate) + "</td><td>";
      html += std::to_string(delta.absoluteDelta);
      if (delta.percentDelta) html += " (" + std::to_string(*delta.percentDelta) + "%)";
      html += "</td><td>";
      html += delta.regression ? "<span class=\"bad\">regression indicator</span>" : "—";
      html += "</td></tr>";
    }
    html += "</table>";
  }
  if (input.bookmarks != nullptr && !input.bookmarks->empty()) {
    html += "<h2>Bookmarks</h2><ul>";
    for (const auto& bookmark : *input.bookmarks) {
      html += "<li>";
      AppendHtmlEscaped(html, bookmark.title);
      html += " [";
      html += FormatTimestamp(bookmark.start);
      html += " – ";
      html += FormatDuration(bookmark.end.ticks - bookmark.start.ticks);
      html += "] ";
      AppendHtmlEscaped(html, bookmark.note);
      html += "</li>";
    }
    html += "</ul>";
  }
  html += "<h2>Errors and gaps</h2><p class=\"note\">Processing errors, lost buffers, truncation, and missing "
          "metadata are listed in the source table. Metrics are omitted instead of being filled with guesses.</p>";
  html += "</body></html>\n";
  return html;
}

}  // namespace pulsescope
