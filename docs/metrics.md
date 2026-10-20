# Metric definitions

Timestamps inside the engine are 100-nanosecond ticks from a session epoch. Fixture files may use integer microseconds (`us`); those are multiplied by 10. QPC values are converted with

```text
ticks = deltaQpc * 10,000,000 / frequency
```

using integer arithmetic that saturates instead of wrapping. Frequency `0` is rejected.

## CPU from context switches

Source events: `cswitch` (kernel opcode 36, TDH properties `NewThreadId`/`OldThreadId`, or the documented 24-byte fallback).

- **executingTicks** — half-open time the thread was switched onto a CPU until the next switch on that CPU, clipped to the window. This is not a sample.
- **contextSwitches** — count of those switch-ins.
- **migrations** — a later running slice of the same thread identity starts on a different CPU.
- **unknownLeadingTicks** — sum, across CPUs, of the gap from the window start to the first switch on that CPU. Not assigned to a thread. The trace does not say who was running.
- **Idle** — pid 0 only when the pid is known. Unknown pids are excluded from utilization and totaled as `unknownPidExecutingTicks`.
- **averageUtilization** — mean over buckets of `nonIdleExecutingTicks / (bucketWidth * cpuCount)`. `cpuCount` comes from the session when set, otherwise from the highest CPU index seen.
- Slices still running at the end of an auto-fit window are closed at the last event and marked `closedAtWindowEnd`. A switch-in with no later event does not invent duration (`openTails`).

## Ready and waiting

These exist only when the trace contains at least one `readyThread` event (kernel dispatcher / opcode 50).

- **waitingTicks** — switch-out until the ReadyThread event.
- **readyTicks** — ReadyThread until the next switch-in.
- If a gap has no ReadyThread before the thread runs again, the gap is **unsplitOffCpuTicks**. It is not called waiting time.
- **maxReadyQueue** — peak number of simultaneous ready slices. A contention indicator, not a cause.

If the trace has no ReadyThread events, waiting and ready are reported as unavailable. Off-CPU gaps stay unsplit.

## Sampled CPU

`sampledCpu` events and the Live page's `GetProcessTimes` delta are **percent of the machine over a sample interval**:

```text
100 * (delta kernel+user filetime / 1e7) / wallSeconds / processorCount
```

The first sample has no delta, so the percent is unknown. This number is not written into `executingTicks`. Comparison refuses to treat it as context-switch time.

## Memory

Source: `memorySample` events, or the Live page via `GetProcessMemoryInfo` (`WorkingSetSize`, `PrivateUsage`, `PagefileUsage`).

- Peaks and last values are taken from samples inside the window.
- **workingSetBytesPerSecond** — least-squares slope of working set against seconds. Needs two or more samples.
- Samples above 256 TiB are suspect and excluded.
- **virtualAllocBytes** — sum of sizes on `virtualAlloc` events actually present. Absence means the events were not collected.

A live working set is not a history of allocations. PulseScope does not show allocation stacks.

## Comparison

- Processes match on image name. Rows that share a name are summed for CPU time.
- Memory rows that are ambiguous (two processes, one image) are skipped.
- Percent change is omitted when the baseline is 0.
- A regression indicator requires a worse move of at least the configured fraction (default 5%) and a minimum absolute delta (1 ms, 1 percentage point of utilization, 1 thread of ready queue, or 64 KiB).
- The report states that the flag is descriptive.

## Window

The default window is the trace. The end bound is exclusive and one tick past the last event so that event is visited; open slices are closed at the last event, not on the extra tick.
