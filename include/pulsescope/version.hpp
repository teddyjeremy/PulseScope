#pragma once

#include <cstdint>
#include <string_view>

namespace pulsescope {

// Source release. The portable analysis engine is covered by the in-repo
// tests. The Win32/ETW shell is part of this version but is only built on
// Windows; see docs/limitations.md.
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 4;
inline constexpr int kVersionPatch = 0;
inline constexpr std::string_view kVersionString = "0.4.0";
inline constexpr std::string_view kProductName = "PulseScope";
inline constexpr std::string_view kTagline = "Understand the work. Find the bottleneck.";

// Minimum OS the Win32 shell is written against (Windows 10 version 1809).
inline constexpr std::uint32_t kMinWindowsBuild = 17763;
inline constexpr std::string_view kMinWindowsName = "Windows 10 version 1809 (build 17763) x64";

}  // namespace pulsescope
