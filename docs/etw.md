# ETW collection

Implemented in `src/win/etw_session.cpp` and `src/win/etw_decode.cpp`. Not executed in the authoring environment.

## Session

`StartTraceW` creates a private session named `PulseScope` (not `NT Kernel Logger`) with:

- `EVENT_TRACE_SYSTEM_LOGGER_MODE`
- `WNODE_FLAG_TRACED_GUID`
- `ClientContext = 1` (QPC)
- optional `EVENT_TRACE_REAL_TIME_MODE`
- optional `EVENT_TRACE_FILE_MODE_SEQUENTIAL` when an ETL path is set

Enable flags, from `evntrace.h`:

| Config | Flag |
| --- | --- |
| Processes and threads | `EVENT_TRACE_FLAG_PROCESS`, `EVENT_TRACE_FLAG_THREAD` |
| Context switches | `EVENT_TRACE_FLAG_CSWITCH` |
| Ready thread | `EVENT_TRACE_FLAG_DISPATCHER` |
| Virtual alloc (off by default) | `EVENT_TRACE_FLAG_VIRTUAL_ALLOC` |

`EnableTraceEx2` also requests the manifest provider **Microsoft-Windows-Kernel-Process** `{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}` at information level. If that enable fails, the session still runs and the note says process names may be missing.

If real-time mode plus a file is rejected with `ERROR_INVALID_PARAMETER`, PulseScope retries file-only and says so. It does not pretend both succeeded.

## Decode

`PROCESS_TRACE_MODE_EVENT_RECORD` delivers `EVENT_RECORD`.

1. TDH property names: `ProcessId`, `TThreadId` / `ThreadId`, `NewThreadId`, `OldThreadId`, `ImageFileName`, `RegionSize`.
2. User-data fallback for opcode 36 only when `UserDataLength >= 24`, using the classic CSwitch layout (new/old tid, priorities, wait reason, wait mode, state, ideal processor, wait time). Process ids are **not** in that struct; they come from thread lifetime events.
3. Opcode 50 ReadyThread fallback reads the leading 4-byte thread id only when the buffer is long enough.
4. Anything else is `EventKind::Unknown`, counted as undecoded, and still listed. It is not guessed into a process.

Records with `UserDataLength > 64 KiB` are not copied.

Processor index prefers `EVENT_HEADER_EXT_TYPE_PROCESSOR_INDEX` when the extended item is present and large enough.

## Stop and loss

`ControlTraceW(..., EVENT_TRACE_CONTROL_STOP)` reads `EventsLost` when the call succeeds. `CloseTrace` unblocks `ProcessTrace`. Both threads are joined.

## ETL import

`ImportEtlFile` checks the file size before `OpenTrace` (default UI cap 8 GiB) and rejects an empty file. Timer frequency is taken from `LogfileHeader.PerfFreq` when it is non-zero. Otherwise the local QPC frequency is used and the note says a trace from another machine may be wrong. Cancellation closes the trace handle.

## What this is not

Heap snapshots, call stacks, and file I/O analysis are not implemented. Virtual alloc capture is opt-in and still has no stacks.
