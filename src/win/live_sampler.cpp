#include "live_sampler.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace pulsescope::win {
namespace {

std::uint64_t FileTimeToU64(const FILETIME& value) {
  ULARGE_INTEGER u{};
  u.LowPart = value.dwLowDateTime;
  u.HighPart = value.dwHighDateTime;
  return u.QuadPart;
}

std::string Narrow(const wchar_t* text) {
  if (text == nullptr || text[0] == L'\0') return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string out(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

std::string BaseName(const std::string& path) {
  const auto pos = path.find_last_of("\\/");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

}  // namespace

LiveSampler::LiveSampler() {
  processors_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (processors_ == 0) processors_ = 1;
}

LiveSampler::~LiveSampler() { Stop(); }

void LiveSampler::Start(std::uint32_t intervalMs) {
  Stop();
  intervalMs_ = intervalMs < 100 ? 100 : intervalMs;
  stop_.store(false);
  thread_ = std::thread([this] { ThreadMain(); });
}

void LiveSampler::Stop() {
  stop_.store(true);
  if (thread_.joinable()) thread_.join();
}

std::vector<LiveProcess> LiveSampler::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return rows_;
}

void LiveSampler::ThreadMain() {
  struct Prev {
    std::uint64_t cpu = 0;
    std::uint64_t qpc = 0;
    bool seen = false;
  };
  std::unordered_map<std::uint32_t, Prev> previous;
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);

  while (!stop_.load(std::memory_order_acquire)) {
    std::vector<LiveProcess> rows;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::uint64_t qpc = static_cast<std::uint64_t>(now.QuadPart);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W entry{};
      entry.dwSize = sizeof(entry);
      if (Process32FirstW(snap, &entry)) {
        do {
          LiveProcess row;
          row.pid = entry.th32ProcessID;
          row.image = Narrow(entry.szExeFile);
          HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid);
          if (process == nullptr) {
            row.accessLimited = true;
          } else {
            wchar_t path[1024]{};
            DWORD pathChars = 1024;
            if (QueryFullProcessImageNameW(process, 0, path, &pathChars)) {
              const std::string full = Narrow(path);
              if (!full.empty()) row.image = BaseName(full);
            }
            PROCESS_MEMORY_COUNTERS_EX counters{};
            if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
              row.workingSet = counters.WorkingSetSize;
              row.privateBytes = counters.PrivateUsage;
              row.commitBytes = counters.PagefileUsage;
            } else {
              row.accessLimited = true;
            }
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(process, &created, &exited, &kernel, &user) && freq.QuadPart > 0) {
              const std::uint64_t cpu = FileTimeToU64(kernel) + FileTimeToU64(user);
              Prev& prev = previous[row.pid];
              if (prev.seen && qpc > prev.qpc && cpu >= prev.cpu) {
                const double wall = static_cast<double>(qpc - prev.qpc) / static_cast<double>(freq.QuadPart);
                const double proc = static_cast<double>(cpu - prev.cpu) / 10000000.0;
                if (wall > 0) {
                  row.cpuPercentOfMachine = 100.0 * proc / wall / static_cast<double>(processors_);
                  if (row.cpuPercentOfMachine < 0) row.cpuPercentOfMachine = 0;
                  if (row.cpuPercentOfMachine > 100.0) row.cpuPercentOfMachine = 100.0;
                  row.cpuKnown = true;
                }
              }
              prev.cpu = cpu;
              prev.qpc = qpc;
              prev.seen = true;
            }
            CloseHandle(process);
          }
          rows.push_back(std::move(row));
        } while (Process32NextW(snap, &entry) && !stop_.load(std::memory_order_relaxed));
      }
      CloseHandle(snap);
    }
    std::sort(rows.begin(), rows.end(), [](const LiveProcess& a, const LiveProcess& b) {
      if (a.cpuKnown != b.cpuKnown) return a.cpuKnown && !b.cpuKnown;
      if (a.cpuPercentOfMachine != b.cpuPercentOfMachine) return a.cpuPercentOfMachine > b.cpuPercentOfMachine;
      return a.pid < b.pid;
    });
    {
      std::lock_guard<std::mutex> lock(mu_);
      rows_ = std::move(rows);
    }
    const auto slice = std::chrono::milliseconds(intervalMs_);
    const auto step = std::chrono::milliseconds(50);
    auto left = slice;
    while (left.count() > 0 && !stop_.load(std::memory_order_acquire)) {
      const auto nap = left < step ? left : step;
      std::this_thread::sleep_for(nap);
      left -= nap;
    }
  }
}

}  // namespace pulsescope::win
