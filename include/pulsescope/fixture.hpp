#pragma once

#include "pulsescope/session.hpp"

#include <string_view>

namespace pulsescope {

struct FixtureLoadOptions {
  std::uint64_t maxEvents = 500'000;
  std::size_t maxBytes = 64u * 1024u * 1024u;
};

// Loads a pulsescope.session.v1 JSON document. This is not an ETL parser.
// ETL files are read on Windows through OpenTrace. synthetic must be present.
Status LoadFixtureJson(std::string_view json, TraceSession& out, const FixtureLoadOptions& options = {},
                       const CancellationToken* cancel = nullptr);

}  // namespace pulsescope
