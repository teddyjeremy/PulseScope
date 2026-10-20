# Contributing

- Keep metric changes in `src/core` with a test that would fail if the definition silently changed.
- Do not label sampled CPU as execution time, and do not fill missing ETW fields with plausible values.
- Windows-only code stays under `src/win` and behind `if(WIN32)` in CMake.
- New third-party code needs an entry in `NOTICE` (purpose, license, how it is obtained). The default build has none.
- Run `make test` before sending a change to the engine. Do not report the Win32 app as tested unless it was built and run on Windows.
- Avoid committing ETL captures that contain other users' command lines or paths you would not publish.
