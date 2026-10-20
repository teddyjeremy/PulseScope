# Architecture

PulseScope splits into a portable engine and a Windows shell. The engine never calls Win32. The shell never invents a metric the engine does not define.

## Components

| Piece | Responsibility |
| --- | --- |
| `TraceSession` | Owns events, the string table, and the event cap. `Finalize` sorts by time and rebuilds identities. |
| `IdentityTracker` | Process and thread lifetimes. A pid or tid is only unique inside `[start, end)`. |
| `ReconstructScheduling` | Turns context switches and ReadyThread events into running, ready, waiting, and unsplit off-CPU slices. |
| `AnalyzeCpu` / `AnalyzeMemory` | Metrics. Definitions are stored on the result and copied into reports. |
| `CompareAnalyses` | Shared metrics only. Skips pairs that are not the same measurement. |
| `BuildTimeline` | Pixel columns so the UI does not create one control per event. |
| `EtwSession` (`src/win`) | `StartTrace` / `EnableTraceEx2` / `OpenTrace` / `ProcessTrace` / `ControlTrace`. |
| `LiveSampler` | Toolhelp + `GetProcessTimes` + `GetProcessMemoryInfo`. No elevation. |
| Direct2D shell | Navigation, tables, timeline, settings, export. |

## Data flow

1. A producer (fixture loader, ETL import, or the ETW callback) emits a normalized `Event`.
2. `Ingest` appends it until `maxEvents`. Further events increment `droppedEvents` and set `truncated`. They are not silently analyzed as if they were kept.
3. `Finalize` sorts `(timestamp, sequence)` and walks in order so process/thread start events are visible to later switches.
4. Scheduling reconstruction clips slices to the analysis window. Time before the first switch on a CPU is `unknownLeadingTicks`, not attributed to a thread.
5. Reports copy the session source, the synthetic flag, truncated/lost/undecoded counts, and the metric definition text.

## Threading (Windows shell)

| Thread | Work |
| --- | --- |
| UI | Message pump, Direct2D paint, file dialogs. Does not call `ProcessTrace`. |
| ETW consumer | `ProcessTrace`. The callback decodes and pushes onto a bounded queue (default 8192). If the queue is full the event is counted as dropped. |
| Decode delivery | Pops the queue and interns strings into `TraceSession`. |
| Live sampler | Toolhelp loop. Sleeps in short steps so `Stop()` does not wait a full interval. |
| Import worker | ETL or JSON load off the UI thread. Joined before the next import and before exit. |

`EtwSession::Stop` calls `ControlTrace(EVENT_TRACE_CONTROL_STOP)`, `CloseTrace` to unblock `ProcessTrace`, then joins both threads. The destructor calls `Stop`.

## Ownership

- `TraceSession` owns the event vector and string table. The UI keeps it in `shared_ptr` so an import worker can finish after the user navigates.
- ETW handles are `TRACEHANDLE` values owned by `EtwSession`, not bare globals.
- Direct2D and DirectWrite objects sit in `ComPtr` (Release on reset).
- Process handles from the live sampler are closed on every iteration.
- Logs go to `%AppData%\PulseScope\pulsescope.log` and are truncated per line. Command lines and raw event payloads are not logged.

## Why identities are not pids

Windows reuses pids and tids. `IdentityTracker::OnProcessStart` closes any still-open lifetime for that pid at the new start time and allocates a new `ProcessKey`. Lookups use the half-open interval. Comparison matches images, not pids, and refuses a memory comparison when one image maps to more than one process.
