# Roadmap

Not started, in order:

1. Confirm the Windows CI build and fix any SDK mismatches in `src/win`.
2. Capture a real ETL on Windows 11 and check CSwitch / ReadyThread decode counts against Windows Performance Analyzer for the same file.
3. Move timeline rebuild fully off the UI thread with a cancellable generation counter.
4. Optional stack-walk collection behind an explicit setting, with a symbol resolver, or a clear "stacks not resolved" state.
5. A tested ZIP layout (EXE + VC++ redistributable note + SHA-256) once an EXE exists.
6. More providers only when the decode path can name their fields without guessing.

Out of scope until the metrics above are checked against a real trace: GPU, disk, and network charts.
