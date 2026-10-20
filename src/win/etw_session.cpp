#include "etw_session.hpp"

#include "etw_decode.hpp"

#include "pulsescope/log.hpp"
#include "pulsescope/timestamp.hpp"

#include <chrono>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>

namespace pulsescope::win {
namespace {

// Microsoft-Windows-Kernel-Process. Used as a manifest provider in addition
// to the kernel flags on the system logger. Documented in the Windows SDK.
constexpr GUID kKernelProcessProvider = {
    0x22FB2CD6, 0x0E7B, 0x422B, {0xA0, 0xC7, 0x2F, 0xAD, 0x1F, 0xD0, 0xE7, 0x16}};

std::string Narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string out(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

std::string FormatWin32(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD n = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::string message = "win32 " + std::to_string(code);
  if (n != 0 && buffer != nullptr) {
    message += ": " + Narrow(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) LocalFree(buffer);
  return message;
}

Status FromStartError(DWORD code) {
  if (code == ERROR_SUCCESS) return Status::Ok();
  if (code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD) {
    return Status{ErrorCode::ElevationRequired,
                  "Kernel tracing was denied (" + FormatWin32(code) +
                      "). PulseScope can still sample per-process CPU and memory without elevation. "
                      "Restart this process as Administrator to collect context switches."};
  }
  if (code == ERROR_ALREADY_EXISTS) {
    return Status{ErrorCode::SessionConflict,
                  "An ETW session with this name is already running. Stop it from the Live page, or reboot if it was "
                  "left behind by a crash. " +
                      FormatWin32(code)};
  }
  if (code == ERROR_DISK_FULL) return Status{ErrorCode::IoFailure, "The disk is full. " + FormatWin32(code)};
  if (code == ERROR_INVALID_PARAMETER) {
    return Status{ErrorCode::Unsupported, "The kernel logger rejected this flag combination. " + FormatWin32(code)};
  }
  return Status{ErrorCode::Internal, FormatWin32(code)};
}

VOID WINAPI RecordThunk(PEVENT_RECORD record) {
  if (record == nullptr || record->UserContext == nullptr) return;
  static_cast<EtwSession*>(record->UserContext)->OnRecord(record);
}

ULONG KernelFlags(const EtwConfig& config) {
  ULONG flags = 0;
  if (config.processesAndThreads) flags |= EVENT_TRACE_FLAG_PROCESS | EVENT_TRACE_FLAG_THREAD;
  if (config.contextSwitches) flags |= EVENT_TRACE_FLAG_CSWITCH;
  if (config.readyThread) flags |= EVENT_TRACE_FLAG_DISPATCHER;
  if (config.virtualAlloc) flags |= EVENT_TRACE_FLAG_VIRTUAL_ALLOC;
  return flags;
}

}  // namespace

EtwSession::EtwSession() = default;

EtwSession::~EtwSession() { Stop(); }

void EtwSession::OnRecord(const void* eventRecord) {
  const auto* record = static_cast<const EVENT_RECORD*>(eventRecord);
  if (record->UserDataLength > 64 * 1024) {
    undecoded_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  DecodedEvent decoded;
  DecodeEventRecord(record, qpcFrequency_, epochQpc_, decoded);
  if (!decoded.decoded) undecoded_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(queueMu_);
    if (queue_.size() >= config_.maxQueuedEvents) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    queue_.push_back(std::move(decoded));
  }
  queueCv_.notify_one();
}

Status EtwSession::Start(const EtwConfig& config, Callback callback) {
  if (running_.load()) return Status{ErrorCode::SessionConflict, "This process already has a running session."};
  if (config.sessionName.empty() || config.sessionName.size() > 200) {
    return Status{ErrorCode::InvalidArgument, "Session name is empty or too long."};
  }
  Stop();
  config_ = config;
  callback_ = std::move(callback);
  note_.clear();
  dropped_ = 0;
  delivered_ = 0;
  undecoded_ = 0;
  eventsLost_ = 0;
  stopDecode_ = false;

  LARGE_INTEGER freq{};
  LARGE_INTEGER now{};
  if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0 || !QueryPerformanceCounter(&now)) {
    return Status{ErrorCode::Internal, "QueryPerformanceCounter failed."};
  }
  qpcFrequency_ = static_cast<std::uint64_t>(freq.QuadPart);
  epochQpc_ = static_cast<std::uint64_t>(now.QuadPart);
  startQpc_ = epochQpc_;

  const std::size_t nameChars = config_.sessionName.size() + 1;
  const std::size_t fileChars = config_.etlPath.empty() ? 0 : config_.etlPath.size() + 1;
  const std::size_t bytes = sizeof(EVENT_TRACE_PROPERTIES) + (nameChars + fileChars) * sizeof(wchar_t);
  std::vector<std::uint8_t> storage(bytes, 0);
  auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data());
  props->Wnode.BufferSize = static_cast<ULONG>(bytes);
  props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
  props->Wnode.ClientContext = 1;
  props->BufferSize = 256;
  props->MinimumBuffers = 16;
  props->MaximumBuffers = 64;
  props->LogFileMode = EVENT_TRACE_SYSTEM_LOGGER_MODE;
  if (config_.realTime) props->LogFileMode |= EVENT_TRACE_REAL_TIME_MODE;
  if (!config_.etlPath.empty()) props->LogFileMode |= EVENT_TRACE_FILE_MODE_SEQUENTIAL;
  props->EnableFlags = KernelFlags(config_);
  props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
  auto* loggerName = reinterpret_cast<wchar_t*>(storage.data() + props->LoggerNameOffset);
  wcscpy_s(loggerName, nameChars, config_.sessionName.c_str());
  if (!config_.etlPath.empty()) {
    props->LogFileNameOffset = props->LoggerNameOffset + static_cast<ULONG>(nameChars * sizeof(wchar_t));
    auto* fileName = reinterpret_cast<wchar_t*>(storage.data() + props->LogFileNameOffset);
    wcscpy_s(fileName, fileChars, config_.etlPath.c_str());
  }

