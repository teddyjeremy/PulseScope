# Build

## Authoring environment (done)

g++ 12, C++20, no CMake required:

```bash
make test
make cli
```

CMake also builds `pulsescope-cli` and `pulsescope-tests` on non-Windows hosts. The `PulseScope` GUI target is compiled only when `WIN32` is set.

## Windows prerequisites

- Windows 10 version 1809 x64 or later, or Windows 11 x64
- Visual Studio 2022 with the Desktop development with C++ workload
- Windows SDK that ships `evntrace.h`, `tdh.h`, `d2d1.h`, `dwrite.h` (10.0.17763 or newer)
- CMake 3.20+

No vcpkg packages are required. GoogleTest is not fetched. Tests use `tests/harness.hpp`.

## Windows commands

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\PulseScope.exe
build\Release\pulsescope-cli.exe version
```

The GUI links `d2d1`, `dwrite`, `ole32`, `shell32`, `advapi32`, `tdh`, and `psapi`. It uses the system MSVC runtime (`/MD` is the CMake default for the Visual Studio generator). Install the matching [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) on machines without Visual Studio.

`src/win/pulsescope.rc` embeds `branding/pulsescope.ico` and `src/win/app.manifest`. The manifest sets `asInvoker` and Per-Monitor V2 DPI awareness.

## Workloads

```bat
cl /nologo /EHsc /std:c++20 /O2 tools\workloads\cpu_saturate.cpp
cpu_saturate.exe 4 10
```

The same sources compile with g++. They do not print fake profiler numbers.

## Not produced here

The authoring environment cannot run `cl` or the Windows SDK. No `PulseScope.exe` was linked, and no installer was built. Do not treat a green portable test run as a Windows GUI run.
