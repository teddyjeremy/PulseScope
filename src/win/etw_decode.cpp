#include "etw_decode.hpp"

#include "pulsescope/timestamp.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <tdh.h>
#include <evntrace.h>
#include <objbase.h>

namespace pulsescope::win {
namespace {

#pragma pack(push, 1)
struct CSwitchRaw {
  std::uint32_t newThreadId;
  std::uint32_t oldThreadId;
  std::int8_t newPriority;
  std::int8_t oldPriority;
  std::uint8_t previousCState;
  std::int8_t spare;
  std::int8_t oldWaitReason;
  std::int8_t oldWaitMode;
  std::int8_t oldThreadState;
  std::int8_t oldIdealProcessor;
  std::uint32_t newThreadWaitTime;
  std::uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(CSwitchRaw) == 24, "CSwitch layout drifted");

std::wstring PropertyName(const TRACE_EVENT_INFO* info, const EVENT_PROPERTY_INFO& prop) {
  if (prop.NameOffset == 0) return {};
  return reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(info) + prop.NameOffset);
}

bool ReadProperty(const EVENT_RECORD* record, const wchar_t* name, std::vector<BYTE>& buffer) {
  PROPERTY_DATA_DESCRIPTOR desc{};
  desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
  desc.ArrayIndex = ULONG_MAX;
  ULONG size = 0;
  ULONG status = TdhGetPropertySize(const_cast<PEVENT_RECORD>(record), 0, nullptr, 1, &desc, &size);
  if (status != ERROR_SUCCESS) {
    desc.ArrayIndex = 0;
    status = TdhGetPropertySize(const_cast<PEVENT_RECORD>(record), 0, nullptr, 1, &desc, &size);
  }
  if (status != ERROR_SUCCESS || size == 0 || size > 64 * 1024) return false;
  buffer.assign(size, 0);
  status = TdhGetProperty(const_cast<PEVENT_RECORD>(record), 0, nullptr, 1, &desc, size, buffer.data());
  return status == ERROR_SUCCESS;
}

bool ReadU32(const EVENT_RECORD* record, const wchar_t* name, std::uint32_t& out) {
  std::vector<BYTE> buffer;
  if (!ReadProperty(record, name, buffer) || buffer.size() > sizeof(std::uint64_t)) return false;
  std::uint64_t value = 0;
  std::memcpy(&value, buffer.data(), buffer.size());
  if (value > 0xFFFFFFFFull) return false;
  out = static_cast<std::uint32_t>(value);
  return true;
}

bool ReadU64(const EVENT_RECORD* record, const wchar_t* name, std::uint64_t& out) {
  std::vector<BYTE> buffer;
  if (!ReadProperty(record, name, buffer) || buffer.size() > sizeof(std::uint64_t)) return false;
  out = 0;
  std::memcpy(&out, buffer.data(), buffer.size());
  return true;
}

std::string NarrowProperty(const std::vector<BYTE>& buffer) {
  if (buffer.size() >= 2 && buffer[1] == 0) {
    const wchar_t* wide = reinterpret_cast<const wchar_t*>(buffer.data());
    const int chars = static_cast<int>(buffer.size() / sizeof(wchar_t));
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, chars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string text(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, chars, text.data(), bytes, nullptr, nullptr);
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
  }
  std::string text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  while (!text.empty() && text.back() == '\0') text.pop_back();
  return text;
}

std::uint16_t CpuIndex(const EVENT_RECORD* record) {
  std::uint16_t cpu = record->BufferContext.ProcessorNumber;
  if (record->ExtendedData == nullptr) return cpu;
  for (USHORT i = 0; i < record->ExtendedDataCount; ++i) {
    const EVENT_HEADER_EXTENDED_DATA_ITEM& item = record->ExtendedData[i];
    if (item.ExtType == EVENT_HEADER_EXT_TYPE_PROCESSOR_INDEX && item.DataSize >= sizeof(std::uint16_t) &&
        item.DataPtr != 0) {
      std::uint16_t index = 0;
      std::memcpy(&index, reinterpret_cast<const void*>(item.DataPtr), sizeof(index));
      return index;
    }
  }
  return cpu;
}

std::string GuidString(const GUID& guid) {
  wchar_t text[64]{};
  if (StringFromGUID2(guid, text, 64) == 0) return {};
  int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string out(static_cast<std::size_t>(bytes > 0 ? bytes - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

bool ApplyCSwitchFallback(const EVENT_RECORD* record, DecodedEvent& out) {
  if (record->EventHeader.EventDescriptor.Opcode != 36) return false;
  if (record->UserData == nullptr || record->UserDataLength < sizeof(CSwitchRaw)) return false;
  CSwitchRaw raw{};
  std::memcpy(&raw, record->UserData, sizeof(raw));
  out.event.kind = EventKind::ContextSwitch;
  out.event.newTid = raw.newThreadId;
  out.event.oldTid = raw.oldThreadId;
  out.event.tid = raw.newThreadId;
  out.event.newPriority = raw.newPriority;
  out.event.oldPriority = raw.oldPriority;
  out.event.oldWaitReason = raw.oldWaitReason;
  out.event.oldThreadState = raw.oldThreadState;
  out.event.cpu = CpuIndex(record);
  out.name = "CSwitch";
  out.decoded = true;
  out.detail = "fallback layout, 24 bytes, opcode 36";
  return true;
}

bool ApplyReadyFallback(const EVENT_RECORD* record, DecodedEvent& out) {
  if (record->EventHeader.EventDescriptor.Opcode != 50) return false;
  if (record->UserData == nullptr || record->UserDataLength < sizeof(std::uint32_t)) return false;
  std::uint32_t tid = 0;
  std::memcpy(&tid, record->UserData, sizeof(tid));
  out.event.kind = EventKind::ReadyThread;
  out.event.readyTid = tid;
  out.event.tid = tid;
  out.name = "ReadyThread";
  out.decoded = true;
  out.detail = "fallback layout, leading thread id, opcode 50";
  return true;
}

}  // namespace

Status DecodeEventRecord(const EVENT_RECORD* record, std::uint64_t qpcFrequency, std::uint64_t epochQpc, DecodedEvent& out) {
  out = {};
  if (record == nullptr) return Status{ErrorCode::InvalidArgument, "null event record"};
  const std::uint64_t qpc = static_cast<std::uint64_t>(record->EventHeader.TimeStamp.QuadPart);
  if (!QpcToTimestamp(qpc, epochQpc, qpcFrequency, out.event.ts)) {
    out.event.ts = {};
  }
  out.event.opcode = record->EventHeader.EventDescriptor.Opcode;
  out.event.cpu = CpuIndex(record);
  out.provider = GuidString(record->EventHeader.ProviderId);

  std::uint32_t pid = 0;
  std::uint32_t tid = 0;
  if (ReadU32(record, L"ProcessId", pid) || ReadU32(record, L"ParentId", pid)) {
    // ProcessId is preferred; ParentId alone must not become the pid.
  }
  if (ReadU32(record, L"ProcessId", pid)) {
    out.event.pid = pid;
    out.event.pidKnown = true;
  }
  bool sawTid = false;
  if (ReadU32(record, L"TThreadId", tid) || ReadU32(record, L"ThreadId", tid)) {
    out.event.tid = tid;
    sawTid = true;
  }
  std::uint32_t newTid = 0;
  std::uint32_t oldTid = 0;
  if (ReadU32(record, L"NewThreadId", newTid) && ReadU32(record, L"OldThreadId", oldTid)) {
    out.event.kind = EventKind::ContextSwitch;
    out.event.newTid = newTid;
    out.event.oldTid = oldTid;
    out.event.tid = newTid;
    out.event.cpu = CpuIndex(record);
    std::int8_t reason = 0;
    std::uint32_t tmp = 0;
    if (ReadU32(record, L"OldThreadWaitReason", tmp)) reason = static_cast<std::int8_t>(tmp);
    out.event.oldWaitReason = reason;
    out.name = "CSwitch";
    out.decoded = true;
  }
  std::uint32_t readyTid = 0;
  if (!out.decoded && (ReadU32(record, L"ReadyThreadId", readyTid) ||
                       (record->EventHeader.EventDescriptor.Opcode == 50 && ReadU32(record, L"TThreadId", readyTid)))) {
    out.event.kind = EventKind::ReadyThread;
    out.event.readyTid = readyTid;
    out.event.tid = readyTid;
    out.name = "ReadyThread";
    out.decoded = true;
  }

  std::vector<BYTE> image;
  if (ReadProperty(record, L"ImageFileName", image) || ReadProperty(record, L"ImageName", image)) {
    out.image = NarrowProperty(image);
  }

  const UCHAR opcode = record->EventHeader.EventDescriptor.Opcode;
  // Thread events carry a thread id and the same start/end opcodes as process
  // events. Classify threads first so a process id on a thread event does not
  // look like a new process.
  if (!out.decoded && sawTid && (opcode == EVENT_TRACE_TYPE_START || opcode == EVENT_TRACE_TYPE_DC_START)) {
    out.event.kind = EventKind::ThreadStart;
    out.name = "ThreadStart";
    out.decoded = true;
  }
  if (!out.decoded && sawTid && (opcode == EVENT_TRACE_TYPE_END || opcode == EVENT_TRACE_TYPE_DC_END)) {
    out.event.kind = EventKind::ThreadEnd;
    out.name = "ThreadEnd";
    out.decoded = true;
  }
  if (!out.decoded && out.event.pidKnown && !sawTid &&
      (opcode == EVENT_TRACE_TYPE_START || opcode == EVENT_TRACE_TYPE_DC_START)) {
    out.event.kind = EventKind::ProcessStart;
    std::uint32_t parent = 0;
    if (ReadU32(record, L"ParentId", parent) || ReadU32(record, L"ParentProcessId", parent)) out.event.parentPid = parent;
    out.name = "ProcessStart";
    out.decoded = true;
  }
  if (!out.decoded && out.event.pidKnown && !sawTid &&
      (opcode == EVENT_TRACE_TYPE_END || opcode == EVENT_TRACE_TYPE_DC_END)) {
    out.event.kind = EventKind::ProcessEnd;
    out.name = "ProcessEnd";
    out.decoded = true;
  }

  std::uint64_t bytes = 0;
  if (!out.decoded && (ReadU64(record, L"RegionSize", bytes) || ReadU64(record, L"Size", bytes))) {
    const bool isFree = opcode == EVENT_TRACE_TYPE_END;
    out.event.kind = isFree ? EventKind::VirtualFree : EventKind::VirtualAlloc;
    out.event.byteCount = bytes;
    out.name = isFree ? "VirtualFree" : "VirtualAlloc";
    out.decoded = true;
    out.detail = "size property only; this is not a heap stack trace";
  }

  if (!out.decoded) {
    if (!ApplyCSwitchFallback(record, out) && !ApplyReadyFallback(record, out)) {
      out.event.kind = EventKind::Unknown;
      out.name = "opcode " + std::to_string(opcode);
      out.decoded = false;
      out.detail = "TDH properties and known layouts did not match";
    }
  }
  return Status::Ok();
}

}  // namespace pulsescope::win
