#pragma once

#include <string>
#include <utility>

namespace pulsescope {

enum class ErrorCode {
  Ok = 0,
  InvalidArgument,
  Cancelled,
  AccessDenied,
  SessionConflict,
  Unsupported,
  IoFailure,
  MalformedInput,
  LimitExceeded,
  NotFound,
  Incompatible,
  ElevationRequired,
  ProviderUnavailable,
  Internal,
};

struct Status {
  ErrorCode code = ErrorCode::Ok;
  std::string message;

  Status() = default;
  explicit Status(ErrorCode c) : code(c) {}
  Status(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}

  bool ok() const { return code == ErrorCode::Ok; }
  explicit operator bool() const { return ok(); }

  static Status Ok() { return Status{}; }
};

inline const char* ToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::Ok:
      return "ok";
    case ErrorCode::InvalidArgument:
      return "invalid-argument";
    case ErrorCode::Cancelled:
      return "cancelled";
    case ErrorCode::AccessDenied:
      return "access-denied";
    case ErrorCode::SessionConflict:
      return "session-conflict";
    case ErrorCode::Unsupported:
      return "unsupported";
    case ErrorCode::IoFailure:
      return "io-failure";
    case ErrorCode::MalformedInput:
      return "malformed-input";
    case ErrorCode::LimitExceeded:
      return "limit-exceeded";
    case ErrorCode::NotFound:
      return "not-found";
    case ErrorCode::Incompatible:
      return "incompatible";
    case ErrorCode::ElevationRequired:
      return "elevation-required";
    case ErrorCode::ProviderUnavailable:
      return "provider-unavailable";
    case ErrorCode::Internal:
      return "internal";
  }
  return "unknown";
}

}  // namespace pulsescope
