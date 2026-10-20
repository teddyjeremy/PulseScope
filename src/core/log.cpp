#include "pulsescope/log.hpp"

#include <chrono>
#include <fstream>
#include <ctime>

namespace pulsescope {

Logger& Logger::Instance() {
  static Logger logger;
  return logger;
}

void Logger::SetPath(std::string path) {
  std::lock_guard<std::mutex> lock(mu_);
  path_ = std::move(path);
  lastError_.clear();
}

void Logger::Info(std::string_view message) { Write("info", message); }
void Logger::Error(std::string_view message) { Write("error", message); }

void Logger::Write(const char* level, std::string_view message) {
  std::lock_guard<std::mutex> lock(mu_);
  if (path_.empty()) return;
  if (message.size() > 2048) message = message.substr(0, 2048);
  std::ofstream out(path_, std::ios::app);
  if (!out) {
    lastError_ = "unable to open log file";
    return;
  }
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  char stamp[32];
  if (std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
    stamp[0] = '\0';
  }
  out << stamp << ' ' << level << ' ' << message << '\n';
}

}  // namespace pulsescope