  TRACEHANDLE session = 0;
  ULONG status = StartTraceW(&session, config_.sessionName.c_str(), props);
  if (status == ERROR_INVALID_PARAMETER && config_.realTime && !config_.etlPath.empty()) {
    props->LogFileMode = EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    status = StartTraceW(&session, config_.sessionName.c_str(), props);
    if (status == ERROR_SUCCESS) {
      note_ = "Real-time delivery was rejected together with the ETL file. The session is file-only; "
              "open the ETL after you stop the trace.";
    }
  }
  if (status != ERROR_SUCCESS) {
    Logger::Instance().Error("StartTrace failed: " + std::to_string(status));
    return FromStartError(status);
  }
  sessionHandle_ = session;

  ULONG enable = EnableTraceEx2(session, &kKernelProcessProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFFULL, 0, 0, nullptr);
  if (enable != ERROR_SUCCESS) {
    note_ += (note_.empty() ? "" : " ");
    note_ += "Kernel-Process manifest provider was not enabled (" + FormatWin32(enable) +
             "). Process names may be missing if classic kernel events cannot be decoded.";
  }

  running_.store(true, std::memory_order_release);
  decoder_ = std::thread([this] { DecodeMain(); });
  if ((props->LogFileMode & EVENT_TRACE_REAL_TIME_MODE) != 0) {
    consumer_ = std::thread([this] { ConsumerMain(); });
  }
  Logger::Instance().Info("ETW session started");
  return Status::Ok();
}

void EtwSession::ConsumerMain() {
  EVENT_TRACE_LOGFILEW log{};
  log.LoggerName = const_cast<LPWSTR>(config_.sessionName.c_str());
  log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
  log.EventRecordCallback = RecordThunk;
  log.Context = this;
  TRACEHANDLE trace = OpenTraceW(&log);
  if (trace == INVALID_PROCESSTRACE_HANDLE) {
    note_ += " OpenTrace failed.";
    Logger::Instance().Error("OpenTrace failed");
    return;
  }
  traceHandle_ = trace;
  if (log.LogfileHeader.PerfFreq.QuadPart > 0) {
    qpcFrequency_ = static_cast<std::uint64_t>(log.LogfileHeader.PerfFreq.QuadPart);
  }
  const ULONG status = ProcessTrace(&trace, 1, nullptr, nullptr);
  if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
    Logger::Instance().Error("ProcessTrace ended: " + std::to_string(status));
  }
}

void EtwSession::DecodeMain() {
  while (true) {
    DecodedEvent event;
    {
      std::unique_lock<std::mutex> lock(queueMu_);
      queueCv_.wait(lock, [&] { return stopDecode_ || !queue_.empty(); });
      if (queue_.empty() && stopDecode_) return;
      event = std::move(queue_.front());
      queue_.pop_front();
    }
    if (callback_) callback_(std::move(event));
    delivered_.fetch_add(1, std::memory_order_relaxed);
  }
}

Status EtwSession::ControlStop() {
  if (sessionHandle_ == 0 && config_.sessionName.empty()) return Status::Ok();
  const std::size_t bytes = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
  std::vector<std::uint8_t> storage(bytes, 0);
  auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data());
  props->Wnode.BufferSize = static_cast<ULONG>(bytes);
  props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
  props->LogFileNameOffset = 0;
  const ULONG status = ControlTraceW(sessionHandle_, config_.sessionName.c_str(), props, EVENT_TRACE_CONTROL_STOP);
  if (status == ERROR_SUCCESS) {
    eventsLost_.store(props->EventsLost, std::memory_order_relaxed);
  }
  sessionHandle_ = 0;
  if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA) return Status::Ok();
  // WNODE_FLAG and ERROR_WMI_INSTANCE_NOT_FOUND (4201) mean it is already gone.
  if (status == 4201) return Status::Ok();
  return Status{ErrorCode::Internal, "ControlTrace stop failed: " + FormatWin32(status)};
}

