#pragma once

#include "pulsescope/status.hpp"

#include <cstdint>
#include <string>

namespace pulsescope {

struct Settings {
  std::string theme = "dark";  // dark | light
  std::string traceDirectory;
  std::uint32_t timelinePixels = 1200;
  std::uint64_t maxEvents = 500'000;
  double regressionFraction = 0.05;
  std::uint32_t liveIntervalMs = 500;
  bool kernelCswitch = true;
  bool kernelReady = true;
  bool kernelVirtualAlloc = false;
};

Status SettingsFromJson(std::string_view json, Settings& out);
std::string SettingsToJson(const Settings& settings);
Status ValidateSettings(const Settings& settings);

}  // namespace pulsescope
