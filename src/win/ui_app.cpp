#include "ui_app.hpp"

#include "com_ptr.hpp"
#include "etw_session.hpp"
#include "live_sampler.hpp"

#include "pulsescope/bookmarks.hpp"
#include "pulsescope/compare.hpp"
#include "pulsescope/cpu.hpp"
#include "pulsescope/export.hpp"
#include "pulsescope/filter.hpp"
#include "pulsescope/fixture.hpp"
#include "pulsescope/log.hpp"
#include "pulsescope/memory.hpp"
#include "pulsescope/settings.hpp"
#include "pulsescope/timeline.hpp"
#include "pulsescope/version.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace pulsescope::win {
namespace {

enum Page { Overview = 0, Live, Trace, Cpu, Memory, Compare, Reports, SettingsPage, PageCount };

struct Hot {
  D2D1_RECT_F rect;
  int id;
};

std::wstring Widen(std::string_view text) {
  if (text.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring AppDataDir() {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) || raw == nullptr) {
    return L".";
  }
  std::wstring path = raw;
  CoTaskMemFree(raw);
  path += L"\\PulseScope";
  CreateDirectoryW(path.c_str(), nullptr);
  return path;
}

struct Palette {
  D2D1_COLOR_F bg, panel, line, text, muted, accent, accent2, warn, hot;
};

Palette DarkPalette() {
  Palette p;
  p.bg = D2D1::ColorF(0x0E141B);
  p.panel = D2D1::ColorF(0x161E28);
  p.line = D2D1::ColorF(0x243140);
  p.text = D2D1::ColorF(0xE7EEF6);
  p.muted = D2D1::ColorF(0x8B9BB0);
  p.accent = D2D1::ColorF(0x3DDC97);
  p.accent2 = D2D1::ColorF(0x7AA2FF);
  p.warn = D2D1::ColorF(0xE7B549);
  p.hot = D2D1::ColorF(0x243447);
  return p;
}

Palette LightPalette() {
  Palette p;
  p.bg = D2D1::ColorF(0xF4F7FB);
  p.panel = D2D1::ColorF(0xFFFFFF);
  p.line = D2D1::ColorF(0xD5DEE8);
  p.text = D2D1::ColorF(0x14202B);
  p.muted = D2D1::ColorF(0x5C6B7A);
  p.accent = D2D1::ColorF(0x0E8F6E);
  p.accent2 = D2D1::ColorF(0x2C5BD6);
  p.warn = D2D1::ColorF(0x9A6B00);
  p.hot = D2D1::ColorF(0xE7EEF6);
  return p;
}

class App {
 public:
  explicit App(HINSTANCE instance) : instance_(instance) {}
  int Loop(int show);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void EnsureTargets();
  void Paint();
  void DrawText(std::wstring_view text, D2D1_RECT_F rect, IDWriteTextFormat* font, const D2D1_COLOR_F& color);
  void Fill(D2D1_RECT_F rect, const D2D1_COLOR_F& color);
  void Button(D2D1_RECT_F rect, const wchar_t* label, int id);
  void DrawSidebar(float width, float height);
  void DrawStatus(float width, float height);
  void DrawOverview(D2D1_RECT_F area);
  void DrawLive(D2D1_RECT_F area);
  void DrawTrace(D2D1_RECT_F area);
  void DrawCpu(D2D1_RECT_F area);
  void DrawMemory(D2D1_RECT_F area);
  void DrawCompare(D2D1_RECT_F area);
  void DrawReports(D2D1_RECT_F area);
  void DrawSettings(D2D1_RECT_F area);
  void Activate(int id);
  void OpenTrace(bool comparison);
  void ToggleEtw();
  void ExportHtml();
  void RefreshAnalysis();
  void LoadSettings();
  void SaveSettings();
  void SetError(std::string text);
  std::wstring StatusLine() const;

