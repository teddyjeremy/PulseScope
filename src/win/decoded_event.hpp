#pragma once

#include "pulsescope/model.hpp"

#include <string>

namespace pulsescope::win {

// ETW callback output before strings are interned into a TraceSession.
struct DecodedEvent {
  Event event;
  std::string provider;
  std::string name;
  std::string image;
  std::string detail;
  bool decoded = false;
};

}  // namespace pulsescope::win
