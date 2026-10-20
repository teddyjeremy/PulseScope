# Limitations

## Environment

- The Win32 application has not been compiled or launched in the environment that produced this source tree. Treat `src/win` as implemented but unverified until the Windows CI job (or a local Visual Studio build) succeeds.
- No distribution ZIP, installer, or SHA-256 of an executable is published, because no executable was produced.
- There are no screenshots of the running Windows window. The files in `branding/` are the logo, not the product UI.

## Product

- Heap profiling and allocation stacks are not implemented.
- CPU utilization ignores unidentified threads and the unmeasured leading edge. It can read low for that reason. The report says so.
- Classic CSwitch payloads do not contain a process id. Attribution depends on thread start events or TDH properties. If those are missing, the thread stays unidentified.
- Kernel decode uses TDH first and two small fallbacks (opcode 36 and opcode 50). Other kernel versions that do not match are counted as undecoded rather than parsed by guesswork.
- A trace recorded on another machine may have a different QPC frequency. ETL import prefers the header and warns when it is missing.
- Event storage is capped (`maxEvents`, default 500,000). The session is marked truncated. Analysis covers retained events only.
- The timeline aggregates sub-pixel slices into columns. Individual sub-pixel events are not hit-tested.
- Comparison by image name merges or skips ambiguous processes. It does not prove a regression was caused by a code change.
- Live CPU percent needs two samples and is easy to misread across a short interval.
- The UI rebuilds the visible timeline on the UI thread when you open the Trace page. Very large retained traces can hitch; collection itself stays off the UI thread.
- Command lines are intentionally not collected.

## Roadmap

See [roadmap.md](roadmap.md).