  HINSTANCE instance_{};
  HWND hwnd_ = nullptr;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dw_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<IDWriteTextFormat> font_;
  ComPtr<IDWriteTextFormat> bold_;
  ComPtr<IDWriteTextFormat> mono_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  Palette palette_ = DarkPalette();
  pulsescope::Settings settings_{};
  int page_ = Overview;
  int scroll_ = 0;
  bool search_ = false;
  std::wstring query_;
  std::wstring status_ = L"Ready";
  std::wstring error_;
  std::vector<Hot> hot_;
  LiveSampler live_;
  EtwSession etw_;
  std::mutex dataMu_;
  std::shared_ptr<TraceSession> session_;
  std::shared_ptr<TraceSession> compare_;
  CpuAnalysis cpu_{};
  MemoryAnalysis memory_{};
  CpuAnalysis compareCpu_{};
  MemoryAnalysis compareMemory_{};
  bool analysisReady_ = false;
  BookmarkStore bookmarks_;
  std::thread worker_;
  float timelineZoom_ = 1.f;
  float timelinePan_ = 0.f;
};

int App::Loop(int show) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  LoadSettings();
  const std::wstring logPath = AppDataDir() + L"\\pulsescope.log";
  Logger::Instance().SetPath(Narrow(logPath));
  Logger::Instance().Info("PulseScope starting");
  live_.Start(settings_.liveIntervalMs);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &App::WndProc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(instance_, MAKEINTRESOURCE(1));
  wc.lpszClassName = L"PulseScopeWindow";
  RegisterClassExW(&wc);

  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"PulseScope", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                          CW_USEDEFAULT, 1280, 800, nullptr, nullptr, instance_, this);
  if (hwnd_ == nullptr) return 1;
  ShowWindow(hwnd_, show);
  SetTimer(hwnd_, 1, 400, nullptr);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  KillTimer(hwnd_, 1);
  if (worker_.joinable()) worker_.join();
  etw_.Stop();
  live_.Stop();
  SaveSettings();
  CoUninitialize();
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = static_cast<App*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  }
  if (self == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);
  return self->Handle(hwnd, msg, wp, lp);
}

LRESULT App::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE:
      if (rt_) {
        rt_->Resize(D2D1::SizeU(static_cast<UINT32>(LOWORD(lp)), static_cast<UINT32>(HIWORD(lp))));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_TIMER:
      if (page_ == Live || etw_.running()) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(wp);
      if (page_ == Trace && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (delta > 0) timelineZoom_ = std::min(64.f, timelineZoom_ * 1.25f);
        else timelineZoom_ = std::max(1.f, timelineZoom_ / 1.25f);
      } else {
        scroll_ = std::max(0, scroll_ - delta / 40);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONUP: {
      const float x = static_cast<float>(GET_X_LPARAM(lp));
      const float y = static_cast<float>(GET_Y_LPARAM(lp));
      for (const auto& hot : hot_) {
        if (x >= hot.rect.left && x <= hot.rect.right && y >= hot.rect.top && y <= hot.rect.bottom) {
          Activate(hot.id);
          break;
        }
      }
      return 0;
    }
    case WM_KEYDOWN: {
      const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (ctrl && wp >= '1' && wp <= '8') {
        page_ = static_cast<int>(wp - '1');
        scroll_ = 0;
      } else if (ctrl && wp == 'O') {
        OpenTrace((GetKeyState(VK_SHIFT) & 0x8000) != 0);
      } else if (ctrl && wp == 'E') {
        ExportHtml();
      } else if (ctrl && wp == 'F') {
        search_ = true;
      } else if (wp == VK_ESCAPE) {
        search_ = false;
        error_.clear();
      } else if (wp == VK_OEM_PLUS || wp == VK_ADD) {
        timelineZoom_ = std::min(64.f, timelineZoom_ * 1.25f);
      } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
        timelineZoom_ = std::max(1.f, timelineZoom_ / 1.25f);
      } else if (search_ && wp == VK_BACK && !query_.empty()) {
        query_.pop_back();
      } else if (wp == VK_LEFT) {
        timelinePan_ = std::max(0.f, timelinePan_ - 0.05f);
      } else if (wp == VK_RIGHT) {
        timelinePan_ = std::min(1.f, timelinePan_ + 0.05f);
      } else if (wp == VK_F5) {
        live_.Stop();
        live_.Start(settings_.liveIntervalMs);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_CHAR:
      if (search_ && wp >= 32 && wp < 127 && query_.size() < 80) {
        query_.push_back(static_cast<wchar_t>(wp));
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_PAINT:
      Paint();
      ValidateRect(hwnd, nullptr);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

void App::EnsureTargets() {
  if (!d2d_) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put());
  }
  if (!dw_) {
    IUnknown* unknown = nullptr;
    if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &unknown)) &&
        unknown != nullptr) {
      unknown->QueryInterface(__uuidof(IDWriteFactory), dw_.put_void());
      unknown->Release();
    }
  }
  if (!rt_ && d2d_ && hwnd_) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, rc.right)), static_cast<UINT32>(std::max(1L, rc.bottom)));
    d2d_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd_, size), rt_.put());
  }
  if (dw_ && !font_) {
    dw_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                          DWRITE_FONT_STRETCH_NORMAL, 15.f, L"en-us", font_.put());
    dw_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                          DWRITE_FONT_STRETCH_NORMAL, 20.f, L"en-us", bold_.put());
    dw_->CreateTextFormat(L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                          DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us", mono_.put());
    if (!mono_) {
      dw_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us", mono_.put());
    }
  }
  if (rt_ && !brush_) rt_->CreateSolidColorBrush(palette_.text, brush_.put());
}

