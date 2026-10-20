#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace pulsescope {

// File logger for diagnostics. Do not pass command lines, ETL payloads, or
// secrets. Failures to open the file are remembered and do not throw.
class Logger {
 public:
  static Logger& Instance();
  void SetPath(std::string path);
  void Info(std::string_view message);
  void Error(std::string_view message);
  const std::string& LastError() const { return lastError_; }

 private:
  void Write(const char* level, std::string_view message);
  std::mutex mu_;
  std::string path_;
  std::string lastError_;
};

}  // namespace pulsescope
