# Benchmarks

These numbers are from the portable engine only (`pulsescope-cli bench`). They are not UI frame times and not ETL parse times.

## Method

`bench` builds a synthetic fixture in memory: one process, one thread, and N pairs of context switches (10 microseconds on, 10 microseconds off) round-robin across 4 CPUs. It then loads that JSON and runs `AnalyzeCpu`. The fixture is synthetic and the output says so.

Host: Linux container used to author the repository, g++ 12.2, `-O2`, x86_64. The machine is a shared build sandbox, not a dedicated profiler workstation. Treat the figures as an order-of-magnitude baseline, not a product SLA.

## Results

| Events | Load | Analyze CPU | Load throughput |
| --- | --- | --- | --- |
| 40,002 | 81 ms | 8.8 ms | about 492,000 events/s |
| 100,002 | 486 ms | 25 ms | about 206,000 events/s |

The 100k run's lower throughput is dominated by building and parsing a large JSON document, not by the scheduler. Non-idle executing ticks matched `events/2 * 10 microseconds` (5,000,000 ticks of 100 ns for 50,000 on-slices), which is the expected synthetic result.

## Targets until a Windows baseline exists

- Portable analyze of 100,000 context switches: under 250 ms on a desktop-class CPU after the events are already in memory.
- UI stays on the message pump during `ProcessTrace` (already structured that way; not measured).
- Cancellation of a fixture load: the next 4096-step checkpoint, not a process kill.

Re-measure on Windows with an ETL of known size before publishing a number in a release note. Do not copy the table above into a release as if it were a Windows result.