void App::Fill(D2D1_RECT_F rect, const D2D1_COLOR_F& color) {
  brush_->SetColor(color);
  rt_->FillRectangle(rect, brush_.get());
}

void App::DrawText(std::wstring_view text, D2D1_RECT_F rect, IDWriteTextFormat* font, const D2D1_COLOR_F& color) {
  if (font == nullptr) return;
  brush_->SetColor(color);
  rt_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), font, rect, brush_.get());
}

void App::Button(D2D1_RECT_F rect, const wchar_t* label, int id) {
  Fill(rect, palette_.hot);
  DrawText(label, D2D1::RectF(rect.left + 10, rect.top + 6, rect.right - 8, rect.bottom - 4), font_.get(), palette_.text);
  hot_.push_back(Hot{rect, id});
}

void App::Paint() {
  EnsureTargets();
  if (!rt_ || !font_) {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    EndPaint(hwnd_, &ps);
    return;
  }
  palette_ = settings_.theme == "light" ? LightPalette() : DarkPalette();
  hot_.clear();
  rt_->BeginDraw();
  rt_->Clear(palette_.bg);
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const float width = static_cast<float>(rc.right);
  const float height = static_cast<float>(rc.bottom);
  DrawSidebar(width, height);
  const D2D1_RECT_F content = D2D1::RectF(232, 16, width - 16, height - 40);
  switch (page_) {
    case Overview:
      DrawOverview(content);
      break;
    case Live:
      DrawLive(content);
      break;
    case Trace:
      DrawTrace(content);
      break;
    case Cpu:
      DrawCpu(content);
      break;
    case Memory:
      DrawMemory(content);
      break;
    case Compare:
      DrawCompare(content);
      break;
    case Reports:
      DrawReports(content);
      break;
    default:
      DrawSettings(content);
      break;
  }
  DrawStatus(width, height);
  if (rt_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
    rt_.reset();
    brush_.reset();
  }
}

void App::DrawSidebar(float, float height) {
  Fill(D2D1::RectF(0, 0, 216, height), palette_.panel);
  DrawText(L"PulseScope", D2D1::RectF(18, 18, 200, 48), bold_.get(), palette_.text);
  DrawText(L"Understand the work.", D2D1::RectF(18, 48, 200, 70), font_.get(), palette_.muted);
  static const wchar_t* kNames[] = {L"Overview", L"Live Session", L"Trace Analysis", L"CPU and Threads",
                                    L"Memory",    L"Comparisons", L"Reports",        L"Settings"};
  for (int i = 0; i < PageCount; ++i) {
    const float y = 96.f + static_cast<float>(i) * 40.f;
    D2D1_RECT_F rect = D2D1::RectF(12, y, 204, y + 34);
    if (page_ == i) Fill(rect, palette_.hot);
    DrawText(kNames[i], D2D1::RectF(24, y + 6, 200, y + 30), font_.get(), page_ == i ? palette_.accent : palette_.text);
    hot_.push_back(Hot{rect, 100 + i});
  }
}

void App::DrawStatus(float width, float height) {
  Fill(D2D1::RectF(0, height - 28, width, height), palette_.panel);
  std::wstring line = StatusLine();
  if (!error_.empty()) line = error_;
  DrawText(line, D2D1::RectF(16, height - 24, width - 16, height - 4), font_.get(), error_.empty() ? palette_.muted : palette_.warn);
}

std::wstring App::StatusLine() const {
  std::wstring line = L"v";
  line += Widen(std::string(kVersionString));
  line += L"   ";
  if (etw_.running()) {
    line += L"ETW running ";
    line += std::to_wstring(static_cast<int>(etw_.elapsedSeconds()));
    line += L"s";
  } else {
    line += L"ETW idle";
  }
  line += L"   search ";
  line += search_ ? query_ + L"_" : L"(Ctrl+F)";
  line += L"   Ctrl+1..8 pages  Ctrl+O open  Ctrl+Shift+O compare  Ctrl+E export";
  return line;
}

void App::DrawOverview(D2D1_RECT_F area) {
  DrawText(L"Find the bottleneck with evidence you can name.", D2D1::RectF(area.left, area.top, area.right, area.top + 32),
           bold_.get(), palette_.text);
  const wchar_t* body =
      L"Live process CPU and memory are sampled with GetProcessTimes and GetProcessMemoryInfo. "
      L"They are not context-switch time. Kernel ETW (context switches, ready threads, process lifetime) "
      L"needs an elevated process and is kept separate so the rest of PulseScope works without it.\n\n"
      L"Imported JSON fixtures are labeled synthetic or imported. ETL files are read with OpenTrace. "
      L"Nothing in a report is invented to fill a gap.";
  DrawText(body, D2D1::RectF(area.left, area.top + 48, area.right, area.top + 200), font_.get(), palette_.muted);
  Button(D2D1::RectF(area.left, area.top + 220, area.left + 220, area.top + 256), L"Open a trace", 5);
  Button(D2D1::RectF(area.left + 232, area.top + 220, area.left + 470, area.top + 256), L"Start kernel trace", 2);
  Button(D2D1::RectF(area.left + 482, area.top + 220, area.left + 760, area.top + 256), L"Restart as administrator", 4);
}

