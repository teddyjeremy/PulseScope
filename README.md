# PulseScope

**Understand the work. Find the bottleneck.**

PulseScope is a native Windows performance profiler. The portable analysis engine turns normalized scheduling and memory events into CPU, thread, memory, comparison, and report output. On Windows, a Win32 / Direct2D shell collects Event Tracing for Windows (ETW) traces, samples live processes without elevation, and opens ETL files through `OpenTrace`.

This repository does not invent measurements. Synthetic fixtures are labeled `synthetic`. Sampled CPU percent and context-switch execution time are different metrics and are never mixed. A comparison flag is not a cause.

## What is verified

| Area | State |
| --- | --- |
| Portable analysis engine (timestamps, identity, CPU, memory, compare, timeline, export) | Built and tested with g++ 12, C++20. `make test` — 11/11 passed in the authoring environment. |
| `pulsescope-cli` fixture analysis and benchmark | Ran on the same host. See [docs/benchmarks.md](docs/benchmarks.md). |
| Win32 UI, ETW session, ETL import, live sampler | Source is in `src/win`. It was **not compiled here**. There is no MSVC toolchain or Windows SDK in the authoring environment, and there is no release EXE in this repository. |
| GitHub Actions | `.github/workflows/ci.yml` builds the portable engine on Ubuntu and the Win32 app on `windows-2022` (Visual Studio 2022). `windows-latest` is Visual Studio 2026 and does not provide the `Visual Studio 17 2022` generator. Windows results are not claimed until a run is green. |

Minimum OS the Win32 shell is written for: **Windows 10 version 1809 (build 17763) x64** and Windows 11 x64.

## Layout

- `include/pulsescope`, `src/core` — portable engine
- `src/cli` — fixture analysis, comparison, benchmark
- `src/win` — ETW, TDH decode, live sampler, Direct2D shell
- `tests` — deterministic tests, including synthetic fixtures
- `tools/workloads` — CPU saturation, lock contention, blocking, and memory growth programs
- `branding` — original SVG, PNG, and ICO
- `docs` — architecture, metrics, ETW, build, limitations

## Build the portable engine

```bash
make test
./build/pulsescope-cli version
./build/pulsescope-cli analyze tests/fixtures/ready-wait.json --out-html /tmp/report.html
```

Windows build steps are in [docs/build.md](docs/build.md).

## Honesty

Completed means the behavior is implemented and, for the portable engine, tested. The Windows shell is implemented in source and still needs a Windows build before it can be called verified. Known limits are listed in [docs/limitations.md](docs/limitations.md). There is no checked-in executable and no claim of a GitHub Release binary.
