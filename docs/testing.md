# Testing

`make test` builds `build/pulsescope-tests` and runs it. The harness in `tests/harness.hpp` is the test framework: each `PS_TEST` registers a function, failures throw, and the process exits non-zero. It fills the same role as GoogleTest without a download during the build.

Covered:

- QPC and FILETIME conversion, including a zero frequency and saturation
- Ready versus waiting versus unsplit off-CPU
- CPU migration
- PID reuse across two images that share pid 10 and tid 3
- Memory least-squares slope, suspect samples, virtual alloc bytes
- Comparison regression flag, and refusal to compare precise CPU with sampled CPU
- Filter, sort, CSV, JSON reparse, HTML escaping (no script tags)
- Timeline column aggregation
- Malformed JSON, event cap, cancellation
- Bookmarks and settings round-trip
- JSON integers past 2^53

Fixtures are C++ string literals in `tests/engine_tests.cpp` plus `tests/fixtures/ready-wait.json`. They are synthetic.

Windows-only behavior (real `StartTrace`, ETL bytes, Direct2D paint) is not marked passed. `.github/workflows/ci.yml` is the place that run belongs.