void App::DrawLive(D2D1_RECT_F area) {
  DrawText(L"Live session", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  std::wstring note = etw_.running() ? L"Kernel session is recording." : L"Kernel session is stopped.";
  note += L" ";
  note += Widen(etw_.note());
  DrawText(note, D2D1::RectF(area.left, area.top + 34, area.right, area.top + 78), font_.get(), palette_.muted);
  Button(D2D1::RectF(area.left, area.top + 84, area.left + 180, area.top + 118), etw_.running() ? L"Stop ETW" : L"Start ETW", 2);
  Button(D2D1::RectF(area.left + 192, area.top + 84, area.left + 430, area.top + 118), L"Restart elevated", 4);

  const auto rows = live_.Snapshot();
  float y = area.top + 140 - static_cast<float>(scroll_ * 22);
  DrawText(L"Sampled CPU% is a delta of GetProcessTimes divided by wall time and processor count. "
           L"The first pass has no percent. Access-limited processes are shown without counters.",
           D2D1::RectF(area.left, area.top + 126, area.right, area.top + 168), font_.get(), palette_.muted);
  y = area.top + 176;
  DrawText(L"Process", D2D1::RectF(area.left, y, area.left + 280, y + 22), font_.get(), palette_.muted);
  DrawText(L"PID", D2D1::RectF(area.left + 290, y, area.left + 360, y + 22), font_.get(), palette_.muted);
  DrawText(L"CPU%", D2D1::RectF(area.left + 370, y, area.left + 450, y + 22), font_.get(), palette_.muted);
  DrawText(L"Working set", D2D1::RectF(area.left + 460, y, area.left + 600, y + 22), font_.get(), palette_.muted);
  y += 26;
  const std::string q = Narrow(query_);
  int shown = 0;
  for (const auto& row : rows) {
    if (!q.empty() && row.image.find(q) == std::string::npos && std::to_string(row.pid).find(q) == std::string::npos) continue;
    if (shown++ < scroll_) continue;
    if (y > area.bottom - 8) break;
    std::wstring cpu = row.cpuKnown ? std::to_wstring(row.cpuPercentOfMachine).substr(0, 4) : L"—";
    std::wstring ws = row.accessLimited && row.workingSet == 0 ? L"limited" : std::to_wstring(row.workingSet / 1024) + L" KB";
    DrawText(Widen(row.image.empty() ? "(unnamed)" : row.image), D2D1::RectF(area.left, y, area.left + 280, y + 20),
             mono_.get(), palette_.text);
    DrawText(std::to_wstring(row.pid), D2D1::RectF(area.left + 290, y, area.left + 360, y + 20), mono_.get(), palette_.text);
    DrawText(cpu, D2D1::RectF(area.left + 370, y, area.left + 450, y + 20), mono_.get(), palette_.accent);
    DrawText(ws, D2D1::RectF(area.left + 460, y, area.left + 640, y + 20), mono_.get(), palette_.text);
    y += 22;
  }
  if (rows.empty()) {
    DrawText(L"Waiting for the first sample.", D2D1::RectF(area.left, y, area.right, y + 24), font_.get(), palette_.muted);
  }
}

void App::DrawTrace(D2D1_RECT_F area) {
  DrawText(L"Trace timeline", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  Button(D2D1::RectF(area.right - 180, area.top, area.right, area.top + 32), L"Open trace", 5);
  std::shared_ptr<TraceSession> session;
  {
    std::lock_guard<std::mutex> lock(dataMu_);
    session = session_;
  }
  if (!session) {
    DrawText(L"No trace loaded. Open a JSON fixture or an ETL file. Fixtures are not presented as ETW recordings.",
             D2D1::RectF(area.left, area.top + 48, area.right, area.top + 100), font_.get(), palette_.muted);
    return;
  }
  std::wstring head = Widen(std::string(ToString(session->Info().source))) + L"  " + Widen(session->Info().label);
  if (session->Info().synthetic) head += L"  · synthetic";
  DrawText(head, D2D1::RectF(area.left, area.top + 40, area.right - 190, area.top + 64), font_.get(), palette_.muted);

  if (!session->finalized()) {
    DrawText(L"This session is still open, so scheduling analysis has not been finalized. Stop the kernel trace to index it. Events retained: " +
                 std::to_wstring(session->Events().size()),
             D2D1::RectF(area.left, area.top + 70, area.right, area.top + 120), font_.get(), palette_.muted);
    return;
  }
  TimelineOptions opt;
  opt.pixels = static_cast<std::uint32_t>(std::max(32.f, area.right - area.left - 180.f));
  opt.maxLanes = 24;
  opt.text = Narrow(query_);
  const Timestamp start = session->TraceStart();
  const Timestamp end = session->TraceEnd();
  if (end.ticks > start.ticks) {
    const double span = static_cast<double>(end.ticks - start.ticks);
    const double view = span / static_cast<double>(timelineZoom_);
    const double begin = static_cast<double>(start.ticks) + static_cast<double>(timelinePan_) * (span - view);
    opt.window.start.ticks = static_cast<std::uint64_t>(std::max(0.0, begin));
    opt.window.end.ticks = opt.window.start.ticks + static_cast<std::uint64_t>(view);
  }
  TimelineModel model;
  const Status st = BuildTimeline(*session, opt, model);
  if (!st) {
    DrawText(Widen(st.message), D2D1::RectF(area.left, area.top + 70, area.right, area.top + 110), font_.get(), palette_.warn);
    return;
  }
  float y = area.top + 78;
  for (const auto& lane : model.lanes) {
    if (y > area.bottom - 20) break;
    DrawText(Widen(lane.label), D2D1::RectF(area.left, y, area.left + 170, y + 18), mono_.get(), palette_.muted);
    const float x0 = area.left + 176;
    const float x1 = area.right;
    const float width = std::max(1.f, x1 - x0);
    for (std::uint32_t i = 0; i < lane.columns.size(); ++i) {
      const auto& col = lane.columns[i];
      D2D1_COLOR_F color = palette_.line;
      if (col.dominant == ColumnKind::Running) color = palette_.accent;
      else if (col.dominant == ColumnKind::Ready) color = palette_.warn;
      else if (col.dominant == ColumnKind::Waiting) color = palette_.accent2;
      else if (col.dominant == ColumnKind::OffCpuUnsplit || col.dominant == ColumnKind::Mixed) color = palette_.muted;
      else continue;
      const float a = x0 + width * static_cast<float>(i) / static_cast<float>(lane.columns.size());
      const float b = x0 + width * static_cast<float>(i + 1) / static_cast<float>(lane.columns.size());
      Fill(D2D1::RectF(a, y + 3, std::max(a + 1.f, b), y + 14), color);
    }
    y += 22;
  }
  if (model.lanes.empty()) {
    DrawText(L"This window has no scheduling slices. A fixture needs context switches, or the filter hid every lane.",
             D2D1::RectF(area.left, y, area.right, y + 48), font_.get(), palette_.muted);
  }
  DrawText(L"Teal running · amber ready · blue waiting · gray unsplit off-CPU. Wheel+Ctrl zooms. Left/Right pans.",
           D2D1::RectF(area.left, area.bottom - 24, area.right, area.bottom), font_.get(), palette_.muted);
}

void App::DrawCpu(D2D1_RECT_F area) {
  RefreshAnalysis();
  DrawText(L"CPU and threads", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  if (!analysisReady_) {
    DrawText(L"Load a trace to see execution time. Sampled CPU% stays on the Live page and is not mixed in here.",
             D2D1::RectF(area.left, area.top + 40, area.right, area.top + 90), font_.get(), palette_.muted);
    return;
  }
  std::wstring summary = L"Non-idle executing " + Widen(FormatDuration(cpu_.nonIdleExecutingTicks));
  if (cpu_.averageUtilization) {
    summary += L"   mean utilization " + std::to_wstring(*cpu_.averageUtilization * 100.0).substr(0, 4) + L"%";
  }
  summary += cpu_.readyTimeAvailable ? L"   ready time is available" : L"   ready/wait not split (no ReadyThread events)";
  DrawText(summary, D2D1::RectF(area.left, area.top + 36, area.right, area.top + 60), font_.get(), palette_.muted);
  float y = area.top + 78;
  const std::string q = Narrow(query_);
  int skipped = 0;
  for (const auto& proc : cpu_.processes) {
    if (!q.empty() && proc.image.find(q) == std::string::npos) continue;
    if (skipped++ < scroll_) continue;
    if (y > area.bottom - 8) break;
    std::wstring line = Widen(proc.image.empty() ? "(unknown image)" : proc.image);
    line += L"   pid " + std::to_wstring(proc.pid);
    line += L"   run " + Widen(FormatDuration(proc.executingTicks));
    line += L"   switches " + std::to_wstring(proc.contextSwitches);
    line += L"   migrations " + std::to_wstring(proc.migrations);
    if (cpu_.readyTimeAvailable) {
      line += L"   ready " + Widen(FormatDuration(proc.readyTicks));
      line += L"   wait " + Widen(FormatDuration(proc.waitingTicks));
    }
    DrawText(line, D2D1::RectF(area.left, y, area.right, y + 22), mono_.get(), proc.idle ? palette_.muted : palette_.text);
    y += 24;
  }
}

void App::DrawMemory(D2D1_RECT_F area) {
  RefreshAnalysis();
  DrawText(L"Memory", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  DrawText(L"Samples are counters, not allocation stacks. If virtual alloc events were not collected, that is stated, not estimated.",
           D2D1::RectF(area.left, area.top + 36, area.right, area.top + 80), font_.get(), palette_.muted);
  if (!analysisReady_ || !memory_.hasSamples) {
    DrawText(L"No memory samples in the loaded trace. The Live page still shows current working sets.",
             D2D1::RectF(area.left, area.top + 90, area.right, area.top + 130), font_.get(), palette_.muted);
    return;
  }
  float y = area.top + 100;
  for (const auto& proc : memory_.processes) {
    if (y > area.bottom) break;
    std::wstring line = Widen(proc.image.empty() ? "(unknown)" : proc.image);
    line += L"  peak ws " + std::to_wstring(proc.workingSetPeak / 1024) + L" KB";
    line += L"  last " + std::to_wstring(proc.workingSetLast / 1024) + L" KB";
    line += L"  private peak " + std::to_wstring(proc.privatePeak / 1024) + L" KB";
    if (proc.slopeValid) line += L"  slope " + std::to_wstring(static_cast<int>(proc.workingSetBytesPerSecond)) + L" B/s";
    DrawText(line, D2D1::RectF(area.left, y, area.right, y + 22), mono_.get(), palette_.text);
    y += 24;
  }
}

void App::DrawCompare(D2D1_RECT_F area) {
  DrawText(L"Compare two traces", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  Button(D2D1::RectF(area.left, area.top + 40, area.left + 220, area.top + 74), L"Load candidate", 6);
  RefreshAnalysis();
  std::shared_ptr<TraceSession> base;
  std::shared_ptr<TraceSession> cand;
  {
    std::lock_guard<std::mutex> lock(dataMu_);
    base = session_;
    cand = compare_;
  }
  if (!base || !cand || !analysisReady_) {
    DrawText(L"Open a baseline with Ctrl+O, then a candidate here. Metrics that are not the same kind of measurement are skipped.",
             D2D1::RectF(area.left, area.top + 90, area.right, area.top + 140), font_.get(), palette_.muted);
    return;
  }
  const ComparisonResult result =
      CompareAnalyses(cpu_, memory_, compareCpu_, compareMemory_, base->Info(), cand->Info(), CompareOptions{settings_.regressionFraction});
  DrawText(Widen(result.summary), D2D1::RectF(area.left, area.top + 88, area.right, area.top + 120), font_.get(), palette_.text);
  float y = area.top + 130;
  for (const auto& caveat : result.caveats) {
    DrawText(Widen(caveat), D2D1::RectF(area.left, y, area.right, y + 36), font_.get(), palette_.muted);
    y += 38;
  }
  for (const auto& delta : result.deltas) {
    if (y > area.bottom - 8) break;
    std::wstring line = Widen(delta.id) + L"  ";
    if (!delta.comparable) line += L"not comparable — " + Widen(delta.reason);
    else {
      line += std::to_wstring(delta.baseline) + L" → " + std::to_wstring(delta.candidate);
      if (delta.regression) line += L"   regression indicator";
    }
    DrawText(line, D2D1::RectF(area.left, y, area.right, y + 22), mono_.get(), delta.regression ? palette_.warn : palette_.text);
    y += 24;
  }
}

void App::DrawReports(D2D1_RECT_F area) {
  DrawText(L"Reports", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  DrawText(L"HTML and JSON reports include the source, whether the data is synthetic, metric definitions, and gaps. "
           L"They do not claim a cause.",
           D2D1::RectF(area.left, area.top + 40, area.right, area.top + 100), font_.get(), palette_.muted);
  Button(D2D1::RectF(area.left, area.top + 120, area.left + 240, area.top + 156), L"Export HTML report", 7);
  Button(D2D1::RectF(area.left + 252, area.top + 120, area.left + 430, area.top + 156), L"Bookmark window", 8);
}

void App::DrawSettings(D2D1_RECT_F area) {
  DrawText(L"Settings", D2D1::RectF(area.left, area.top, area.right, area.top + 30), bold_.get(), palette_.text);
  std::wstring body = L"Theme: " + Widen(settings_.theme) + L"\nTrace directory: " + Widen(settings_.traceDirectory) +
                      L"\nEvent cap: " + std::to_wstring(settings_.maxEvents) + L"\nRegression fraction: " +
                      std::to_wstring(settings_.regressionFraction) + L"\nVirtual alloc capture: " +
                      (settings_.kernelVirtualAlloc ? L"on" : L"off") +
                      L"\n\nSettings are stored under %AppData%\\PulseScope\\settings.json. "
                      L"Logs do not include command lines or event payloads.";
  DrawText(body, D2D1::RectF(area.left, area.top + 44, area.right, area.top + 220), font_.get(), palette_.text);
  Button(D2D1::RectF(area.left, area.top + 240, area.left + 180, area.top + 274), L"Toggle theme", 9);
  Button(D2D1::RectF(area.left + 192, area.top + 240, area.left + 460, area.top + 274), L"Toggle virtual alloc", 10);
}

void App::SetError(std::string text) {
  error_ = Widen(text);
  Logger::Instance().Error(text);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::Activate(int id) {
  error_.clear();
  if (id >= 100 && id < 100 + PageCount) {
    page_ = id - 100;
    scroll_ = 0;
  } else if (id == 2) {
    ToggleEtw();
  } else if (id == 4) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    ShellExecuteW(hwnd_, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
  } else if (id == 5) {
    OpenTrace(false);
  } else if (id == 6) {
    OpenTrace(true);
  } else if (id == 7) {
    ExportHtml();
  } else if (id == 8) {
    Bookmark mark;
    mark.id = "ui-" + std::to_string(bookmarks_.All().size() + 1);
    mark.title = "Saved interval";
    mark.note = "Saved from the Reports page.";
    bookmarks_.Add(std::move(mark));
    status_ = L"Bookmark stored for the next report.";
  } else if (id == 9) {
    settings_.theme = settings_.theme == "dark" ? "light" : "dark";
    SaveSettings();
  } else if (id == 10) {
    settings_.kernelVirtualAlloc = !settings_.kernelVirtualAlloc;
    SaveSettings();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ToggleEtw() {
  if (etw_.running()) {
    const Status st = etw_.Stop();
    if (!st) SetError(st.message);
    std::lock_guard<std::mutex> lock(dataMu_);
    if (session_) {
      session_->Finalize(nullptr);
      if (etw_.eventsLostFromSession() > 0) session_->AddLostEvents(etw_.eventsLostFromSession());
      analysisReady_ = false;
    }
    return;
  }
  EtwConfig config;
  config.contextSwitches = settings_.kernelCswitch;
  config.readyThread = settings_.kernelReady;
  config.virtualAlloc = settings_.kernelVirtualAlloc;
  if (!settings_.traceDirectory.empty()) {
    config.etlPath = Widen(settings_.traceDirectory);
    if (!config.etlPath.empty() && config.etlPath.back() != L'\\') config.etlPath += L'\\';
    config.etlPath += L"pulsescope.etl";
  }
  {
    std::lock_guard<std::mutex> lock(dataMu_);
    SessionInfo info;
    info.source = DataSource::EtwRealtime;
    info.synthetic = false;
    info.label = "Live ETW";
    info.cpuCount = live_.processorCount();
    info.clockNote = "Session-relative 100 ns ticks converted from QPC.";
    info.collectionNote = "Kernel flags: process, thread, cswitch, dispatcher. Manifest provider Kernel-Process is requested.";
    SessionLimits limits;
    limits.maxEvents = settings_.maxEvents;
    session_ = std::make_shared<TraceSession>(std::move(info), limits);
    analysisReady_ = false;
  }
  const Status st = etw_.Start(config, [this](DecodedEvent decoded) {
    std::lock_guard<std::mutex> lock(dataMu_);
    if (!session_) return;
    Event event = decoded.event;
    event.providerId = session_->Strings().Intern(decoded.provider);
    event.nameId = session_->Strings().Intern(decoded.name);
    event.imageId = session_->Strings().Intern(decoded.image);
    event.detailId = session_->Strings().Intern(decoded.detail);
    event.synthetic = false;
    if (!decoded.decoded) session_->NoteUndecoded();
    session_->Ingest(std::move(event), nullptr);
  });
  if (!st) {
    SetError(st.message);
    std::lock_guard<std::mutex> lock(dataMu_);
    session_->Info().collectionNote = st.message;
  } else {
    page_ = Live;
  }
}

void App::OpenTrace(bool comparison) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) {
    SetError("The file dialog could not be created.");
    return;
  }
  COMDLG_FILTERSPEC filters[] = {{L"Traces", L"*.etl;*.json"}, {L"ETL", L"*.etl"}, {L"JSON fixture", L"*.json"}};
  dialog->SetFileTypes(3, filters);
  if (FAILED(dialog->Show(hwnd_))) return;
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(item.put()))) return;
  PWSTR raw = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || raw == nullptr) return;
  const std::wstring path = raw;
  CoTaskMemFree(raw);
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, path, comparison] {
    const bool etl = path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".etl") == 0;
    if (etl) {
      std::string note;
      TraceSession local;
      SessionInfo info;
      info.source = DataSource::EtlFile;
      info.synthetic = false;
      info.label = Narrow(path);
      info.sourceName = info.label;
      info.cpuCount = live_.processorCount();
      info.clockNote = "QPC timestamps converted with the trace header frequency when present.";
      SessionLimits limits;
      limits.maxEvents = settings_.maxEvents;
      local = TraceSession(std::move(info), limits);
      const Status st = ImportEtlFile(
          path,
          [&](DecodedEvent decoded) {
            Event event = decoded.event;
            event.providerId = local.Strings().Intern(decoded.provider);
            event.nameId = local.Strings().Intern(decoded.name);
            event.imageId = local.Strings().Intern(decoded.image);
            event.detailId = local.Strings().Intern(decoded.detail);
            if (!decoded.decoded) local.NoteUndecoded();
            local.Ingest(std::move(event), nullptr);
          },
          nullptr, 8ull << 30, note);
      local.Info().collectionNote = note;
      local.Finalize(nullptr);
      if (!st) {
        SetError(st.message);
        return;
      }
      auto shared = std::make_shared<TraceSession>(std::move(local));
      std::lock_guard<std::mutex> lock(dataMu_);
      if (comparison) compare_ = shared;
      else {
        session_ = shared;
        analysisReady_ = false;
      }
    } else {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        SetError("Could not open the file.");
        return;
      }
      std::ostringstream ss;
      ss << in.rdbuf();
      TraceSession loaded;
      FixtureLoadOptions opt;
      opt.maxEvents = settings_.maxEvents;
      const Status st = LoadFixtureJson(ss.str(), loaded, opt);
      if (!st) {
        SetError(st.message);
        return;
      }
      loaded.Info().sourceName = Narrow(path);
      auto shared = std::make_shared<TraceSession>(std::move(loaded));
      std::lock_guard<std::mutex> lock(dataMu_);
      if (comparison) compare_ = shared;
      else {
        session_ = shared;
        analysisReady_ = false;
      }
    }
    PostMessageW(hwnd_, WM_TIMER, 1, 0);
  });
  page_ = comparison ? Compare : Trace;
}

