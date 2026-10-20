#pragma once

#include "pulsescope/status.hpp"
#include "pulsescope/timestamp.hpp"

#include "decoded_event.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntcons.h>

namespace pulsescope::win {

// Converts one EVENT_RECORD into a normalized event.
// Timestamps use the session QPC frequency and epoch. User data is treated
// as untrusted: lengths are checked before any read. Undecoded records set
// decoded=false and still carry provider/opcode so the gap is visible.
Status DecodeEventRecord(const EVENT_RECORD* record, std::uint64_t qpcFrequency, std::uint64_t epochQpc,
                         DecodedEvent& out);

}  // namespace pulsescope::win
