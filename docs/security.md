# Security notes

- ETL and JSON inputs are untrusted. The JSON parser caps bytes, depth, and element count. Strings are length-capped. Event payloads longer than 64 KiB are not copied. PID maps are hash maps, not arrays sized by the raw pid.
- Memory counters above 256 TiB are suspect and dropped from the series.
- The process does not start elevated. Kernel tracing fails closed with an explanation.
- Logs and reports do not include process command lines. A command line can contain secrets.
- HTML export escapes `&`, `<`, `>`, and quotes. Reports do not embed scripts.
- Settings and logs live under the user's roaming profile. The trace directory is whatever the settings file says; PulseScope does not write ETLs into the install directory by default.
- There is no network client in the application.