void App::RefreshAnalysis() {
  std::lock_guard<std::mutex> lock(dataMu_);
  if (analysisReady_ || !session_ || !session_->finalized()) return;
  if (!AnalyzeCpu(*session_, {}, cpu_)) return;
  if (!AnalyzeMemory(*session_, {}, memory_)) return;
  if (compare_ && compare_->finalized()) {
    AnalyzeCpu(*compare_, {}, compareCpu_);
    AnalyzeMemory(*compare_, {}, compareMemory_);
  }
  analysisReady_ = true;
}

void App::ExportHtml() {
  RefreshAnalysis();
  std::shared_ptr<TraceSession> session;
  {
    std::lock_guard<std::mutex> lock(dataMu_);
    session = session_;
  }
  if (!session) {
    SetError("There is no trace to export.");
    return;
  }
  ComPtr<IFileSaveDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) return;
  dialog->SetFileName(L"PulseScope-report.html");
  if (FAILED(dialog->Show(hwnd_))) return;
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(item.put()))) return;
  PWSTR raw = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return;
  const std::wstring path = raw;
  CoTaskMemFree(raw);
  ReportInput report;
  report.session = session.get();
  report.cpu = analysisReady_ ? &cpu_ : nullptr;
  report.memory = analysisReady_ ? &memory_ : nullptr;
  report.bookmarks = &bookmarks_.All();
  report.title = session->Info().label.empty() ? "PulseScope report" : session->Info().label;
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    SetError("Could not write the report.");
    return;
  }
  out << ExportReportHtml(report);
  status_ = L"Wrote " + path;
}

void App::LoadSettings() {
  std::ifstream in(AppDataDir() + L"\\settings.json", std::ios::binary);
  if (!in) {
    settings_.traceDirectory = Narrow(AppDataDir());
    return;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  pulsescope::Settings loaded;
  if (SettingsFromJson(ss.str(), loaded)) settings_ = std::move(loaded);
  if (settings_.traceDirectory.empty()) settings_.traceDirectory = Narrow(AppDataDir());
}

void App::SaveSettings() {
  std::ofstream out(AppDataDir() + L"\\settings.json", std::ios::binary);
  if (!out) return;
  out << SettingsToJson(settings_);
}

}  // namespace

int Run(HINSTANCE instance, int show) {
  App app(instance);
  return app.Loop(show);
}

}  // namespace pulsescope::win