Status EtwSession::Stop() {
  const bool wasRunning = running_.exchange(false);
  if (!wasRunning && sessionHandle_ == 0 && !consumer_.joinable() && !decoder_.joinable()) return Status::Ok();
  Status stopStatus = ControlStop();
  if (traceHandle_ != 0) {
    CloseTrace(traceHandle_);
    traceHandle_ = 0;
  }
  if (consumer_.joinable()) consumer_.join();
  {
    std::lock_guard<std::mutex> lock(queueMu_);
    stopDecode_ = true;
  }
  queueCv_.notify_all();
  if (decoder_.joinable()) decoder_.join();
  callback_ = nullptr;
  if (wasRunning) Logger::Instance().Info("ETW session stopped");
  return stopStatus;
}

double EtwSession::elapsedSeconds() const {
  if (!running_.load(std::memory_order_acquire) || qpcFrequency_ == 0) return 0;
  LARGE_INTEGER now{};
  if (!QueryPerformanceCounter(&now)) return 0;
  const auto delta = static_cast<std::uint64_t>(now.QuadPart) - startQpc_;
  std::uint64_t ticks = 0;
  if (!QpcDeltaToTicks(delta, qpcFrequency_, ticks)) return 0;
  return TicksToSeconds(ticks);
}

Status ImportEtlFile(const std::wstring& path, const EtwSession::Callback& callback, const CancellationToken* cancel,
                     std::uint64_t maxFileBytes, std::string& note) {
  note.clear();
  if (path.empty()) return Status{ErrorCode::InvalidArgument, "ETL path is empty."};
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
    return Status{ErrorCode::IoFailure, "Could not read the ETL file."};
  }
  const std::uint64_t size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  if (size == 0) return Status{ErrorCode::MalformedInput, "The ETL file is empty."};
  if (size > maxFileBytes) {
    return Status{ErrorCode::LimitExceeded, "The ETL file exceeds the configured size limit."};
  }

  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  struct State {
    const EtwSession::Callback* callback;
    std::uint64_t frequency;
    std::uint64_t epoch;
    bool haveEpoch;
    std::uint64_t undecoded;
    const CancellationToken* cancel;
    TRACEHANDLE handle;
  } state{&callback, static_cast<std::uint64_t>(freq.QuadPart), 0, false, 0, cancel, 0};

  EVENT_TRACE_LOGFILEW log{};
  log.LogFileName = const_cast<LPWSTR>(path.c_str());
  log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
  log.Context = &state;
  log.EventRecordCallback = [](PEVENT_RECORD record) {
    auto* state = static_cast<State*>(record->UserContext);
    if (state->cancel != nullptr && state->cancel->IsCancelled()) {
      CloseTrace(state->handle);
      return;
    }
    if (record->UserDataLength > 64 * 1024) {
      ++state->undecoded;
      return;
    }
    const std::uint64_t qpc = static_cast<std::uint64_t>(record->EventHeader.TimeStamp.QuadPart);
    if (!state->haveEpoch) {
      state->epoch = qpc;
      state->haveEpoch = true;
    }
    DecodedEvent decoded;
    DecodeEventRecord(record, state->frequency, state->epoch, decoded);
    if (!decoded.decoded) ++state->undecoded;
    if (*state->callback) (*state->callback)(std::move(decoded));
  };

  const TRACEHANDLE handle = OpenTraceW(&log);
  if (handle == INVALID_PROCESSTRACE_HANDLE) {
    return Status{ErrorCode::MalformedInput, "OpenTrace could not open this file. It may not be an ETL trace."};
  }
  state.handle = handle;
  if (log.LogfileHeader.PerfFreq.QuadPart > 0) {
    state.frequency = static_cast<std::uint64_t>(log.LogfileHeader.PerfFreq.QuadPart);
    note = "ETL timer frequency was taken from the trace header.";
  } else {
    note = "ETL timer frequency was not in the header; the local QPC frequency was used and may be wrong for a trace "
           "recorded on another machine.";
  }
  const ULONG status = ProcessTrace(const_cast<PTRACEHANDLE>(&handle), 1, nullptr, nullptr);
  CloseTrace(handle);
  if (cancel != nullptr && cancel->IsCancelled()) return Status{ErrorCode::Cancelled, "ETL import cancelled."};
  if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
    return Status{ErrorCode::MalformedInput, "ProcessTrace failed: " + FormatWin32(status)};
  }
  if (state.undecoded > 0) {
    note += " Undecoded events: " + std::to_string(state.undecoded) + ".";
  }
  return Status::Ok();
}

}  // namespace pulsescope::win
