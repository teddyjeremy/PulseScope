# Release

Version **0.4.0** is a source release of the engine and the unverified Windows shell.

## Not in this release

- `PulseScope.exe`
- A ZIP of runtime files
- An installer
- A SHA-256 of a binary
- A GitHub Release asset

Those wait until `cmake --build` succeeds on Windows and the resulting EXE starts.

## When a binary exists

1. Build Release x64 as in [build.md](build.md).
2. Copy `PulseScope.exe` and `pulsescope-cli.exe` into a folder. Do not copy the Windows SDK.
3. Note the VC++ Redistributable requirement.
4. `certutil -hashfile PulseScope.exe SHA256` and publish the hash next to the zip.
5. Write the release note from a real Windows run: test count, a short ETW start/stop, and one ETL import. Do not paste the Linux benchmark table as a Windows result.

`build/` and `*.exe` are gitignored on purpose.
